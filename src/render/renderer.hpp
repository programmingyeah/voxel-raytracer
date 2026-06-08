#pragma once

#include "instance.hpp"
#include "swapchain.hpp"
#include "descriptor_manager.hpp"
#include "command_pool.hpp"
#include "image.hpp"
#include "buffer.hpp"
#include "sync_manager.hpp"
#include "world/voxel_world.hpp"
#include "camera.hpp"
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VulkanApp {
public:
    GLFWwindow* init(const VoxelWorld& world);
    void drawFrame(const Camera& camera);
    void cleanup();

    Instance instance;
private:
    const VoxelWorld* world = nullptr;
    GLFWwindow* window = nullptr;
    Swapchain swapchain;
    DescriptorManager descriptorManager;
    CommandPool commandPool;
    std::vector<Image> computeImages;
    std::vector<bool> computeImagesInitialized;
    Buffer chunkBrickMapBuffer{};
    Buffer brickPoolBuffer{};
    SyncManager syncManager;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    bool framebufferResized = false;
    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    int currentFrame = 0;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void initWindow();
    void initVulkan();
    void createComputeImages();
    void createWorldBuffers();
    void createDescriptorSets(bool allocateSets);
    void createComputePipeline();
    VkShaderModule createShaderModule(const std::vector<char>& code);

    void recreateSwapChain();
    void cleanupSwapchain();

        
    void recordComputeCommand(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera);
};