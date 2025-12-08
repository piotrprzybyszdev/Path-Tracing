#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.hpp>
#include <nfd.hpp>

#include "Core/Core.h"

#include "Shaders/Debug/DebugShaderTypes.incl"

#include "Renderer/DeviceContext.h"
#include "Renderer/Renderer.h"

#include "SceneManager.h"
#include "UIComponents.h"
#include "UserInterface.h"
#include "Window.h"

namespace PathTracing
{

std::string UserInterface::s_IniFilePath = {};
bool UserInterface::s_IsVisible = false;
bool UserInterface::s_IsFocused = false;
ImGuiIO *UserInterface::s_Io = nullptr;

struct UIComponents;

vk::PresentModeKHR s_PresentMode = vk::PresentModeKHR::eFifo;
Shaders::SpecializationConstant s_RenderMode = Shaders::RenderModeColor;
Shaders::SpecializationConstant s_RaygenFlags = Shaders::RaygenFlagsNone;
Shaders::SpecializationConstant s_HitGroupFlags = Shaders::HitGroupFlagsNone;
std::span<const vk::PresentModeKHR> s_PresentModes = {};
Renderer::Settings s_Settings = {};
bool s_DebuggingEnabled = false;

std::unique_ptr<UIComponents> s_Components = nullptr;

static void CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return;

    logger::error("ImGui Vulkan Error: {}", vk::to_string(static_cast<vk::Result>(err)));
}

static const std::string &ToProgressString(BackgroundTaskType type)
{
    static const std::string shaderCompilationString = "Compiling Shaders";
    static const std::string textureUploadString = "Uploading Textures";
    static const std::string sceneImportString = "Importing Scene";

    switch (type)
    {
    case BackgroundTaskType::ShaderCompilation:
        return shaderCompilationString;
    case BackgroundTaskType::TextureUpload:
        return textureUploadString;
    case BackgroundTaskType::SceneImport:
        return sceneImportString;
    default:
        throw error("Unsupported BackgroundTaskType");
    }
}

void UserInterface::Init(
    vk::Instance instance, vk::Format format, uint32_t swapchainImageCount,
    std::span<const vk::PresentModeKHR> presentModes
)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    s_Io = &ImGui::GetIO();
    s_IniFilePath = (Application::GetConfig().ConfigDirectoryPath / "Path-Tracing-UI.ini").string();
    s_Io->IniFilename = s_IniFilePath.c_str();

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(Window::GetHandle(), true);
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = DeviceContext::GetPhysical();
    initInfo.Device = DeviceContext::GetLogical();
    initInfo.QueueFamily = DeviceContext::GetGraphicsQueue().FamilyIndex;
    initInfo.Queue = DeviceContext::GetGraphicsQueue().Handle;
    initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    initInfo.MinImageCount = swapchainImageCount;
    initInfo.ImageCount = swapchainImageCount;
    initInfo.CheckVkResultFn = CheckVkResult;
    initInfo.UseDynamicRendering = true;
    std::array<vk::Format, 1> formats = { format };
    initInfo.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfoKHR(0, formats);
    
    bool imguiResult = ImGui_ImplVulkan_Init(&initInfo);
    if (imguiResult == false)
        throw error("Failed to initialize ImGui");

    nfdresult_t nfdResult = NFD_Init();
    if (nfdResult == nfdresult_t::NFD_ERROR)
        throw error("Failed to initialize NFD");

    s_PresentModes = presentModes;
    s_Components = std::make_unique<UIComponents>();
}

void UserInterface::Shutdown()
{
    s_Components.reset();

    NFD::Quit();
    
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    s_IniFilePath.clear();
}

void UserInterface::OnUpdate(float timeStep)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (s_IsVisible)
        DefineUI();
}

