#include <set>

#include "Core/Core.h"

#include "Window.h"

namespace PathTracing
{

GLFWwindow *Window::s_Handle = nullptr;
const GLFWvidmode *Window::s_VideoMode = nullptr;
vk::Offset2D Window::s_LastPos = {};
vk::Extent2D Window::s_LastSize = {};
WindowMode Window::s_Mode = WindowMode::Windowed;
std::vector<vk::Extent2D> Window::s_Resolutions = {};

void Window::Create(int width, int height, const char *title)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    s_Handle = glfwCreateWindow(width, height, title, nullptr, nullptr);

    if (s_Handle == nullptr)
        throw error("Window creation failed!");

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    s_VideoMode = glfwGetVideoMode(monitor);
    s_LastSize = vk::Extent2D(width, height);

    int count;
    const GLFWvidmode *modes = glfwGetVideoModes(monitor, &count);
    std::set<vk::Extent2D, std::greater<vk::Extent2D>> resolutions;
    resolutions.emplace(width, height);
    for (int i = 0; i < count; i++)
        if (modes[i].width != 0 && modes[i].height != 0)
            resolutions.emplace(modes[i].width, modes[i].height);

    s_Resolutions.assign(resolutions.begin(), resolutions.end());
}

void Window::Destroy()
{
    s_Resolutions.clear();
    glfwDestroyWindow(s_Handle);
    glfwTerminate();
}

void Window::PollEvents()
{
    glfwPollEvents();
}

vk::Offset2D Window::GetPos()
{
    int x, y;
    glfwGetWindowPos(s_Handle, &x, &y);
    return vk::Offset2D(x, y);
}

vk::Extent2D Window::GetSize()
{
    int x, y;
    glfwGetWindowSize(s_Handle, &x, &y);
    return vk::Extent2D(x, y);
}

std::span<const vk::Extent2D> Window::GetResolutions()
{
    return s_Resolutions;
}

GLFWwindow *Window::GetHandle()
{
    return s_Handle;
}

bool Window::IsMinimized()
{
    int width, height;
    glfwGetWindowSize(s_Handle, &width, &height);
    return glfwGetWindowAttrib(s_Handle, GLFW_ICONIFIED) == GLFW_TRUE || width == 0 || height == 0;
}

bool Window::ShouldClose()
{
    return glfwWindowShouldClose(s_Handle) == GLFW_TRUE;
}

void Window::SetMode(WindowMode mode)
{
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    auto [width, height] = GetSize();

    switch (mode)
    {
    case WindowMode::Windowed:
        glfwSetWindowMonitor(
            s_Handle, nullptr, s_LastPos.x, s_LastPos.y, s_LastSize.width, s_LastSize.height, GLFW_DONT_CARE
        );
        break;
    case WindowMode::FullScreen:
        glfwSetWindowMonitor(s_Handle, monitor, 0, 0, width, height, GLFW_DONT_CARE);
        break;
    case WindowMode::FullScreenWindowed:
        glfwSetWindowMonitor(
            s_Handle, monitor, 0, 0, s_VideoMode->width, s_VideoMode->height, s_VideoMode->refreshRate
        );
        break;
    }

    s_Mode = mode;
}

void Window::SetResolution(vk::Extent2D extent)
{
    glfwSetWindowSize(s_Handle, extent.width, extent.height);
}

vk::SurfaceKHR Window::CreateSurface(vk::Instance instance)
{
    VkSurfaceKHR surface;
    glfwCreateWindowSurface(instance, s_Handle, nullptr, &surface);
    return surface;
}

void Window::OnUpdate(float timeStep)
{
    if (s_Mode == WindowMode::Windowed)
    {
        s_LastPos = GetPos();
        s_LastSize = GetSize();
    }
}

}
