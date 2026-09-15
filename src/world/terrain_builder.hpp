#pragma once

#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VoxelWorld;
enum class WorldLod : uint8_t;

struct TerrainBuildResult {
    uint64_t solidVoxelCount = 0;
    std::vector<uint32_t> allocatedBrickIndices;
};

void buildTerrainChunk(VoxelWorld& world, WorldLod lod, uint32_t chunkSlotIndex, uint32_t terrainWorldHeight, TerrainBuildResult& outResult);
