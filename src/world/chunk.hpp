#pragma once

#include "brick.hpp"
#include "materials.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

inline constexpr uint32_t BRICK_MAP_EMPTY = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t PACKED_BRICK_MAP_ENTRY_WORD_COUNT = 2u;

struct BrickMapEntry {
    uint32_t index = BRICK_MAP_EMPTY;
    uint8_t materialId = AIR_MATERIAL;
};

class Chunk {
public:
    static constexpr uint32_t SIZE = 128;
    static constexpr uint32_t BRICKS_PER_AXIS = SIZE / BRICK_SIZE;
    static constexpr size_t BRICK_COUNT = static_cast<size_t>(BRICKS_PER_AXIS) * BRICKS_PER_AXIS * BRICKS_PER_AXIS;
    static constexpr size_t VOXEL_COUNT = static_cast<size_t>(SIZE) * SIZE * SIZE;

    static_assert(SIZE % BRICK_SIZE == 0, "Chunk::SIZE must be divisible by BRICK_SIZE");

    using EncodedBrickMap = std::array<BrickMapEntry, BRICK_COUNT>;

    explicit Chunk(glm::ivec3 chunkCoordinate = glm::ivec3(0));

    uint32_t get(uint32_t x, uint32_t y, uint32_t z) const;
    void set(uint32_t x, uint32_t y, uint32_t z, uint32_t value);

    void setBrickUniform(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId);
    void setBrickExplicit(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId, const Brick& brick);
    void clear();

    glm::ivec3 getChunkCoordinate() const { return chunkCoordinate; }
    const EncodedBrickMap& getBrickMap() const { return brickMap; }

    static const std::vector<Brick>& getBrickPool() { return bricks; }
    static void reserveBrickPool(size_t brickCount);
    static void resetBrickPool();
    static void recomputeOccupancyMask(Brick& brick);
    static void fillBrick(Brick& brick, uint8_t value);

private:
    glm::ivec3 chunkCoordinate{};
    EncodedBrickMap brickMap{};

    // Brick map entries always store a material id for uniform shading / LOD fallback.
    // index == BRICK_MAP_EMPTY means no explicit brick pool entry is referenced.
    static std::vector<Brick> bricks;
};