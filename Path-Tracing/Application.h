#pragma once

#include <memory>

#include <vulkan/vulkan.hpp>

#include "Core/Config.h"

#include "Renderer/Swapchain.h"

namespace PathTracing
{

class Application
{
public:
    static void Init(int argc, char *argv[]);
    static void Shutdown();

    static void Run();

    static uint32_t GetVulkanApiVersion();
    static const vk::detail::DispatchLoaderDynamic &GetDispatchLoader();
    static const Config &GetConfig();

private:
    static uint32_t s_VulkanApiVersion;
    static vk::Instance s_Instance;
    static std::unique_ptr<vk::detail::DispatchLoaderDynamic> s_DispatchLoader;

#if defined(CONFIG_VALIDATION_LAYERS) || defined(CONFIG_SHADER_DEBUG_INFO)
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

    static Config s_Config;

private:
    static void SetupLogger();
    static bool CheckInstanceSupport(
        const std::vector<const char *> &requestedExtensions, const std::vector<const char *> &requestedLayers
    );
};

}
