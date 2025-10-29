#pragma once

#include <atomic>
#include <memory>

#include <vulkan/vulkan.hpp>

#include "Core/Config.h"

#include "Renderer/Swapchain.h"

namespace PathTracing
{

enum class BackgroundTaskType : uint8_t
{
    ShaderCompilation,
    TextureUpload,
    SceneImport,
};

struct BackgroundTask
{
    std::atomic<uint32_t> TotalCount;
    std::atomic<uint32_t> DoneCount;
};

struct BackgroundTaskState
{
    uint32_t TotalCount;
    uint32_t DoneCount;

    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] float GetDoneFraction() const;
};

class Application
{
public:
    static void Init(int argc, const char *argv[]);
    static void Shutdown();

    static void Run();

    static uint32_t GetVulkanApiVersion();
    static const vk::detail::DispatchLoaderDynamic &GetDispatchLoader();
    static const Config &GetConfig();

    static void ResetBackgroundTask(BackgroundTaskType type);
    static void AddBackgroundTask(BackgroundTaskType type, uint32_t totalCount);
    static void IncrementBackgroundTaskDone(BackgroundTaskType type, uint32_t value = 1);
    static BackgroundTaskState GetBackgroundTaskState(BackgroundTaskType type);

public:
    static inline constexpr std::array<BackgroundTaskType, 3> g_BackgroundTasks = {
        BackgroundTaskType::ShaderCompilation,
        BackgroundTaskType::TextureUpload,
        BackgroundTaskType::SceneImport,
    };

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
    static std::array<BackgroundTask, 3> s_BackgroundTasks;

private:
    static void SetupLogger();
    static bool CheckInstanceSupport(
        const std::vector<const char *> &requestedExtensions, const std::vector<const char *> &requestedLayers
    );
};

}