void UserInterface::OnRender(vk::CommandBuffer commandBuffer)
{
    ImGui::Render();
    // Current ImGui implementation might vkQueueSubmit here so we have to lock
    auto lock = DeviceContext::GetGraphicsQueue().GetLock();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void UserInterface::OnKeyRelease(Key key)
{
    switch (key)
    {
    case Key::Space:
        s_IsVisible = !s_IsVisible;
        break;
    case Key::H:
        Renderer::ReloadShaders();
        break;
    case Key::P:
        SceneManager::GetActiveScene()->ToggleAnimationPause();
    }
}

bool UserInterface::GetIsFocused()
{
    return s_IsVisible && s_IsFocused;
}

vk::PresentModeKHR UserInterface::GetPresentMode()
{
    return s_PresentMode;
}

class SceneListContent : public Content
{
public:
    ~SceneListContent() override = default;

    void Render() override;
};

void SceneListContent::Render()
{
    for (auto &group : SceneManager::GetSceneGroupNames())
    {
        if (ImGui::TreeNode(group.c_str()))
        {
            const auto sceneNames = SceneManager::GetSceneNames(group);
            if (!sceneNames.empty())
            {
                for (auto &scene : sceneNames)
                {
                    ApplyLeftMargin();
                    if (ImGui::Selectable(scene.c_str()))
                        SceneManager::SetActiveScene(group, scene);
                }
            }
            else
                ImGui::Text(
                    group == "High Quality Scenes"
                        ? "You can install more scenes via CMake - Check README for more info"
                        : "There are no scenes in this group"
                );
            ImGui::TreePop();
        }
    }
}

class CameraListContent : public Content
{
public:
    ~CameraListContent() override = default;

    void Render() override;
};

void CameraListContent::Render()
{
    auto scene = SceneManager::GetActiveScene();

    ImGui::Text("Cameras");
    ApplyLeftMargin();
    if (ImGui::RadioButton("Input Camera", scene->GetActiveCameraId() == Scene::g_InputCameraId))
        scene->SetActiveCamera(Scene::g_InputCameraId);

    for (int i = 0; i < scene->GetSceneCamerasCount(); i++)
    {
        ApplyLeftMargin();
        ImGui::PushID(i);
        if (ImGui::RadioButton(std::format("Scene Camera {}", i).c_str(), scene->GetActiveCameraId() == i))
            scene->SetActiveCamera(i);
        ImGui::PopID();
    }
}

class BackgroundTaskListContent : public Content
{
public:
    ~BackgroundTaskListContent() override = default;

    void Render() override;

private:
    std::array<double, 3> m_LastTimeRunning = {};
    const double m_WaitAfterCompleteTime = 1.0;
};

void BackgroundTaskListContent::Render()
{
    double time = ImGui::GetTime();
    for (auto taskType : Application::g_BackgroundTasks)
    {
        auto taskState = Application::GetBackgroundTaskState(taskType);
        if (taskState.IsRunning())
            m_LastTimeRunning[static_cast<uint8_t>(taskType)] = time;

        if (time - m_LastTimeRunning[static_cast<uint8_t>(taskType)] > m_WaitAfterCompleteTime)
            continue;

        float fraction = taskState.GetDoneFraction();
        std::string text = std::format("{} {:.1f}%", ToProgressString(taskType), fraction * 100);

        const float size = ImGui::GetContentRegionAvail().x - 2 * (ImGui::GetCursorPosX() + m_LeftMargin);
        ApplyLeftMargin();
        ImGui::ProgressBar(fraction, ImVec2 { size, 0 }, text.c_str());
    }
}

class SettingsTab : public Tab
{
public:
    SettingsTab() : Tab("Settings")
    {
    }
    ~SettingsTab() override = default;

protected:
    void RenderContent() override;

private:
    CameraListContent m_CameraList;
};

void SettingsTab::RenderContent()
{
    ImGui::Dummy({ 0.0f, 5.0f });
    m_CameraList.SetLeftMargin(10.0f);
    m_CameraList.Render();
    ImGui::Dummy({ 0.0f, 5.0f });

    float exposure = std::log2(s_Settings.Exposure);
    int bounceCount = s_Settings.BounceCount, sampleCount = s_Settings.SampleCount;

    bool hasChanged = false;
    ImGui::Text("Exposure:");
    ImGui::SameLine();
    hasChanged |= ImGui::SliderFloat("##Exposure", &exposure, -10.0f, 10.0f, "%.2f");

    if (s_DebuggingEnabled)
        ImGui::BeginDisabled();

    ImGui::Text("Bounces:");
    ImGui::SameLine();
    hasChanged |= ImGui::SliderInt("##Bounces", &bounceCount, 1, 16, "%d");
    ImGui::Text("Samples:");
    ImGui::SameLine();
    hasChanged |= ImGui::SliderInt("##Samples", &sampleCount, 1, 16, "%d");

    if (s_DebuggingEnabled)
        ImGui::EndDisabled();

    if (hasChanged)
    {
        s_Settings.Exposure = std::pow(2.0f, exposure);
        s_Settings.BounceCount = bounceCount;
        s_Settings.SampleCount = sampleCount;
        Renderer::SetSettings(s_Settings);
    }
}

class SceneTab : public Tab
{
public:
    SceneTab() : Tab("Scenes"), m_Widget("Discovered Scenes:", { SceneListContent() }, 10.0f, 5.0f)
    {
    }
    ~SceneTab() override = default;

protected:
    void RenderContent() override;

private:
    Widget<SceneListContent, 1> m_Widget;
};

void SceneTab::RenderContent()
{
    ImGui::Dummy({ 0.0f, 5.0f });
    ImGui::Dummy({ 3.0f, 0.0f });
    ImGui::SameLine();
    if (ImGui::Button("Import scene from file"))
    {
        NFD::UniquePath path;
        nfdresult_t result = NFD::OpenDialog(path);
        if (result == nfdresult_t::NFD_OKAY)
            SceneManager::SetActiveScene(std::filesystem::path(path.get()));
        else if (result == nfdresult_t::NFD_ERROR)
        {
            logger::error("File dialog error: {}", NFD::GetError());
            NFD::ClearError();
        }
    }

    ImGui::Dummy({ 0.0f, 3.0f });
    ImGui::Dummy({ 3.0f, 0.0f });
    ImGui::SameLine();
    if (ImGui::Button("Refresh discovered scenes"))
        SceneManager::DiscoverScenes();

    m_Widget.Render();
}

class DisplayTab : public Tab
{
public:
    DisplayTab()
        : Tab("Display"),
          m_PresentModeOptions(InitPresentModes(s_PresentModes), s_PresentMode, "Present Mode"),
          m_WindowModeOptions(g_WindowModes, m_Mode, "Window Mode"),
          m_ResolutionOptions(InitResolutions(Window::GetResolutions()), m_Resolution, "Resolution")
    {
        m_PresentModeOptions.SetLeftMargin(5.0f);
        m_WindowModeOptions.SetLeftMargin(5.0f);
        m_ResolutionOptions.SetLeftMargin(5.0f);

    }
    ~DisplayTab() override = default;

protected:
    void RenderContent() override;

private:
    std::span<const ComboOption<vk::Extent2D>> InitResolutions(std::span<const vk::Extent2D> resolutions)
    {
        m_ResolutionStrings.reserve(resolutions.size());
        m_Resolutions.reserve(resolutions.size());
        for (vk::Extent2D resolution : resolutions)
        {
            m_ResolutionStrings.push_back(std::format("{}x{}", resolution.width, resolution.height));
            m_Resolutions.emplace_back(resolution, m_ResolutionStrings.back().c_str());
        }

        return m_Resolutions;
    }

    std::span<const ComboOption<vk::PresentModeKHR>> InitPresentModes(
        std::span<const vk::PresentModeKHR> presentModes
    )
    {
        m_PresentStrings.reserve(presentModes.size());
        m_PresentModes.reserve(presentModes.size());
        for (vk::PresentModeKHR presentMode : presentModes)
        {
            m_PresentStrings.push_back(vk::to_string(presentMode));
            m_PresentModes.emplace_back(presentMode, m_PresentStrings.back().c_str());
        }

        return m_PresentModes;
    }

private:
    std::vector<std::string> m_PresentStrings;
    std::vector<ComboOption<vk::PresentModeKHR>> m_PresentModes;

    static constexpr std::array<ComboOption<WindowMode>, 3> g_WindowModes = { {
        { WindowMode::Windowed, "Windowed" },
        { WindowMode::FullScreen, "Full Screen" },
        { WindowMode::FullScreenWindowed, "Full Screen Windowed" },
    } };

    std::vector<std::string> m_ResolutionStrings;
    std::vector<ComboOption<vk::Extent2D>> m_Resolutions;

    WindowMode m_Mode = WindowMode::Windowed;
    vk::Extent2D m_Resolution = Window::GetSize();

    ComboOptions<vk::PresentModeKHR> m_PresentModeOptions;
    ComboOptions<WindowMode> m_WindowModeOptions;
    ComboOptions<vk::Extent2D> m_ResolutionOptions;
};

void DisplayTab::RenderContent()
{
    ImGui::Dummy({ 0.0f, 10.0f });
    m_PresentModeOptions.Render();
    ImGui::Dummy({ 0.0f, 5.0f });
    m_WindowModeOptions.Render();
    if (m_Mode != WindowMode::Windowed)
    {
        ImGui::Dummy({ 0.0f, 5.0f });
        m_ResolutionOptions.Render();

        if (m_ResolutionOptions.HasChanged())
            Window::SetResolution(m_Resolution);
    }

    if (m_WindowModeOptions.HasChanged())
    {
        Window::SetMode(m_Mode);
        m_Resolution = Window::GetSize();
    }
}

class DebugTab : public Tab
{
public:
    DebugTab()
        : Tab("Debug"), m_HitGroupOptions(g_HitGroupFlags, s_HitGroupFlags),
          m_RaygenOptions(g_RaygenFlags, s_RaygenFlags), m_RenderOptions(g_RenderModes, s_RenderMode),
          m_Flags("Render Flags", { m_HitGroupOptions, m_RaygenOptions }, 10.0f, 5.0f),
          m_Modes("Render Modes", { m_RenderOptions }, 10.0f, 5.0f)
    {
    }
    ~DebugTab() override = default;

protected:
    void RenderContent() override;

private:
    static constexpr std::array<CheckboxOption<Shaders::SpecializationConstant>, 2> g_RaygenFlags = { {
        { Shaders::RaygenFlagsForceOpaque, "Force Opaque" },
        { Shaders::RaygenFlagsCullBackFaces, "Cull Back Faces" },
    } };

    static constexpr std::array<CheckboxOption<Shaders::SpecializationConstant>, 6> g_HitGroupFlags = { {
        { Shaders::HitGroupFlagsDisableColorTexture, "Disable Color Texture" },
        { Shaders::HitGroupFlagsDisableNormalTexture, "Disable Normal Texture" },
        { Shaders::HitGroupFlagsDisableRoughnessTexture, "Disable Roughness Texture" },
        { Shaders::HitGroupFlagsDisableMetalicTexture, "Disable Metalic Texture" },
        { Shaders::HitGroupFlagsDisableMipMaps, "Disable Mip Maps" },
        { Shaders::HitGroupFlagsDisableShadows, "Disable Shadows" },
    } };

    static constexpr std::array<RadioOption<Shaders::SpecializationConstant>, 8> g_RenderModes = { {
        { Shaders::RenderModeColor, "Color" },
        { Shaders::RenderModeWorldPosition, "World Position" },
        { Shaders::RenderModeNormal, "Normal" },
        { Shaders::RenderModeTextureCoords, "Texture Coords" },
        { Shaders::RenderModeMips, "Mips" },
        { Shaders::RenderModeGeometry, "Geometry" },
        { Shaders::RenderModePrimitive, "Primitive" },
        { Shaders::RenderModeInstance, "Instance" },
    } };

    CheckboxOptions<Shaders::SpecializationConstant> m_HitGroupOptions;
    CheckboxOptions<Shaders::SpecializationConstant> m_RaygenOptions;
    RadioOptions<Shaders::SpecializationConstant> m_RenderOptions;

    Widget<CheckboxOptions<Shaders::SpecializationConstant>, 2> m_Flags;
    Widget<RadioOptions<Shaders::SpecializationConstant>, 1> m_Modes;

    bool m_DebuggingEnabled = false;
};

void DebugTab::RenderContent()
{
    ImGui::Dummy({ 0.0f, 5.0f });
    ImGui::Dummy({ 5.0f, 0.0f });
    ImGui::SameLine();

    bool hasChanged = ImGui::Checkbox("Enable debugging", &m_DebuggingEnabled);

    if (!m_DebuggingEnabled)
        ImGui::BeginDisabled();

    m_Flags.Render();
    ImGui::Dummy({ 0.0f, 5.0f });
    m_Modes.Render();

    if (!m_DebuggingEnabled)
        ImGui::EndDisabled();

    hasChanged |= std::ranges::any_of(m_Flags.GetContents(), [](const auto &option) { return option.HasChanged(); });
    hasChanged |= std::ranges::any_of(m_Modes.GetContents(), [](const auto &option) { return option.HasChanged(); });

    if (hasChanged)
    {
        if (m_DebuggingEnabled)
            Renderer::SetDebugRaytracingPipeline(
                DebugRaytracingPipelineConfig { s_RenderMode, s_RaygenFlags, 0, s_HitGroupFlags }
            );
        else
            Renderer::SetPathTracingPipeline({});
        s_DebuggingEnabled = m_DebuggingEnabled;
    }
}

class StatisticsTab : public Tab
{
public:
    StatisticsTab() : Tab("Statistics")
    {
    }
    ~StatisticsTab() override = default;

protected:
    void RenderContent() override;
};

void StatisticsTab::RenderContent()
{
    for (const auto &[key, value] : Stats::GetStats())
        ImGui::Text("%s", value.c_str());
}

class AboutTab : public Tab
{
public:
    AboutTab() : Tab("About")
    {
    }
    ~AboutTab() override = default;

protected:
    void RenderContent() override;
};

void AboutTab::RenderContent()
{
    ImGui::Text("Path-Tracing");
    ImGui::Text("Piotr Przybysz, Michal Popkowicz, 2025");
}

struct UIComponents
{
    SettingsTab Settings;
    SceneTab Scene;
    DisplayTab Display;
    DebugTab Debug;
    StatisticsTab Statistics;
    AboutTab About;

    Widget<BackgroundTaskListContent, 1> BackgroundTaskList =
        Widget<BackgroundTaskListContent, 1>("Background Tasks", { BackgroundTaskListContent() }, 5.0f, 0.0f);
};

void UserInterface::DefineUI()
{
    s_IsFocused = false;

    Stats::AddStat(
        "Framerate", "Framerate: {:.3f} ms/frame ({:.1f} FPS)", 1000.0f / s_Io->Framerate, s_Io->Framerate
    );

    ImGui::SetNextWindowPos(ImVec2(15, 15), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

    ImGui::Begin("Options");

    s_IsFocused |= ImGui::IsWindowFocused();

    if (ImGui::BeginTabBar("Options"))
    {
        s_Components->Settings.Render();
        s_Components->Scene.Render();
        s_Components->Display.Render();
        s_Components->Debug.Render();
        s_Components->Statistics.Render();
        s_Components->About.Render();

        ImGui::EndTabBar();
    }

    ImGui::End();

    ImVec2 size = s_Io->DisplaySize;
    ImVec2 pos = ImVec2(size.x - 320, size.y - 170);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize({ 300, 150 });
    ImGui::Begin(
        "Background tasks", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    );

    s_Components->BackgroundTaskList.Render();

    ImGui::End();
}

}
