#include "chunk.hpp"

#include <cstring>
#include <utility>

std::vector<Brick> Chunk::bricks{};

namespace {
constexpr uint32_t LAST_OCCUPANCY_WORD_BITS = COARSE_CELL_COUNT % 32u;

uint32_t brickMapIndexFromVoxel(uint32_t x, uint32_t y, uint32_t z) {
    const uint32_t brickX = x / BRICK_SIZE;
    const uint32_t brickY = y / BRICK_SIZE;
    const uint32_t brickZ = z / BRICK_SIZE;

    return brickX +
           brickY * Chunk::BRICKS_PER_AXIS +
           brickZ * Chunk::BRICKS_PER_AXIS * Chunk::BRICKS_PER_AXIS;
}

uint32_t brickMapIndexFromBrickCoord(uint32_t brickX, uint32_t brickY, uint32_t brickZ) {
    return brickX +
           brickY * Chunk::BRICKS_PER_AXIS +
           brickZ * Chunk::BRICKS_PER_AXIS * Chunk::BRICKS_PER_AXIS;
}

uint32_t coarseCellIndex(uint32_t x, uint32_t y, uint32_t z) {
    return x + COARSE_CELLS_PER_AXIS * (y + COARSE_CELLS_PER_AXIS * z);
}

BrickMapEntry makeBrickMapEntry(uint32_t materialId, uint32_t index = BRICK_MAP_EMPTY) {
    return BrickMapEntry{index, static_cast<uint8_t>(materialId)}; //in-case we make materialId larger in the future
}

bool hasExplicitBrick(const BrickMapEntry& entry) {
    return entry.index != BRICK_MAP_EMPTY;
}

void setOccupancyBit(Brick& brick, uint32_t bitIndex) {
    const uint32_t wordIndex = bitIndex / 32u;
    const uint32_t bitInWord = bitIndex % 32u;
    brick.occupancyMaskWords[wordIndex] |= 1u << bitInWord;
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

Chunk::Chunk(glm::ivec3 inChunkCoordinate, size_t inChunkIndex)
    : chunkCoordinate(inChunkCoordinate), chunkIndex(inChunkIndex) {
    clear();
}

uint32_t Chunk::get(uint32_t x, uint32_t y, uint32_t z) const {
    const BrickMapEntry& entry = brickMap[brickMapIndexFromVoxel(x, y, z)];
    if (!hasExplicitBrick(entry)) {
        return entry.materialId;
    }

    const Brick& brick = bricks[entry.index];
    const uint32_t localX = x % BRICK_SIZE;
    const uint32_t localY = y % BRICK_SIZE;
    const uint32_t localZ = z % BRICK_SIZE;

    return brick.voxels[localX][localY][localZ] != BRICK_EMPTY_VOXEL ? entry.materialId : AIR_MATERIAL;
}

void Chunk::set(uint32_t x, uint32_t y, uint32_t z, uint32_t value) {
    const uint32_t mapIndex = brickMapIndexFromVoxel(x, y, z);
    BrickMapEntry& entry = brickMap[mapIndex];
    const uint32_t localX = x % BRICK_SIZE;
    const uint32_t localY = y % BRICK_SIZE;
    const uint32_t localZ = z % BRICK_SIZE;

    if (!hasExplicitBrick(entry)) {
        if (entry.materialId == value) {
            return;
        }

        Brick brick{};
        fillBrick(brick, static_cast<uint8_t>(entry.materialId));
        brick.voxels[localX][localY][localZ] = value != AIR_MATERIAL ? BRICK_SOLID_VOXEL : BRICK_EMPTY_VOXEL;
        recomputeOccupancyMask(brick);

        if (isUniformBrickOccupancy(brick, BRICK_EMPTY_VOXEL)) {
            entry = makeBrickMapEntry(AIR_MATERIAL);
            markBrickMapDirty(mapIndex);
            return;
        }
        if (isUniformBrickOccupancy(brick, BRICK_SOLID_VOXEL)) {
            entry = makeBrickMapEntry(value);
            markBrickMapDirty(mapIndex);
            return;
        }

        const uint32_t explicitMaterialId = value != AIR_MATERIAL ? value : entry.materialId;
        bricks.push_back(brick);
        const uint32_t brickIndex = static_cast<uint32_t>(bricks.size() - 1);
        entry = makeBrickMapEntry(explicitMaterialId, brickIndex);
        markBrickMapDirty(mapIndex);
        markBrickPoolDirty(brickIndex);
        return;
    }

    const uint32_t explicitBrickIndex = entry.index;
    Brick& brick = bricks[explicitBrickIndex];
    brick.voxels[localX][localY][localZ] = value != AIR_MATERIAL ? BRICK_SOLID_VOXEL : BRICK_EMPTY_VOXEL;
    if (value != AIR_MATERIAL) {
        entry.materialId = value;
    }
    recomputeOccupancyMask(brick);
    markBrickPoolDirty(explicitBrickIndex);

    if (isUniformBrickOccupancy(brick, BRICK_EMPTY_VOXEL)) {
        entry = makeBrickMapEntry(AIR_MATERIAL);
        markBrickMapDirty(mapIndex);
    } else if (isUniformBrickOccupancy(brick, BRICK_SOLID_VOXEL)) {
        entry = makeBrickMapEntry(entry.materialId);
        markBrickMapDirty(mapIndex);
    } else {
        markBrickMapDirty(mapIndex);
    }
}

void Chunk::setBrickUniform(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId) {
    const uint32_t mapIndex = brickMapIndexFromBrickCoord(brickX, brickY, brickZ);
    brickMap[mapIndex] = makeBrickMapEntry(materialId);
    markBrickMapDirty(mapIndex);
}

void Chunk::setBrickExplicit(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId, const Brick& brick) {
    const uint32_t mapIndex = brickMapIndexFromBrickCoord(brickX, brickY, brickZ);

    if (isUniformBrickOccupancy(brick, BRICK_EMPTY_VOXEL)) {
        brickMap[mapIndex] = makeBrickMapEntry(AIR_MATERIAL);
        markBrickMapDirty(mapIndex);
        return;
    }

    if (isUniformBrickOccupancy(brick, BRICK_SOLID_VOXEL)) {
        brickMap[mapIndex] = makeBrickMapEntry(materialId);
        markBrickMapDirty(mapIndex);
        return;
    }

    bricks.push_back(brick);
    const uint32_t brickIndex = static_cast<uint32_t>(bricks.size() - 1);
    brickMap[mapIndex] = makeBrickMapEntry(materialId, brickIndex);
    markBrickMapDirty(mapIndex);
    markBrickPoolDirty(brickIndex);
}

void Chunk::clear() {
    brickMap.fill(makeBrickMapEntry(AIR_MATERIAL));
    markWholeChunkDirty();
}

void Chunk::setDirtyCallbacks(
    ChunkBrickMapDirtyCallback inChunkBrickMapDirtyCallback,
    BrickPoolDirtyCallback inBrickPoolDirtyCallback
) {
    chunkBrickMapDirtyCallback = std::move(inChunkBrickMapDirtyCallback);
    brickPoolDirtyCallback = std::move(inBrickPoolDirtyCallback);
}

void Chunk::reserveBrickPool(size_t brickCount) {
    bricks.reserve(brickCount);
}

void Chunk::resetBrickPool() {
    bricks.clear();
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
        chunkBrickMapDirtyCallback(chunkIndex, mapIndex);
    }
}

void Chunk::markWholeChunkDirty() {
    if (!chunkBrickMapDirtyCallback) {
        return;
    }

    for (uint32_t mapIndex = 0; mapIndex < BRICK_COUNT; mapIndex++) {
        chunkBrickMapDirtyCallback(chunkIndex, mapIndex);
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