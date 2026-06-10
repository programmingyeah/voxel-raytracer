#include "renderer.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "utils.hpp"

namespace {
struct ComputePushConstants {
    glm::vec4 cameraPos;
    glm::vec4 cameraForward;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    glm::ivec4 worldDimensions;
    glm::ivec4 chunkCounts;
    glm::vec4 renderParams;
};

void uploadStorageBuffer(Instance& instance, Buffer& buffer, const std::vector<uint32_t>& data)
{
    const VkDeviceSize bufferSize = sizeof(uint32_t) * static_cast<VkDeviceSize>(data.empty() ? 1 : data.size());
    buffer.createBuffer(
        &instance,
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    const uint32_t zero = 0;
    buffer.upload(&instance, data.empty() ? static_cast<const void*>(&zero) : static_cast<const void*>(data.data()), bufferSize);
}

void insertImageBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

std::string formatByteSize(VkDeviceSize bytes)
{
    static const char* units[] = {"B", "KiB", "MiB", "GiB"};
    double size = static_cast<double>(bytes);
    size_t unitIndex = 0;
    while (size >= 1024.0 && unitIndex + 1 < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
    return buffer;
}

void checkVkResult(VkResult result)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error("ImGui Vulkan backend call failed");
    }
}
}

void VulkanApp::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto app = reinterpret_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

GLFWwindow* VulkanApp::init(const VoxelWorld& worldRef, const WorldGenerationStats& inWorldStats) {
    world = &worldRef;
    worldStats = inWorldStats;
    lastFrameTimestamp = std::chrono::steady_clock::now();
    initWindow();
    initVulkan();

    return window;
}

void VulkanApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void VulkanApp::initVulkan() {
    instance.init(window);
    swapchain.init(&instance, window);
    commandPool.init(&instance);

    VkDescriptorSetLayoutBinding storageImageBinding{};
    storageImageBinding.binding = 0;
    storageImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageImageBinding.descriptorCount = 1;
    storageImageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    storageImageBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding chunkBrickMapBinding{};
    chunkBrickMapBinding.binding = 1;
    chunkBrickMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    chunkBrickMapBinding.descriptorCount = 1;
    chunkBrickMapBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    chunkBrickMapBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding brickPoolBinding{};
    brickPoolBinding.binding = 2;
    brickPoolBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    brickPoolBinding.descriptorCount = 1;
    brickPoolBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    brickPoolBinding.pImmutableSamplers = nullptr;

    descriptorManager.initLayout(&instance, {storageImageBinding, chunkBrickMapBinding, brickPoolBinding});
    descriptorManager.initPool(
        &instance,
        {
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_FRAMES_IN_FLIGHT},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * MAX_FRAMES_IN_FLIGHT}
        },
        MAX_FRAMES_IN_FLIGHT
    );

    createComputeImages();
    createWorldBuffers();
    createDescriptorSets(true);
    createComputePipeline();
    createImGuiDescriptorPool();
    createImGuiRenderPass();
    createImGuiFramebuffers();
    initImGui();
    commandPool.allocateCommandBuffers(&instance, MAX_FRAMES_IN_FLIGHT);
    syncManager.init(&instance, swapchain.getSwapImgCount(), MAX_FRAMES_IN_FLIGHT);
}

VkShaderModule VulkanApp::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(instance.device(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute shader module!");
    }

    return shaderModule;
}

