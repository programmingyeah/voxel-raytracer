#include "render/renderer.hpp"
#include "world/world_gen.hpp"
#include <chrono>
#include <iostream>

// note to future self: implement SDF at the BrickMap level, you gain sparsity win with little cost, 
// test result: with 16 chunk render distance (204 meters), SDF data would be 2 MiB, brick data would be 150 MiB (awesome?)

GLFWwindow* window = nullptr;
VulkanApp renderer;

void gameLoop(Camera& camera) {
    auto previousTime = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTimeSeconds = std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;

        glfwPollEvents();
        camera.update(window, deltaTimeSeconds);
        renderer.drawFrame(camera);
    }

    vkDeviceWaitIdle(renderer.instance.device());
}

int main() {
    try {
        VoxelWorld world(glm::uvec3(6, 2, 6));
        WorldGenerator worldGenerator;
        const WorldGenerationStats worldStats = worldGenerator.generateTerrain(world);

        Camera camera(glm::vec3(0.0f, 160.0f, 0.0f));
        window = renderer.init(world, worldStats);
        camera.attachWindow(window);

        gameLoop(camera);
        renderer.cleanup();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

