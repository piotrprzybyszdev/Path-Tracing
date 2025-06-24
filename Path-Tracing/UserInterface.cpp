#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include "Core/Core.h"

#include "UserInterface.h"

namespace PathTracing
{

bool UserInterface::s_IsFocused = false;

static void CheckVkResult(VkResult err)
{
    if (err == VkResult::VK_SUCCESS)
        return;

    logger::error("ImGui Vulkan Error: {}", vk::to_string(static_cast<vk::Result>(err)));
}

void UserInterface::Init(
    GLFWwindow *window, vk::Instance instance, vk::Format format, vk::PhysicalDevice physicalDevice,
    vk::Device device, uint32_t queueFamily, vk::Queue queue, uint32_t swapchainImageCount
)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    // TODO: Add allocation Callback for when VMA is integrated
    ImGui_ImplGlfw_InitForVulkan(window, true);
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
    ImGui::ShowDemoWindow();

    ImGui::Begin("Test Window");
    char textBuf[256] = {};
    ImGui::InputText("test", textBuf, 256);
    s_IsFocused = ImGui::IsWindowFocused();
    ImGui::End();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

bool UserInterface::GetIsFocused()
{
    return s_IsFocused;
}

}