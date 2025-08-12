#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.hpp>

#include "Core/Core.h"

#include "Renderer/Renderer.h"
#include "Renderer/DeviceContext.h"
#include "UserInterface.h"
#include "Window.h"

namespace PathTracing
{

static vk::PresentModeKHR s_PresentMode = vk::PresentModeKHR::eFifo;
static Shaders::EnabledTextureFlags s_EnabledTextures = Shaders::TexturesEnableAll;
static Shaders::RenderModeFlags s_RenderMode = Shaders::RenderModeColor;
static Shaders::RaygenFlags s_RaygenFlags = Shaders::RaygenFlagsNone;
static Shaders::ClosestHitFlags s_ClosestHitFlags = Shaders::ClosestHitFlagsNone;
bool UserInterface::s_IsVisible = false;
bool UserInterface::s_IsFocused = false;
ImGuiIO *UserInterface::s_Io = nullptr;

static void CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return;

    logger::error("ImGui Vulkan Error: {}", vk::to_string(static_cast<vk::Result>(err)));
}

void UserInterface::Init(vk::Instance instance, vk::Format format, uint32_t swapchainImageCount)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    s_Io = &ImGui::GetIO();

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(Window::GetHandle(), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = DeviceContext::GetPhysical();
    init_info.Device = DeviceContext::GetLogical();
    init_info.QueueFamily = DeviceContext::GetGraphicsQueueFamilyIndex();
    init_info.Queue = DeviceContext::GetGraphicsQueue();
    init_info.DescriptorPoolSize = swapchainImageCount;
    init_info.MinImageCount = swapchainImageCount;
    init_info.ImageCount = swapchainImageCount;
    init_info.CheckVkResultFn = CheckVkResult;
    init_info.UseDynamicRendering = true;
    std::vector<vk::Format> formats = { format };
    init_info.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfoKHR(0, formats);
    ImGui_ImplVulkan_Init(&init_info);
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
        s_IsVisible = !s_IsVisible;
        break;
    case Key::H:
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

    static int selected_idx = 0;
    const char *combo_preview_value = modeNames[selected_idx];

    if (ImGui::BeginCombo("Present Mode", combo_preview_value))
    {
        for (int i = 0; i < 3; i++)
        {
            ImGui::PushID(i);
            const bool is_selected = (selected_idx == i);
            if (ImGui::Selectable(modeNames[i], is_selected))
            {
                selected_idx = i;
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
        Shaders::RenderModeColor,
        Shaders::RenderModeWorldPosition,
        Shaders::RenderModeNormal,
        Shaders::RenderModeTextureCoords,
        Shaders::RenderModeMips,
    };

    static constexpr const char *renderModeNames[] = {
        "Color", "World Position", "Normal", "TextureCoords", "Mips",
    };

    for (int i = 0; i < 5; i++)
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

    // TODO: Remove
    char textBuf[256] = {};
    ImGui::InputText("Text", textBuf, 256);
    ImGui::End();

    ImGui::Begin("Statistics");
    s_IsFocused |= ImGui::IsWindowFocused();
    Stats::AddStat(
        "Framerate", "Framerate: {:.3f} ms/frame ({:.1f} FPS)", 1000.0f / s_Io->Framerate, s_Io->Framerate
    );

    for (auto &[key, value] : Stats::GetStats())
        ImGui::Text("%s", value.c_str());
    ImGui::End();
}

}
