#include "world_upload.hpp"

#include "gpu_world_layout.hpp"
#include "voxel_world.hpp"

#include <algorithm>
#include <utility>

namespace {
BrickMapEntry emptyBrickMapEntry() {
    return BrickMapEntry{BRICK_MAP_EMPTY, AIR_MATERIAL};
}

void clearDirtyFlags(std::vector<uint8_t>& flags) {
    std::fill(flags.begin(), flags.end(), 0u);
}

void setPackedBit(std::vector<uint32_t>& words, size_t wordBase, uint32_t bitIndex) {
    words[wordBase + bitIndex / 32u] |= 1u << (bitIndex % 32u);
}

uint32_t accelIndex8(uint32_t x, uint32_t y, uint32_t z) {
    return x + 8u * (y + 8u * z);
}

uint32_t accelIndex4(uint32_t x, uint32_t y, uint32_t z) {
    return x + 4u * (y + 4u * z);
}

uint32_t accelIndex2(uint32_t x, uint32_t y, uint32_t z) {
    return x + 2u * (y + 2u * z);
}

bool brickEntryHasRenderableContent(const BrickMapEntry& entry) {
    return entry.index != BRICK_MAP_EMPTY || entry.materialId != AIR_MATERIAL;
}

void packBrick(std::vector<uint32_t>& brickData, size_t brickIndex, const Brick& brick) {
    const size_t brickBaseIndex = brickIndex * PACKED_BRICK_WORD_COUNT;

    for (uint32_t maskWordIndex = 0; maskWordIndex < OCCUPANCY_MASK_WORD_COUNT; maskWordIndex++) {
        brickData[brickBaseIndex + maskWordIndex] = brick.occupancyMaskWords[maskWordIndex];
    }

    for (uint32_t z = 0; z < BRICK_SIZE; z++) {
        for (uint32_t y = 0; y < BRICK_SIZE; y++) {
            for (uint32_t x = 0; x < BRICK_SIZE; x++) {
                const uint32_t flatIndex = x + BRICK_SIZE * (y + BRICK_SIZE * z);
                const size_t packedWordIndex = brickBaseIndex + OCCUPANCY_MASK_WORD_COUNT + flatIndex / 4u;
                const uint32_t bitShift = 8u * (flatIndex % 4u);
                brickData[packedWordIndex] |= static_cast<uint32_t>(brick.voxels[x][y][z]) << bitShift;
            }
        }
    }
}

void packBrickMapEntry(std::vector<uint32_t>& chunkBrickMaps, size_t packedEntryIndex, const BrickMapEntry& entry) {
    chunkBrickMaps[packedEntryIndex] = entry.index;
    chunkBrickMaps[packedEntryIndex + 1u] = static_cast<uint32_t>(entry.materialId) & GPU_BRICK_METADATA_MATERIAL_MASK;
}

void packChunkRecord(
    std::vector<uint32_t>& chunkBrickMaps,
    size_t chunkSlotIndex,
    const Chunk::EncodedBrickMap& brickMap,
    bool chunkGenerated
) {
    const size_t chunkBaseIndex = chunkSlotIndex * PACKED_CHUNK_WORD_COUNT;
    std::fill_n(
        chunkBrickMaps.begin() + static_cast<std::ptrdiff_t>(chunkBaseIndex),
        PACKED_CHUNK_WORD_COUNT,
        0u
    );

    if (!chunkGenerated) {
        chunkBrickMaps[chunkBaseIndex] = CHUNK_ACCEL_EMPTY_FLAG;
        return;
    }

    bool chunkEmpty = true;
    for (uint32_t brickZ = 0; brickZ < Chunk::BRICKS_PER_AXIS; brickZ++) {
        for (uint32_t brickY = 0; brickY < Chunk::BRICKS_PER_AXIS; brickY++) {
            for (uint32_t brickX = 0; brickX < Chunk::BRICKS_PER_AXIS; brickX++) {
                const uint32_t brickMapIndex = brickX + Chunk::BRICKS_PER_AXIS * (brickY + Chunk::BRICKS_PER_AXIS * brickZ);
                const uint32_t packedBrickMapIndex = mortonBrickIndex(brickX, brickY, brickZ);
                const BrickMapEntry entry = brickMap[brickMapIndex];
                const size_t entryBaseIndex = chunkBaseIndex + CHUNK_BRICK_MAP_OFFSET +
                    static_cast<size_t>(packedBrickMapIndex) * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
                packBrickMapEntry(chunkBrickMaps, entryBaseIndex, entry);

                if (!brickEntryHasRenderableContent(entry)) {
                    continue;
                }

                chunkEmpty = false;
                setPackedBit(chunkBrickMaps, chunkBaseIndex + CHUNK_ACCEL_8_OFFSET, accelIndex8(brickX / 2u, brickY / 2u, brickZ / 2u));
                setPackedBit(chunkBrickMaps, chunkBaseIndex + CHUNK_ACCEL_4_OFFSET, accelIndex4(brickX / 4u, brickY / 4u, brickZ / 4u));
                setPackedBit(chunkBrickMaps, chunkBaseIndex + CHUNK_ACCEL_2_OFFSET, accelIndex2(brickX / 8u, brickY / 8u, brickZ / 8u));
            }
        }
    }

    if (chunkEmpty) {
        chunkBrickMaps[chunkBaseIndex] = CHUNK_ACCEL_EMPTY_FLAG;
    }
}

std::vector<std::pair<size_t, size_t>> buildDirtySpans(const std::vector<uint8_t>& dirtyFlags) {
    std::vector<std::pair<size_t, size_t>> spans;

    size_t spanStart = 0;
    size_t spanLength = 0;
    for (size_t i = 0; i < dirtyFlags.size(); i++) {
        if (dirtyFlags[i] != 0u) {
            if (spanLength == 0) {
                spanStart = i;
            }
            spanLength++;
            continue;
        }

        if (spanLength > 0) {
            spans.emplace_back(spanStart, spanLength);
            spanLength = 0;
        }
    }

    if (spanLength > 0) {
        spans.emplace_back(spanStart, spanLength);
    }

    return spans;
}
}

