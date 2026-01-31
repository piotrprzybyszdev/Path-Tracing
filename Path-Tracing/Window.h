#pragma once

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

#include <vector>

namespace PathTracing
{

enum class WindowMode
{
    Windowed,
    FullScreen,
    FullScreenWindowed
};

class Window
{
public:
    static void Create(int width, int height, const char *title);
    static void Destroy();

    static void PollEvents();

    static GLFWwindow *GetHandle();
    static vk::Offset2D GetPos();
    static vk::Extent2D GetSize();
    static std::span<const vk::Extent2D> GetResolutions();

    static bool IsMinimized();
    static bool ShouldClose();

    static void SetMode(WindowMode mode);
    [[nodiscard]] static WindowMode GetMode();

    static void SetResolution(vk::Extent2D extent);

    static vk::SurfaceKHR CreateSurface(vk::Instance instance);

    static void OnUpdate(float timeStep);

private:
    static GLFWwindow *s_Handle;
    static const GLFWvidmode *s_VideoMode;
    static vk::Offset2D s_LastPos;
    static vk::Extent2D s_LastSize;
    static WindowMode s_Mode;
    static std::vector<vk::Extent2D> s_Resolutions;
};

}
