#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include <ranges>
#include <string_view>

#include "Core/Camera.h"
#include "Core/Core.h"
#include "Core/Input.h"

#include "Renderer/DeviceContext.h"
#include "Renderer/Renderer.h"
#include "Renderer/Swapchain.h"

#include "Application.h"
#include "MaterialSystem.h"
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

vk::Instance Application::s_Instance = nullptr;
vk::detail::DispatchLoaderDynamic *Application::s_DispatchLoader = nullptr;

#ifndef NDEBUG
vk::DebugUtilsMessengerEXT Application::s_DebugMessenger = nullptr;
#endif

vk::SurfaceKHR Application::s_Surface = nullptr;
std::unique_ptr<Swapchain> Application::s_Swapchain = nullptr;

Application::State Application::s_State = Application::State::Shutdown;

void Application::Init()
{
    uint32_t version = vk::enumerateInstanceVersion();

    uint32_t variant = vk::apiVersionVariant(version);
    uint32_t major = vk::apiVersionMajor(version);
    uint32_t minor = vk::apiVersionMinor(version);
    uint32_t patch = vk::apiVersionPatch(version);

    logger::debug("Highest supported vulkan version: {}.{}.{}", major, minor, patch);

    version = vk::makeApiVersion(variant, major, minor, 0u);
    logger::info("Selected vulkan version: {}.{}.{}", major, minor, 0u);

    if (version == 0)
        logger::error(std::format("Vulkan API version variant is not equal to 0: ({})", variant));

    const char *applicationName = "Path Tracing";
    vk::ApplicationInfo applicationInfo(applicationName, 1, applicationName, 1, version);

    {
        int result = glfwInit();

#ifndef NDEBUG
        if (result == GLFW_FALSE)
            throw error("Glfw initialization failed!");

        glfwSetErrorCallback(GlfwErrorCallback);
#endif
    }

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> requestedExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    std::vector<const char *> requestedLayers;
#ifndef NDEBUG
    requestedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    requestedLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    if (!CheckInstanceSupport(requestedExtensions, requestedLayers))
        throw error("Instance doesn't have required extensions or layers");

    vk::InstanceCreateInfo createInfo(
        vk::InstanceCreateFlags(), &applicationInfo, requestedLayers, requestedExtensions
    );

    s_Instance = vk::createInstance(createInfo);
    s_State = State::HasInstance;

    s_DispatchLoader = new vk::detail::DispatchLoaderDynamic(s_Instance, vkGetInstanceProcAddr);

#ifndef NDEBUG
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

    constexpr vk::Extent2D windowSize(1280, 720);

    Window::Create(windowSize.width, windowSize.height, applicationName);
    s_Surface = Window::CreateSurface(s_Instance);
    Input::SetWindow(Window::GetHandle());
    s_State = State::HasWindow;

    DeviceContext::Init(s_Instance, requestedLayers, s_Surface);
    s_State = State::HasDevice;

    s_Swapchain = std::make_unique<Swapchain>(
        s_Surface, vk::SurfaceFormatKHR(vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear),
        UserInterface::GetPresentMode(), windowSize
    );
    s_State = State::HasSwapchain;

    UserInterface::Init(
        s_Instance, s_Swapchain->GetSurfaceFormat().format, DeviceContext::GetPhysical(),
        DeviceContext::GetLogical(), DeviceContext::GetGraphicsQueueFamilyIndex(),
        DeviceContext::GetGraphicsQueue(), s_Swapchain->GetImageCount()
    );
    s_State = State::HasUserInterface;

    Renderer::Init(s_Swapchain.get());
    s_State = State::Initialized;

    MaterialSystem::Init();

    // TODO: Move to Scene class when implementing model loading
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets";
    const std::array<std::string, 3> assetNames = { "Metal", "PavingStones", "Logs" };
    const std::array<std::string, 3> materials = {
        "Metal062C_1K-JPG",
        "PavingStones142_1K-JPG",
        "Logs001_1K-JPG",
    };

    for (int i = 0; i < 3; i++)
    {
        const std::filesystem::path materialPath = base / assetNames[i];
        const std::string &material = materials[i];
        MaterialSystem::AddMaterial(
            assetNames[i], materialPath / (material + "_Color.jpg"),
            materialPath / (material + "_NormalGL.jpg"),
            materialPath / (material + "_Roughness.jpg"), materialPath / (material + "_Roughness.jpg")
        );
    }

    MaterialSystem::UploadBuffer();
    Renderer::SetScene();
}

void Application::Shutdown()
{
    if (DeviceContext::GetLogical())
        DeviceContext::GetLogical().waitIdle();

    MaterialSystem::Shutdown();

    switch (s_State)
    {
    case State::Initialized:
        Renderer::Shutdown();
        [[fallthrough]];
    case State::HasUserInterface:
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
#ifndef NDEBUG
        s_Instance.destroyDebugUtilsMessengerEXT(s_DebugMessenger, nullptr, *s_DispatchLoader);
#endif
        delete s_DispatchLoader;
        s_Instance.destroy();
    }

    s_State = State::Shutdown;
}

void Application::Run()
{
    s_State = State::Running;

    Camera camera(45.0f, 100.0f, 0.1f);

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
            DeviceContext::GetLogical().waitIdle();
            s_Swapchain->Recreate(UserInterface::GetPresentMode());
        }

        const vk::Extent2D windowSize = Window::GetSize();
        if (windowSize != previousSize)
        {
            logger::info("Resize event for: {}x{}", windowSize.width, windowSize.height);

            DeviceContext::GetLogical().waitIdle();
            s_Swapchain->Recreate(windowSize);

            camera.OnResize(windowSize.width, windowSize.height);
            Renderer::OnResize(windowSize);

            previousSize = windowSize;
        }

        {
            Timer timer("Frame total");

            {
                Timer timer("Update");

                Window::OnUpdate(timeStep);
                camera.OnUpdate(timeStep);
                Renderer::s_EnabledTextures = UserInterface::GetEnabledTextures();
                Renderer::s_RenderMode = UserInterface::GetRenderMode();
                Renderer::OnUpdate(timeStep);
            }

            {
                Timer timer("Render");

                if (!s_Swapchain->AcquireImage())
                    continue;

                Renderer::Render(camera);

                if (!s_Swapchain->Present())
                    continue;
            }
        }

        Stats::FlushTimers();
    }

    s_State = State::Initialized;
}

const vk::detail::DispatchLoaderDynamic &Application::GetDispatchLoader()
{
    return *s_DispatchLoader;
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
