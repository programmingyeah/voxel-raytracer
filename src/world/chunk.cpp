#include "chunk.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace {
constexpr uint32_t LAST_OCCUPANCY_WORD_BITS = COARSE_CELL_COUNT % 32u;

uint32_t brickMapIndex(uint32_t brickX, uint32_t brickY, uint32_t brickZ) {
    return brickX +
           brickY * Chunk::BRICKS_PER_AXIS +
           brickZ * Chunk::BRICKS_PER_AXIS * Chunk::BRICKS_PER_AXIS;
}

uint32_t brickMapIndexFromVoxel(uint32_t x, uint32_t y, uint32_t z) {
    return brickMapIndex(x / BRICK_SIZE, y / BRICK_SIZE, z / BRICK_SIZE);
}

uint32_t coarseCellIndex(uint32_t x, uint32_t y, uint32_t z) {
    return x + COARSE_CELLS_PER_AXIS * (y + COARSE_CELLS_PER_AXIS * z);
}

BrickMapEntry makeBrickMapEntry(uint32_t materialId, uint32_t index = BRICK_MAP_EMPTY) {
    return BrickMapEntry{index, static_cast<uint8_t>(materialId)}; //in-case we make materialId larger in the future
}

bool hasBrick(const BrickMapEntry& entry) {
    return entry.index != BRICK_MAP_EMPTY;
}

void setOccupancyBit(Brick& brick, uint32_t bitIndex) {
    brick.occupancyMaskWords[bitIndex / 32u] |= 1u << (bitIndex % 32u);
}

void setAllOccupancyBits(Brick& brick) {
    brick.occupancyMaskWords.fill(0xffffffffu);

    if constexpr (LAST_OCCUPANCY_WORD_BITS != 0u) {
        brick.occupancyMaskWords.back() = (1u << LAST_OCCUPANCY_WORD_BITS) - 1u;
    }
}

bool isUniformBrickOccupancy(const Brick& brick, uint8_t occupancyValue) {
    for (uint32_t z = 0; z < BRICK_SIZE; z++) {
        for (uint32_t y = 0; y < BRICK_SIZE; y++) {
            for (uint32_t x = 0; x < BRICK_SIZE; x++) {
                if (brick.voxels[x][y][z] != occupancyValue) {
                    return false;
                }
            }
        }
    }

    return true;
}
}

Chunk::Chunk(glm::ivec3 inChunkCoordinate, size_t inChunkSlotIndex)
    : chunkCoordinate(inChunkCoordinate), chunkSlotIndex(inChunkSlotIndex) {
    clear();
}

std::vector<Brick>& Chunk::brickPool() {
    if (externalBrickPool == nullptr) {
        throw std::runtime_error("chunk brick pool is not initialized");
    }

    return *externalBrickPool;
}

const std::vector<Brick>& Chunk::brickPool() const {
    if (externalBrickPool == nullptr) {
        throw std::runtime_error("chunk brick pool is not initialized");
    }

    return *externalBrickPool;
}

uint32_t Chunk::allocateBrick() {
    if (!allocateBrickCallback) {
        throw std::runtime_error("chunk brick allocator callback is not initialized");
    }

    return allocateBrickCallback();
}

void Chunk::releaseBrick(uint32_t brickIndex) {
    if (!releaseBrickCallback) {
        throw std::runtime_error("chunk brick release callback is not initialized");
    }

    releaseBrickCallback(brickIndex);
}

uint32_t Chunk::get(uint32_t x, uint32_t y, uint32_t z) const {
    const BrickMapEntry& entry = brickMap[brickMapIndexFromVoxel(x, y, z)];
    if (!hasBrick(entry)) {
        return entry.materialId;
    }

    const Brick& brick = brickPool()[entry.index];
    return brick.voxels[x % BRICK_SIZE][y % BRICK_SIZE][z % BRICK_SIZE] != BRICK_EMPTY_VOXEL ? entry.materialId : AIR_MATERIAL;
}

void Chunk::set(uint32_t x, uint32_t y, uint32_t z, uint32_t value) {
    const uint32_t mapIndex = brickMapIndexFromVoxel(x, y, z);
    BrickMapEntry& entry = brickMap[mapIndex];
    const uint32_t localX = x % BRICK_SIZE, localY = y % BRICK_SIZE, localZ = z % BRICK_SIZE;

    if (!hasBrick(entry)) {
        if (entry.materialId == value) {
            return;
        }

        Brick brick{};
        fillBrick(brick, static_cast<uint8_t>(entry.materialId));
        brick.voxels[localX][localY][localZ] = value != AIR_MATERIAL ? BRICK_SOLID_VOXEL : BRICK_EMPTY_VOXEL;
        recomputeOccupancyMask(brick);

        const bool isAllAir = isUniformBrickOccupancy(brick, BRICK_EMPTY_VOXEL);
        const bool isAllSolid = !isAllAir && isUniformBrickOccupancy(brick, BRICK_SOLID_VOXEL);
        if (isAllAir || isAllSolid) {
            entry = makeBrickMapEntry(isAllAir ? AIR_MATERIAL : value);
            markBrickMapDirty(mapIndex);
            return;
        }

        const uint32_t brickIndex = allocateBrick();
        brickPool().at(brickIndex) = brick;
        entry = makeBrickMapEntry(value != AIR_MATERIAL ? value : entry.materialId, brickIndex);
        markBrickPoolDirty(brickIndex);
        markBrickMapDirty(mapIndex);
        return;
    }

    const uint32_t brickIndex = entry.index;
    Brick& brick = brickPool()[brickIndex];
    brick.voxels[localX][localY][localZ] = value != AIR_MATERIAL ? BRICK_SOLID_VOXEL : BRICK_EMPTY_VOXEL;
    if (value != AIR_MATERIAL) {
        entry.materialId = value;
    }
    recomputeOccupancyMask(brick);

    const bool isAllAir = isUniformBrickOccupancy(brick, BRICK_EMPTY_VOXEL);
    const bool isAllSolid = !isAllAir && isUniformBrickOccupancy(brick, BRICK_SOLID_VOXEL);
    if (isAllAir || isAllSolid) {
        releaseBrick(brickIndex);
        entry = makeBrickMapEntry(isAllAir ? AIR_MATERIAL : entry.materialId);
    } else {
        markBrickPoolDirty(brickIndex);
    }

    markBrickMapDirty(mapIndex);
}

