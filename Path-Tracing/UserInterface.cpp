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
bool s_AllowHdr = false;
bool s_IsHdrSupported = false;
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
    vk::Instance instance, uint32_t swapchainImageCount, std::span<const vk::PresentModeKHR> presentModes
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
    std::array<vk::Format, 1> formats = { vk::Format::eR8G8B8A8Unorm };
    initInfo.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfoKHR(0, formats);
    
    bool imguiResult = ImGui_ImplVulkan_Init(&initInfo);
    if (imguiResult == false)
        throw error("Failed to initialize ImGui");

    nfdresult_t nfdResult = NFD_Init();
    if (nfdResult == nfdresult_t::NFD_ERROR)
        throw error("Failed to initialize NFD");

    s_PresentModes = presentModes;
    s_Components = std::make_unique<UIComponents>();

    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.34f, 0.38f, 1.00f);

    colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.27f, 0.32f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);

    colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.45f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.55f, 1.00f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.55f, 1.00f, 1.00f);

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);

    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);

    colors[ImGuiCol_PlotHistogram] = ImVec4(0.20f, 0.45f, 0.90f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.30f, 0.55f, 1.00f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);

    style.WindowRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);
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

bool UserInterface::IsHdrAllowed()
{
    return s_AllowHdr;
}

void UserInterface::SetHdrSupported(bool isSupported)
{
    s_IsHdrSupported = isSupported;
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
        ImGui::Dummy({ 10.0f, 0.0f });
        ImGui::SameLine();
        if (ImGui::CollapsingHeader(group.c_str()))
        {
            const auto sceneNames = SceneManager::GetSceneNames(group);
            if (!sceneNames.empty())
            {
                for (auto &scene : sceneNames)
                {
                    ApplyLeftMargin();
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
            ImGui::Dummy({ 0.0f, 5.0f });
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
    SceneDescription m_SceneDescription;

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

static const char *ToString(TextureType type)
{
    static const char *emissiveString = "Emissive";
    static const char *colorString = "Color";
    static const char *normalString = "Normal";
    static const char *roughnessString = "Roughness";
    static const char *metallicString = "Metallic";
    static const char *specularString = "Specular";
    static const char *glossinessString = "Glossiness";
    static const char *shininessString = "Shininess";

    switch (type)
    {
    case TextureType::Emisive:
        return emissiveString;
    case TextureType::Color:
        return colorString;
    case TextureType::Normal:
        return normalString;
    case TextureType::Roughness:
        return roughnessString;
    case TextureType::Metallic:
        return metallicString;
    case TextureType::Specular:
        return specularString;
    case TextureType::Glossiness:
        return glossinessString;
    case TextureType::Shininess:
        return shininessString;
    default:
        throw error("Unsupported texture type");
    }
}

static std::array<TextureType, 8> s_AllTextures = { TextureType::Emisive,    TextureType::Color,
                                                    TextureType::Normal,     TextureType::Roughness,
                                                    TextureType::Metallic,   TextureType::Specular,
                                                    TextureType::Glossiness, TextureType::Shininess };

static std::span<TextureType, 4> GetTextures(MaterialType type)
{
    static std::array<TextureType, 4> metallicRoughnessTextures = { TextureType::Color, TextureType::Normal,
                                                                    TextureType::Roughness,
                                                                    TextureType::Metallic };
    static std::array<TextureType, 4> specularGlossinessTextures = { TextureType::Color, TextureType::Normal,
                                                                     TextureType::Specular,
                                                                     TextureType::Glossiness };
    static std::array<TextureType, 4> phongTextures = { TextureType::Color, TextureType::Normal,
                                                        TextureType::Specular, TextureType::Shininess };

    switch (type)
    {
    case MaterialType::MetallicRoughness:
        return metallicRoughnessTextures;
    case MaterialType::SpecularGlossiness:
        return specularGlossinessTextures;
    case MaterialType::Phong:
        return phongTextures;
    default:
        throw error("Unsupported material type");
    }
}

TextureType &getMappedMR(MetallicRoughnessTextureMapping &mapping, TextureType type)
{
    switch (type)
    {
    case TextureType::Color:
        return mapping.ColorTexture;
    case TextureType::Normal:
        return mapping.NormalTexture;
    case TextureType::Roughness:
        return mapping.RoughnessTexture;
    case TextureType::Metallic:
        return mapping.MetallicTexture;
    default:
        throw error("Unsupported texture type");
    }
};

TextureType &getMappedSG(SpecularGlossinessTextureMapping &mapping, TextureType type)
{
    switch (type)
    {
    case TextureType::Color:
        return mapping.ColorTexture;
    case TextureType::Normal:
        return mapping.NormalTexture;
    case TextureType::Specular:
        return mapping.SpecularTexture;
    case TextureType::Glossiness:
        return mapping.GlossinessTexture;
    default:
        throw error("Unsupported texture type");
    }
};

TextureType &getMappedP(PhongTextureMapping &mapping, TextureType type)
{
    switch (type)
    {
    case TextureType::Color:
        return mapping.ColorTexture;
    case TextureType::Normal:
        return mapping.NormalTexture;
    case TextureType::Specular:
        return mapping.SpecularTexture;
    case TextureType::Shininess:
        return mapping.ShininessTexture;
    default:
        throw error("Unsupported texture type");
    }
};

const char *ToString(MaterialType type)
{
    static const char *metallicRoughnessString = "Metallic/Roughness";
    static const char *specularGlossinessString = "SpecularGlossiness";
    static const char *phongString = "Phong";

    switch (type)
    {
    case MaterialType::MetallicRoughness:
        return metallicRoughnessString;
    case MaterialType::SpecularGlossiness:
        return specularGlossinessString;
    case MaterialType::Phong:
        return phongString;
    default:
        throw error("Unsupported material type");
    }
}

void ImportSceneContent::Render()
{
    static bool showAdvanced = false;

    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 buttonSize(100, 20);

    ImGui::Dummy({ 0, 3 });
    ApplyLeftMargin();
    ImGui::Text("Skybox");
    ImGui::Dummy({ 0, 2 });

    if (m_SceneDescription.SkyboxPath.has_value())
    {
        ApplyLeftMargin();
        if (RenderPathText(m_SceneDescription.SkyboxPath.value(), size.x))
            m_SceneDescription.SkyboxPath.reset();
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
                m_SceneDescription.SkyboxPath = result.front();
            }
        }
    }
    
    ImGui::Dummy({ 0, 10 });
    ApplyLeftMargin();
    ImGui::Text("Components");
    ImGui::Dummy({ 0, 3 });

    int deleteIndex = -1;
    AlignItemRight(size.x, size.x - 20, 10);
    if (ImGui::BeginListBox("##Components", ImVec2(size.x - 20, 250)))
    {
        for (int i = 0; i < m_SceneDescription.ComponentPaths.size(); i++)
        {
            ImGui::PushID(i);
            ImGui::Dummy({ 0, 2 });
            ApplyLeftMargin();
            if (RenderPathText(m_SceneDescription.ComponentPaths[i], size.x - 10))
                deleteIndex = i;
            ImGui::PopID();
        }

        if (deleteIndex != -1)
            m_SceneDescription.ComponentPaths.erase(m_SceneDescription.ComponentPaths.begin() + deleteIndex);

        ImGui::Dummy({ 0, 10 });
        CenterItemHorizontally(size.x, buttonSize.x, -10.0f);
        if (ImGui::Button("Add Component", buttonSize))
        {
            auto result = OpenFileDialog(true);
            m_SceneDescription.ComponentPaths.insert(
                m_SceneDescription.ComponentPaths.begin(), result.begin(), result.end()
            );
        }
        ImGui::EndListBox();
    }

    auto textureTypeCombo = [](TextureType left, TextureType &current) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", ToString(left));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##textype", ToString(current)))
        {
            for (int i = 0; i < s_AllTextures.size(); i++)
            {
                ImGui::PushID(i);
                if (ImGui::Selectable(
                        ToString(s_AllTextures[i]), ToString(current) == ToString(s_AllTextures[i])
                    ))
                    current = s_AllTextures[i];
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    };

    static MetallicRoughnessTextureMapping mrMapping = {
        .ColorTexture = TextureType::Color,
        .NormalTexture = TextureType::Normal,
        .RoughnessTexture = TextureType::Roughness,
        .MetallicTexture = TextureType::Metallic,
    };

    static SpecularGlossinessTextureMapping sgMapping = {
        .ColorTexture = TextureType::Color,
        .NormalTexture = TextureType::Normal,
        .SpecularTexture = TextureType::Specular,
        .GlossinessTexture = TextureType::Glossiness,
    };

    static PhongTextureMapping pMapping = {
        .ColorTexture = TextureType::Color,
        .NormalTexture = TextureType::Normal,
        .SpecularTexture = TextureType::Specular,
        .ShininessTexture = TextureType::Shininess,
    };

    static MaterialType currentMaterial = MaterialType::MetallicRoughness;

    static std::array<MaterialType, 3> materials = { MaterialType::MetallicRoughness,
                                                     MaterialType::SpecularGlossiness, MaterialType::Phong };
    ImGui::Separator();
    if (showAdvanced)
    {
        ImGui::Dummy({ 0, 5 });
        
        ImGui::Dummy({ 50, 0 });
        ImGui::SameLine();
        ImGui::Checkbox("DX Normal Textures", &m_SceneDescription.HasDxNormalTextures);
        ImGui::SetItemTooltip("By default the normal textures are considered to be in the GL convention");
        ImGui::SameLine();
        ImGui::Dummy({ 3, 0 });
        ImGui::SameLine();
        ImGui::Checkbox("Force Full Texture Size", &m_SceneDescription.ForceFullTextureSize);
        ImGui::SetItemTooltip(
            "By default the application will limit the texture size,\n"
            "so that they fit into the texture budget set by the compilation macros,\n"
            "on some scenes the approximation might limit the texture size more than necessary"
        );

        ImGui::Dummy({ 0, 3 });
        CenterItemHorizontally(ImGui::GetWindowWidth(), 300);
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("##mapping", ToString(currentMaterial)))
        {
            for (auto material : materials)
                if (ImGui::Selectable(ToString(material), material == currentMaterial))
                    currentMaterial = material;
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "By default the application will try to infer the material format,\n"
            "you can force a given format and specify how textures should be mapped"
        );
        
        CenterItemHorizontally(ImGui::GetWindowWidth(), 300);
        if (ImGui::BeginTable("mapping", 2, ImGuiTableFlags_None, ImVec2(300, 0)))
        {
            if (currentMaterial == MaterialType::MetallicRoughness)
            {
                auto textures = GetTextures(MaterialType::MetallicRoughness);
                for (int i = 0; i < textures.size(); i++)
                {
                    ImGui::PushID(i);
                    textureTypeCombo(textures[i], getMappedMR(mrMapping, textures[i]));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            else if (currentMaterial == MaterialType::SpecularGlossiness)
            {
                auto textures = GetTextures(MaterialType::SpecularGlossiness);
                for (int i = 0; i < textures.size(); i++)
                {
                    ImGui::PushID(i);
                    textureTypeCombo(textures[i], getMappedSG(sgMapping, textures[i]));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            else
            {
                auto textures = GetTextures(MaterialType::Phong);
                for (int i = 0; i < textures.size(); i++)
                {
                    ImGui::PushID(i);
                    textureTypeCombo(textures[i], getMappedP(pMapping, textures[i]));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }

    ImGui::Dummy({ 0, 5 });
    const float margin = 20;
    ImGui::SetCursorPosX(margin);
    AlignItemBottom(size.y, buttonSize.y, margin);
    if (ImGui::Button("Cancel", buttonSize))
        s_ShowingImportScene = false;
    ImGui::SameLine();
    CenterItemHorizontally(ImGui::GetWindowWidth(), buttonSize.x);
    if (ImGui::Button(showAdvanced ? "Hide Advanced" : "Show Advanced", buttonSize))
        showAdvanced = !showAdvanced;
    ImGui::SameLine();
    AlignItemRight(size.x, buttonSize.x, margin);
    AlignItemBottom(size.y, buttonSize.y, margin);
    if (ImGui::Button("Import", buttonSize))
    {
        if (!m_SceneDescription.ComponentPaths.empty())
        {
            switch (currentMaterial)
            {
            case MaterialType::MetallicRoughness:
                m_SceneDescription.Mapping = mrMapping;
                break;
            case MaterialType::SpecularGlossiness:
                m_SceneDescription.Mapping = sgMapping;
                break;
            case MaterialType::Phong:
                m_SceneDescription.Mapping = pMapping;
                break;
            }
            if (!showAdvanced)
                m_SceneDescription.Mapping = std::monostate {};

            auto loader = m_SceneDescription.ToLoader();
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
    ImVec2 buttonSize(100, 25);

    ImGui::Separator();
    ImGui::Dummy({ 15, 0 });
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
    ImGui::Dummy({ 5, 0 });
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
    ImGui::Dummy({ 5, 0 });
    ImGui::SameLine();
    if (RenderPathButton())
        SaveFileDialog();
    ImGui::Separator();
    ImGui::Dummy({ 0, 10 });

    ImGui::Dummy({ 20, 0 });
    ImGui::SameLine();
    ImGui::SeparatorText("Output Settings");

    ImGui::Dummy({ 45, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Output Settings", 2, ImGuiTableFlags_None, { 400.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 130);
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

        ImGui::EndTable();
    }
    
    ImGui::Dummy({ 0, 10 });
    ImGui::Dummy({ 20, 0 });
    ImGui::SameLine();
    ImGui::SeparatorText("Render Settings");

    ImGui::Dummy({ 45, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Render Settings", 2, ImGuiTableFlags_None, { 400.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Max Bounce Count");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##MaxBounceCount", &m_MaxBounceCount, 1, 64);

        ImGui::EndTable();
    }

    ImGui::Dummy({ 0, 5 });
    ImGui::Dummy({ 30, 0 });
    ImGui::SameLine();
    ImGui::Checkbox("##Depth of field", &m_DepthOfField);
    ImGui::SameLine();
    ImGui::SeparatorText("Depth of field");
    if (!m_DepthOfField)
        ImGui::BeginDisabled();
    ImGui::Dummy({ 45, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Depth of field", 2, ImGuiTableFlags_None, { 400.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

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

        ImGui::EndTable();
    }

    if (!m_DepthOfField)
        ImGui::EndDisabled();

    ImGui::Dummy({ 0, 5 });
    ImGui::Dummy({ 45, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Exposure", 2, ImGuiTableFlags_None, { 400.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Exposure");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##Exposure", &m_Exposure, -10.0f, 10.0f, "%.2f");

        ImGui::EndTable();
    }

    ImGui::Dummy({ 30, 0 });
    ImGui::SameLine();
    ImGui::Checkbox("##Bloom", &m_Bloom);
    ImGui::SameLine();
    ImGui::SeparatorText("Bloom");
    if (!m_Bloom)
        ImGui::BeginDisabled();
    ImGui::Dummy({ 45, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Bloom", 2, ImGuiTableFlags_None, { 400.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

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

        ImGui::EndTable();
    }

    if (!m_Bloom)
        ImGui::EndDisabled();

    ImGui::Dummy({ 0, 5 });
    const float margin = 20;
    ImGui::SetCursorPosX(margin);
    AlignItemBottom(size.y, buttonSize.y, margin);
    if (ImGui::Button("Cancel", buttonSize))
        s_ShowingOfflineRender = false;
    ImGui::SameLine();
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
        Renderer::UpdateHdr();
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

    ImGui::Dummy({ 5, 0 });
    ImGui::SameLine();
    ImGui::SeparatorText("Path-tracing");
    ImGui::Dummy({ 25, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Path-tracing", 2, ImGuiTableFlags_None , { 480.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Bounces");
        ImGui::TableNextColumn();
        pathTracingSettingsChanged |= ImGui::SliderInt("##Bounces", &m_BounceCount, 1, 16, "%d");
        ImGui::EndTable();
    }

    ImGui::Dummy({ 0, 5 });
    ImGui::Dummy({ 25, 0 });
    ImGui::SameLine();
    pathTracingSettingsChanged |= ImGui::Checkbox("##Depth of field", &m_DepthOfField);
    ImGui::SameLine();
    ImGui::SeparatorText("Depth of field");
    ImGui::SameLine();
    ImGui::Dummy({ 30, 0 });
    if (!m_DepthOfField)
        ImGui::BeginDisabled();
    ImGui::Dummy({ 25, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Depth of field", 2, ImGuiTableFlags_None, { 480.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Lens Radius");
        ImGui::TableNextColumn();
        pathTracingSettingsChanged |= ImGui::SliderFloat(
            "##LensRadius", &m_LensRadius, 0.01f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic
        );

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Focal Distance");
        ImGui::TableNextColumn();
        pathTracingSettingsChanged |= ImGui::SliderFloat(
            "##FocalDistance", &m_FocalDistance, 1.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic
        );

        ImGui::EndTable();
    }
    if (!m_DepthOfField)
        ImGui::EndDisabled();

    if (s_DebuggingEnabled)
        ImGui::EndDisabled();

    ImGui::Dummy({ 0, 10 });
    ImGui::Dummy({ 5, 0 });
    ImGui::SameLine();
    ImGui::SeparatorText("Post-processing");
    ImGui::Dummy({ 25, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Post-processing", 2, ImGuiTableFlags_None, { 480.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::Text("Exposure");
        ImGui::TableNextColumn();
        postProcessSettingsChanged |= ImGui::SliderFloat("##Exposure", &m_Exposure, -10.0f, 10.0f, "%.2f");

        ImGui::EndTable();
    }

    ImGui::Dummy({ 0, 5 });
    ImGui::Dummy({ 25, 0 });
    ImGui::SameLine();
    postProcessSettingsChanged |= ImGui::Checkbox("##Bloom", &m_Bloom);
    ImGui::SameLine();
    ImGui::SeparatorText("Bloom");
    if (!m_Bloom)
        ImGui::BeginDisabled();
    ImGui::Dummy({ 25, 0 });
    ImGui::SameLine();
    if (ImGui::BeginTable("Bloom", 2, ImGuiTableFlags_None, { 480.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Bloom Threshold");
        ImGui::TableNextColumn();
        postProcessSettingsChanged |=
            ImGui::SliderFloat("##BloomThreshold", &m_BloomThreshold, 1.0f, 5.0f, "%.2f");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Bloom Intensity");
        ImGui::TableNextColumn();
        postProcessSettingsChanged |=
            ImGui::SliderFloat("##BloomIntensity", &m_BloomIntensity, 0.0f, 1.0f, "%.2f");

        ImGui::EndTable();
    }

    if (!m_Bloom)
        ImGui::EndDisabled();

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

    if (ImGui::CollapsingHeader("Cameras", ImGuiTreeNodeFlags_DefaultOpen))
    {
        m_CameraList.SetLeftMargin(10.0f);
        m_CameraList.Render();
    }

    ImGui::Dummy({ 0.0f, 5.0f });
    ImGui::Dummy({ 5.0f, 0.0f });
    ImGui::SameLine();

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        m_Settings.SetLeftMargin(10.0f);
        m_Settings.Render();
    }

    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 buttonSize(100, 25);

    ImGui::Dummy({ 0, 10 });

    AlignItemLeft(15);
    AlignItemBottom(size.y, buttonSize.y, 15);
    if (Application::IsRendering())
    {
        ImGui::EndDisabled();
        if (ImGui::Button("Cancel Render", buttonSize))
            Renderer::CancelRendering();
        ImGui::BeginDisabled();
        ImGui::SameLine();
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

    ImGui::Dummy({ 10.0f, 0.0f });
    ImGui::SameLine();
    if (ImGui::BeginTable("Display", 2, ImGuiTableFlags_None, { 490.0f, 0.0f }))
    {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Present Modes");
        ImGui::TableNextColumn();
        ImGui::Dummy({ 5.0f, 0.0f });
        ImGui::SameLine();
        m_PresentModeOptions.Render();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Window Modes");
        ImGui::TableNextColumn();
        ImGui::Dummy({ 5.0f, 0.0f });
        ImGui::SameLine();
        m_WindowModeOptions.Render();

        if (!s_IsHdrSupported)
        {
            ImGui::BeginDisabled();
            s_AllowHdr = false;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("HDR");
        ImGui::TableNextColumn();
        ImGui::Dummy({ 5.0f, 0.0f });
        ImGui::SameLine();
        ImGui::Checkbox("##HDR", &s_AllowHdr);

        if (!s_IsHdrSupported)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("HDR is not supported");
            ImGui::EndDisabled();
        }

        ImGui::EndTable();
    }

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

void StatisticsTabFunc()
{
    if (ImGui::BeginTabItem("Statistics"))
    {
        for (const auto &[key, value] : Stats::GetStats())
            ImGui::Text("%s", value.c_str());

        ImGui::EndTabItem();
    }
}

void AboutTabFunc()
{
    if (ImGui::BeginTabItem("About"))
    {
        ImGui::Text("Path-Tracing");
        ImGui::Text("Piotr Przybysz, Michal Popkowicz, 2026");

        ImGui::Dummy({ 0, 20 });
        ImGui::Text("Keybindings");
        ImGui::Text("[Space] - Open/Close User Interface");
        ImGui::Text("  [H]   - Hot Reload Shaders");
        ImGui::Text("  [P]   - Pause/Unpause Animations");

        ImGui::EndTabItem();
    }
}

struct UIComponents
{
    SettingsTab Settings;
    SceneTab Scene;
    DisplayTab Display;

    FixedWindow<BackgroundTaskListContent, 1> BackgroundTasksWindow = FixedWindow(
        ImVec2(300, 150), "Background tasks",
        Widget<BackgroundTaskListContent, 1>("Background Tasks", { BackgroundTaskListContent() }, 3.0f, 0.0f),
        true
    );

    FixedWindow<ImportSceneContent, 1> ImportSceneWindow = FixedWindow(
        ImVec2(500, 500), "Import Scene",
        Widget<ImportSceneContent, 1>("", { ImportSceneContent() }, 0.0f, 0.0f)
    );

    FixedWindow<OfflineRenderContent, 1> OfflineRenderWindow = FixedWindow(
        ImVec2(500, 590), "Offline Render Settings",
        Widget<OfflineRenderContent, 1>("", { OfflineRenderContent() }, 0.0f, 0.0f)
    );
};

void LeftPadding()
{
    ImGui::Dummy({ 5.0f, 0.0f });
    ImGui::SameLine();
}

bool Flags(
    Shaders::SpecializationConstant &ret, std::span<const char *> names,
    std::span<Shaders::SpecializationConstant> values
)
{
    bool hasChanged = false;
    for (int i = 0; i < names.size(); i++)
    {
        ImGui::PushID(i);
        LeftPadding();
        bool isEnabled = values[i] & ret;
        if (ImGui::Checkbox(names[i], &isEnabled))
        {
            ret ^= values[i];
            hasChanged = true;
        }
        ImGui::PopID();
    }

    return hasChanged;
}

bool Modes(
    Shaders::SpecializationConstant &ret, std::span<const char *> names, const char *&current,
    std::span<Shaders::SpecializationConstant> values
)
{
    bool hasChanged = false;
    for (int i = 0; i < names.size(); i++)
    {
        ImGui::PushID(i);
        LeftPadding();
        if (ImGui::RadioButton(names[i], current == names[i]))
        {
            current = names[i];
            s_RenderMode = values[i];
            hasChanged = true;
        }
        ImGui::PopID();
    }

    return hasChanged;
}

void DebugTabFunc()
{
    static std::array<const char *, 8> modes = { "Color", "World Position", "Normal",    "Texture Coords",
                                                 "Mips",  "Geometry",       "Primitive", "Instance" };
    static std::array<Shaders::SpecializationConstant, 8> modeValues = {
        Shaders::RenderModeColor,         Shaders::RenderModeWorldPosition, Shaders::RenderModeNormal,
        Shaders::RenderModeTextureCoords, Shaders::RenderModeMips,          Shaders::RenderModeGeometry,
        Shaders::RenderModePrimitive,     Shaders::RenderModeInstance,
    };
    static std::array<const char *, 2> raygenFlags = { "Force Opaque", "Cull Back Faces" };
    static std::array<Shaders::SpecializationConstant, 2> raygenFlagValues = {
        Shaders::RaygenFlagsForceOpaque, Shaders::RaygenFlagsCullBackFaces
    };
    static std::array<const char *, 6> hitFlags = {
        "Disable Color Texture",    "Disable Normal Texture", "Disable Roughness Texture",
        "Disable Metallic Texture", "Disable Mip Maps",       "Disable Shadows",
    };
    static std::array<Shaders::SpecializationConstant, 6> hitFlagValues = {
        Shaders::HitGroupFlagsDisableColorTexture,     Shaders::HitGroupFlagsDisableNormalTexture,
        Shaders::HitGroupFlagsDisableRoughnessTexture, Shaders::HitGroupFlagsDisableMetallicTexture,
        Shaders::HitGroupFlagsDisableMipMaps,          Shaders::HitGroupFlagsDisableShadows,
    };


    static bool debuggingEnabled = false;
    static const char *currentMode = modes[0];

    if (ImGui::BeginTabItem("Debug"))
    {
        if (Application::IsRendering())
            ImGui::BeginDisabled();

        ImGui::Dummy({ 0.0f, 5.0f });
        LeftPadding();
        bool hasChanged = ImGui::Checkbox("Enable debugging", &debuggingEnabled);

        if (!debuggingEnabled)
            ImGui::BeginDisabled();

        ImGui::Dummy({ 0.0f, 5.0f });
        ImGui::Text("%s", "Render Flags");
        hasChanged |= Flags(s_HitGroupFlags, hitFlags, hitFlagValues);
        hasChanged |= Flags(s_RaygenFlags, raygenFlags, raygenFlagValues);

        ImGui::Dummy({ 0.0f, 5.0f });
        ImGui::Text("%s", "Render Modes");
        hasChanged |= Modes(s_RenderMode, modes, currentMode, modeValues);

        if (!debuggingEnabled)
            ImGui::EndDisabled();

        if (hasChanged)
        {
            if (debuggingEnabled)
                Renderer::SetDebugRaytracingPipeline(
                    DebugRaytracingPipelineConfig { s_RenderMode, s_RaygenFlags, 0, s_HitGroupFlags }
                );
            else
                Renderer::SetPathTracingPipeline({});
            s_DebuggingEnabled = debuggingEnabled;
        }

        if (Application::IsRendering())
            ImGui::EndDisabled();

        ImGui::EndTabItem();
    }
}

void UserInterface::DefineUI()
{
    s_IsFocused = false;

    Stats::AddStat(
        "Framerate", "Framerate: {:.3f} ms/frame ({:.1f} FPS)", 1000.0f / s_Io->Framerate, s_Io->Framerate
    );

    ImGui::SetNextWindowPos(ImVec2(15, 15), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

    ImGui::Begin("Options", nullptr, ImGuiWindowFlags_NoResize);

    s_IsFocused |= ImGui::IsWindowFocused();

    if (ImGui::BeginTabBar("Options"))
    {
        s_Components->Settings.Render(Application::IsRendering());
        s_Components->Scene.Render(Application::IsRendering());
        s_Components->Display.Render();
        DebugTabFunc();
        StatisticsTabFunc();
        AboutTabFunc();
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
