#include "world_gen.hpp"

#include "chunk_selection.hpp"
#include "terrain_builder.hpp"

#include <chrono>
#include <iostream>
#include <algorithm>
#include <optional>
#include <thread>

namespace {
size_t defaultWorkerThreadCount() {
    const unsigned int hardwareThreadCount = std::thread::hardware_concurrency();
    if (hardwareThreadCount <= 1u) {
        return 1u;
    }

    return static_cast<size_t>(hardwareThreadCount - 1u);
}

size_t defaultMaxInFlightJobCount(size_t workerThreadCount) {
    return workerThreadCount > 0u ? workerThreadCount * 2u : 1u;
}

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

WorldGenerator::WorldGenerator() {
    workerThreadCount = defaultWorkerThreadCount();
    maxInFlightJobs = defaultMaxInFlightJobCount(workerThreadCount);

    workerThreads.reserve(workerThreadCount);
    for (size_t workerIndex = 0; workerIndex < workerThreadCount; workerIndex++) {
        workerThreads.emplace_back(&WorldGenerator::workerMain, this);
    }
}

WorldGenerator::~WorldGenerator() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopRequested = true;
    }
    queueCondition.notify_all();

    for (std::thread& workerThread : workerThreads) {
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
}

void WorldGenerator::requestNextChunk(VoxelWorld& world, glm::ivec2 focusChunkXZ, glm::vec3 viewForward) {
    if (world.getGeneratedChunkCount() >= world.getChunkCount()) {
        return;
    }

    size_t jobBudget = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        const size_t inFlightJobCount = activeWorkerCount + pendingJobs.size();
        if (stopRequested || inFlightJobCount >= maxInFlightJobs) {
            return;
        }

        jobBudget = maxInFlightJobs - inFlightJobCount;
    }

    const std::vector<uint32_t> nextWindowIndices = findBestUngeneratedChunkWindowIndices(
        world,
        focusChunkXZ,
        viewForward,
        jobBudget
    );
    if (nextWindowIndices.empty()) {
        return;
    }

    size_t queuedJobCount = 0;
    for (uint32_t nextWindowIndex : nextWindowIndices) {
        uint32_t chunkSlotIndex = 0;
        if (!world.tryBeginChunkGeneration(nextWindowIndex, chunkSlotIndex)) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stopRequested) {
                world.abortChunkGeneration(chunkSlotIndex, {});
                return;
            }

            pendingJobs.push({&world, chunkSlotIndex, world.getVoxelDimensions()});
        }

        queuedJobCount++;
    }

    if (queuedJobCount > 0u) {
        queueCondition.notify_all();
    }
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
            activeWorkerCount++;
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
        activeWorkerCount--;
    }
}

