#include "Core/Core.h"

#include "Window.h"

namespace PathTracing
{

GLFWwindow *Window::s_Handle = nullptr;

void Window::Create(int width, int height, const char *title)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    s_Handle = glfwCreateWindow(width, height, title, nullptr, nullptr);

#ifndef NDEBUG
    if (s_Handle == nullptr)
        throw error("Window creation failed!");
#endif
}

void Window::Destroy()
{
    glfwDestroyWindow(s_Handle);
    glfwTerminate();
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

vk::SurfaceKHR Window::CreateSurface(vk::Instance instance)
{
    VkSurfaceKHR surface;
    glfwCreateWindowSurface(instance, s_Handle, nullptr, &surface);

    return surface;
}

void Window::OnUpdate(float timeStep)
{
    glfwPollEvents();
}

}