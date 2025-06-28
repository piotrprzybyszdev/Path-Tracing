#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.hpp>

#include "Core/Core.h"

#include "UserInterface.h"
#include "Window.h"

namespace PathTracing
{

vk::PresentModeKHR s_PresentMode = vk::PresentModeKHR::eMailbox;
bool UserInterface::s_IsVisible = false;
bool UserInterface::s_IsFocused = false;
ImGuiIO *UserInterface::s_Io = nullptr;

static void CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return;

    logger::error("ImGui Vulkan Error: {}", vk::to_string(static_cast<vk::Result>(err)));
}

void UserInterface::Init(
    vk::Instance instance, vk::Format format, vk::PhysicalDevice physicalDevice, vk::Device device,
    uint32_t queueFamily, vk::Queue queue, uint32_t swapchainImageCount
)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    s_Io = &ImGui::GetIO();

    ImGui::StyleColorsDark();

    // TODO: Add allocation Callback for when VMA is integrated
    ImGui_ImplGlfw_InitForVulkan(Window::GetHandle(), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = queue;
    init_info.DescriptorPoolSize = swapchainImageCount;
    init_info.Allocator = nullptr;
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

void UserInterface::ToggleVisible()
{
    s_IsVisible = !s_IsVisible;
}

bool UserInterface::GetIsFocused()
{
    return s_IsVisible && s_IsFocused;
}

vk::PresentModeKHR UserInterface::GetPresentMode()
{
    return s_PresentMode;
}

void UserInterface::DefineUI()
{
    ImGui::ShowDemoWindow();  // TODO: Remove

    ImGui::Begin("Settings");
    s_IsFocused |= ImGui::IsWindowFocused();

    static constexpr vk::PresentModeKHR modes[] = {
        vk::PresentModeKHR::eMailbox,
        vk::PresentModeKHR::eFifo,
        vk::PresentModeKHR::eImmediate,
    };
    static constexpr const char *modeNames[] = {
        "Mailbox",
        "Fifo",
        "Immediate",
    };

    static int selected_idx = 0;
    const char *combo_preview_value = modeNames[selected_idx];

    if (ImGui::BeginCombo("Present Mode", combo_preview_value))
    {
        for (int i = 0; i < 3; i++)
        {
            const bool is_selected = (selected_idx == i);
            if (ImGui::Selectable(modeNames[i], is_selected))
            {
                selected_idx = i;
                s_PresentMode = modes[i];
            }
        }
        ImGui::EndCombo();
    }

    s_IsFocused = false;

    char textBuf[256] = {};
    ImGui::InputText("Text", textBuf, 256);
    ImGui::End();

    ImGui::Begin("Statistics");
    s_IsFocused |= ImGui::IsWindowFocused();
    Stats::AddStat(
        "Framerate", "Framerate: {:.3f} ms/frame ({:.1f} FPS)", 1000.0f / s_Io->Framerate, s_Io->Framerate
    );

    for (auto &[key, value] : Stats::GetStats())
        ImGui::Text(value.c_str());
    ImGui::End();
}

}