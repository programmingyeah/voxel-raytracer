#include "chunk.hpp"
#include "materials.hpp"

std::vector<Brick> Chunk::bricks{};

namespace {
constexpr uint32_t BRICKS_PER_AXIS = Chunk::SIZE / BRICK_SIZE;

uint32_t getBrickIndex(uint32_t x, uint32_t y, uint32_t z)
{
    const uint32_t bx = x / BRICK_SIZE;
    const uint32_t by = y / BRICK_SIZE;
    const uint32_t bz = z / BRICK_SIZE;

    return bx +
           by * BRICKS_PER_AXIS +
           bz * BRICKS_PER_AXIS * BRICKS_PER_AXIS;
}

void fillBrick(Brick& brick, uint8_t value)
{
    for (uint32_t x = 0; x < BRICK_SIZE; x++) {
        for (uint32_t y = 0; y < BRICK_SIZE; y++) {
            for (uint32_t z = 0; z < BRICK_SIZE; z++) {
                brick.voxels[x][y][z] = value;
            }
        }
    }
}
}

Chunk::Chunk(glm::ivec3 inChunkCoordinate) : chunkCoordinate(inChunkCoordinate)
{
    clear();
}

uint32_t Chunk::get(uint32_t x, uint32_t y, uint32_t z) const
{
    const uint32_t encodedId = brickMap[getBrickIndex(x, y, z)];
    if (encodedId < MATERIALS_COUNT) {
        return encodedId;
    }

    const Brick& brick = bricks[encodedId - MATERIALS_COUNT];
    const uint32_t lx = x % BRICK_SIZE;
    const uint32_t ly = y % BRICK_SIZE;
    const uint32_t lz = z % BRICK_SIZE;

    return brick.voxels[lx][ly][lz];
}

void Chunk::set(uint32_t x, uint32_t y, uint32_t z, uint32_t value)
{
    const uint32_t brickIndex = getBrickIndex(x, y, z);
    const uint32_t encodedId = brickMap[brickIndex];

    const uint32_t lx = x % BRICK_SIZE;
    const uint32_t ly = y % BRICK_SIZE;
    const uint32_t lz = z % BRICK_SIZE;

    if (encodedId < MATERIALS_COUNT) {
        if (encodedId == value) {
            return;
        }

        Brick brick{};
        fillBrick(brick, static_cast<uint8_t>(encodedId));
        brick.voxels[lx][ly][lz] = static_cast<uint8_t>(value);

        bricks.push_back(brick);
        brickMap[brickIndex] = MATERIALS_COUNT + static_cast<uint32_t>(bricks.size() - 1);
        return;
    }

    Brick& brick = bricks[encodedId - MATERIALS_COUNT];
    brick.voxels[lx][ly][lz] = static_cast<uint8_t>(value);

    //if all voxels the same -> remove brick, save as id=materialId

    for (uint32_t x = 0; x < BRICK_SIZE; x++) {
        for (uint32_t y = 0; y < BRICK_SIZE; y++) {
            for (uint32_t z = 0; z < BRICK_SIZE; z++) {
                if (brick.voxels[x][y][z] != value) return; 
            }
        }
    }

    brickMap[brickIndex] = static_cast<uint8_t> (value);
}

void Chunk::clear()
{
    brickMap.fill(0u);
}