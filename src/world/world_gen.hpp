#pragma once

#include "voxel_world.hpp"

#include <cstdint>

struct WorldGenerationStats {
    uint64_t solidVoxelCount = 0;
    double totalGenerationMs = 0.0;
    double averageChunkGenerationMs = 0.0;
};

class WorldGenerator {
public:
    WorldGenerationStats generateTerrain(VoxelWorld& world) const;
};