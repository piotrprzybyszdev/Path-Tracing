#include "Core/Core.h"

#include "Window.h"

namespace PathTracing
{

static void GlfwErrorCallback(int error, const char *description)
{
    throw PathTracing::error(std::format("GLFW error {} {}", error, description).c_str());
}

Window::Window(int width, int height, const char *title, bool vsync)
    : m_Width(width), m_Height(height), m_Vsync(vsync)
{
    int result = glfwInit();

#ifndef NDEBUG
    if (result == GLFW_FALSE)
        throw error("Glfw initialization failed!");

    glfwSetErrorCallback(GlfwErrorCallback);
#endif

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_Handle = glfwCreateWindow(width, height, title, nullptr, nullptr);

#ifndef NDEBUG
    if (m_Handle == nullptr)
        throw error("Window creation failed!");
#endif
}

Window::~Window()
{
    glfwDestroyWindow(m_Handle);
    glfwTerminate();
}

GLFWwindow *Window::GetHandle() const
{
    return m_Handle;
}

std::pair<uint32_t, uint32_t> Window::GetSize() const
{
    return std::make_pair(m_Width, m_Height);
}

bool Window::ShouldClose() const
{
    return glfwWindowShouldClose(m_Handle) == GLFW_TRUE;
}

vk::SurfaceKHR Window::CreateSurface(vk::Instance instance)
{
    VkSurfaceKHR surface;
    glfwCreateWindowSurface(instance, m_Handle, nullptr, &surface);

    return surface;
}

void Window::OnUpdate(float timeStep)
{
    glfwPollEvents();

    int width, height;
    glfwGetWindowSize(m_Handle, &width, &height);
    m_Width = width;
    m_Height = height;
}

void Window::OnRender()
{
}

}