GpuVoxelBuffers buildGpuVoxelBuffers(const VoxelWorld& world) {
    GpuVoxelBuffers gpuBuffers;

    gpuBuffers.chunkWindowIndices = world.window;
    gpuBuffers.chunkBrickMaps.resize(world.slots.size() * PACKED_CHUNK_WORD_COUNT);
    for (size_t chunkSlotIndex = 0; chunkSlotIndex < world.slots.size(); chunkSlotIndex++) {
        packChunkRecord(
            gpuBuffers.chunkBrickMaps,
            chunkSlotIndex,
            world.slots[chunkSlotIndex].chunk.getBrickMap(),
            world.isChunkSlotGenerated(chunkSlotIndex)
        );
    }

    gpuBuffers.brickData.assign(world.brickPool.bricks.size() * PACKED_BRICK_WORD_COUNT, 0u);
    for (size_t brickIndex = 0; brickIndex < world.brickPool.bricks.size(); brickIndex++) {
        packBrick(gpuBuffers.brickData, brickIndex, world.brickPool.bricks[brickIndex]);
    }

    return gpuBuffers;
}

GpuWorldDiff buildGpuWorldDiff(VoxelWorld& world) {
    std::lock_guard<std::mutex> dirtyLock(world.dirtyStateMutex);

    GpuWorldDiff worldDiff{};
    worldDiff.chunkWindowIndices.totalWordCount = world.window.size();
    worldDiff.chunkBrickMaps.totalWordCount = world.slots.size() * PACKED_CHUNK_WORD_COUNT;

    for (const auto& span : buildDirtySpans(world.dirty.window)) {
        const size_t spanStartIndex = span.first;
        const size_t spanIndexCount = span.second;
        const size_t srcWordOffset = worldDiff.chunkWindowIndices.data.size();

        worldDiff.chunkWindowIndices.data.insert(
            worldDiff.chunkWindowIndices.data.end(),
            world.window.begin() + static_cast<std::ptrdiff_t>(spanStartIndex),
            world.window.begin() + static_cast<std::ptrdiff_t>(spanStartIndex + spanIndexCount)
        );
        worldDiff.chunkWindowIndices.regions.push_back({srcWordOffset, spanStartIndex, spanIndexCount});
    }

    for (const auto& span : buildDirtySpans(world.dirty.brickMaps)) {
        const size_t spanStartChunk = span.first;
        const size_t spanChunkCount = span.second;
        const size_t dstWordOffset = spanStartChunk * PACKED_CHUNK_WORD_COUNT;
        const size_t wordCount = spanChunkCount * PACKED_CHUNK_WORD_COUNT;
        const size_t srcWordOffset = worldDiff.chunkBrickMaps.data.size();

        worldDiff.chunkBrickMaps.data.resize(srcWordOffset + wordCount, 0u);
        worldDiff.chunkBrickMaps.regions.push_back({srcWordOffset, dstWordOffset, wordCount});

        for (size_t chunkOffset = 0; chunkOffset < spanChunkCount; chunkOffset++) {
            const size_t chunkSlotIndex = spanStartChunk + chunkOffset;
            packChunkRecord(
                worldDiff.chunkBrickMaps.data,
                srcWordOffset / PACKED_CHUNK_WORD_COUNT + chunkOffset,
                world.slots[chunkSlotIndex].chunk.getBrickMap(),
                world.isChunkSlotGenerated(chunkSlotIndex)
            );
        }
    }

    clearDirtyFlags(world.dirty.window);
    clearDirtyFlags(world.dirty.brickMaps);
    clearDirtyFlags(world.dirty.brickPool);
    return worldDiff;
}

void clearGpuUploadDirtyState(VoxelWorld& world) {
    std::lock_guard<std::mutex> dirtyLock(world.dirtyStateMutex);
    clearDirtyFlags(world.dirty.window);
    clearDirtyFlags(world.dirty.brickMaps);
    clearDirtyFlags(world.dirty.brickPool);
}
