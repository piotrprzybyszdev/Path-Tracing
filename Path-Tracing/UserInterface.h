#pragma once

#include <imgui.h>
#include <vulkan/vulkan.hpp>

#include <string>
#include <span>

#include "Core/Input.h"

namespace PathTracing
{

class UserInterface
{
public:
    static void Init(
        vk::Instance instance, uint32_t swapchainImageCount, std::span<const vk::PresentModeKHR> presentModes
    );

    static void Shutdown();

    static void Reinitialize(vk::Instance instance, uint32_t swapchainImageCount);

    static void OnUpdate(float timeStep);
    static void OnRender(vk::CommandBuffer commandBuffer);

    static void OnKeyRelease(Key key);

    static bool GetIsFocused();

    static vk::PresentModeKHR GetPresentMode();
    static bool IsHdrAllowed();

    static void SetHdrSupported(bool isSupported);

private:
    static bool s_IsVisible;
    static bool s_IsFocused;
    static std::string s_IniFilePath;
    static ImGuiIO *s_Io;

private:
    static void DefineUI();
};

}
