#pragma once

#include "chunk.hpp"
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct GpuVoxelBuffers {
    std::vector<uint32_t> chunkWindowIndices;
    std::vector<uint32_t> chunkBrickMaps;
    std::vector<uint32_t> brickData;
};

struct GpuBufferCopyRegion {
    size_t srcWordOffset = 0;
    size_t dstWordOffset = 0;
    size_t wordCount = 0;
};

struct GpuBufferDiff {
    std::vector<uint32_t> data;
    std::vector<GpuBufferCopyRegion> regions;
    size_t totalWordCount = 0;

    bool empty() const { return regions.empty(); }
};

struct GpuWorldDiff {
    GpuBufferDiff chunkWindowIndices;
    GpuBufferDiff chunkBrickMaps;
    GpuBufferDiff brickData;

    bool empty() const {
        return chunkWindowIndices.empty() && chunkBrickMaps.empty() && brickData.empty();
    }
};

class VoxelWorld {
public:
    explicit VoxelWorld(glm::uvec3 chunkCounts);
    VoxelWorld(const VoxelWorld&) = delete;
    VoxelWorld& operator=(const VoxelWorld&) = delete;
    VoxelWorld(VoxelWorld&&) = delete;
    VoxelWorld& operator=(VoxelWorld&&) = delete;

    Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z);
    const Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z) const;

    bool containsVoxel(int32_t x, int32_t y, int32_t z) const;
    uint32_t getVoxel(uint32_t x, uint32_t y, uint32_t z) const;
    void setVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t value);
    std::vector<uint32_t> shiftChunkWindow(glm::ivec3 deltaChunks);

    glm::uvec3 getChunkCounts() const { return chunkCounts; }
    glm::ivec3 getVoxelMin() const;
    glm::ivec3 getVoxelMax() const;
    size_t getChunkCount() const { return chunkWindowIndices.size(); }
    Chunk& getChunkByWindowIndex(size_t localWindowIndex);
    const Chunk& getChunkByWindowIndex(size_t localWindowIndex) const;
    void setChunkSolidVoxelCountByWindowIndex(size_t localWindowIndex, uint64_t solidVoxelCount);
    uint64_t getTotalSolidVoxelCount() const;
    glm::uvec3 getVoxelDimensions() const;

    GpuVoxelBuffers buildGpuBuffers() const;
    GpuWorldDiff buildGpuBufferDiffs();
    void clearDirtyState();

private:
    void onChunkBrickMapDirty(size_t chunkIndex, uint32_t mapIndex);
    void onBrickPoolDirty(uint32_t brickIndex);
    size_t chunkIndex(uint32_t x, uint32_t y, uint32_t z) const;
    glm::uvec3 chunkCoordFromWindowIndex(size_t localWindowIndex) const;

    glm::uvec3 chunkCounts{};
    glm::ivec3 chunkOrigin{0};
    std::vector<Chunk> chunks;
    std::vector<uint32_t> chunkWindowIndices;
    std::vector<uint64_t> chunkSlotSolidVoxelCounts;
    std::vector<uint8_t> dirtyChunkWindowIndices;
    std::vector<uint8_t> dirtyChunkBrickMapEntries;
    std::vector<uint8_t> dirtyBrickPoolEntries;
};
