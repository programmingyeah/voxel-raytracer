#pragma once

#include "buffer.hpp"
#include "command_pool.hpp"
#include "instance.hpp"
#include "renderer_shared.hpp"
#include "../world/chunk.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

class VoxelWorld;
class VulkanApp;
struct GpuBufferCopyRegion;

class BrickResidencyManager {
public:
    static constexpr uint32_t INVALID_GPU_BRICK_SLOT = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t INITIAL_GPU_BRICK_CAPACITY = 65536u;
    static constexpr uint32_t BRICK_REQUEST_CAPACITY = 65536u;

    struct RequestBuffer {
        Buffer buffer{};
        uint32_t* mappedWords = nullptr;
    };

    void initializeForWorld(const VoxelWorld& world);
    void createRequestBuffers(Instance& instance, size_t frameCount);
    void destroyRequestBuffers(Instance& instance);
    void rebuildTrackedState(VoxelWorld& world, WorldLod lod, const std::vector<uint32_t>& chunkBrickMapWords, const std::vector<struct GpuBufferCopyRegion>& regions);
    void resetRequestBuffer(size_t frameIndex);
    void ensureGpuBrickCapacity(
        Instance& instance,
        CommandPool& commandPool,
        Buffer& brickPoolBuffer,
        uint32_t requiredCapacity,
        VulkanApp& renderer
    );
    void processBrickRequests(
        size_t frameIndex,
        WorldLod lod,
        VoxelWorld& world,
        Instance& instance,
        CommandPool& commandPool,
        Buffer& brickPoolBuffer,
        Buffer& chunkBrickMapBuffer,
        struct VulkanAppFrameUploads& uploads,
        VulkanApp& renderer
    );

    const std::vector<RequestBuffer>& getRequestBuffers(WorldLod lod) const { return requestBuffers.at(static_cast<size_t>(lod)); }
    std::vector<RequestBuffer>& getRequestBuffers(WorldLod lod) { return requestBuffers.at(static_cast<size_t>(lod)); }
    uint32_t getGpuBrickCapacity() const { return gpuBrickCapacity; }
    uint32_t getAllocatedGpuBrickCount() const { return nextGpuBrickSlot; }
    uint32_t getResidentGpuBrickCount() const { return nextGpuBrickSlot - static_cast<uint32_t>(freeGpuBrickSlots.size()); }
    uint32_t getLastBrickRequestCount(WorldLod lod) const { return lastBrickRequestCount.at(static_cast<size_t>(lod)); }
    uint32_t getLastDroppedBrickRequestCount(WorldLod lod) const { return lastDroppedBrickRequestCount.at(static_cast<size_t>(lod)); }

private:
    std::array<std::vector<RequestBuffer>, WORLD_LOD_COUNT> requestBuffers;
    std::array<std::vector<uint32_t>, WORLD_LOD_COUNT> gpuSlotByChunkEntry;
    std::array<std::vector<uint64_t>, WORLD_LOD_COUNT> requestedChunkEntryBits;
    std::vector<uint32_t> cpuBrickToGpuBrick;
    std::vector<uint32_t> gpuBrickToCpuBrick;
    std::vector<uint32_t> freeGpuBrickSlots;
    uint32_t gpuBrickCapacity = 0;
    uint32_t nextGpuBrickSlot = 0;
    std::array<uint32_t, WORLD_LOD_COUNT> lastBrickRequestCount{};
    std::array<uint32_t, WORLD_LOD_COUNT> lastDroppedBrickRequestCount{};
};