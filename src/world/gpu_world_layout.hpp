#pragma once

#include "brick.hpp"
#include "chunk.hpp"

#include <cstddef>
#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

inline constexpr uint32_t BRICK_REQUEST_HEADER_WORD_COUNT = 3u;
inline constexpr uint32_t CHUNK_ACCEL_EMPTY_FLAG = 1u << 0u;
inline constexpr uint32_t CHUNK_ACCEL_8_WORD_COUNT = (8u * 8u * 8u + 31u) / 32u;
inline constexpr uint32_t CHUNK_ACCEL_4_WORD_COUNT = (4u * 4u * 4u + 31u) / 32u;
inline constexpr uint32_t CHUNK_ACCEL_2_WORD_COUNT = (2u * 2u * 2u + 31u) / 32u;
inline constexpr uint32_t CHUNK_ACCEL_FLAG_WORD_COUNT = 1u;
inline constexpr uint32_t CHUNK_ACCEL_WORD_COUNT =
    CHUNK_ACCEL_FLAG_WORD_COUNT + CHUNK_ACCEL_8_WORD_COUNT + CHUNK_ACCEL_4_WORD_COUNT + CHUNK_ACCEL_2_WORD_COUNT;
inline constexpr uint32_t CHUNK_ACCEL_8_OFFSET = CHUNK_ACCEL_FLAG_WORD_COUNT;
inline constexpr uint32_t CHUNK_ACCEL_4_OFFSET = CHUNK_ACCEL_8_OFFSET + CHUNK_ACCEL_8_WORD_COUNT;
inline constexpr uint32_t CHUNK_ACCEL_2_OFFSET = CHUNK_ACCEL_4_OFFSET + CHUNK_ACCEL_4_WORD_COUNT;
inline constexpr uint32_t CHUNK_BRICK_MAP_OFFSET = CHUNK_ACCEL_WORD_COUNT;
inline constexpr uint32_t PACKED_CHUNK_WORD_COUNT = CHUNK_ACCEL_WORD_COUNT + Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;

inline constexpr uint32_t chunkEntryWordOffset(uint32_t packedBrickIndex) {
    return CHUNK_BRICK_MAP_OFFSET + packedBrickIndex * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
}

inline constexpr size_t chunkEntryCount(size_t chunkCount) {
    return chunkCount * Chunk::BRICK_COUNT;
}

inline constexpr size_t chunkEntryIndex(size_t chunkSlotIndex, uint32_t packedBrickIndex) {
    return chunkSlotIndex * Chunk::BRICK_COUNT + packedBrickIndex;
}

// Interleaves 4 bits per axis into a 12-bit Morton index:
// x -> 0/3/6/9, y -> 1/4/7/10, z -> 2/5/8/11.
inline constexpr uint32_t mortonBrickIndex(uint32_t x, uint32_t y, uint32_t z) {
    return
        ((x & 0x1u) << 0u) |
        ((y & 0x1u) << 1u) |
        ((z & 0x1u) << 2u) |
        ((x & 0x2u) << 2u) |
        ((y & 0x2u) << 3u) |
        ((z & 0x2u) << 4u) |
        ((x & 0x4u) << 4u) |
        ((y & 0x4u) << 5u) |
        ((z & 0x4u) << 6u) |
        ((x & 0x8u) << 6u) |
        ((y & 0x8u) << 7u) |
        ((z & 0x8u) << 8u);
}

// Inverts the 4-bit-per-axis Morton layout used for chunk brick map entries:
// x bits live at 0/3/6/9, y at 1/4/7/10, z at 2/5/8/11.
inline glm::uvec3 decodeBrickMorton(uint32_t mortonIndex) {
    auto decodeAxis = [mortonIndex](uint32_t axisBitOffset) {
        return ((mortonIndex >> axisBitOffset) & 0x1u) |
               (((mortonIndex >> (axisBitOffset + 2u)) & 0x2u)) |
               (((mortonIndex >> (axisBitOffset + 4u)) & 0x4u)) |
               (((mortonIndex >> (axisBitOffset + 6u)) & 0x8u));
    };

    return glm::uvec3(
        decodeAxis(0u),
        decodeAxis(1u),
        decodeAxis(2u)
    );
}