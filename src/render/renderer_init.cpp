#include "renderer.hpp"

#include "renderer_shared.hpp"
#include "utils.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "../world/gpu_world_layout.hpp"

#include <stdexcept>
#include <string>
#include <vector>

void VulkanApp::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto app = reinterpret_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

GLFWwindow* VulkanApp::init(VoxelWorld& worldRef, const WorldGenerationStats& inWorldStats) {
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

    VkDescriptorSetLayoutBinding chunkWindowIndexBinding{};
    chunkWindowIndexBinding.binding = 1;
    chunkWindowIndexBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    chunkWindowIndexBinding.descriptorCount = 1;
    chunkWindowIndexBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding chunkBrickMapBinding{};
    chunkBrickMapBinding.binding = 2;
    chunkBrickMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    chunkBrickMapBinding.descriptorCount = 1;
    chunkBrickMapBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding brickPoolBinding{};
    brickPoolBinding.binding = 3;
    brickPoolBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    brickPoolBinding.descriptorCount = 1;
    brickPoolBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding brickRequestBinding{};
    brickRequestBinding.binding = 4;
    brickRequestBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    brickRequestBinding.descriptorCount = 1;
    brickRequestBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    descriptorManager.initLayout(&instance, {storageImageBinding, chunkWindowIndexBinding, chunkBrickMapBinding, brickPoolBinding, brickRequestBinding});
    descriptorManager.initPool(
        &instance,
        {
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_FRAMES_IN_FLIGHT},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * MAX_FRAMES_IN_FLIGHT}
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
    frameUploads.resize(MAX_FRAMES_IN_FLIGHT);
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