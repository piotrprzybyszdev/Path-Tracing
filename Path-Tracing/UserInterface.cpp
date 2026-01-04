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
bool s_DebuggingEnabled = false;
bool s_ShowingImportScene = false;
bool s_ShowingOfflineRender = false;

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
    static const std::string renderingString = "Rendering";

    switch (type)
    {
    case BackgroundTaskType::ShaderCompilation:
        return shaderCompilationString;
    case BackgroundTaskType::TextureUpload:
        return textureUploadString;
    case BackgroundTaskType::SceneImport:
        return sceneImportString;
    case BackgroundTaskType::Rendering:
        return renderingString;
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

    ImGui::EndFrame();
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
        if (!Application::IsRendering())
            Renderer::ReloadShaders();
        break;
    case Key::P:
        SceneManager::GetActiveScene()->ToggleAnimationPause();
        break;
    case Key::Esc:
        Window::SetMode(WindowMode::Windowed);
        break;
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
    std::array<double, Application::g_BackgroundTasks.size()> m_LastTimeRunning = {};
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

        if (time - m_LastTimeRunning[static_cast<uint8_t>(taskType)] > m_WaitAfterCompleteTime ||
            taskState.TotalCount == 0)
            continue;

        float fraction = taskState.GetDoneFraction();
        std::string text = std::format("{} {:.1f}%", ToProgressString(taskType), fraction * 100);

        const float size = ImGui::GetContentRegionAvail().x - 2 * (ImGui::GetCursorPosX() + m_LeftMargin);
        ApplyLeftMargin();
        ImGui::ProgressBar(fraction, ImVec2 { size, 0 }, text.c_str());
    }
}

class ImportSceneContent : public Content
{
public:
    ~ImportSceneContent() override = default;

    void Render() override;

private:
    std::vector<std::filesystem::path> m_ComponentPaths;
    std::optional<std::filesystem::path> m_SkyboxPath;

private:
    bool RenderPathText(const std::filesystem::path &path, float width);
    std::vector<std::filesystem::path> OpenFileDialog(bool multiple = false);
};

bool ImportSceneContent::RenderPathText(const std::filesystem::path &path, float width)
{
    const size_t maxLength = 40;
    ImVec2 buttonSize(80, 20);

    const std::string pathString = path.string();
    const char *fmt = pathString.size() > maxLength ? "...%s" : "%s";
    const size_t offset = pathString.size() > maxLength ? (pathString.size() - maxLength + 3) : 0;
    ApplyLeftMargin();
    ImGui::Text(fmt, pathString.c_str() + offset);
    ImGui::SameLine();
    AlignItemRight(width, buttonSize.x, 30);
    ItemMarginTop(-2);
    return ImGui::Button("Remove", buttonSize);
}

std::vector<std::filesystem::path> ImportSceneContent::OpenFileDialog(bool multiple)
{
    auto checkError = [](nfdresult_t result) {
        if (result == nfdresult_t::NFD_ERROR)
        {
            logger::error("File dialog error: {}", NFD::GetError());
            NFD::ClearError();
        }
        return result == nfdresult_t::NFD_OKAY;
    };

    if (multiple)
    {
        NFD::UniquePathSet paths;
        nfdresult_t result = NFD::OpenDialogMultiple(paths, (const nfdu8filteritem_t *)nullptr);
        glfwRestoreWindow(Window::GetHandle());
        if (checkError(result))
        {
            nfdpathsetsize_t size;
            nfdresult_t result = NFD::PathSet::Count(paths, size);
            assert(result == nfdresult_t::NFD_OKAY);
            
            std::vector<std::filesystem::path> ret;
            for (int i = 0; i < size; i++)
            {
                NFD::UniquePathSetPath path;
                nfdresult_t result = NFD::PathSet::GetPath(paths, i, path);
                assert(result == nfdresult_t::NFD_OKAY);
                ret.push_back(path.get());
            }

            return ret;
        }
    }
    else
    {
        NFD::UniquePath path;
        nfdresult_t result = NFD::OpenDialog(path);
        glfwRestoreWindow(Window::GetHandle());
        if (checkError(result))
            return { std::filesystem::path(path.get()) };
    }

    return {};
}