void Chunk::setBrickUniform(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId) {
    const uint32_t mapIndex = brickMapIndex(brickX, brickY, brickZ);
    BrickMapEntry& entry = brickMap[mapIndex];

    if (hasBrick(entry)) {
        releaseBrick(entry.index);
    }

    entry = makeBrickMapEntry(materialId);
    markBrickMapDirty(mapIndex);
}

void Chunk::setBrick(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId, const Brick& brick) {
    const bool isAllAir = isUniformBrickOccupancy(brick, BRICK_EMPTY_VOXEL);
    const bool isAllSolid = !isAllAir && isUniformBrickOccupancy(brick, BRICK_SOLID_VOXEL);
    if (isAllAir || isAllSolid) {
        setBrickUniform(brickX, brickY, brickZ, isAllAir ? AIR_MATERIAL : materialId);
        return;
    }

    const uint32_t mapIndex = brickMapIndex(brickX, brickY, brickZ);
    BrickMapEntry& entry = brickMap[mapIndex];
    const uint32_t brickIndex = hasBrick(entry) ? entry.index : allocateBrick();
    brickPool().at(brickIndex) = brick;
    entry = makeBrickMapEntry(materialId, brickIndex);
    markBrickPoolDirty(brickIndex);
    markBrickMapDirty(mapIndex);
}

void Chunk::clear() {
    for (BrickMapEntry& entry : brickMap) {
        if (hasBrick(entry)) {
            releaseBrick(entry.index);
        }

        entry = makeBrickMapEntry(AIR_MATERIAL);
    }

    markWholeChunkDirty();
}

void Chunk::resetBrickMapToAirNoCallbacks() {
    for (BrickMapEntry& entry : brickMap) {
        entry = makeBrickMapEntry(AIR_MATERIAL);
    }
}

void Chunk::setStorageCallbacks(
    std::vector<Brick>* inBrickPool,
    AllocateBrickCallback inAllocateBrickCallback,
    ReleaseBrickCallback inReleaseBrickCallback,
    ChunkBrickMapDirtyCallback inChunkBrickMapDirtyCallback,
    BrickPoolDirtyCallback inBrickPoolDirtyCallback
) {
    externalBrickPool = inBrickPool;
    allocateBrickCallback = std::move(inAllocateBrickCallback);
    releaseBrickCallback = std::move(inReleaseBrickCallback);
    chunkBrickMapDirtyCallback = std::move(inChunkBrickMapDirtyCallback);
    brickPoolDirtyCallback = std::move(inBrickPoolDirtyCallback);
}

void Chunk::recomputeOccupancyMask(Brick& brick) {
    brick.occupancyMaskWords.fill(0u);

    for (uint32_t coarseZ = 0; coarseZ < COARSE_CELLS_PER_AXIS; coarseZ++) {
        for (uint32_t coarseY = 0; coarseY < COARSE_CELLS_PER_AXIS; coarseY++) {
            for (uint32_t coarseX = 0; coarseX < COARSE_CELLS_PER_AXIS; coarseX++) {
                bool occupied = false;

                for (uint32_t localZ = 0; localZ < COARSE_CELL_SIZE && !occupied; localZ++) {
                    for (uint32_t localY = 0; localY < COARSE_CELL_SIZE && !occupied; localY++) {
                        for (uint32_t localX = 0; localX < COARSE_CELL_SIZE; localX++) {
                            const uint32_t voxelX = coarseX * COARSE_CELL_SIZE + localX;
                            const uint32_t voxelY = coarseY * COARSE_CELL_SIZE + localY;
                            const uint32_t voxelZ = coarseZ * COARSE_CELL_SIZE + localZ;
                            if (brick.voxels[voxelX][voxelY][voxelZ] != BRICK_EMPTY_VOXEL) {
                                occupied = true;
                                break;
                            }
                        }
                    }
                }

                if (occupied) {
                    setOccupancyBit(brick, coarseCellIndex(coarseX, coarseY, coarseZ));
                }
            }
        }
    }
}

void Chunk::markBrickMapDirty(uint32_t mapIndex) {
    if (chunkBrickMapDirtyCallback) {
        chunkBrickMapDirtyCallback(chunkSlotIndex, mapIndex);
    }
}

void Chunk::markWholeChunkDirty() {
    if (!chunkBrickMapDirtyCallback) {
        return;
    }

    for (uint32_t mapIndex = 0; mapIndex < BRICK_COUNT; mapIndex++) {
        markBrickMapDirty(mapIndex);
    }
}

void Chunk::markBrickPoolDirty(uint32_t brickIndex) {
    if (brickPoolDirtyCallback) {
        brickPoolDirtyCallback(brickIndex);
    }
}

void Chunk::fillBrick(Brick& brick, uint8_t value) {
    std::memset(
        brick.voxels,
        value == AIR_MATERIAL ? BRICK_EMPTY_VOXEL : BRICK_SOLID_VOXEL,
        sizeof(brick.voxels)
    );

    if (value == AIR_MATERIAL) {
        brick.occupancyMaskWords.fill(0u);
        return;
    }

    setAllOccupancyBits(brick);
}