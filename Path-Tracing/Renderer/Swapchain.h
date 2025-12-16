#pragma once

#include <vulkan/vulkan.hpp>

#include <span>
#include <vector>

namespace PathTracing
{

class Swapchain
{
public:
    Swapchain(
        vk::SurfaceKHR surface, vk::SurfaceFormatKHR surfaceFormat, vk::Format linearFormat,
        vk::PresentModeKHR presentMode, vk::Extent2D extent, uint32_t imageCount
    );
    ~Swapchain();

    void Recreate();
    void Recreate(vk::PresentModeKHR presentMode);
    void Recreate(vk::Extent2D extent);
    void Recreate(uint32_t imageCount);

    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;

    [[nodiscard]] uint32_t GetImageCount() const;
    [[nodiscard]] uint32_t GetInFlightCount() const;

    struct Frame
    {
        vk::Image Image;
        vk::ImageView LinearImageView;
        vk::ImageView NonLinearImageView;
    };

    struct SynchronizationObjects
    {
        vk::Semaphore ImageAcquiredSemaphore;
        vk::Semaphore RenderCompleteSemaphore;
        vk::Fence InFlightFence;
    };

    [[nodiscard]] const Frame &GetCurrentFrame() const;
    [[nodiscard]] const SynchronizationObjects &GetCurrentSyncObjects() const;
    [[nodiscard]] uint32_t GetCurrentFrameInFlightIndex() const;

    bool AcquireImage();
    bool Present();

    [[nodiscard]] vk::Extent2D GetExtent() const;
    [[nodiscard]] vk::SurfaceFormatKHR GetSurfaceFormat() const;
    [[nodiscard]] std::span<const vk::PresentModeKHR> GetPresentModes() const;
    [[nodiscard]] vk::PresentModeKHR GetPresentMode() const;

private:
    vk::SwapchainKHR m_Handle { nullptr };

    const vk::SurfaceKHR m_Surface;
    const vk::SurfaceFormatKHR m_SurfaceFormat;
    const vk::Format m_LinearFormat;
    std::vector<vk::PresentModeKHR> m_PresentModes;

    vk::PresentModeKHR m_PresentMode = vk::PresentModeKHR::eFifo;

    uint32_t m_ImageCount;
    uint32_t m_InFlightCount;
    vk::Extent2D m_Extent;

    std::vector<Frame> m_Frames;
    std::vector<SynchronizationObjects> m_SynchronizationObjects;

    uint32_t m_CurrentFrameInFlightIndex = 0;
    uint32_t m_CurrentFrameIndex = 0;
};

}
