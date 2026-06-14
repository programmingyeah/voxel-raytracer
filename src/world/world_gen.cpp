#include "world_gen.hpp"

#include "chunk_selection.hpp"
#include "terrain_builder.hpp"

#include <chrono>
#include <iostream>
#include <optional>

namespace {
WorldGenerationStats generateChunkOnWorker(VoxelWorld& world, uint32_t chunkSlotIndex, const glm::uvec3& voxelDimensions) {
    WorldGenerationStats stats{};
    const auto generationStart = std::chrono::steady_clock::now();
    TerrainBuildResult buildResult{};

    try {
        const auto chunkBuildStart = std::chrono::steady_clock::now();
        buildTerrainChunk(world, chunkSlotIndex, voxelDimensions, buildResult);
        stats.averageChunkGenerationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - chunkBuildStart
        ).count();
        world.publishChunkGeneration(chunkSlotIndex, buildResult.solidVoxelCount, buildResult.allocatedBrickIndices);
        stats.totalGenerationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - generationStart
        ).count();
        stats.solidVoxelCount = world.getTotalSolidVoxelCount();
        return stats;
    } catch (...) {
        world.abortChunkGeneration(chunkSlotIndex, buildResult.allocatedBrickIndices);
        throw;
    }
}
}

WorldGenerator::WorldGenerator() : workerThread(&WorldGenerator::workerMain, this) {}

WorldGenerator::~WorldGenerator() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopRequested = true;
    }
    queueCondition.notify_all();

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void WorldGenerator::requestNextChunk(VoxelWorld& world, glm::ivec2 focusChunkXZ, glm::vec3 viewForward) {
    if (world.getGeneratedChunkCount() >= world.getChunkCount()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopRequested || workerBusy || !pendingJobs.empty()) {
            return;
        }
    }

    const std::optional<uint32_t> nextWindowIndex = findBestUngeneratedChunkWindowIndex(world, focusChunkXZ, viewForward);
    if (!nextWindowIndex) {
        return;
    }

    uint32_t chunkSlotIndex = 0;
    if (!world.tryBeginChunkGeneration(*nextWindowIndex, chunkSlotIndex)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopRequested) {
            world.abortChunkGeneration(chunkSlotIndex, {});
            return;
        }

        pendingJobs.push({&world, chunkSlotIndex, world.getVoxelDimensions()});
    }

    queueCondition.notify_one();
}

std::optional<WorldGenerationStats> WorldGenerator::consumeCompletedGeneration() {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (completedStats.empty()) {
        return std::nullopt;
    }

    WorldGenerationStats stats = completedStats.front();
    completedStats.pop();
    return stats;
}

void WorldGenerator::workerMain() {
    while (true) {
        GenerationJob job{};

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait(lock, [this]() {
                return stopRequested || !pendingJobs.empty();
            });

            if (stopRequested && pendingJobs.empty()) {
                return;
            }

            job = pendingJobs.front();
            pendingJobs.pop();
            workerBusy = true;
        }

        std::optional<WorldGenerationStats> stats;
        try {
            stats = generateChunkOnWorker(*job.world, job.chunkSlotIndex, job.voxelDimensions);
        } catch (const std::exception& e) {
            std::cerr << "world generation failed: " << e.what() << std::endl;
        }

        std::lock_guard<std::mutex> lock(queueMutex);
        if (stats) {
            completedStats.push(*stats);
        }
        workerBusy = false;
    }
}

