#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class Instance;

class Swapchain {
public:
    void init(Instance* instance, GLFWwindow* window);

    VkFormat getSwapImgFormat() const {return swapChainImageFormat; }
    size_t getSwapImgCount() const {return swapChainImages.size(); }
    VkExtent2D getSwapExtent() const {return swapChainExtent; }
    VkSwapchainKHR getHandle() const {return _swapchain; }
    VkImage getImage(size_t index) const {return swapChainImages.at(index); }

    void cleanup(Instance* instance);
private:
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    GLFWwindow* _window = nullptr;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat{};
    VkExtent2D swapChainExtent{};

    void createSwapchain(Instance* instance);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};