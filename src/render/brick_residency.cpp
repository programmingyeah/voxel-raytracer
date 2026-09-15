#include "brick_residency.hpp"

#include "renderer.hpp"
#include "renderer_shared.hpp"
#include "../world/gpu_world_layout.hpp"
#include "../world/voxel_world.hpp"

#include <algorithm>

namespace {
void setRequestedBit(std::vector<uint64_t>& bits, size_t entryIndex, bool value)
{
    const size_t wordIndex = entryIndex / 64u;
    const uint64_t mask = 1ull << (entryIndex % 64u);
    if (value) {
        bits[wordIndex] |= mask;
    } else {
        bits[wordIndex] &= ~mask;
    }
}

uint32_t growCapacity(uint32_t currentCapacity, uint32_t requiredCapacity)
{
    uint32_t newCapacity = currentCapacity == 0u ? 1u : currentCapacity;
    while (newCapacity < requiredCapacity) {
        if (newCapacity > std::numeric_limits<uint32_t>::max() / 2u) {
            return requiredCapacity;
        }
        newCapacity *= 2u;
    }

    return newCapacity;
}
}

void BrickResidencyManager::initializeForWorld(const VoxelWorld& world)
{
    gpuBrickCapacity = std::min<uint32_t>(static_cast<uint32_t>(world.getBrickCapacity()), INITIAL_GPU_BRICK_CAPACITY);
    if (gpuBrickCapacity == 0u) {
        gpuBrickCapacity = 1u;
    }

    nextGpuBrickSlot = 0u;
    freeGpuBrickSlots.clear();
    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        const WorldLod lod = static_cast<WorldLod>(lodIndex);
        gpuSlotByChunkEntry[lodIndex].assign(chunkEntryCount(world.getChunkCount(lod)), INVALID_GPU_BRICK_SLOT);
        requestedChunkEntryBits[lodIndex].assign((gpuSlotByChunkEntry[lodIndex].size() + 63u) / 64u, 0u);
        lastBrickRequestCount[lodIndex] = 0u;
        lastDroppedBrickRequestCount[lodIndex] = 0u;
    }
    cpuBrickToGpuBrick.assign(world.getBrickCapacity(), INVALID_GPU_BRICK_SLOT);
    gpuBrickToCpuBrick.assign(gpuBrickCapacity, BRICK_MAP_EMPTY);
}

void BrickResidencyManager::createRequestBuffers(Instance& instance, size_t frameCount)
{
    destroyRequestBuffers(instance);
    const VkDeviceSize brickRequestBufferSize = storageBufferSize(static_cast<size_t>(BRICK_REQUEST_HEADER_WORD_COUNT + BRICK_REQUEST_CAPACITY));
    for (auto& perLodBuffers : requestBuffers) {
        perLodBuffers.resize(frameCount);
        for (RequestBuffer& requestBuffer : perLodBuffers) {
        requestBuffer.buffer.createBuffer(
            &instance,
            brickRequestBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
            requestBuffer.buffer.map(&instance);
            requestBuffer.mappedWords = static_cast<uint32_t*>(requestBuffer.buffer.mapped);
        }
    }

    if (!requestBuffers.empty()) {
        for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
            for (size_t frameIndex = 0; frameIndex < requestBuffers[lodIndex].size(); frameIndex++) {
                RequestBuffer& requestBuffer = requestBuffers[lodIndex][frameIndex];
                uint32_t* words = requestBuffer.mappedWords;
                words[0] = 0u;
                words[1] = 0u;
                words[2] = BRICK_REQUEST_CAPACITY;
            }
        }
    }
}

void BrickResidencyManager::destroyRequestBuffers(Instance& instance)
{
    for (auto& perLodBuffers : requestBuffers) {
        for (RequestBuffer& requestBuffer : perLodBuffers) {
        if (requestBuffer.buffer.mapped != nullptr) {
            requestBuffer.buffer.unmap(&instance);
        }
        requestBuffer.buffer.cleanup(&instance);
        requestBuffer.mappedWords = nullptr;
        }
        perLodBuffers.clear();
    }
}