void ImportSceneContent::Render()
{
    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 buttonSize(100, 20);

    ImGui::Dummy({ 0, 5 });
    ApplyLeftMargin();
    ImGui::Text("Skybox");
    ImGui::Dummy({ 0, 3 });

    if (m_SkyboxPath.has_value())
    {
        ApplyLeftMargin();
        if (RenderPathText(m_SkyboxPath.value(), size.x))
            m_SkyboxPath.reset();
    }
    else
    {
        CenterItemHorizontally(size.x, buttonSize.x);
        if (ImGui::Button("Add Skybox", buttonSize))
        {
            auto result = OpenFileDialog();
            if (!result.empty())
            {
                assert(result.size() == 1); 
                m_SkyboxPath = result.front();
            }
        }
    }
    
    ImGui::Dummy({ 0, 10 });
    ApplyLeftMargin();
    ImGui::Text("Components");
    ImGui::Dummy({ 0, 3 });

    int deleteIndex = -1;
    AlignItemRight(size.x, size.x - 20, 10);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
    ImGui::BeginListBox("##Components", ImVec2(size.x - 20, 300));
    for (int i = 0; i < m_ComponentPaths.size(); i++)
    {
        ImGui::PushID(i);
        ImGui::Dummy({ 0, 2 });
        ApplyLeftMargin();
        if (RenderPathText(m_ComponentPaths[i], size.x - 10))
            deleteIndex = i;
        ImGui::PopID();
    }

    if (deleteIndex != -1)
        m_ComponentPaths.erase(m_ComponentPaths.begin() + deleteIndex);

    ImGui::Dummy({ 0, 10 });
    CenterItemHorizontally(size.x, buttonSize.x, -10.0f);
    if (ImGui::Button("Add Component", buttonSize))
    {
        auto result = OpenFileDialog(true);
        m_ComponentPaths.insert(m_ComponentPaths.begin(), result.begin(), result.end());
    }
    ImGui::EndListBox();
    ImGui::PopStyleColor();

    ImGui::Dummy({ 0, 5 });
    const float margin = 20;
    ImGui::SetCursorPosX(margin);
    AlignItemBottom(size.y, buttonSize.y, margin);
    if (ImGui::Button("Cancel", buttonSize))
        s_ShowingImportScene = false;
    AlignItemRight(size.x, buttonSize.x, margin);
    AlignItemBottom(size.y, buttonSize.y, margin);
    if (ImGui::Button("Import", buttonSize))
    {
        if (!m_ComponentPaths.empty())
        {
            auto loader = std::make_unique<CombinedSceneLoader>();
            loader->AddComponents(m_ComponentPaths);
            if (m_SkyboxPath.has_value())
                loader->AddSkybox2D(m_SkyboxPath.value());

            SceneManager::SetActiveScene(std::move(loader), "User Scene");
        }
        s_ShowingImportScene = false;
    }
}

class OfflineRenderContent : public Content
{
public:
    ~OfflineRenderContent() override = default;

    void Render() override;

private:
    std::optional<std::filesystem::path> m_OutputPath;
    int m_MaxSampleCount = 1000;
    int m_MaxBounceCount = 4;
    bool m_DepthOfField = false;
    float m_LensRadius = 0.1f;
    float m_FocalDistance = 10.0f;
    bool m_Bloom = true;
    float m_BloomThreshold = 1.0f;
    float m_BloomIntensity = 0.1f;
    int m_Extent[2] = { 1280, 720 };
    int m_FrameCount = 60;
    int m_Framerate = 60;
    int m_Time = 5;
    float m_Exposure = 0.0f;
    bool m_IsImageOutput = true;
    const char *m_TimeUnit = s_TimeUnitMinutes;
    const char *m_OutputFormat = s_OutputFormatPng;

private:
    static inline const char *s_TimeUnitSeconds = "sec";
    static inline const char *s_TimeUnitMinutes = "min";
    static inline const char *s_TimeUnitHours = "hr";

    static inline const char *s_OutputFormatPng = "png";
    static inline const char *s_OutputFormatJpg = "jpg";
    static inline const char *s_OutputFormatTga = "tga";
    static inline const char *s_OutputFormatHdr = "hdr";
    static inline const char *s_OutputFormatMp4 = "mp4";

