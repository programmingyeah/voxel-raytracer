#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

class Camera {
public:
    explicit Camera(glm::vec3 startPosition = glm::vec3(0.0f, 32.0f, -96.0f))
        : position(startPosition) {}

    void attachWindow(GLFWwindow* window) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        firstMouseSample = false;
    }

    void update(GLFWwindow* window, float deltaTimeSeconds) {
        updateMouseLook(window);
        updateMovement(window, deltaTimeSeconds);
    }

    glm::vec3 getPosition() const { return position; }

    glm::vec3 getForward() const {
        const float yawRadians = glm::radians(yawDegrees);
        const float pitchRadians = glm::radians(pitchDegrees);

        return glm::normalize(glm::vec3(
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians),
            std::sin(yawRadians) * std::cos(pitchRadians)
        ));
    }

    glm::vec3 getRight() const {
        return glm::normalize(glm::cross(getForward(), worldUp));
    }

    glm::vec3 getUp() const {
        return glm::normalize(glm::cross(getRight(), getForward()));
    }

private:
    void updateMouseLook(GLFWwindow* window) {
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (firstMouseSample) {
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            firstMouseSample = false;
        }

        const float offsetX = static_cast<float>(mouseX - lastMouseX);
        const float offsetY = static_cast<float>(lastMouseY - mouseY);
        lastMouseX = mouseX;
        lastMouseY = mouseY;

        yawDegrees += offsetX * lookSensitivity;
        pitchDegrees += offsetY * lookSensitivity;
        pitchDegrees = std::clamp(pitchDegrees, -89.0f, 89.0f);
    }

    void updateMovement(GLFWwindow* window, float deltaTimeSeconds) {
        const float speed = movementSpeed * deltaTimeSeconds;
        const glm::vec3 forward = getForward();
        const glm::vec3 right = getRight();

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position += forward * speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position -= forward * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position -= right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position += right * speed;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) position += worldUp * speed;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) position -= worldUp * speed;
    }

    glm::vec3 position{};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    float yawDegrees = 90.0f;
    float pitchDegrees = 0.0f;
    float movementSpeed = 28.0f;
    float lookSensitivity = 0.08f;
    bool firstMouseSample = true;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
};
