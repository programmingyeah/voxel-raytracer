#include "terrain_builder.hpp"

#include "materials.hpp"
#include "terrain_noise.hpp"
#include "voxel_world.hpp"

namespace {
constexpr uint32_t COLUMN_HEIGHT_SAMPLE_STRIDE = 8u;

struct BrickClassification {
    bool isAllAir = true;
    bool isAllSolid = true;
};

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

size_t columnIndex(uint32_t x, uint32_t z) {
    return static_cast<size_t>(x) + static_cast<size_t>(Chunk::SIZE) * z;
}

size_t brickMapIndex(uint32_t brickX, uint32_t brickY, uint32_t brickZ) {
    return static_cast<size_t>(brickX) +
           static_cast<size_t>(brickY) * Chunk::BRICKS_PER_AXIS +
           static_cast<size_t>(brickZ) * Chunk::BRICKS_PER_AXIS * Chunk::BRICKS_PER_AXIS;
}

uint32_t coarseSampleCountPerAxis() {
    return ((Chunk::SIZE - 1u) / COLUMN_HEIGHT_SAMPLE_STRIDE) + 2u;
}

uint32_t coarseSampleLocalCoordinate(uint32_t sampleIndex) {
    const uint32_t coordinate = sampleIndex * COLUMN_HEIGHT_SAMPLE_STRIDE;
    return coordinate < Chunk::SIZE ? coordinate : (Chunk::SIZE - 1u);
}

size_t coarseSampleIndex(uint32_t sampleX, uint32_t sampleZ, uint32_t sampleCountPerAxis) {
    return static_cast<size_t>(sampleX) + static_cast<size_t>(sampleCountPerAxis) * sampleZ;
}

void sampleChunkColumnHeights(
    const VoxelWorld& world,
    WorldLod lod,
    std::vector<uint32_t>& columnHeights,
    glm::ivec3 chunkCoordinate,
    uint32_t terrainWorldHeight
) {
    const uint32_t sampleCountPerAxis = coarseSampleCountPerAxis();
    std::vector<float> coarseHeights(static_cast<size_t>(sampleCountPerAxis) * sampleCountPerAxis);
    const glm::ivec3 chunkWorldMin = world.getChunkWorldMin(lod, chunkCoordinate);
    const int32_t cellWorldStep = static_cast<int32_t>(world.getLocalCellWorldStep(lod));

    for (uint32_t sampleZ = 0; sampleZ < sampleCountPerAxis; sampleZ++) {
        const uint32_t localZ = coarseSampleLocalCoordinate(sampleZ);
        const int32_t worldZ = chunkWorldMin.z + static_cast<int32_t>(localZ) * cellWorldStep;

        for (uint32_t sampleX = 0; sampleX < sampleCountPerAxis; sampleX++) {
            const uint32_t localX = coarseSampleLocalCoordinate(sampleX);
            const int32_t worldX = chunkWorldMin.x + static_cast<int32_t>(localX) * cellWorldStep;
            coarseHeights[coarseSampleIndex(sampleX, sampleZ, sampleCountPerAxis)] = sampleTerrainHeight(
                glm::vec2(static_cast<float>(worldX), static_cast<float>(worldZ)), terrainWorldHeight
            );
        }
    }

    for (uint32_t localZ = 0; localZ < Chunk::SIZE; localZ++) {
        const uint32_t sampleZ0 = localZ / COLUMN_HEIGHT_SAMPLE_STRIDE;
        const uint32_t sampleZ1 = sampleZ0 + 1u;
        const uint32_t localZ0 = coarseSampleLocalCoordinate(sampleZ0);
        const uint32_t localZ1 = coarseSampleLocalCoordinate(sampleZ1);
        const float zDenominator = static_cast<float>(localZ1 - localZ0);
        const float zT = zDenominator > 0.0f ? static_cast<float>(localZ - localZ0) / zDenominator : 0.0f;

        for (uint32_t localX = 0; localX < Chunk::SIZE; localX++) {
            const uint32_t sampleX0 = localX / COLUMN_HEIGHT_SAMPLE_STRIDE;
            const uint32_t sampleX1 = sampleX0 + 1u;
            const uint32_t localX0 = coarseSampleLocalCoordinate(sampleX0);
            const uint32_t localX1 = coarseSampleLocalCoordinate(sampleX1);
            const float xDenominator = static_cast<float>(localX1 - localX0);
            const float xT = xDenominator > 0.0f ? static_cast<float>(localX - localX0) / xDenominator : 0.0f;

            const float h00 = coarseHeights[coarseSampleIndex(sampleX0, sampleZ0, sampleCountPerAxis)];
            const float h10 = coarseHeights[coarseSampleIndex(sampleX1, sampleZ0, sampleCountPerAxis)];
            const float h01 = coarseHeights[coarseSampleIndex(sampleX0, sampleZ1, sampleCountPerAxis)];
            const float h11 = coarseHeights[coarseSampleIndex(sampleX1, sampleZ1, sampleCountPerAxis)];
            const float interpolatedHeight = lerp(lerp(h00, h10, xT), lerp(h01, h11, xT), zT);

            columnHeights[columnIndex(localX, localZ)] = static_cast<uint32_t>(interpolatedHeight + 0.5f);
        }
    }
}

BrickClassification classifyBrick(
    const std::vector<uint32_t>& columnHeights,
    uint32_t brickBaseLocalX,
    uint32_t brickBaseLocalZ,
    uint32_t brickMinWorldY,
    uint32_t brickMaxWorldY
) {
    BrickClassification classification{};

    for (uint32_t localZ = 0; localZ < BRICK_SIZE; localZ++) {
        for (uint32_t localX = 0; localX < BRICK_SIZE; localX++) {
            const uint32_t maxSolidWorldY = columnHeights[columnIndex(brickBaseLocalX + localX, brickBaseLocalZ + localZ)];
            if (maxSolidWorldY >= brickMinWorldY) {
                classification.isAllAir = false;
            }
            if (maxSolidWorldY < brickMaxWorldY) {
                classification.isAllSolid = false;
            }
        }
    }

    return classification;
}

uint64_t buildMixedBrick(
    const VoxelWorld& world,
    WorldLod lod,
    Brick& brick,
    const std::vector<uint32_t>& columnHeights,
    uint32_t brickBaseLocalX,
    uint32_t brickBaseLocalZ,
    uint32_t brickMinWorldY
) {
    uint64_t solidVoxelCount = 0;
    const uint32_t cellWorldStep = world.getLocalCellWorldStep(lod);

    for (uint32_t localZ = 0; localZ < BRICK_SIZE; localZ++) {
        for (uint32_t localX = 0; localX < BRICK_SIZE; localX++) {
            const uint32_t maxSolidWorldY = columnHeights[columnIndex(brickBaseLocalX + localX, brickBaseLocalZ + localZ)];

            for (uint32_t localY = 0; localY < BRICK_SIZE; localY++) {
                const uint32_t worldY = brickMinWorldY + localY * cellWorldStep;
                const uint8_t voxelValue = worldY <= maxSolidWorldY ? BRICK_SOLID_VOXEL : BRICK_EMPTY_VOXEL;
                brick.voxels[localX][localY][localZ] = voxelValue;
                solidVoxelCount += voxelValue != BRICK_EMPTY_VOXEL;
            }
        }
    }

    Chunk::recomputeOccupancyMask(brick);
    return solidVoxelCount;
}
}

