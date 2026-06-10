#pragma once

#include <array>
#include <cstdint>

inline constexpr uint32_t BRICK_SIZE = 8;
inline constexpr uint32_t BRICK_VOXEL_COUNT = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;

inline constexpr uint32_t COARSE_CELL_SIZE = 2;
inline constexpr uint32_t COARSE_CELLS_PER_AXIS = BRICK_SIZE / COARSE_CELL_SIZE;
inline constexpr uint32_t COARSE_CELL_COUNT = COARSE_CELLS_PER_AXIS * COARSE_CELLS_PER_AXIS * COARSE_CELLS_PER_AXIS;
inline constexpr uint32_t OCCUPANCY_MASK_WORD_COUNT = (COARSE_CELL_COUNT + 31u) / 32u;

inline constexpr uint8_t BRICK_EMPTY_VOXEL = 0u;
inline constexpr uint8_t BRICK_SOLID_VOXEL = 1u;

inline constexpr uint32_t PACKED_BRICK_VOXEL_WORD_COUNT = (BRICK_VOXEL_COUNT + 3u) / 4u;
inline constexpr uint32_t PACKED_BRICK_WORD_COUNT = OCCUPANCY_MASK_WORD_COUNT + PACKED_BRICK_VOXEL_WORD_COUNT;

static_assert(BRICK_SIZE % COARSE_CELL_SIZE == 0, "BRICK_SIZE must be divisible by COARSE_CELL_SIZE");

struct Brick {
    std::array<uint32_t, OCCUPANCY_MASK_WORD_COUNT> occupancyMaskWords{};
    uint8_t voxels[BRICK_SIZE][BRICK_SIZE][BRICK_SIZE]{};
};

