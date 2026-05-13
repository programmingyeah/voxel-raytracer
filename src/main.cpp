#include "render/renderer.hpp"
#include "world/world_gen.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

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
        VoxelWorld world(glm::uvec3(5, 3, 5));
        WorldGenerator worldGenerator;
        worldGenerator.generateTerrain(world);

        Camera camera(glm::vec3(160.0f, 128.0f, -96.0f));
        window = renderer.init(world);
        camera.attachWindow(window);

        gameLoop(camera);
        renderer.cleanup();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