void buildTerrainChunk(VoxelWorld& world, WorldLod lod, uint32_t chunkSlotIndex, uint32_t terrainWorldHeight, TerrainBuildResult& outResult) {
    Chunk& chunk = world.getChunkByMutableSlotIndex(lod, chunkSlotIndex);
    Chunk::EncodedBrickMap& brickMap = chunk.accessBrickMapForGeneration();
    std::vector<uint32_t> columnHeights(static_cast<size_t>(Chunk::SIZE) * Chunk::SIZE);

    outResult.solidVoxelCount = 0;
    outResult.allocatedBrickIndices.clear();
    outResult.allocatedBrickIndices.reserve(Chunk::BRICK_COUNT);
    chunk.resetBrickMapToAirNoCallbacks();

    const glm::ivec3 chunkCoordinate = chunk.getChunkCoordinate();
    sampleChunkColumnHeights(world, lod, columnHeights, chunkCoordinate, terrainWorldHeight);

    const uint32_t chunkBaseWorldY = static_cast<uint32_t>(world.getChunkWorldMin(lod, chunkCoordinate).y);
    const uint32_t cellWorldStep = world.getLocalCellWorldStep(lod);
    for (uint32_t brickZ = 0; brickZ < Chunk::BRICKS_PER_AXIS; brickZ++) {
        const uint32_t brickBaseLocalZ = brickZ * BRICK_SIZE;

        for (uint32_t brickY = 0; brickY < Chunk::BRICKS_PER_AXIS; brickY++) {
            const uint32_t brickBaseLocalY = brickY * BRICK_SIZE;
            const uint32_t brickMinWorldY = chunkBaseWorldY + brickBaseLocalY * cellWorldStep;
            const uint32_t brickMaxWorldY = brickMinWorldY + BRICK_SIZE * cellWorldStep - 1u;

            for (uint32_t brickX = 0; brickX < Chunk::BRICKS_PER_AXIS; brickX++) {
                const uint32_t brickBaseLocalX = brickX * BRICK_SIZE;
                const size_t mapIndex = brickMapIndex(brickX, brickY, brickZ);
                const BrickClassification classification = classifyBrick(
                    columnHeights,
                    brickBaseLocalX,
                    brickBaseLocalZ,
                    brickMinWorldY,
                    brickMaxWorldY
                );

                if (classification.isAllAir) {
                    brickMap[mapIndex] = BrickMapEntry{BRICK_MAP_EMPTY, AIR_MATERIAL};
                    continue;
                }

                if (classification.isAllSolid) {
                    brickMap[mapIndex] = BrickMapEntry{BRICK_MAP_EMPTY, STONE_MATERIAL};
                    outResult.solidVoxelCount += BRICK_VOXEL_COUNT;
                    continue;
                }

                Brick brick{};
                outResult.solidVoxelCount += buildMixedBrick(
                    world,
                    lod,
                    brick,
                    columnHeights,
                    brickBaseLocalX,
                    brickBaseLocalZ,
                    brickMinWorldY
                );

                const uint32_t brickIndex = world.allocateBrick();
                world.brickPool.bricks.at(brickIndex) = brick;
                brickMap[mapIndex] = BrickMapEntry{brickIndex, STONE_MATERIAL};
                outResult.allocatedBrickIndices.push_back(brickIndex);
            }
        }
    }
}
