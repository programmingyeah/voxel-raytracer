#pragma once

#include "buffer.hpp"
#include "command_pool.hpp"
#include "instance.hpp"
#include "renderer_shared.hpp"
#include "../world/chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
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
    void rebuildTrackedState(VoxelWorld& world, const std::vector<uint32_t>& chunkBrickMapWords, const std::vector<struct GpuBufferCopyRegion>& regions);
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
        VoxelWorld& world,
        Instance& instance,
        CommandPool& commandPool,
        Buffer& brickPoolBuffer,
        Buffer& chunkBrickMapBuffer,
        struct VulkanAppFrameUploads& uploads,
        VulkanApp& renderer
    );

    const std::vector<RequestBuffer>& getRequestBuffers() const { return requestBuffers; }
    std::vector<RequestBuffer>& getRequestBuffers() { return requestBuffers; }
    uint32_t getGpuBrickCapacity() const { return gpuBrickCapacity; }
    uint32_t getNextGpuBrickSlot() const { return nextGpuBrickSlot; }
    uint32_t getLastBrickRequestCount() const { return lastBrickRequestCount; }
    uint32_t getLastDroppedBrickRequestCount() const { return lastDroppedBrickRequestCount; }

private:
    std::vector<RequestBuffer> requestBuffers;
    std::vector<uint32_t> gpuSlotByChunkEntry;
    std::vector<uint64_t> requestedChunkEntryBits;
    std::vector<uint32_t> cpuBrickToGpuBrick;
    std::vector<uint32_t> gpuBrickToCpuBrick;
    uint32_t gpuBrickCapacity = 0;
    uint32_t nextGpuBrickSlot = 0;
    uint32_t lastBrickRequestCount = 0;
    uint32_t lastDroppedBrickRequestCount = 0;
};