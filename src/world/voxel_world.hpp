#pragma once

#include "chunk.hpp"
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct GpuVoxelBuffers {
    std::vector<uint32_t> chunkBrickMaps;
    std::vector<uint32_t> brickData;
};

class VoxelWorld {
public:
    explicit VoxelWorld(glm::uvec3 chunkCounts);

    Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z);
    const Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z) const;

    glm::uvec3 getChunkCounts() const { return chunkCounts; }
    glm::uvec3 getVoxelDimensions() const;

    GpuVoxelBuffers buildGpuBuffers() const;
    std::vector<uint32_t> buildVoxelBuffer() const;

private:
    size_t chunkIndex(uint32_t x, uint32_t y, uint32_t z) const;

    glm::uvec3 chunkCounts{};
    std::vector<Chunk> chunks;
};
