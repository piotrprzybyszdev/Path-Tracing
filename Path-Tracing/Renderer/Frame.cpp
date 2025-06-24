#include "Core/Core.h"

#include "Frame.h"

namespace PathTracing
{

Frame::Frame(
    vk::Device device, vk::CommandPool commandPool, vk::Image image,
    vk::Format format, uint32_t width, uint32_t height
)
    : m_Device(device), m_CommandPool(commandPool), m_Image(image)
{
    vk::ImageViewCreateInfo createInfo(
        vk::ImageViewCreateFlags(), image, vk::ImageViewType::e2D, format, vk::ComponentMapping(),
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    );
    m_ImageView = m_Device.createImageView(createInfo);

    vk::CommandBufferAllocateInfo allocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
    m_CommandBuffer = m_Device.allocateCommandBuffers(allocateInfo)[0];
}

Frame::~Frame()
{
    if (m_IsMoved)
        return;

    m_Device.freeCommandBuffers(m_CommandPool, { m_CommandBuffer });
    m_Device.destroyFramebuffer(m_FrameBuffer);
    m_Device.destroyImageView(m_ImageView);
}

Frame::Frame(Frame &&frame) noexcept
{
    if (m_Device)
        this->~Frame();

    m_Device = frame.m_Device;
    m_CommandPool = frame.m_CommandPool;
    m_Image = frame.m_Image;
    m_ImageView = frame.m_ImageView;
    m_FrameBuffer = frame.m_FrameBuffer;
    m_CommandBuffer = frame.m_CommandBuffer;

    frame.m_IsMoved = true;
}

vk::Image Frame::GetImage() const
{
    return m_Image;
}

vk::ImageView Frame::GetImageView() const
{
    return m_ImageView;
}

vk::Framebuffer Frame::GetFrameBuffer() const
{
    return m_FrameBuffer;
}

vk::CommandBuffer Frame::GetCommandBuffer() const
{
    return m_CommandBuffer;
}

}
