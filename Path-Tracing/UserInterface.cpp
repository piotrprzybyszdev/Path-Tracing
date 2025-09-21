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
Shaders::EnabledTextureFlags s_EnabledTextures = Shaders::TexturesEnableAll;
Shaders::RenderModeFlags s_RenderMode = Shaders::RenderModeColor;
Shaders::RaygenFlags s_RaygenFlags = Shaders::RaygenFlagsNone;
Shaders::ClosestHitFlags s_ClosestHitFlags = Shaders::ClosestHitFlagsNone;
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

void UserInterface::Render(vk::CommandBuffer commandBuffer)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (s_IsVisible)
        DefineUI();

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

Shaders::EnabledTextureFlags UserInterface::GetEnabledTextures()
{
    return s_EnabledTextures;
}

Shaders::RenderModeFlags UserInterface::GetRenderMode()
{
    return s_RenderMode;
}

Shaders::RaygenFlags UserInterface::GetRaygenFlags()
{
    return s_RaygenFlags;
}

Shaders::ClosestHitFlags UserInterface::GetClosestHitFlags()
{
    return s_ClosestHitFlags;
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

    static constexpr Shaders::EnabledTextureFlags textureFlags[] = {
        Shaders::TexturesEnableColor,
        Shaders::TexturesEnableNormal,
        Shaders::TexturesEnableMetalic,
        Shaders::TexturesEnableRoughness,
    };
    static constexpr const char *textureNames[] = {
        "Color Texture",
        "Normal Texture",
        "Metalic Texture",
        "Roughness Texture",
    };

    // FIX: If you change the starting flags this will be out of sync
    static bool isTextureEnabled[] = { true, true, true, true };

    for (int i = 0; i < 4; i++)
    {
        ImGui::PushID(i);
        if (ImGui::Checkbox(textureNames[i], &isTextureEnabled[i]))
            s_EnabledTextures ^= textureFlags[i];
        ImGui::PopID();
    }

    static constexpr Shaders::RenderModeFlags renderModes[] = {
        Shaders::RenderModeColor,         Shaders::RenderModeWorldPosition, Shaders::RenderModeNormal,
        Shaders::RenderModeTextureCoords, Shaders::RenderModeMips,          Shaders::RenderModeGeometry,
        Shaders::RenderModePrimitive,     Shaders::RenderModeInstance,
    };

    static constexpr const char *renderModeNames[] = {
        "Color", "World Position", "Normal", "TextureCoords", "Mips", "Geometry", "Primitive", "Instance",
    };

    for (int i = 0; i < 8; i++)
    {
        ImGui::PushID(i);
        if (ImGui::RadioButton(renderModeNames[i], s_RenderMode == renderModes[i]))
            s_RenderMode = renderModes[i];
        ImGui::PopID();
    }

    static bool forceOpaque;
    if (ImGui::Checkbox("Force Opaque", &forceOpaque))
        s_RaygenFlags ^= Shaders::RaygenFlagsForceOpaque;

    static bool cullBackFaces;
    if (ImGui::Checkbox("Cull Back Faces", &cullBackFaces))
        s_RaygenFlags ^= Shaders::RaygenFlagsCullBackFaces;

    static bool disableMipMaps;
    if (ImGui::Checkbox("Disable Mip Maps", &disableMipMaps))
        s_ClosestHitFlags ^= Shaders::ClosestHitFlagsDisableMipMaps;

    static bool disableShadows;
    if (ImGui::Checkbox("Disable Shadows", &disableShadows))
        s_ClosestHitFlags ^= Shaders::ClosestHitFlagsDisableShadows;

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