    static inline const nfdfilteritem_t s_PngItemFilter = { .name = "Png Image (.png)", .spec = "png" };
    static inline const nfdfilteritem_t s_JpgItemFilter = { .name = "Jpg Image (.jpg)", .spec = "jpg,jpeg" };
    static inline const nfdfilteritem_t s_TgaItemFilter = { .name = "Tga Image (.tga)", .spec = "tga" };
    static inline const nfdfilteritem_t s_HdrItemFilter = { .name = "Hdr Image (.hdr)", .spec = "hdr" };
    static inline const nfdfilteritem_t s_Mp4ItemFilter = { .name = "Mp4 Video (.mp4)", .spec = "mp4" };

private:
    void SaveFileDialog();
    bool RenderPathButton();
    std::chrono::seconds GetTime();
    OutputFormat GetOutputFormat();
    const nfdfilteritem_t *GetFileFilter();
    bool IsRenderDisalbed(const char *&reason);
};

void OfflineRenderContent::Render()
{
    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 buttonSize(100, 20);

    ImGui::Dummy({ 0, 5 });
    ImGui::Separator();
    ImGui::Dummy({ 10, 0 });
    ImGui::SameLine();

    if (!Renderer::CanRenderVideo())
        ImGui::BeginDisabled();
    if (ImGui::Button(m_IsImageOutput ? "Image" : "Video"))
    {
        m_IsImageOutput = !m_IsImageOutput;
        m_OutputFormat = m_IsImageOutput ? s_OutputFormatPng : s_OutputFormatMp4;
    }
    if (!Renderer::CanRenderVideo())
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("FFmpeg wasn't found - video output is disabled");
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::Dummy({ 2, 0 });
    ImGui::SameLine();
    ImGui::Text("Format");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    if (ImGui::BeginCombo("##OuptutFormat", m_OutputFormat, ImGuiComboFlags_NoArrowButton))
    {
        if (m_IsImageOutput)
        {
            if (ImGui::Selectable(s_OutputFormatPng, m_OutputFormat == s_OutputFormatPng))
                m_OutputFormat = s_OutputFormatPng;
            if (ImGui::Selectable(s_OutputFormatJpg, m_OutputFormat == s_OutputFormatJpg))
                m_OutputFormat = s_OutputFormatJpg;
            if (ImGui::Selectable(s_OutputFormatTga, m_OutputFormat == s_OutputFormatTga))
                m_OutputFormat = s_OutputFormatTga;
            if (ImGui::Selectable(s_OutputFormatHdr, m_OutputFormat == s_OutputFormatHdr))
                m_OutputFormat = s_OutputFormatHdr;
        }
        else
        {
            if (ImGui::Selectable(s_OutputFormatMp4, m_OutputFormat == s_OutputFormatMp4))
                m_OutputFormat = s_OutputFormatMp4;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Dummy({ 2, 0 });
    ImGui::SameLine();
    if (RenderPathButton())
        SaveFileDialog();
    ImGui::Separator();
    ImGui::Dummy({ 0, 10 });

    ImGui::Dummy({ 40, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Settings", 2, ImGuiTableFlags_None, { 400.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Image Size");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt2("##ImageSize", m_Extent);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Max Sample Count");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##MaxSampleCount", &m_MaxSampleCount, 0, 10000);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Max Render Time");
        ImGui::TableNextColumn();
        float width = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(width - 70);
        ImGui::InputInt("##MaxRenderTime", &m_Time, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        if (ImGui::BeginCombo("##MaxRenderTimeUnit", m_TimeUnit, ImGuiComboFlags_NoArrowButton))
        {
            if (ImGui::Selectable(s_TimeUnitSeconds, m_TimeUnit == s_TimeUnitSeconds))
                m_TimeUnit = s_TimeUnitSeconds;
            if (ImGui::Selectable(s_TimeUnitMinutes, m_TimeUnit == s_TimeUnitMinutes))
                m_TimeUnit = s_TimeUnitMinutes;
            if (ImGui::Selectable(s_TimeUnitHours, m_TimeUnit == s_TimeUnitHours))
                m_TimeUnit = s_TimeUnitHours;
            ImGui::EndCombo();
        }

        if (!m_IsImageOutput)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Framerate");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt("##Framerate", &m_Framerate, 0);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Frame Count");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt("##FrameCount", &m_FrameCount, 0);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Max Bounce Count");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##MaxBounceCount", &m_MaxBounceCount, 1, 64);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Depth of field simulation");
        ImGui::TableNextColumn();
        ImGui::Checkbox("##DepthOfField", &m_DepthOfField);

        if (!m_DepthOfField)
            ImGui::BeginDisabled();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Lens Radius");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##LensRadius", &m_LensRadius, 0.01f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Focal Distance");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat(
            "##FocalDistance", &m_FocalDistance, 1.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic
        );

        if (!m_DepthOfField)
            ImGui::EndDisabled();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Exposure");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##Exposure", &m_Exposure, -10.0f, 10.0f, "%.2f");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Bloom");
        ImGui::TableNextColumn();
        ImGui::Checkbox("##Bloom", &m_Bloom);

        if (!m_Bloom)
            ImGui::BeginDisabled();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Bloom Threshold");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##BloomThreshold", &m_BloomThreshold, 1.0f, 5.0f, "%.2f");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Bloom Intensity");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##BloomIntensity", &m_BloomIntensity, 0.0f, 1.0f, "%.2f");

        if (!m_Bloom)
            ImGui::EndDisabled();

        ImGui::EndTable();
    }

    ImGui::Dummy({ 0, 5 });
    const float margin = 20;
    ImGui::SetCursorPosX(margin);
    AlignItemBottom(size.y, buttonSize.y, margin);
    if (ImGui::Button("Cancel", buttonSize))
        s_ShowingOfflineRender = false;
    AlignItemRight(size.x, buttonSize.x, margin);
    AlignItemBottom(size.y, buttonSize.y, margin);

    const char *reason;
    const bool isRenderDisabled = IsRenderDisalbed(reason);

    if (isRenderDisabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Render", buttonSize))
    {
        Application::BeginOfflineRendering();
        Renderer::SetSettings(Renderer::PathTracingSettings(
            m_MaxBounceCount, m_DepthOfField ? m_LensRadius : 0.0f, m_FocalDistance
        ));
        Renderer::SetSettings(Renderer::PostProcessSettings(std::pow(2.0f, m_Exposure), m_BloomThreshold, m_Bloom ? m_BloomIntensity : 0.0f));
        Renderer::SetSettings(
            Renderer::RenderSettings(
                OutputInfo(
                    m_OutputPath.value(), vk::Extent2D(m_Extent[0], m_Extent[1]), m_Framerate,
                    GetOutputFormat()
                ),
                m_IsImageOutput ? 1 : m_FrameCount, m_MaxSampleCount, GetTime()
            )
        );
        s_ShowingOfflineRender = false;
    }
    if (isRenderDisabled)
    {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", reason);
    }
}

