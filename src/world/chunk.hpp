#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class Chunk {
public:
    static constexpr uint32_t SIZE = 64;
    static constexpr size_t VOXEL_COUNT = static_cast<size_t>(SIZE) * SIZE * SIZE;

    explicit Chunk(glm::ivec3 chunkCoordinate = glm::ivec3(0));

    uint32_t get(uint32_t x, uint32_t y, uint32_t z) const;
    void set(uint32_t x, uint32_t y, uint32_t z, uint32_t value);
    void clear(uint32_t value = 0);

    glm::ivec3 getChunkCoordinate() const { return chunkCoordinate; }
    const std::array<uint32_t, VOXEL_COUNT>& data() const { return voxels; }

private:
    static size_t index(uint32_t x, uint32_t y, uint32_t z);

    glm::ivec3 chunkCoordinate{};
    std::array<uint32_t, VOXEL_COUNT> voxels{};
};