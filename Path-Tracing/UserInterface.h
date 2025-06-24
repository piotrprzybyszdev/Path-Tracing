#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

namespace PathTracing
{

class UserInterface
{
public:
    static void Init(
        GLFWwindow *window, vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device,
        uint32_t queueFamily, vk::Queue queue, uint32_t minImageCount,
        uint32_t imageCount, vk::RenderPass renderPass
    );

    static void Shutdown();

    static void Render(vk::CommandBuffer commandBuffer);

    static bool GetIsFocused();

private:
    static bool s_IsFocused;
};

}
