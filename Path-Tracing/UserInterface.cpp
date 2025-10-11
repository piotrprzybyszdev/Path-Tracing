#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.hpp>

#include "Core/Core.h"

#include "Renderer/DeviceContext.h"
#include "Renderer/Renderer.h"

#include "SceneManager.h"
#include "UserInterface.h"
#include "Window.h"

namespace PathTracing
{

bool UserInterface::s_IsVisible = false;
bool UserInterface::s_IsFocused = false;
ImGuiIO *UserInterface::s_Io = nullptr;
float UserInterface::s_Exposure = 0.0f;

namespace
{

vk::PresentModeKHR s_PresentMode = vk::PresentModeKHR::eFifo;
Shaders::SpecializationConstant s_RenderMode = Shaders::RenderModeColor;
Shaders::SpecializationConstant s_RaygenFlags = Shaders::RaygenFlagsNone;
Shaders::SpecializationConstant s_HitGroupFlags = Shaders::HitGroupFlagsNone;
const char *s_SceneChange = nullptr;

void CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return;

    logger::error("ImGui Vulkan Error: {}", vk::to_string(static_cast<vk::Result>(err)));
}

}

void UserInterface::Init(vk::Instance instance, vk::Format format, uint32_t swapchainImageCount)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    s_Io = &ImGui::GetIO();

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
    ImGui_ImplVulkan_Init(&initInfo);
}

void UserInterface::Shutdown()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
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
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void UserInterface::OnKeyRelease(Key key)
{
    switch (key)
    {
    case Key::Space:
        if (!s_IsFocused)
            s_IsVisible = !s_IsVisible;
        break;
    case Key::H:
        if (!s_IsFocused)
            Renderer::ReloadShaders();
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

const char *UserInterface::SceneChange()
{
    const char *ret = s_SceneChange;
    s_SceneChange = nullptr;
    return ret;
}

float UserInterface::GetExposure()
{
    return std::pow(2.0f, s_Exposure);
}

namespace
{

struct Flag
{
    const Shaders::SpecializationConstant Value;
    const char *Name;
    bool IsEnabled = false;
};

bool displayFlags(Shaders::SpecializationConstant &bitmask, std::span<const Flag> flags)
{
    bool changed = false;
    for (int i = 0; i < flags.size(); i++)
    {
        const Flag &flag = flags[i];

        ImGui::PushID(i);
        bool isEnabled = bitmask & flag.Value;
        if (ImGui::Checkbox(flag.Name, &isEnabled))
        {
            bitmask ^= flag.Value;
            changed = true;
        }
        ImGui::PopID();
    }

    return changed;
}

struct Mode
{
    const Shaders::SpecializationConstant Value;
    const char *Name;
};

bool displayModes(Shaders::SpecializationConstant &value, std::span<const Mode> modes)
{
    bool changed = false;
    for (int i = 0; i < modes.size(); i++)
    {
        const Mode &mode = modes[i];

        ImGui::PushID(i);
        if (ImGui::RadioButton(mode.Name, s_RenderMode == mode.Value))
        {
            s_RenderMode = mode.Value;
            changed = true;
        }
        ImGui::PopID();
    }

    return changed;
}

}

void UserInterface::DefineUI()
{
    s_IsFocused = false;

    ImGui::ShowDemoWindow();  // TODO: Remove

    ImGui::Begin("Settings");
    s_IsFocused |= ImGui::IsWindowFocused();

    static constexpr vk::PresentModeKHR modes[] = {
        vk::PresentModeKHR::eFifo,
        vk::PresentModeKHR::eMailbox,
        vk::PresentModeKHR::eImmediate,
    };
    static constexpr const char *modeNames[] = {
        "Fifo",
        "Mailbox",
        "Immediate",
    };

    static int selectedIdx = 0;
    const char *comboPreviewValue = modeNames[selectedIdx];

    if (ImGui::BeginCombo("Present Mode", comboPreviewValue))
    {
        for (int i = 0; i < 3; i++)
        {
            ImGui::PushID(i);
            const bool isSelected = (selectedIdx == i);
            if (ImGui::Selectable(modeNames[i], isSelected))
            {
                selectedIdx = i;
                s_PresentMode = modes[i];
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    static constexpr std::array<Flag, 2> raygenFlags = { {
        { Shaders::RaygenFlagsForceOpaque, "Force Opaque" },
        { Shaders::RaygenFlagsCullBackFaces, "Cull Back Faces" },
    } };

    static constexpr std::array<Flag, 6> HitGroupFlags = { {
        { Shaders::HitGroupFlagsDisableColorTexture, "Disable Color Texture" },
        { Shaders::HitGroupFlagsDisableNormalTexture, "Disable Normal Texture" },
        { Shaders::HitGroupFlagsDisableRoughnessTexture, "Disable Roughness Texture" },
        { Shaders::HitGroupFlagsDisableMetalicTexture, "Disable Metalic Texture" },
        { Shaders::HitGroupFlagsDisableMipMaps, "Disable Mip Maps" },
        { Shaders::HitGroupFlagsDisableShadows, "Disable Shadows" },
    } };

    static constexpr std::array<Mode, 8> renderModes = { {
        { Shaders::RenderModeColor, "Color" },
        { Shaders::RenderModeWorldPosition, "World Position" },
        { Shaders::RenderModeNormal, "Normal" },
        { Shaders::RenderModeTextureCoords, "Texture Coords" },
        { Shaders::RenderModeMips, "Mips" },
        { Shaders::RenderModeGeometry, "Geometry" },
        { Shaders::RenderModePrimitive, "Primitive" },
        { Shaders::RenderModeInstance, "Instance" },
    } };

    bool changed = false;
    changed |= displayFlags(s_HitGroupFlags, HitGroupFlags);
    changed |= displayFlags(s_RaygenFlags, raygenFlags);

    changed |= displayModes(s_RenderMode, renderModes);

    if (changed)
        Renderer::UpdateSpecializations(Shaders::SpecializationData {
            .RenderMode = s_RenderMode,
            .RaygenFlags = s_RaygenFlags,
            .HitGroupFlags = s_HitGroupFlags,
        });

    if (ImGui::BeginListBox("Scene"))
    {
        for (auto &scene : SceneManager::GetSceneNames())
            if (ImGui::Selectable(scene.c_str()))
                s_SceneChange = scene.c_str();
        ImGui::EndListBox();
    }
    
    auto scene = SceneManager::GetActiveScene();

    ImGui::Text("Cameras");
    if (ImGui::RadioButton("Input Camera", scene->GetActiveCameraId() == Scene::g_InputCameraId))
        scene->SetActiveCamera(Scene::g_InputCameraId);

    for (int i = 0; i < scene->GetSceneCamerasCount(); i++)
    {
        ImGui::PushID(i);
        if (ImGui::RadioButton(std::format("Scene Camera {}", i).c_str(), scene->GetActiveCameraId() == i))
            scene->SetActiveCamera(i);
        ImGui::PopID();
    }

    ImGui::SliderFloat("Exposure:", &s_Exposure, -10.0f, 10.0f, "%.2f");
    
    ImGui::End();

    ImGui::Begin("Statistics");
    s_IsFocused |= ImGui::IsWindowFocused();
    Stats::AddStat(
        "Framerate", "Framerate: {:.3f} ms/frame ({:.1f} FPS)", 1000.0f / s_Io->Framerate, s_Io->Framerate
    );

    for (const auto &[key, value] : Stats::GetStats())
        ImGui::Text("%s", value.c_str());
    ImGui::End();
}

}
