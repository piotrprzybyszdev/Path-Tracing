#pragma once

#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cstdint>

namespace PathTracing
{

class Window
{
public:
    Window(int width, int height, const char *title, bool vsync);
    ~Window();

    GLFWwindow *GetHandle() const;
    std::pair<uint32_t, uint32_t> GetSize() const;

    bool ShouldClose() const;
    vk::SurfaceKHR CreateSurface(vk::Instance instance);

    void OnUpdate(float timeStep);
    void OnRender();

private:
    uint32_t m_Width, m_Height;
    bool m_Vsync;

    GLFWwindow *m_Handle;
};

}