void OfflineRenderContent::SaveFileDialog()
{
    auto checkError = [](nfdresult_t result) {
        if (result == nfdresult_t::NFD_ERROR)
        {
            logger::error("File dialog error: {}", NFD::GetError());
            NFD::ClearError();
        }
        return result == nfdresult_t::NFD_OKAY;
    };

    NFD::UniquePath path;
    nfdresult_t result =
        NFD::SaveDialog(path, GetFileFilter(), 1, nullptr, SceneManager::GetActiveScene()->GetName().c_str());
    if (checkError(result))
        m_OutputPath = std::filesystem::path(path.get());
    glfwRestoreWindow(Window::GetHandle());
}

bool OfflineRenderContent::RenderPathButton()
{
    ImGui::Text("Path");
    ImGui::SameLine();

    std::string pathString = "Output path not selected";

    if (m_OutputPath.has_value())
    {
        pathString = m_OutputPath.value().string();

        const size_t maxLength = 30;
        if (pathString.size() > maxLength)
            pathString = "..." + pathString.substr(pathString.size() - maxLength + 3);
    }

    return ImGui::Button(pathString.c_str());
}

std::chrono::seconds OfflineRenderContent::GetTime()
{
    if (m_TimeUnit == s_TimeUnitSeconds)
        return std::chrono::seconds(m_Time);
    if (m_TimeUnit == s_TimeUnitMinutes)
        return std::chrono::minutes(m_Time);
    if (m_TimeUnit == s_TimeUnitHours)
        return std::chrono::hours(m_Time);

    throw error("Unsupported time unit type");
}