void BrickResidencyManager::rebuildTrackedState(VoxelWorld& world, WorldLod rebuildLod, const std::vector<uint32_t>& data, const std::vector<GpuBufferCopyRegion>& regions)
{
    for (const GpuBufferCopyRegion& region : regions) {
        const size_t srcStart = region.srcWordOffset;
        const size_t dstStart = region.dstWordOffset;

        for (size_t wordOffset = 0; wordOffset + 1u < region.wordCount; wordOffset += PACKED_BRICK_MAP_ENTRY_WORD_COUNT) {
            const size_t dstWordIndex = dstStart + wordOffset;
            const size_t chunkLocalWordIndex = dstWordIndex % PACKED_CHUNK_WORD_COUNT;
            if (chunkLocalWordIndex < CHUNK_ACCEL_WORD_COUNT) {
                continue;
            }

            const size_t chunkSlotIndex = dstWordIndex / PACKED_CHUNK_WORD_COUNT;
            const uint32_t packedBrickIndex = static_cast<uint32_t>((chunkLocalWordIndex - CHUNK_ACCEL_WORD_COUNT) / PACKED_BRICK_MAP_ENTRY_WORD_COUNT);
            const size_t entryIndex = chunkEntryIndex(chunkSlotIndex, packedBrickIndex);

            const uint32_t oldGpuSlot = gpuSlotByChunkEntry[static_cast<size_t>(rebuildLod)][entryIndex];
            if (oldGpuSlot < gpuBrickToCpuBrick.size()) {
                const uint32_t cpuBrickIndex = gpuBrickToCpuBrick[oldGpuSlot];
                if (cpuBrickIndex < cpuBrickToGpuBrick.size() && cpuBrickToGpuBrick[cpuBrickIndex] == oldGpuSlot) {
                    cpuBrickToGpuBrick[cpuBrickIndex] = INVALID_GPU_BRICK_SLOT;
                    gpuBrickToCpuBrick[oldGpuSlot] = BRICK_MAP_EMPTY;
                    freeGpuBrickSlots.push_back(oldGpuSlot);
                }
            }

            gpuSlotByChunkEntry[static_cast<size_t>(rebuildLod)][entryIndex] = INVALID_GPU_BRICK_SLOT;
            setRequestedBit(requestedChunkEntryBits[static_cast<size_t>(rebuildLod)], entryIndex, false);

            const uint32_t newMetadata = data[srcStart + wordOffset + 1u];
            if ((newMetadata & GPU_BRICK_METADATA_REQUESTED_BIT) != 0u) {
                setRequestedBit(requestedChunkEntryBits[static_cast<size_t>(rebuildLod)], entryIndex, true);
            }
        }
    }
}

void BrickResidencyManager::resetRequestBuffer(size_t frameIndex)
{
    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        if (frameIndex >= requestBuffers[lodIndex].size() || requestBuffers[lodIndex][frameIndex].mappedWords == nullptr) {
            continue;
        }
        uint32_t* words = requestBuffers[lodIndex][frameIndex].mappedWords;
        words[0] = 0u;
        words[1] = 0u;
        words[2] = BRICK_REQUEST_CAPACITY;
    }
}

