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
#include <limits>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VulkanApp {
public:
    GLFWwindow* init(VoxelWorld& world, const WorldGenerationStats& worldStats);
    void setWorldStats(const WorldGenerationStats& inWorldStats) { worldStats = inWorldStats; }
    void drawFrame(const Camera& camera);
    void cleanup();

    Instance instance;
private:
    VoxelWorld* world = nullptr;
    WorldGenerationStats worldStats{};
    GLFWwindow* window = nullptr;
    Swapchain swapchain;
    DescriptorManager descriptorManager;
    CommandPool commandPool;
    std::vector<Image> computeImages;
    std::vector<bool> computeImagesInitialized;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> imguiFramebuffers;
    Buffer chunkWindowIndexBuffer{};
    Buffer chunkBrickMapBuffer{};
    Buffer brickPoolBuffer{};
    struct RequestBuffer {
        Buffer buffer{};
        uint32_t* mappedWords = nullptr;
    };
    std::vector<RequestBuffer> brickRequestBuffers;
    std::vector<uint32_t> gpuSlotByChunkEntry;
    std::vector<uint64_t> requestedChunkEntryBits;
    std::vector<uint32_t> cpuBrickToGpuBrick;
    std::vector<uint32_t> gpuBrickToCpuBrick;
    uint32_t gpuBrickCapacity = 0;
    uint32_t nextGpuBrickSlot = 0;
    static constexpr uint32_t INVALID_GPU_BRICK_SLOT = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t INITIAL_GPU_BRICK_CAPACITY = 65536u;
    static constexpr uint32_t BRICK_REQUEST_CAPACITY = 65536u;
    struct BufferUpload {
        Buffer stagingBuffer{};
        VkBuffer destinationBuffer = VK_NULL_HANDLE;
        std::vector<BufferCopyRegion> regions;
    };
    struct FrameUploads {
        std::vector<BufferUpload> pending;
    };
    std::vector<FrameUploads> frameUploads;
    SyncManager syncManager;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    bool framebufferResized = false;
    static constexpr uint32_t WIDTH = 800, HEIGHT = 600;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    int currentFrame = 0;
    std::chrono::steady_clock::time_point lastFrameTimestamp{};
    float frameRate = 0.0f;
    float frameTimeMs = 0.0f;
    int rayQueryVisualizationMode = 0;
    float rayQueryVisualizationIntensity = 1.0f;
    uint32_t lastBrickRequestCount = 0;
    uint32_t lastDroppedBrickRequestCount = 0;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void initWindow();
    void initVulkan();
    void createComputeImages();
    void createWorldBuffers();
    void syncWorld();
    void processBrickRequests(size_t frameIndex);
    void ensureGpuBrickCapacity(uint32_t requiredCapacity);
    void queueBufferUpload(FrameUploads& uploads, Buffer& destinationBuffer, const std::vector<uint32_t>& data, const std::vector<BufferCopyRegion>& regions);
    void resetRequestBuffer(size_t frameIndex);
    void recordUploads(VkCommandBuffer commandBuffer);
    void cleanupUploads(size_t frameIndex);
    void createDescriptorSets(bool allocateSets);
    void createComputePipeline();
    void createImGuiDescriptorPool();
    void createImGuiRenderPass();
    void createImGuiFramebuffers();
    void initImGui();
    void updateFrameTiming();
    void drawStatsUi();
    VkShaderModule createShaderModule(const std::vector<char>& code);

    void recreateSwapChain();
    void cleanupSwapchain();
    void cleanupImGui();
    void cleanupImGuiFramebuffers();

    void recordComputeCommand(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera);
};