OutputFormat OfflineRenderContent::GetOutputFormat()
{
    if (m_OutputFormat == s_OutputFormatPng)
        return OutputFormat::Png;
    if (m_OutputFormat == s_OutputFormatJpg)
        return OutputFormat::Jpg;
    if (m_OutputFormat == s_OutputFormatTga)
        return OutputFormat::Tga;
    if (m_OutputFormat == s_OutputFormatHdr)
        return OutputFormat::Hdr;
    if (m_OutputFormat == s_OutputFormatMp4)
        return OutputFormat::Mp4;

    throw error("Unsupported output format");
}

const nfdfilteritem_t *OfflineRenderContent::GetFileFilter()
{
    if (m_OutputFormat == s_OutputFormatPng)
        return &s_PngItemFilter;
    if (m_OutputFormat == s_OutputFormatJpg)
        return &s_JpgItemFilter;
    if (m_OutputFormat == s_OutputFormatTga)
        return &s_TgaItemFilter;
    if (m_OutputFormat == s_OutputFormatHdr)
        return &s_HdrItemFilter;
    if (m_OutputFormat == s_OutputFormatMp4)
        return &s_Mp4ItemFilter;

    throw error("Unsupported output format");
}

bool OfflineRenderContent::IsRenderDisalbed(const char *&reason)
{
    if (!m_OutputPath.has_value())
    {
        reason = "Select output file path";
        return true;
    }

    if (s_DebuggingEnabled)
    {
        reason = "Disable debug mode to begin render";
        return true;
    }

    auto task = Application::GetBackgroundTaskState(BackgroundTaskType::TextureUpload);
    if (task.IsRunning())
    {
        reason = "Texture upload is still in progress";
        return true;
    }

    task = Application::GetBackgroundTaskState(BackgroundTaskType::SceneImport);
    if (task.IsRunning())
    {
        reason = "Scene import is still in progress";
        return true;
    }

    return false;
}

class SettingsContent : public Content
{
public:
    ~SettingsContent() override = default;

    void Render() override;

private:
    float m_Exposure = 0.0f;
    int m_BounceCount = 4;
    int m_SampleCount = 1;
    bool m_DepthOfField = false;
    float m_LensRadius = 0.1f;
    float m_FocalDistance = 10.0f;
    bool m_Bloom = true;
    float m_BloomThreshold = 1.0f;
    float m_BloomIntensity = 0.1f;
};

