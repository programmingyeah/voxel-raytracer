#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class VoxelWorld;

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

struct GpuVoxelBuffers {
    std::vector<uint32_t> chunkWindowIndices;
    std::vector<uint32_t> chunkBrickMaps;
    std::vector<uint32_t> brickData;
};

struct GpuWorldDiff {
    GpuBufferDiff chunkWindowIndices;
    GpuBufferDiff chunkBrickMaps;
    GpuBufferDiff brickData;

    bool empty() const {
        return chunkWindowIndices.empty() && chunkBrickMaps.empty() && brickData.empty();
    }
};

GpuVoxelBuffers buildGpuVoxelBuffers(const VoxelWorld& world);
GpuWorldDiff buildGpuWorldDiff(VoxelWorld& world);
void clearGpuUploadDirtyState(VoxelWorld& world);
