#pragma once

#include <vulkan/vulkan.hpp>

namespace PathTracing
{

class Frame
{
public:
    Frame(
        vk::Device device, vk::CommandPool commandPool, vk::Image image,
        vk::Format format, uint32_t width, uint32_t height
    );
    ~Frame();

    Frame(Frame &frame) = delete;
    Frame &operator=(Frame frame) = delete;

    Frame(Frame &&frame) noexcept;

    vk::Image GetImage() const;
    vk::ImageView GetImageView() const;
    vk::Framebuffer GetFrameBuffer() const;
    vk::CommandBuffer GetCommandBuffer() const;

private:
    vk::Device m_Device;
    vk::CommandPool m_CommandPool;

    vk::Image m_Image;
    vk::ImageView m_ImageView;
    vk::Framebuffer m_FrameBuffer;

    vk::CommandBuffer m_CommandBuffer;

    bool m_IsMoved = false;
};

}
