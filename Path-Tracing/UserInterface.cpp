#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

#include "UserInterface.h"

namespace PathTracing
{

bool UserInterface::s_IsFocused = false;

void UserInterface::Init(
    GLFWwindow *window, vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device,
    uint32_t queueFamily, vk::Queue queue, uint32_t minImageCount,
    uint32_t imageCount, vk::RenderPass renderPass
)
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    // TODO: Add allocation Callback for when VMA is integrated
    // TODO: Add debug vkcheck callback
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = queue;
    init_info.DescriptorPoolSize = 2;
    init_info.Allocator = nullptr;
    init_info.RenderPass = renderPass;
    init_info.MinImageCount = minImageCount;
    init_info.ImageCount = imageCount;
    init_info.CheckVkResultFn = nullptr;
    init_info.UseDynamicRendering = false;
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