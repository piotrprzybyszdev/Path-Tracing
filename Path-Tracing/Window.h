#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

#include <cstdint>

namespace PathTracing
{

class Window
{
public:
    static void Create(int width, int height, const char *title);
    static void Destroy();

    static GLFWwindow *GetHandle();
    static bool IsMinimized();
    static bool ShouldClose();

    static vk::SurfaceKHR CreateSurface(vk::Instance instance);

    static void OnUpdate(float timeStep);

private:
    static GLFWwindow *s_Handle;
};

}
