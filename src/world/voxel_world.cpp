#include "voxel_world.hpp"
#include "materials.hpp"
#include <stdexcept>

namespace {
size_t voxelIndex(const glm::uvec3& dimensions, uint32_t x, uint32_t y, uint32_t z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(dimensions.x) *
               (static_cast<size_t>(y) + static_cast<size_t>(dimensions.y) * static_cast<size_t>(z));
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
}

VoxelWorld::VoxelWorld(glm::uvec3 inChunkCounts) : chunkCounts(inChunkCounts) {
    if (chunkCounts.x == 0 || chunkCounts.y == 0 || chunkCounts.z == 0) {
        throw std::runtime_error("voxel world must contain at least one chunk in every dimension");
    }

    chunks.reserve(static_cast<size_t>(chunkCounts.x) * chunkCounts.y * chunkCounts.z);
    for (uint32_t z = 0; z < chunkCounts.z; z++) {
        for (uint32_t y = 0; y < chunkCounts.y; y++) {
            for (uint32_t x = 0; x < chunkCounts.x; x++) {
                chunks.emplace_back(glm::ivec3(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)));
            }
        }
    }
}

Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) {
    return chunks.at(chunkIndex(x, y, z));
}

const Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) const {
    return chunks.at(chunkIndex(x, y, z));
}

glm::uvec3 VoxelWorld::getVoxelDimensions() const {
    return chunkCounts * Chunk::SIZE;
}

GpuVoxelBuffers VoxelWorld::buildGpuBuffers() const {
    GpuVoxelBuffers gpuBuffers;

    gpuBuffers.chunkBrickMaps.resize(chunks.size() * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT);
    for (size_t chunkIndex = 0; chunkIndex < chunks.size(); chunkIndex++) {
        const auto& brickMap = chunks[chunkIndex].getBrickMap();
        const size_t chunkBaseIndex = chunkIndex * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;

        for (size_t brickIndex = 0; brickIndex < Chunk::BRICK_COUNT; brickIndex++) {
            const BrickMapEntry& entry = brickMap[brickIndex];
            const size_t entryBaseIndex = chunkBaseIndex + brickIndex * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
            gpuBuffers.chunkBrickMaps[entryBaseIndex] = entry.index;
            gpuBuffers.chunkBrickMaps[entryBaseIndex + 1u] = entry.materialId;
        }
    }

    const std::vector<Brick>& brickPool = Chunk::getBrickPool();
    gpuBuffers.brickData.assign(brickPool.size() * PACKED_BRICK_WORD_COUNT, 0u);

    for (size_t brickIndex = 0; brickIndex < brickPool.size(); brickIndex++) {
        packBrick(gpuBuffers.brickData, brickIndex, brickPool[brickIndex]);
    }

    return gpuBuffers;
}

std::vector<uint32_t> VoxelWorld::buildVoxelBuffer() const {
    const glm::uvec3 voxelDimensions = getVoxelDimensions();
    std::vector<uint32_t> voxels(
        static_cast<size_t>(voxelDimensions.x) * voxelDimensions.y * voxelDimensions.z,
        AIR_MATERIAL
    );

    for (uint32_t chunkZ = 0; chunkZ < chunkCounts.z; chunkZ++) {
        for (uint32_t chunkY = 0; chunkY < chunkCounts.y; chunkY++) {
            for (uint32_t chunkX = 0; chunkX < chunkCounts.x; chunkX++) {
                const Chunk& chunk = getChunk(chunkX, chunkY, chunkZ);

                for (uint32_t localZ = 0; localZ < Chunk::SIZE; localZ++) {
                    for (uint32_t localY = 0; localY < Chunk::SIZE; localY++) {
                        for (uint32_t localX = 0; localX < Chunk::SIZE; localX++) {
                            const glm::uvec3 worldPos(
                                chunkX * Chunk::SIZE + localX,
                                chunkY * Chunk::SIZE + localY,
                                chunkZ * Chunk::SIZE + localZ
                            );

                            voxels[voxelIndex(voxelDimensions, worldPos.x, worldPos.y, worldPos.z)] =
                                chunk.get(localX, localY, localZ);
                        }
                    }
                }
            }
        }
    }

    return voxels;
}

size_t VoxelWorld::chunkIndex(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= chunkCounts.x || y >= chunkCounts.y || z >= chunkCounts.z) {
        throw std::out_of_range("voxel chunk index out of range");
    }

    return static_cast<size_t>(x) +
           static_cast<size_t>(chunkCounts.x) *
               (static_cast<size_t>(y) + static_cast<size_t>(chunkCounts.y) * static_cast<size_t>(z));
}
