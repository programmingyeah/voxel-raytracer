#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <set>
#include <string>

struct SwapChainSupportDetails;
struct QueueFamilyIndices;
class CommandPool;

// wrapper for vkCreateDebugUtilsMessengerEXT
VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger
);

class Instance { 
public:
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    
    void init(GLFWwindow* window);

    VkSurfaceKHR surface() const { return _surface; }
    VkDevice device() const { return _device; }
    VkPhysicalDevice physicalDevice() const {return _physicalDevice; }
    VkInstance getHandle() const {return _instance; }
    VkQueue getGraphicsQueue() const {return graphicsQueue; }
    VkQueue getPresentQueue() const {return presentQueue; }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkFormat findDepthFormat();

    VkCommandBuffer beginSingleTimeCommands(CommandPool pool);
    void endSingleTimeCommands(CommandPool pool, VkCommandBuffer commandBuffer);

    void cleanup();
private:
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkDevice _device;
    VkPhysicalDevice _physicalDevice;
    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;
    VkSurfaceKHR _surface;
    GLFWwindow* _window;

    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME };
                                                    

    const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
    bool enableValidationLayers = true;

    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();

    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    VkSampleCountFlagBits getMaxUsableSampleCount();
    bool isDeviceSuitable(VkPhysicalDevice device);
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    );

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
};
