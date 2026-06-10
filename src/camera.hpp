#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

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
        lockCursor(window);
    }

    void update(GLFWwindow* window, float deltaTimeSeconds) {
        handleCursorToggle(window);
        if (!cursorLocked) {
            return;
        }

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

    bool isCursorLocked() const { return cursorLocked; }

private:
    void handleCursorToggle(GLFWwindow* window) {
        const bool escapePressed = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escapePressed && !escapePressedLastFrame) {
            if (cursorLocked) {
                unlockCursor(window);
            } else {
                lockCursor(window);
            }
        }
        escapePressedLastFrame = escapePressed;
    }

    void lockCursor(GLFWwindow* window) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        centerCursor(window);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        firstMouseSample = true;
        cursorLocked = true;
    }

    void unlockCursor(GLFWwindow* window) {
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        cursorLocked = false;
    }

    void centerCursor(GLFWwindow* window) {
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);

        const double centerX = static_cast<double>(windowWidth) * 0.5;
        const double centerY = static_cast<double>(windowHeight) * 0.5;
        glfwSetCursorPos(window, centerX, centerY);
        lastMouseX = centerX;
        lastMouseY = centerY;
    }

    void updateMouseLook(GLFWwindow* window) {
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (firstMouseSample) {
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            firstMouseSample = false;
            return;
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
    bool cursorLocked = true;
    bool escapePressedLastFrame = false;
    bool firstMouseSample = true;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
};