void VulkanApp::createComputePipeline() {
    const auto shaderCode = readFile(std::string(SHADER_DIR) + "/comp.spv");
    VkShaderModule shaderModule = createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";

    VkDescriptorSetLayout descriptorSetLayout = descriptorManager.getLayout();
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ComputePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(instance.device(), &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(instance.device(), shaderModule, nullptr);
        throw std::runtime_error("failed to create compute pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = computePipelineLayout;

    if (vkCreateComputePipelines(instance.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(instance.device(), shaderModule, nullptr);
        throw std::runtime_error("failed to create compute pipeline!");
    }

    vkDestroyShaderModule(instance.device(), shaderModule, nullptr);
}

void VulkanApp::createImGuiDescriptorPool() {
    const std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 100u * static_cast<uint32_t>(poolSizes.size());
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(instance.device(), &poolInfo, nullptr, &imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ImGui descriptor pool!");
    }
}

void VulkanApp::createImGuiRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchain.getSwapImgFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(instance.device(), &renderPassInfo, nullptr, &imguiRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ImGui render pass!");
    }
}

void VulkanApp::createImGuiFramebuffers() {
    cleanupImGuiFramebuffers();

    const VkExtent2D extent = swapchain.getSwapExtent();
    swapchainImageViews.resize(swapchain.getSwapImgCount());
    imguiFramebuffers.resize(swapchain.getSwapImgCount());

    for (size_t i = 0; i < swapchain.getSwapImgCount(); i++) {
        swapchainImageViews[i] = instance.createImageView(swapchain.getImage(i), swapchain.getSwapImgFormat(), VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageView attachments[] = {swapchainImageViews[i]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = imguiRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(instance.device(), &framebufferInfo, nullptr, &imguiFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create ImGui framebuffer!");
        }
    }
}

void VulkanApp::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    const QueueFamilyIndices queueFamilyIndices = instance.findQueueFamilies(instance.physicalDevice());
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance.getHandle();
    initInfo.PhysicalDevice = instance.physicalDevice();
    initInfo.Device = instance.device();
    initInfo.QueueFamily = queueFamilyIndices.graphicsFamily.value();
    initInfo.Queue = instance.getGraphicsQueue();
    initInfo.DescriptorPool = imguiDescriptorPool;
    initInfo.RenderPass = imguiRenderPass;
    initInfo.MinImageCount = static_cast<uint32_t>(swapchain.getSwapImgCount());
    initInfo.ImageCount = static_cast<uint32_t>(swapchain.getSwapImgCount());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = checkVkResult;

    ImGui_ImplVulkan_Init(&initInfo);
}

void VulkanApp::createComputeImages() {
    if (swapchain.getSwapImgFormat() != VK_FORMAT_R8G8B8A8_UNORM) {
        throw std::runtime_error("compute path currently requires VK_FORMAT_R8G8B8A8_UNORM swapchain images");
    }

    computeImages.clear();
    computeImages.resize(MAX_FRAMES_IN_FLIGHT);
    computeImagesInitialized.assign(MAX_FRAMES_IN_FLIGHT, false);

    const VkExtent2D extent = swapchain.getSwapExtent();
    for (auto& image : computeImages) {
        image.create(
            &instance,
            extent.width,
            extent.height,
            VK_SAMPLE_COUNT_1_BIT,
            swapchain.getSwapImgFormat(),
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    }
}

void VulkanApp::createWorldBuffers() {
    if (world == nullptr) {
        throw std::runtime_error("voxel world must be set before creating world buffers");
    }

    const GpuVoxelBuffers gpuBuffers = world->buildGpuBuffers();
    uploadStorageBuffer(instance, chunkBrickMapBuffer, gpuBuffers.chunkBrickMaps);
    uploadStorageBuffer(instance, brickPoolBuffer, gpuBuffers.brickData);
}

void VulkanApp::createDescriptorSets(bool allocateSets) {
    std::vector<VkDescriptorImageInfo> imageInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> chunkBrickMapInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> brickPoolInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<std::vector<DescriptorWrite>> descriptorWrites(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageInfos[i].sampler = VK_NULL_HANDLE;
        imageInfos[i].imageView = computeImages[i].getView();
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        descriptorWrites[i].push_back({
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            nullptr,
            &imageInfos[i]
        });

        chunkBrickMapInfos[i].buffer = chunkBrickMapBuffer.buffer;
        chunkBrickMapInfos[i].offset = 0;
        chunkBrickMapInfos[i].range = chunkBrickMapBuffer.size;

        descriptorWrites[i].push_back({
            1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            &chunkBrickMapInfos[i],
            nullptr
        });

        brickPoolInfos[i].buffer = brickPoolBuffer.buffer;
        brickPoolInfos[i].offset = 0;
        brickPoolInfos[i].range = brickPoolBuffer.size;

        descriptorWrites[i].push_back({
            2,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            &brickPoolInfos[i],
            nullptr
        });
    }

    if (allocateSets) {
        descriptorManager.initSets(&instance, MAX_FRAMES_IN_FLIGHT, descriptorWrites);
    } else {
        descriptorManager.updateSets(&instance, descriptorWrites);
    }
}

void VulkanApp::updateFrameTiming() {
    const auto now = std::chrono::steady_clock::now();
    const float deltaMs = std::chrono::duration<float, std::milli>(now - lastFrameTimestamp).count();
    lastFrameTimestamp = now;

    frameTimeMs = deltaMs;
    if (deltaMs > 0.0f) {
        const float instantFrameRate = 1000.0f / deltaMs;
        frameRate = frameRate == 0.0f ? instantFrameRate : frameRate * 0.9f + instantFrameRate * 0.1f;
    }
}

void VulkanApp::buildDiagnosticsUi() {
    const size_t chunkCount = world != nullptr ? world->getChunkCount() : 0;
    const uint64_t worldVoxelCount = static_cast<uint64_t>(chunkCount) * Chunk::VOXEL_COUNT;
    const size_t brickCount = Chunk::getBrickPool().size();
    const VkDeviceSize worldBufferBytes = chunkBrickMapBuffer.size + brickPoolBuffer.size;

    const std::string worldBufferSize = formatByteSize(worldBufferBytes);
    const std::string chunkMapBufferSize = formatByteSize(chunkBrickMapBuffer.size);
    const std::string brickPoolBufferSize = formatByteSize(brickPoolBuffer.size);
    static const char* rayQueryVisualizationModes[] = {
        "Off",
        "Query heat",
        "Hierarchy breakdown"
    };

    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Diagnostics");
    ImGui::Text("Frame rate: %.1f FPS", frameRate);
    ImGui::Text("Frame time: %.2f ms", frameTimeMs);
    ImGui::Separator();
    ImGui::Text("Chunk count: %zu", chunkCount);
    ImGui::Text("Voxel count: %llu", static_cast<unsigned long long>(worldVoxelCount));
    ImGui::Text("Solid voxels: %llu", static_cast<unsigned long long>(worldStats.solidVoxelCount));
    ImGui::Text("Brick count: %zu", brickCount);
    ImGui::Text("Avg chunk load: %.3f ms", worldStats.averageChunkGenerationMs);
    ImGui::Text("World gen total: %.2f ms", worldStats.totalGenerationMs);
    ImGui::Separator();
    ImGui::Text("World buffer VRAM: %s", worldBufferSize.c_str());
    ImGui::Text("Chunk map buffer: %s", chunkMapBufferSize.c_str());
    ImGui::Text("Brick pool buffer: %s", brickPoolBufferSize.c_str());
    ImGui::Separator();
    ImGui::Text("Ray query visualization");
    ImGui::Combo(
        "Mode",
        &rayQueryVisualizationMode,
        rayQueryVisualizationModes,
        IM_ARRAYSIZE(rayQueryVisualizationModes)
    );
    ImGui::SliderFloat("Intensity", &rayQueryVisualizationIntensity, 0.25f, 4.0f, "%.2fx");
    ImGui::TextWrapped("Heat shows weighted traversal cost. Breakdown colors surfaces by query source: red = voxel tests, green = sparse brick/coarse checks, blue = chunk traversal.");
    ImGui::End();
}

void VulkanApp::cleanup() {
    vkDeviceWaitIdle(instance.device());
    cleanupSwapchain();
    cleanupImGui();

    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(instance.device(), computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }

    if (computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(instance.device(), computePipelineLayout, nullptr);
        computePipelineLayout = VK_NULL_HANDLE;
    }

    chunkBrickMapBuffer.cleanup(&instance);
    brickPoolBuffer.cleanup(&instance);
    descriptorManager.cleanup(&instance);
    syncManager.cleanup(&instance);
    commandPool.cleanup(&instance);
    instance.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();
}

void VulkanApp::drawFrame(const Camera& camera) {
    FrameSyncObjects frameSyncObjects = syncManager.getFrame(currentFrame);

    vkWaitForFences(instance.device(), 1, &frameSyncObjects.inFlight, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        instance.device(),
        swapchain.getHandle(),
        UINT64_MAX,
        frameSyncObjects.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    updateFrameTiming();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    buildDiagnosticsUi();
    ImGui::Render();

    syncManager.waitForImageIfNeeded(&instance, imageIndex);
    vkResetFences(instance.device(), 1, &frameSyncObjects.inFlight);

    commandPool.resetBufferAtIndex(currentFrame);
    recordComputeCommand(commandPool.getBufferByIndex(currentFrame), imageIndex, camera);

    VkSemaphore waitSemaphores[] = {frameSyncObjects.imageAvailable};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore signalSemaphores[] = {syncManager.getRenderFinishedSemaphore(imageIndex)};
    VkCommandBuffer commandBuffer = commandPool.getBufferByIndex(currentFrame);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(instance.getGraphicsQueue(), 1, &submitInfo, frameSyncObjects.inFlight) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit compute command buffer!");
    }

    syncManager.setImageFence(imageIndex, frameSyncObjects.inFlight);

    VkSwapchainKHR swapChains[] = {swapchain.getHandle()};
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(instance.getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanApp::recordComputeCommand(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording compute command buffer!");
    }

    Image& computeImage = computeImages[currentFrame];
    const VkExtent2D extent = swapchain.getSwapExtent();

    insertImageBarrier(
        commandBuffer,
        computeImage.getImage(),
        computeImagesInitialized[currentFrame] ? VK_ACCESS_TRANSFER_READ_BIT : 0,
        VK_ACCESS_SHADER_WRITE_BIT,
        computeImagesInitialized[currentFrame] ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        computeImagesInitialized[currentFrame] ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    VkDescriptorSet descriptorSet = descriptorManager.getDescriptorSet(currentFrame);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    const glm::uvec3 worldDimensions = world->getVoxelDimensions();
    const glm::uvec3 chunkCounts = world->getChunkCounts();
    ComputePushConstants pushConstants{};
    pushConstants.cameraPos = glm::vec4(camera.getPosition(), 1.0f);
    pushConstants.cameraForward = glm::vec4(camera.getForward(), 0.0f);
    pushConstants.cameraRight = glm::vec4(camera.getRight(), 0.0f);
    pushConstants.cameraUp = glm::vec4(camera.getUp(), 0.0f);
    pushConstants.worldDimensions = glm::ivec4(
        static_cast<int>(worldDimensions.x),
        static_cast<int>(worldDimensions.y),
        static_cast<int>(worldDimensions.z),
        static_cast<int>(BRICK_SIZE)
    );
    pushConstants.chunkCounts = glm::ivec4(
        static_cast<int>(chunkCounts.x),
        static_cast<int>(chunkCounts.y),
        static_cast<int>(chunkCounts.z),
        static_cast<int>(Chunk::SIZE)
    );
    pushConstants.renderParams = glm::vec4(
        rayQueryVisualizationIntensity,
        static_cast<float>(glm::length(glm::vec3(worldDimensions))) * 2.0f,
        1.4f,
        static_cast<float>(rayQueryVisualizationMode)
    );

    vkCmdPushConstants(
        commandBuffer,
        computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ComputePushConstants),
        &pushConstants
    );

    const uint32_t groupCountX = (extent.width + 7) / 8;
    const uint32_t groupCountY = (extent.height + 7) / 8;
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

    insertImageBarrier(
        commandBuffer,
        computeImage.getImage(),
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    insertImageBarrier(
        commandBuffer,
        swapchain.getImage(imageIndex),
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.extent = {extent.width, extent.height, 1};

    vkCmdCopyImage(
        commandBuffer,
        computeImage.getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchain.getImage(imageIndex), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    insertImageBarrier(
        commandBuffer,
        swapchain.getImage(imageIndex),
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = imguiRenderPass;
    renderPassInfo.framebuffer = imguiFramebuffers.at(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    computeImagesInitialized[currentFrame] = true;

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record compute command buffer!");
    }
}

void VulkanApp::recreateSwapChain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(instance.device());
    cleanupSwapchain();

    swapchain.init(&instance, window);
    createComputeImages();
    createDescriptorSets(false);
    createImGuiFramebuffers();
    ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(swapchain.getSwapImgCount()));
    syncManager.resetImages(&instance, swapchain.getSwapImgCount());
}

void VulkanApp::cleanupImGuiFramebuffers() {
    for (VkFramebuffer framebuffer : imguiFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(instance.device(), framebuffer, nullptr);
        }
    }
    imguiFramebuffers.clear();

    for (VkImageView imageView : swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(instance.device(), imageView, nullptr);
        }
    }
    swapchainImageViews.clear();
}

void VulkanApp::cleanupSwapchain() {
    cleanupImGuiFramebuffers();

    for (auto& image : computeImages) {
        image.cleanup(&instance);
    }

    computeImages.clear();
    computeImagesInitialized.clear();
    swapchain.cleanup(&instance);
}

void VulkanApp::cleanupImGui() {
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    if (imguiRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(instance.device(), imguiRenderPass, nullptr);
        imguiRenderPass = VK_NULL_HANDLE;
    }

    if (imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(instance.device(), imguiDescriptorPool, nullptr);
        imguiDescriptorPool = VK_NULL_HANDLE;
    }
}