void SettingsContent::Render()
{
    bool pathTracingSettingsChanged = false;
    bool postProcessSettingsChanged = false;
    if (s_DebuggingEnabled)
        ImGui::BeginDisabled();

    ImGui::Dummy({ 0, 5 });
    if (ImGui::TreeNodeEx("Path-tracing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Bounces: ");
        ImGui::SameLine();
        pathTracingSettingsChanged |= ImGui::SliderInt("##Bounces", &m_BounceCount, 1, 16, "%d");

        pathTracingSettingsChanged |= ImGui::Checkbox("Depth of field simulation", &m_DepthOfField);

        if (!m_DepthOfField)
            ImGui::BeginDisabled();

        ImGui::Text("Lens Radius: ");
        ImGui::SameLine();
        pathTracingSettingsChanged |=
            ImGui::SliderFloat("##LensRadius", &m_LensRadius, 0.01f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

        ImGui::Text("Focal Distance: ");
        ImGui::SameLine();
        pathTracingSettingsChanged |= ImGui::SliderFloat(
            "##FocalDistance", &m_FocalDistance, 1.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic
        );

        if (!m_DepthOfField)
            ImGui::EndDisabled();

        ImGui::TreePop();
    }
    if (s_DebuggingEnabled)
        ImGui::EndDisabled();

    ImGui::Dummy({ 0, 5 });
    if (ImGui::TreeNodeEx("Post-processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Exposure:");
        ImGui::SameLine();
        postProcessSettingsChanged |= ImGui::SliderFloat("##Exposure", &m_Exposure, -10.0f, 10.0f, "%.2f");

        postProcessSettingsChanged |= ImGui::Checkbox("Bloom", &m_Bloom);

        if (!m_Bloom)
            ImGui::BeginDisabled();

        ImGui::Text("Bloom Threshold:");
        ImGui::SameLine();
        postProcessSettingsChanged |=
            ImGui::SliderFloat("##BloomThreshold", &m_BloomThreshold, 1.0f, 5.0f, "%.2f");

        ImGui::Text("Bloom Intensity:");
        ImGui::SameLine();
        postProcessSettingsChanged |=
            ImGui::SliderFloat("##BloomIntensity", &m_BloomIntensity, 0.0f, 1.0f, "%.2f");

        if (!m_Bloom)
            ImGui::EndDisabled();

        ImGui::TreePop();
    }

    if (pathTracingSettingsChanged)
        Renderer::SetSettings(Renderer::PathTracingSettings(
            m_BounceCount, m_DepthOfField ? m_LensRadius : 0.0f, m_FocalDistance
        ));
    if (postProcessSettingsChanged)
        Renderer::SetSettings(Renderer::PostProcessSettings(std::pow(2.0f, m_Exposure), m_BloomThreshold, m_Bloom ? m_BloomIntensity : 0.0f));
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
    SettingsContent m_Settings;
};

void SettingsTab::RenderContent()
{
    ImGui::Dummy({ 0.0f, 5.0f });
    ImGui::Dummy({ 5.0f, 0.0f });
    ImGui::SameLine();

    if (ImGui::TreeNodeEx("Cameras", ImGuiTreeNodeFlags_DefaultOpen))
    {
        m_CameraList.SetLeftMargin(10.0f);
        m_CameraList.Render();
        ImGui::TreePop();
    }

    ImGui::Dummy({ 0.0f, 5.0f });
    ImGui::Dummy({ 5.0f, 0.0f });
    ImGui::SameLine();

    if (ImGui::TreeNodeEx("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        m_Settings.SetLeftMargin(10.0f);
        m_Settings.Render();
        ImGui::TreePop();
    }

    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 buttonSize(100, 25);

    AlignItemLeft(15);
    AlignItemBottom(size.y, buttonSize.y, 15);
    if (Application::IsRendering())
    {
        ImGui::EndDisabled();
        if (ImGui::Button("Cancel Render", buttonSize))
            Renderer::CancelRendering();
        ImGui::BeginDisabled();
    }

    AlignItemRight(size.x, buttonSize.x, 15);
    AlignItemBottom(size.y, buttonSize.y, 15);
    
    if (ImGui::Button("Render", buttonSize))
        s_ShowingOfflineRender = true;
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
        s_ShowingImportScene = true;
    ImGui::SameLine();
    ImGui::Dummy({ 5.0f, 0.0f });
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
        { Shaders::HitGroupFlagsDisableMetallicTexture, "Disable Metallic Texture" },
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
    ImGui::Text("Piotr Przybysz, Michal Popkowicz, 2026");
}

struct UIComponents
{
    SettingsTab Settings;
    SceneTab Scene;
    DisplayTab Display;
    DebugTab Debug;
    StatisticsTab Statistics;
    AboutTab About;

    FixedWindow<BackgroundTaskListContent, 1> BackgroundTasksWindow = FixedWindow(
        ImVec2(300, 150), "Background tasks",
        Widget<BackgroundTaskListContent, 1>("Background Tasks", { BackgroundTaskListContent() }, 5.0f, 0.0f)
    );

    FixedWindow<ImportSceneContent, 1> ImportSceneWindow = FixedWindow(
        ImVec2(500, 500), "Import Scene",
        Widget<ImportSceneContent, 1>("Import Scene", { ImportSceneContent() }, 5.0f, 0.0f)
    );

    FixedWindow<OfflineRenderContent, 1> OfflineRenderWindow = FixedWindow(
        ImVec2(500, 500), "Offline Render",
        Widget<OfflineRenderContent, 1>("Offline Rendering Settings", { OfflineRenderContent() }, 5.0f, 0.0f)
    );
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
        s_Components->Settings.Render(Application::IsRendering());
        s_Components->Scene.Render(Application::IsRendering());
        s_Components->Display.Render();
        s_Components->Debug.Render(Application::IsRendering());
        s_Components->Statistics.Render();
        s_Components->About.Render();

        ImGui::EndTabBar();
    }

    ImGui::End();

    s_Components->BackgroundTasksWindow.RenderBottomRight(s_Io->DisplaySize, ImVec2(20, 20));

    if (s_ShowingImportScene)
        s_Components->ImportSceneWindow.RenderCenter(s_Io->DisplaySize);

    if (s_ShowingOfflineRender)
        s_Components->OfflineRenderWindow.RenderCenter(s_Io->DisplaySize);
}

}
