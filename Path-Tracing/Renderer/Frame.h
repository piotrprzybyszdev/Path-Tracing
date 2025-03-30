#pragma once

#include <vulkan/vulkan.hpp>

namespace PathTracing
{

struct SynchronizationObjects
{
    vk::Semaphore ImageAcquiredSemaphore;
    vk::Semaphore RenderCompleteSemaphore;
    vk::Fence InFlightFence;
};

class Frame
{
public:
    Frame(vk::Device device, vk::RenderPass renderPass, vk::CommandPool commandPool, vk::Image image, vk::Format format, uint32_t width, uint32_t height);
    ~Frame();

    Frame(Frame &frame) = delete;
    Frame &operator=(Frame frame) = delete;

    Frame(Frame &&frame) noexcept;

    vk::Image GetImage() const;
    vk::CommandBuffer GetCommandBuffer() const;
    SynchronizationObjects GetSynchronizationObjects() const;

private:
    vk::Device m_Device;
    vk::CommandPool m_CommandPool;

    vk::Image m_Image;
    vk::ImageView m_ImageView;
    vk::Framebuffer m_FrameBuffer;

    vk::CommandBuffer m_CommandBuffer;
    SynchronizationObjects m_SynchronizationObjects;

    bool m_IsMoved = false;
};

}
