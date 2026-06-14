#include "world_upload.hpp"

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
    chunkBrickMaps[packedEntryIndex + 1u] = entry.materialId;
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
    gpuBuffers.chunkBrickMaps.resize(world.slots.size() * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT);
    for (size_t chunkSlotIndex = 0; chunkSlotIndex < world.slots.size(); chunkSlotIndex++) {
        const auto& brickMap = world.slots[chunkSlotIndex].chunk.getBrickMap();
        const size_t chunkBaseIndex = chunkSlotIndex * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
        const bool chunkGenerated = world.isChunkSlotGenerated(chunkSlotIndex);

        for (size_t brickIndex = 0; brickIndex < Chunk::BRICK_COUNT; brickIndex++) {
            const size_t entryBaseIndex = chunkBaseIndex + brickIndex * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
            packBrickMapEntry(
                gpuBuffers.chunkBrickMaps,
                entryBaseIndex,
                chunkGenerated ? brickMap[brickIndex] : emptyBrickMapEntry()
            );
        }
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
    worldDiff.chunkBrickMaps.totalWordCount = world.slots.size() * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
    worldDiff.brickData.totalWordCount = world.brickPool.bricks.size() * PACKED_BRICK_WORD_COUNT;

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
        const size_t spanStartEntry = span.first;
        const size_t spanEntryCount = span.second;
        const size_t dstWordOffset = spanStartEntry * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
        const size_t wordCount = spanEntryCount * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
        const size_t srcWordOffset = worldDiff.chunkBrickMaps.data.size();

        worldDiff.chunkBrickMaps.data.resize(srcWordOffset + wordCount, 0u);
        worldDiff.chunkBrickMaps.regions.push_back({srcWordOffset, dstWordOffset, wordCount});

        for (size_t entryOffset = 0; entryOffset < spanEntryCount; entryOffset++) {
            const size_t globalEntryIndex = spanStartEntry + entryOffset;
            const size_t chunkSlotIndex = globalEntryIndex / Chunk::BRICK_COUNT;
            const size_t brickIndex = globalEntryIndex % Chunk::BRICK_COUNT;
            const size_t packedEntryIndex = srcWordOffset + entryOffset * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
            const BrickMapEntry entry = world.isChunkSlotGenerated(chunkSlotIndex)
                ? world.slots[chunkSlotIndex].chunk.getBrickMap()[brickIndex]
                : emptyBrickMapEntry();
            packBrickMapEntry(worldDiff.chunkBrickMaps.data, packedEntryIndex, entry);
        }
    }

    for (const auto& span : buildDirtySpans(world.dirty.brickPool)) {
        const size_t spanStartBrick = span.first;
        const size_t spanBrickCount = span.second;
        const size_t dstWordOffset = spanStartBrick * PACKED_BRICK_WORD_COUNT;
        const size_t wordCount = spanBrickCount * PACKED_BRICK_WORD_COUNT;
        const size_t srcWordOffset = worldDiff.brickData.data.size();

        worldDiff.brickData.data.resize(srcWordOffset + wordCount, 0u);
        worldDiff.brickData.regions.push_back({srcWordOffset, dstWordOffset, wordCount});

        for (size_t brickOffset = 0; brickOffset < spanBrickCount; brickOffset++) {
            packBrick(
                worldDiff.brickData.data,
                (srcWordOffset / PACKED_BRICK_WORD_COUNT) + brickOffset,
                world.brickPool.bricks[spanStartBrick + brickOffset]
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
