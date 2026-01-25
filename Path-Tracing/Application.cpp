#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <vulkan/vulkan.hpp>

#include <ranges>
#include <string_view>
#include <thread>

#include "Core/Camera.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Input.h"

#include "Renderer/DeviceContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/Swapchain.h"

#include "Application.h"
#include "SceneImporter.h"
#include "SceneManager.h"
#include "UserInterface.h"
#include "Window.h"

namespace PathTracing
{

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageType,
    const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData
)
{
    switch (messageSeverity)
    {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
        logger::error(pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
        logger::warn(pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
        logger::info(pCallbackData->pMessage);
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
        logger::debug(pCallbackData->pMessage);
        break;
    }

    return VK_FALSE;
}

static void GlfwErrorCallback(int error, const char *description)
{
    throw PathTracing::error(std::format("GLFW error {} {}", error, description).c_str());
}

static logger::level::level_enum GetLoggerLevel(Config::LogLevel level)
{
    switch (level)
    {
    case Config::LogLevel::Trace:
        return logger::level::trace;
    case Config::LogLevel::Debug:
        return logger::level::debug;
    case Config::LogLevel::Info:
        return logger::level::info;
    case Config::LogLevel::Warning:
        return logger::level::warn;
    case Config::LogLevel::Error:
        return logger::level::err;
    default:
        return logger::level::critical;
    }
}

bool BackgroundTaskState::IsRunning() const
{
    return TotalCount != DoneCount;
}

float BackgroundTaskState::GetDoneFraction() const
{
    if (!IsRunning())
        return 1.0f;
    return static_cast<float>(DoneCount) / TotalCount;
}

uint32_t Application::s_VulkanApiVersion = vk::ApiVersion;

vk::Instance Application::s_Instance = nullptr;
std::unique_ptr<vk::detail::DispatchLoaderDynamic> Application::s_DispatchLoader = nullptr;

#if defined(CONFIG_VALIDATION_LAYERS) || defined(CONFIG_SHADER_DEBUG_INFO)
vk::DebugUtilsMessengerEXT Application::s_DebugMessenger = nullptr;
#endif

vk::SurfaceKHR Application::s_Surface = nullptr;
std::unique_ptr<Swapchain> Application::s_Swapchain = nullptr;

Application::State Application::s_State = Application::State::Shutdown;
bool Application::s_AdvanceFrameOfflineRendering = false;

Config Application::s_Config = {};
std::array<BackgroundTask, Application::g_BackgroundTasks.size()> Application::s_BackgroundTasks = {};

void Application::Init(int argc, const char *argv[])
{
    s_Config = Config::Create(argc, argv);
    SetupLogger();

    uint32_t version = vk::enumerateInstanceVersion();

    uint32_t variant = vk::apiVersionVariant(version);
    uint32_t major = vk::apiVersionMajor(version);
    uint32_t minor = vk::apiVersionMinor(version);
    uint32_t patch = vk::apiVersionPatch(version);

    uint32_t requiredMajor = 1, requiredMinor = 3;

    logger::debug("Highest supported vulkan version: {}.{}.{}", major, minor, patch);

    if (major < requiredMajor || (major == requiredMajor && minor < requiredMinor))
        throw error(std::format(
            "Application requires Vulkan API version {}.{} or newer", requiredMajor, requiredMinor
        ));

    s_VulkanApiVersion = vk::makeApiVersion(variant, major, minor, 0u);
    logger::info("Selected vulkan version: {}.{}.{}", major, minor, 0u);

    if (variant != 0)
        logger::error(std::format("Vulkan API version variant is not equal to 0: ({})", variant));

    const char *applicationName = "Path Tracing";
    vk::ApplicationInfo applicationInfo(applicationName, 1, applicationName, 1, s_VulkanApiVersion);

    if (glfwInit() == GLFW_FALSE)
        throw error("Glfw initialization failed!");

#ifdef CONFIG_ASSERTS
    glfwSetErrorCallback(GlfwErrorCallback);
#endif

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> requestedExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    std::vector<const char *> requestedLayers;

    requestedExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    requestedExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    requestedExtensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
#if defined(CONFIG_VALIDATION_LAYERS) || defined(CONFIG_SHADER_DEBUG_INFO)
    requestedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
#ifdef CONFIG_VALIDATION_LAYERS
    requestedLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    if (!CheckInstanceSupport(requestedExtensions, requestedLayers))
        throw error("Instance doesn't have required extensions or layers");

    vk::InstanceCreateInfo createInfo(
        vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR, &applicationInfo, requestedLayers,
        requestedExtensions
    );

    s_Instance = vk::createInstance(createInfo);
    s_State = State::HasInstance;

    s_DispatchLoader = std::make_unique<vk::detail::DispatchLoaderDynamic>(s_Instance, vkGetInstanceProcAddr);

#if defined(CONFIG_VALIDATION_LAYERS) || defined(CONFIG_SHADER_DEBUG_INFO)
    {
        vk::DebugUtilsMessengerCreateInfoEXT createInfo(
            vk::DebugUtilsMessengerCreateFlagsEXT(),
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
            debugCallback, nullptr
        );

        s_DebugMessenger = s_Instance.createDebugUtilsMessengerEXT(createInfo, nullptr, *s_DispatchLoader);
    }
#endif

    const vk::Extent2D windowSize(1280, 720);

    Window::Create(windowSize.width, windowSize.height, applicationName);
    s_Surface = Window::CreateSurface(s_Instance);
    Input::SetWindow(Window::GetHandle());
    s_State = State::HasWindow;

    DeviceContext::Init(s_Instance, s_Surface);
    s_State = State::HasDevice;

    s_Swapchain = std::make_unique<Swapchain>(s_Surface, UserInterface::GetPresentMode(), windowSize, 2);
    s_State = State::HasSwapchain;

    UserInterface::Init(s_Instance, s_Swapchain->GetImageCount(), s_Swapchain->GetPresentModes());
    s_State = State::HasUserInterface;

    SceneImporter::Init();
    SceneManager::Init();

    Renderer::Init(s_Swapchain.get());
    s_State = State::Initialized;
}

void Application::Shutdown()
{
    switch (s_State)
    {
    case State::Rendering:
        [[fallthrough]];
    case State::Running:
        [[fallthrough]];
    case State::Initialized:
        Renderer::Shutdown();
        [[fallthrough]];
    case State::HasUserInterface:
        SceneManager::Shutdown();
        SceneImporter::Shutdown();
        UserInterface::Shutdown();
        [[fallthrough]];
    case State::HasSwapchain:
        s_Swapchain.reset();
        [[fallthrough]];
    case State::HasDevice:
        DeviceContext::Shutdown();
        [[fallthrough]];
    case State::HasWindow:
        s_Instance.destroySurfaceKHR(s_Surface);
        Window::Destroy();
        [[fallthrough]];
    case State::HasInstance:
#if defined(CONFIG_VALIDATION_LAYERS) || defined(CONFIG_SHADER_DEBUG_INFO)
        s_Instance.destroyDebugUtilsMessengerEXT(s_DebugMessenger, nullptr, *s_DispatchLoader);
#endif
        s_DispatchLoader.reset();
        s_Instance.destroy();
    }

    s_State = State::Shutdown;
}

void Application::Run()
{
    s_State = State::Running;

    bool recreateSwapchain = false;
    float lastFrameTime = 0.0f;
    vk::Extent2D previousSize = {};

    while (!Window::ShouldClose())
    {
        float time = glfwGetTime();

        float timeStep = time - lastFrameTime;
        lastFrameTime = time;

        Window::PollEvents();

        if (Window::IsMinimized())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (s_Swapchain->GetPresentMode() != UserInterface::GetPresentMode())
        {
            DeviceContext::GetGraphicsQueue().WaitIdle();
            s_Swapchain->Recreate(UserInterface::GetPresentMode());
            recreateSwapchain = false;
        }

        if (s_Swapchain->IsHdrAllowed() != UserInterface::IsHdrAllowed())
        {
            DeviceContext::GetGraphicsQueue().WaitIdle();
            s_Swapchain->Recreate(UserInterface::IsHdrAllowed());
            Renderer::UpdateHdr();
            recreateSwapchain = false;
        }

        if (s_Swapchain->GetImageCount() != Renderer::GetPreferredImageCount())
        {
            DeviceContext::GetGraphicsQueue().WaitIdle();
            s_Swapchain->Recreate(Renderer::GetPreferredImageCount());
            recreateSwapchain = false;
        }

        const vk::Extent2D windowSize = Window::GetSize();
        if (windowSize != previousSize)
        {
            logger::debug("Resize event for: {}x{}", windowSize.width, windowSize.height);

            DeviceContext::GetGraphicsQueue().WaitIdle();
            s_Swapchain->Recreate(windowSize);

            Renderer::OnResize(windowSize);

            previousSize = windowSize;
            recreateSwapchain = false;
        }

        if (recreateSwapchain)
        {
            s_Swapchain->Recreate();
            recreateSwapchain = false;
        }

        assert(recreateSwapchain == false);
        UserInterface::SetHdrSupported(s_Swapchain->IsHdrSupported());

        {
            MaxTimer timer("Frame total");

            {
                Timer timer("Update");

                Window::OnUpdate(timeStep);
                UserInterface::OnUpdate(timeStep);

                const auto scene = SceneManager::GetActiveScene();
                
                bool updated = false;
                if (!IsRendering())
                    updated = scene->Update(timeStep);
                if (IsRendering() && s_AdvanceFrameOfflineRendering)
                    updated = scene->Update(1.0f / Renderer::GetRenderFramerate());
                s_AdvanceFrameOfflineRendering = false;

                Renderer::UpdateSceneData(scene, updated);

                Renderer::OnUpdate(timeStep);
            }

            {
                MaxTimer timer("Render");

                if (!s_Swapchain->AcquireImage())
                {
                    recreateSwapchain = true;
                    continue;
                }

                Renderer::Render();

                if (!s_Swapchain->Present())
                {
                    recreateSwapchain = true;
                    continue;
                }
            }
        }

        Stats::FlushTimers();
    }

    s_State = State::Initialized;
}

uint32_t Application::GetVulkanApiVersion()
{
    return s_VulkanApiVersion;
}

const vk::detail::DispatchLoaderDynamic &Application::GetDispatchLoader()
{
    return *s_DispatchLoader;
}

const Config &Application::GetConfig()
{
    return s_Config;
}

void Application::ResetBackgroundTask(BackgroundTaskType type)
{
    s_BackgroundTasks[static_cast<uint8_t>(type)].TotalCount = 0;
    s_BackgroundTasks[static_cast<uint8_t>(type)].DoneCount = 0;
}

void Application::AddBackgroundTask(BackgroundTaskType type, uint32_t totalCount)
{
    s_BackgroundTasks[static_cast<uint8_t>(type)].TotalCount += totalCount;
}

void Application::IncrementBackgroundTaskDone(BackgroundTaskType type, uint32_t value)
{
    s_BackgroundTasks[static_cast<uint8_t>(type)].DoneCount += value;
}

void Application::SetBackgroundTaskDone(BackgroundTaskType type)
{
    auto &task = s_BackgroundTasks[static_cast<uint8_t>(type)];
    task.DoneCount = task.TotalCount.load();
}

BackgroundTaskState Application::GetBackgroundTaskState(BackgroundTaskType type)
{
    return BackgroundTaskState {
        .TotalCount = s_BackgroundTasks[static_cast<uint8_t>(type)].TotalCount,
        .DoneCount = s_BackgroundTasks[static_cast<uint8_t>(type)].DoneCount,
    };
}

void Application::BeginOfflineRendering()
{
    assert(s_State == State::Running);
    s_State = State::Rendering;
    const auto scene = SceneManager::GetActiveScene();
    if (scene->IsAnimationPaused())
        scene->ToggleAnimationPause();
    InputCamera::DisableInput();
}

void Application::EndOfflineRendering()
{
    assert(s_State == State::Rendering);
    s_State = State::Running;
    InputCamera::EnableInput();
}

void Application::AdvanceFrameOfflineRendering()
{
    assert(s_State == State::Rendering);
    s_AdvanceFrameOfflineRendering = true;
}

bool Application::IsRendering()
{
    assert(s_State == State::Running || s_State == State::Rendering);
    return s_State == State::Rendering;
}

void Application::SetupLogger()
{
    if (s_Config.LogToFile)
    {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto basic_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(s_Config.LogFilePath.string());
        std::array<spdlog::sink_ptr, 2> sinks { console_sink, basic_sink };
        auto logger = std::make_shared<spdlog::logger>("", sinks.begin(), sinks.end());
        spdlog::set_default_logger(logger);
    }

    spdlog::set_level(GetLoggerLevel(s_Config.LoggerLevel));
}

bool Application::CheckInstanceSupport(
    const std::vector<const char *> &requestedExtensions, const std::vector<const char *> &requestedLayers
)
{
    std::vector<vk::ExtensionProperties> supportedExtensions = vk::enumerateInstanceExtensionProperties();
    auto supportedExtensionNames =
        supportedExtensions | std::views::transform([](auto &props) { return props.extensionName.data(); });
    for (const auto &extension : supportedExtensionNames)
        logger::debug("Instance supports extension {}", extension);

    for (std::string_view extension : requestedExtensions)
    {
        logger::info("Instance Extension {} is required", extension);
        if (std::ranges::find(supportedExtensionNames, extension) == supportedExtensionNames.end())
        {
            logger::error("Instance Extension {} not supported", extension);
            return false;
        }
    }

    std::vector<vk::LayerProperties> supportedLayers = vk::enumerateInstanceLayerProperties();
    auto supportedLayerNames =
        supportedLayers | std::views::transform([](auto &props) { return props.layerName.data(); });

    for (const auto &extension : supportedLayerNames)
        logger::debug("Instance supports layer {}", extension);

    for (std::string_view layer : requestedLayers)
    {
        logger::info("Layer {} is required", layer);
        if (std::ranges::find(supportedLayerNames, layer) == supportedLayerNames.end())
        {
            logger::error("Instance Layer {} not supported", layer);
            return false;
        }
    }

    return true;
}

}
