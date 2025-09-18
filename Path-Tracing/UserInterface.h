#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <vulkan/vulkan.hpp>

#include "Core/Input.h"

#include "Shaders/ShaderTypes.incl"

namespace PathTracing
{

class UserInterface
{
public:
    static void Init(vk::Instance instance, vk::Format format, uint32_t swapchainImageCount);

    static void Shutdown();

    static void Render(vk::CommandBuffer commandBuffer);

    static void OnKeyRelease(Key key);

    static bool GetIsFocused();

    static vk::PresentModeKHR GetPresentMode();
    static Shaders::EnabledTextureFlags GetEnabledTextures();
    static Shaders::RenderModeFlags GetRenderMode();
    static Shaders::RaygenFlags GetRaygenFlags();
    static Shaders::ClosestHitFlags GetClosestHitFlags();

    static const char *SceneChange();

private:
    static bool s_IsVisible;
    static bool s_IsFocused;
    static ImGuiIO *s_Io;

private:
    static void DefineUI();
};

}
