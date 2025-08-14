#pragma once

#include <memory>

#include <vulkan/vulkan.hpp>

#include "Renderer/Swapchain.h"

namespace PathTracing
{

class Application
{
public:
    static void Init();
    static void Shutdown();

    static void Run();

    static uint32_t GetVulkanApiVersion();
    static const vk::detail::DispatchLoaderDynamic &GetDispatchLoader();

private:
    static uint32_t s_VulkanApiVersion;
    static vk::Instance s_Instance;
    static vk::detail::DispatchLoaderDynamic *s_DispatchLoader;

#ifndef NDEBUG
    static vk::DebugUtilsMessengerEXT s_DebugMessenger;
#endif

    static vk::SurfaceKHR s_Surface;
    static std::unique_ptr<Swapchain> s_Swapchain;

    enum class State
    {
        Shutdown,
        HasInstance,
        HasWindow,
        HasDevice,
        HasSwapchain,
        HasUserInterface,
        Initialized,
        Running
    };

    static State s_State;

private:
    static bool CheckInstanceSupport(
        const std::vector<const char *> &requestedExtensions, const std::vector<const char *> &requestedLayers
    );
};

}
