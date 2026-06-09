#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// huge note to self: first MATERIAL_COUNT indexes in the brick map stand for uniform-material bricks, past MATERIAL_COUNT
// starts the actual indexing of the bricks array

static constexpr uint32_t BRICK_SIZE = 8; // should be a divisor of SIZE
static constexpr uint32_t BRICK_VOXEL_COUNT = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;
static constexpr uint32_t COARSE_CELL_SIZE = 2;
static constexpr uint32_t COARSE_CELLS_PER_AXIS = BRICK_SIZE / COARSE_CELL_SIZE;
static constexpr uint32_t COARSE_CELL_COUNT = COARSE_CELLS_PER_AXIS * COARSE_CELLS_PER_AXIS * COARSE_CELLS_PER_AXIS;
static constexpr uint32_t OCCUPANCY_MASK_WORD_COUNT = (COARSE_CELL_COUNT + 31u) / 32u;
static constexpr uint32_t PACKED_BRICK_VOXEL_WORD_COUNT = (BRICK_VOXEL_COUNT + 3u) / 4u;
static constexpr uint32_t PACKED_BRICK_WORD_COUNT = OCCUPANCY_MASK_WORD_COUNT + PACKED_BRICK_VOXEL_WORD_COUNT;

struct Brick {
    uint64_t occupancy_mask;
    uint8_t voxels[BRICK_SIZE][BRICK_SIZE][BRICK_SIZE]{};
};

class Chunk {
public:
    static constexpr uint32_t SIZE = 128;
    static constexpr size_t BRICK_COUNT = (size_t) (SIZE/BRICK_SIZE) * (SIZE/BRICK_SIZE) * (SIZE/BRICK_SIZE);
    static constexpr size_t VOXEL_COUNT = static_cast<size_t>(SIZE) * SIZE * SIZE;

    explicit Chunk(glm::ivec3 chunkCoordinate = glm::ivec3(0));

    uint32_t get(uint32_t x, uint32_t y, uint32_t z) const;
    void set(uint32_t x, uint32_t y, uint32_t z, uint32_t value);
    void clear();

    glm::ivec3 getChunkCoordinate() const { return chunkCoordinate; }
    const std::array<uint32_t, BRICK_COUNT>& getBrickMap() const { return brickMap; }
    static const std::vector<Brick>& getBrickPool() { return bricks; }

private:
    glm::ivec3 chunkCoordinate{};

    //brickmap implementation
    static std::vector<Brick> bricks;
    std::array<uint32_t, BRICK_COUNT> brickMap{};
};