#pragma once

#include "instance.hpp"
#include "swapchain.hpp"
#include "descriptor_manager.hpp"
#include "command_pool.hpp"
#include "image.hpp"
#include "buffer.hpp"
#include "sync_manager.hpp"
#include "../world/voxel_world.hpp"
#include "../world/world_gen.hpp"
#include "../camera.hpp"
#include <chrono>
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VulkanApp {
public:
    GLFWwindow* init(const VoxelWorld& world, const WorldGenerationStats& worldStats);
    void drawFrame(const Camera& camera);
    void cleanup();

    Instance instance;
private:
    const VoxelWorld* world = nullptr;
    WorldGenerationStats worldStats{};
    GLFWwindow* window = nullptr;
    Swapchain swapchain;
    DescriptorManager descriptorManager;
    CommandPool commandPool;
    std::vector<Image> computeImages;
    std::vector<bool> computeImagesInitialized;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> imguiFramebuffers;
    Buffer chunkBrickMapBuffer{};
    Buffer brickPoolBuffer{};
    SyncManager syncManager;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    bool framebufferResized = false;
    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    int currentFrame = 0;
    std::chrono::steady_clock::time_point lastFrameTimestamp{};
    float frameRate = 0.0f;
    float frameTimeMs = 0.0f;
    int rayQueryVisualizationMode = 1;
    float rayQueryVisualizationIntensity = 1.0f;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void initWindow();
    void initVulkan();
    void createComputeImages();
    void createWorldBuffers();
    void createDescriptorSets(bool allocateSets);
    void createComputePipeline();
    void createImGuiDescriptorPool();
    void createImGuiRenderPass();
    void createImGuiFramebuffers();
    void initImGui();
    void updateFrameTiming();
    void buildDiagnosticsUi();
    VkShaderModule createShaderModule(const std::vector<char>& code);

    void recreateSwapChain();
    void cleanupSwapchain();
    void cleanupImGui();
    void cleanupImGuiFramebuffers();

    void recordComputeCommand(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera);
};