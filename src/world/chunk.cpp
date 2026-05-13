#include "chunk.hpp"
#include <stdexcept>

Chunk::Chunk(glm::ivec3 inChunkCoordinate) : chunkCoordinate(inChunkCoordinate) {
    clear();
}

uint32_t Chunk::get(uint32_t x, uint32_t y, uint32_t z) const {
    return voxels.at(index(x, y, z));
}

void Chunk::set(uint32_t x, uint32_t y, uint32_t z, uint32_t value) {
    voxels.at(index(x, y, z)) = value;
}

void Chunk::clear(uint32_t value) {
    voxels.fill(value);
}

size_t Chunk::index(uint32_t x, uint32_t y, uint32_t z) {
    if (x >= SIZE || y >= SIZE || z >= SIZE) {
        throw std::out_of_range("chunk voxel index out of range");
    }

    return static_cast<size_t>(x) +
           static_cast<size_t>(SIZE) *
               (static_cast<size_t>(y) + static_cast<size_t>(SIZE) * static_cast<size_t>(z));
}