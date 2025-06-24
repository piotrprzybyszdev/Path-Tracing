#pragma once

#include <vulkan/vulkan.hpp>

#include "PhysicalDevice.h"

namespace PathTracing
{

class LogicalDevice
{
public:
    LogicalDevice(
        vk::Instance instance = nullptr, vk::SurfaceKHR surface = nullptr,
        std::vector<const char *> layers = {}, std::vector<const char *> extensions = {},
        vk::PhysicalDeviceFeatures2 *features = nullptr
    );
    ~LogicalDevice();

    vk::Device GetHandle() const;
    vk::PhysicalDevice GetPhysicalDeviceHandle() const;

    uint32_t GetGraphicsQueueFamilyIndex() const;
    vk::Queue GetGraphicsQueue() const;
    vk::CommandPool GetGraphicsCommandPool() const;
    vk::SwapchainKHR CreateSwapchain(
        uint32_t width, uint32_t height, vk::SwapchainKHR oldSwapchain, vk::SurfaceKHR surface
    );
    vk::SurfaceFormatKHR GetSurfaceFormat() const;
    vk::PresentModeKHR GetPresentMode() const;

    uint32_t GetSwapchainImageCount() const;
    uint32_t GetFrameInFlightCount() const;

    BufferBuilder CreateBufferBuilder() const;
    std::unique_ptr<BufferBuilder> CreateBufferBuilderUnique() const;
    std::unique_ptr<ImageBuilder> CreateImageBuilderUnique() const;
    std::unique_ptr<ShaderLibrary> CreateShaderLibrary() const;

private:
    PhysicalDevice Physical;

    vk::Device m_Handle;

    uint32_t m_GraphicsQueueFamilyIndex = 0;
    vk::Queue m_GraphicsQueue;
    vk::CommandPool m_GraphicsCommandPool;

    vk::SurfaceFormatKHR m_SurfaceFormat;
    vk::PresentModeKHR m_PresentMode;
    uint32_t m_SwapchainImageCount = 0;

    static inline constexpr vk::Format PreferredFormat = vk::Format::eR8G8B8A8Unorm;
    static inline constexpr vk::ColorSpaceKHR PreferredColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    static inline constexpr vk::PresentModeKHR PreferredPresentMode = vk::PresentModeKHR::eMailbox;
    static inline constexpr std::array<vk::QueueFlags, 1> QueueFlags = { vk::QueueFlagBits::eGraphics };

    uint32_t GetPreferredSwapchainImageCount(vk::PresentModeKHR presentMode) const;
};

}
