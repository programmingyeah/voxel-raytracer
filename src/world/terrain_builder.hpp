#pragma once

#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VoxelWorld;

struct TerrainBuildResult {
    uint64_t solidVoxelCount = 0;
    std::vector<uint32_t> allocatedBrickIndices;
};

void buildTerrainChunk(VoxelWorld& world, uint32_t chunkSlotIndex, const glm::uvec3& voxelDimensions, TerrainBuildResult& outResult);
