#pragma once

#include "voxel_world.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

struct WorldGenerationStats {
    WorldLod lod = WorldLod::Lod0;
    uint64_t solidVoxelCount = 0;
    double totalGenerationMs = 0.0;
    double averageChunkGenerationMs = 0.0;
};

class WorldGenerator {
public:
    WorldGenerator();
    ~WorldGenerator();
    WorldGenerator(const WorldGenerator&) = delete;
    WorldGenerator& operator=(const WorldGenerator&) = delete;
    WorldGenerator(WorldGenerator&&) = delete;
    WorldGenerator& operator=(WorldGenerator&&) = delete;

    void requestNextChunk(VoxelWorld& world, glm::vec3 cameraPosition, glm::vec3 viewForward);
    std::optional<WorldGenerationStats> consumeCompletedGeneration();

private:
    struct GenerationJob { VoxelWorld* world = nullptr; WorldLod lod = WorldLod::Lod0; uint32_t chunkSlotIndex = 0; uint32_t terrainWorldHeight = 0; };

    void workerMain();

    std::vector<std::thread> workerThreads;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::queue<GenerationJob> pendingJobs;
    std::queue<WorldGenerationStats> completedStats;
    size_t workerThreadCount = 0;
    size_t maxInFlightJobs = 0;
    size_t activeWorkerCount = 0;
    bool stopRequested = false;
};