#include "render/renderer.hpp"
#include "world/materials.hpp"
#include "world/world_gen.hpp"
#include <chrono>
#include <iostream>

// note to future self: implement SDF at the BrickMap level, you gain sparsity win with little cost, 
// test result: with 16 chunk render distance (204 meters), SDF data would be 2 MiB, brick data would be 150 MiB (awesome?)

// second note: i am aware of the god awful performance, this is just because were assuming a worst-case amount of bricks per chunk
// in the future we will use a fixed size buffer and dynamically evict bricks based on usage, optimization is not the goal yet

namespace {
//settings
constexpr uint32_t RENDER_DISTANCE = 10;
constexpr uint32_t WORLD_HEIGHT_CHUNKS = 3u;

constexpr float PLACE_VOXEL_RANGE = 16.0f;
constexpr float PLACE_VOXEL_STEP = 0.25f;

//helper functions
glm::uvec3 worldChunkCounts() {
    return glm::uvec3(
        2u * RENDER_DISTANCE + 1u,
        WORLD_HEIGHT_CHUNKS,
        2u * RENDER_DISTANCE + 1u
    );
}

glm::ivec2 chunkXZFromPosition(const glm::vec3& position) {
    const glm::vec3 chunkPosition = glm::floor(position / static_cast<float>(Chunk::SIZE));
    return glm::ivec2(
        static_cast<int32_t>(chunkPosition.x),
        static_cast<int32_t>(chunkPosition.z)
    );
}

bool tryPlaceStoneVoxel(VoxelWorld& world, const Camera& camera) {
    if (!camera.isCursorLocked()) {
        return false;
    }

    const glm::vec3 origin = camera.getPosition();
    const glm::vec3 direction = camera.getForward();

    glm::ivec3 lastEmptyVoxel(0);
    bool foundEmptyVoxel = false;

    for (float distance = 0.0f; distance <= PLACE_VOXEL_RANGE; distance += PLACE_VOXEL_STEP) {
        const glm::vec3 samplePosition = origin + direction * distance;
        const glm::ivec3 voxelCoord = glm::floor(samplePosition);

        if (!world.containsVoxel(voxelCoord.x, voxelCoord.y, voxelCoord.z)) {
            continue;
        }

        const uint32_t voxelValue = world.getVoxel(
            static_cast<uint32_t>(voxelCoord.x),
            static_cast<uint32_t>(voxelCoord.y),
            static_cast<uint32_t>(voxelCoord.z)
        );

        if (voxelValue != AIR_MATERIAL) {
            if (foundEmptyVoxel) {
                world.setVoxel(
                    static_cast<uint32_t>(lastEmptyVoxel.x),
                    static_cast<uint32_t>(lastEmptyVoxel.y),
                    static_cast<uint32_t>(lastEmptyVoxel.z),
                    STONE_MATERIAL
                );
                return true;
            }

            return false;
        }

        lastEmptyVoxel = voxelCoord;
        foundEmptyVoxel = true;
    }

    return false;
}

//main
GLFWwindow* window = nullptr;
VulkanApp renderer;

void gameLoop(VoxelWorld& world, WorldGenerator& worldGenerator, Camera& camera) {
    auto previousTime = std::chrono::steady_clock::now();
    bool rightMousePressedLastFrame = false;

    while (!glfwWindowShouldClose(window)) {
        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTimeSeconds = std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;

        glfwPollEvents();
        camera.update(window, deltaTimeSeconds);

        const glm::ivec2 focusChunkXZ = chunkXZFromPosition(camera.getPosition());
        world.centerChunkWindow(focusChunkXZ);
        worldGenerator.requestNextChunk(world, focusChunkXZ, camera.getForward());
        while (true) {
            const std::optional<WorldGenerationStats> generationStats = worldGenerator.consumeCompletedGeneration();
            if (!generationStats.has_value()) {
                break;
            }

            renderer.setWorldStats(*generationStats);
        }

        const bool rightMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rightMousePressed && !rightMousePressedLastFrame) {
            tryPlaceStoneVoxel(world, camera);
        }
        rightMousePressedLastFrame = rightMousePressed;

        renderer.drawFrame(camera);
    }

    vkDeviceWaitIdle(renderer.instance.device());
}
}

int main() {
    try {
        VoxelWorld world(worldChunkCounts());
        WorldGenerator worldGenerator;
        Camera camera(glm::vec3(320.0f, 160.0f, 256.0f));
        world.centerChunkWindow(chunkXZFromPosition(camera.getPosition()));
        const WorldGenerationStats worldStats{};

        window = renderer.init(world, worldStats);
        camera.attachWindow(window);

        gameLoop(world, worldGenerator, camera);
        renderer.cleanup();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

