#include "voxel_world.hpp"
#include <stdexcept>

namespace {
size_t voxelIndex(const glm::uvec3& dims, uint32_t x, uint32_t y, uint32_t z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(dims.x) *
               (static_cast<size_t>(y) + static_cast<size_t>(dims.y) * static_cast<size_t>(z));
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

std::vector<uint32_t> VoxelWorld::buildVoxelBuffer() const {
    const glm::uvec3 voxelDimensions = getVoxelDimensions();
    std::vector<uint32_t> voxels(
        static_cast<size_t>(voxelDimensions.x) * voxelDimensions.y * voxelDimensions.z,
        0
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