void BrickResidencyManager::ensureGpuBrickCapacity(
    Instance& instance,
    CommandPool& commandPool,
    Buffer& brickPoolBuffer,
    uint32_t requiredCapacity,
    VulkanApp& renderer
)
{
    if (requiredCapacity <= gpuBrickCapacity) {
        return;
    }

    const uint32_t newCapacity = growCapacity(gpuBrickCapacity, requiredCapacity);
    vkDeviceWaitIdle(instance.device());

    Buffer newBrickPoolBuffer{};
    newBrickPoolBuffer.createBuffer(
        &instance,
        storageBufferSize(static_cast<size_t>(newCapacity) * PACKED_BRICK_WORD_COUNT),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (brickPoolBuffer.buffer != VK_NULL_HANDLE && nextGpuBrickSlot > 0u) {
        Buffer::copyBuffer(
            &instance,
            brickPoolBuffer.buffer,
            newBrickPoolBuffer.buffer,
            std::vector<BufferCopyRegion>{{0, 0, storageBufferSize(static_cast<size_t>(nextGpuBrickSlot) * PACKED_BRICK_WORD_COUNT)}},
            commandPool
        );
        brickPoolBuffer.cleanup(&instance);
    }

    brickPoolBuffer = newBrickPoolBuffer;
    gpuBrickCapacity = newCapacity;
    gpuBrickToCpuBrick.resize(gpuBrickCapacity, BRICK_MAP_EMPTY);
    renderer.createDescriptorSets(false);
}

void BrickResidencyManager::processBrickRequests(
    size_t frameIndex,
    WorldLod requestLod,
    VoxelWorld& world,
    Instance& instance,
    CommandPool& commandPool,
    Buffer& brickPoolBuffer,
    Buffer& chunkBrickMapBuffer,
    VulkanAppFrameUploads& uploads,
    VulkanApp& renderer
)
{
    const size_t lodIndex = static_cast<size_t>(requestLod);
    if (lodIndex >= WORLD_LOD_COUNT || frameIndex >= requestBuffers[lodIndex].size()) {
        return;
    }

    RequestBuffer& requestBuffer = requestBuffers[lodIndex][frameIndex];
    if (requestBuffer.mappedWords == nullptr) {
        return;
    }

    const uint32_t requestCount = std::min(requestBuffer.mappedWords[0], BRICK_REQUEST_CAPACITY);
    lastBrickRequestCount[lodIndex] = requestCount;
    lastDroppedBrickRequestCount[lodIndex] = requestBuffer.mappedWords[1];
    if (requestCount == 0u) {
        return;
    }

    std::vector<uint32_t> brickUploadWords;
    std::vector<BufferCopyRegion> brickUploadRegions;
    std::vector<uint32_t> chunkPatchWords;
    std::vector<BufferCopyRegion> chunkPatchRegions;

    for (uint32_t requestIndex = 0; requestIndex < requestCount; requestIndex++) {
        const uint32_t packedEntryIndex = requestBuffer.mappedWords[BRICK_REQUEST_HEADER_WORD_COUNT + requestIndex];
        const uint32_t chunkRecordBase = (packedEntryIndex / PACKED_CHUNK_WORD_COUNT) * PACKED_CHUNK_WORD_COUNT;
        const uint32_t chunkSlotIndex = chunkRecordBase / PACKED_CHUNK_WORD_COUNT;
        if (chunkSlotIndex >= world.getChunkCount()) {
            continue;
        }
        const uint32_t chunkLocalEntryWordOffset = packedEntryIndex - chunkRecordBase;
        if (chunkLocalEntryWordOffset < CHUNK_ACCEL_WORD_COUNT) {
            continue;
        }

        const uint32_t brickEntryWordOffset = chunkLocalEntryWordOffset - CHUNK_ACCEL_WORD_COUNT;
        if ((brickEntryWordOffset % PACKED_BRICK_MAP_ENTRY_WORD_COUNT) != 0u) {
            continue;
        }

        const uint32_t packedBrickIndex = brickEntryWordOffset / PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
        if (chunkSlotIndex >= world.getChunkCount(requestLod) || packedBrickIndex >= Chunk::BRICK_COUNT) {
            continue;
        }
        const size_t entryIndex = chunkEntryIndex(chunkSlotIndex, packedBrickIndex);
        const Chunk::EncodedBrickMap& brickMap = world.getChunkBySlotIndex(requestLod, chunkSlotIndex).getBrickMap();
        const glm::uvec3 brickCoord = decodeBrickMorton(packedBrickIndex);
        const uint32_t linearBrickIndex = brickCoord.x +
            Chunk::BRICKS_PER_AXIS * (brickCoord.y + Chunk::BRICKS_PER_AXIS * brickCoord.z);
        const BrickMapEntry& canonicalEntry = brickMap[linearBrickIndex];
        const uint32_t cpuBrickIndex = canonicalEntry.index;
        if (cpuBrickIndex == BRICK_MAP_EMPTY || cpuBrickIndex >= cpuBrickToGpuBrick.size()) {
            continue;
        }
        uint32_t gpuBrickSlot = cpuBrickToGpuBrick[cpuBrickIndex];
        if (gpuBrickSlot == INVALID_GPU_BRICK_SLOT) {
            if (!freeGpuBrickSlots.empty()) {
                gpuBrickSlot = freeGpuBrickSlots.back();
                freeGpuBrickSlots.pop_back();
            } else {
                ensureGpuBrickCapacity(instance, commandPool, brickPoolBuffer, nextGpuBrickSlot + 1u, renderer);
                gpuBrickSlot = nextGpuBrickSlot++;
            }
            cpuBrickToGpuBrick[cpuBrickIndex] = gpuBrickSlot;
            gpuBrickToCpuBrick[gpuBrickSlot] = cpuBrickIndex;

            const size_t brickWordOffset = brickUploadWords.size();
            brickUploadWords.resize(brickWordOffset + PACKED_BRICK_WORD_COUNT, 0u);
            const Brick& brick = world.getBrickByIndex(cpuBrickIndex);
            for (uint32_t maskWordIndex = 0; maskWordIndex < OCCUPANCY_MASK_WORD_COUNT; maskWordIndex++) {
                brickUploadWords[brickWordOffset + maskWordIndex] = brick.occupancyMaskWords[maskWordIndex];
            }
            for (uint32_t z = 0; z < BRICK_SIZE; z++) {
                for (uint32_t y = 0; y < BRICK_SIZE; y++) {
                    for (uint32_t x = 0; x < BRICK_SIZE; x++) {
                        const uint32_t flatIndex = x + BRICK_SIZE * (y + BRICK_SIZE * z);
                        const size_t packedWordIndex = brickWordOffset + OCCUPANCY_MASK_WORD_COUNT + flatIndex / 4u;
                        const uint32_t bitShift = 8u * (flatIndex % 4u);
                        brickUploadWords[packedWordIndex] |= static_cast<uint32_t>(brick.voxels[x][y][z]) << bitShift;
                    }
                }
            }
            brickUploadRegions.push_back({
                sizeof(uint32_t) * static_cast<VkDeviceSize>(brickWordOffset),
                sizeof(uint32_t) * static_cast<VkDeviceSize>(static_cast<size_t>(gpuBrickSlot) * PACKED_BRICK_WORD_COUNT),
                sizeof(uint32_t) * static_cast<VkDeviceSize>(PACKED_BRICK_WORD_COUNT)
            });
        }

        const uint32_t metadataWord = static_cast<uint32_t>(canonicalEntry.materialId) | GPU_BRICK_METADATA_RESIDENT_BIT;
        const size_t patchWordOffset = chunkPatchWords.size();
        chunkPatchWords.push_back(gpuBrickSlot);
        chunkPatchWords.push_back(metadataWord);
        gpuSlotByChunkEntry[lodIndex][entryIndex] = gpuBrickSlot;
        setRequestedBit(requestedChunkEntryBits[lodIndex], entryIndex, false);
        chunkPatchRegions.push_back({
            sizeof(uint32_t) * static_cast<VkDeviceSize>(patchWordOffset),
            sizeof(uint32_t) * static_cast<VkDeviceSize>(packedEntryIndex),
            sizeof(uint32_t) * static_cast<VkDeviceSize>(PACKED_BRICK_MAP_ENTRY_WORD_COUNT)
        });
    }

    if (!brickUploadWords.empty()) {
        renderer.queueBufferUpload(uploads, brickPoolBuffer, brickUploadWords, brickUploadRegions);
    }
    if (!chunkPatchWords.empty()) {
        renderer.queueBufferUpload(uploads, chunkBrickMapBuffer, chunkPatchWords, chunkPatchRegions);
    }
}