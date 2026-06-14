#pragma once

#include "brick.hpp"
#include "materials.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

using ChunkBrickMapDirtyCallback = std::function<void(size_t, uint32_t)>;
using AllocateBrickCallback = std::function<uint32_t()>;
using ReleaseBrickCallback = std::function<void(uint32_t)>;
using BrickPoolDirtyCallback = std::function<void(uint32_t)>;

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VoxelWorld;
struct TerrainBuildResult;
void buildTerrainChunk(VoxelWorld& world, uint32_t chunkSlotIndex, const glm::uvec3& voxelDimensions, TerrainBuildResult& outResult);

inline constexpr uint32_t BRICK_MAP_EMPTY = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t PACKED_BRICK_MAP_ENTRY_WORD_COUNT = 2u;

struct BrickMapEntry {
    uint32_t index = BRICK_MAP_EMPTY;
    MaterialId materialId = AIR_MATERIAL;
};

class Chunk {
public:
    static constexpr uint32_t SIZE = 128;
    static constexpr uint32_t BRICKS_PER_AXIS = SIZE / BRICK_SIZE;
    static constexpr size_t BRICK_COUNT = static_cast<size_t>(BRICKS_PER_AXIS) * BRICKS_PER_AXIS * BRICKS_PER_AXIS;
    static constexpr size_t VOXEL_COUNT = static_cast<size_t>(SIZE) * SIZE * SIZE;

    static_assert(SIZE % BRICK_SIZE == 0, "Chunk::SIZE must be divisible by BRICK_SIZE");

    using EncodedBrickMap = std::array<BrickMapEntry, BRICK_COUNT>;

    explicit Chunk(glm::ivec3 chunkCoordinate = glm::ivec3(0), size_t chunkSlotIndex = 0);

    glm::ivec3 getChunkCoordinate() const { return chunkCoordinate; }
    const EncodedBrickMap& getBrickMap() const { return brickMap; }

    uint32_t get(uint32_t x, uint32_t y, uint32_t z) const;
    void set(uint32_t x, uint32_t y, uint32_t z, uint32_t value);
    void setBrickUniform(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId);
    void setBrick(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId, const Brick& brick);
    void clear();

    static void recomputeOccupancyMask(Brick& brick);
    static void fillBrick(Brick& brick, uint8_t value);

private:
    friend class VoxelWorld;
    friend void buildTerrainChunk(VoxelWorld& world, uint32_t chunkSlotIndex, const glm::uvec3& voxelDimensions, TerrainBuildResult& outResult);

    void setChunkCoordinate(glm::ivec3 inChunkCoordinate) { chunkCoordinate = inChunkCoordinate; }
    EncodedBrickMap& accessBrickMapForGeneration() { return brickMap; }
    void resetBrickMapToAirNoCallbacks();
    void setStorageCallbacks(
        std::vector<Brick>* inBrickPool,
        AllocateBrickCallback inAllocateBrickCallback,
        ReleaseBrickCallback inReleaseBrickCallback,
        ChunkBrickMapDirtyCallback inChunkBrickMapDirtyCallback,
        BrickPoolDirtyCallback inBrickPoolDirtyCallback
    );
    std::vector<Brick>& brickPool();
    const std::vector<Brick>& brickPool() const;
    uint32_t allocateBrick();
    void releaseBrick(uint32_t brickIndex);
    void markBrickMapDirty(uint32_t mapIndex);
    void markWholeChunkDirty();
    void markBrickPoolDirty(uint32_t brickIndex);

    //position of the chunk
    glm::ivec3 chunkCoordinate{};
    //slot index of the chunk (in VoxelWorld)
    size_t chunkSlotIndex = 0;
    //chunks brickmap!!
    EncodedBrickMap brickMap{};

    //brick pool is owned by VoxelWorld
    std::vector<Brick>* externalBrickPool = nullptr;

    //allocations happen through VoxelWorld
    AllocateBrickCallback allocateBrickCallback;
    ReleaseBrickCallback releaseBrickCallback;
    ChunkBrickMapDirtyCallback chunkBrickMapDirtyCallback;
    BrickPoolDirtyCallback brickPoolDirtyCallback;
};