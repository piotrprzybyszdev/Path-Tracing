#include <vulkan/vulkan_format_traits.hpp>
#include <stb_image_write.h>

#include <utility>
#include <vector>

#include "DeviceContext.h"
#include "OutputSaver.h"

namespace PathTracing
{

OutputSaver::OutputSaver()
{
    m_Semaphore = DeviceContext::GetLogical().createSemaphore(vk::SemaphoreCreateInfo());
    m_Fence = DeviceContext::GetLogical().createFence(vk::FenceCreateInfo());

    vk::CommandPoolCreateInfo createInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, DeviceContext::GetGraphicsQueue().FamilyIndex
    );

    m_CommandPool = DeviceContext::GetLogical().createCommandPool(createInfo);
    vk::CommandBufferAllocateInfo allocateInfo(m_CommandPool, vk::CommandBufferLevel::ePrimary, 1);
    m_CommandBuffer = DeviceContext::GetLogical().allocateCommandBuffers(allocateInfo)[0];
}

OutputSaver::~OutputSaver()
{
    if (m_Thread.joinable())
        m_Thread.join();

    DeviceContext::GetLogical().destroyCommandPool(m_CommandPool);
    DeviceContext::GetLogical().destroyFence(m_Fence);
    DeviceContext::GetLogical().destroySemaphore(m_Semaphore);
}

vk::Semaphore OutputSaver::GetSignalSemaphore() const
{
    return m_Semaphore;
}

vk::Image OutputSaver::RegisterOutput(const OutputInfo &info)
{
    if (m_Thread.joinable())
        m_Thread.join();

    m_Image = ImageBuilder()
                  .SetUsageFlags(
                      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eTransferSrc
                  )
                  .SetFormat(SelectImageFormat(info.Format))
                  .CreateImage(info.Extent, "Output Image");

    m_Buffer = BufferBuilder()
                   .SetUsageFlags(vk::BufferUsageFlagBits::eTransferDst)
                   .CreateHostBuffer(m_Image.GetMipSize(0), "Output Read Buffer");

    m_Info = info;

    return m_Image.GetHandle();
}

void OutputSaver::StartOutputWait()
{
    m_CommandBuffer.reset();
    m_CommandBuffer.begin(vk::CommandBufferBeginInfo());

    vk::BufferImageCopy imageCopy(
        0, 0, 0, { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, vk::Offset3D(0, 0, 0),
        vk::Extent3D(m_Image.GetExtent(), 1)
    );

    m_Image.Transition(
        m_CommandBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal
    );

    m_CommandBuffer.copyImageToBuffer(
        m_Image.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, m_Buffer.GetHandle(), imageCopy
    );

    m_CommandBuffer.end();

    vk::CommandBufferSubmitInfo cmdInfo(m_CommandBuffer);
    vk::SemaphoreSubmitInfo waitInfo(
        m_Semaphore, 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );
    vk::SubmitInfo2 submitInfo(vk::SubmitFlags(), waitInfo, cmdInfo, {});

    {
        auto lock = DeviceContext::GetGraphicsQueue().GetLock();
        DeviceContext::GetGraphicsQueue().Handle.submit2({ submitInfo }, m_Fence);
    }

    m_Thread = std::jthread([this](std::stop_token stopToken) {
        vk::Result result = DeviceContext::GetLogical().waitForFences(
            m_Fence, vk::True, std::numeric_limits<uint64_t>::max()
        );
        assert(result == vk::Result::eSuccess);
        DeviceContext::GetLogical().resetFences(m_Fence);

        std::vector<std::byte> data(m_Buffer.GetSize());
        m_Buffer.Readback(data);

        bool success = WriteImage(m_Info, data);

        if (success)
            logger::info(std::format("Successfully saved file {}", m_Info.Path.string()));
        else
            logger::error(std::format("Could not save output file {}", m_Info.Path.string()));
    });
}

bool OutputSaver::WriteImage(const OutputInfo &info, std::span<const std::byte> data)
{
    const std::string path = info.Path.string();
    int ret;
    
    switch (info.Format)
    {
    case OutputFormat::Png:
        ret = stbi_write_png(path.c_str(), info.Extent.width, info.Extent.height, 4, data.data(), 0);
        break;
    case OutputFormat::Jpg:
        ret = stbi_write_jpg(path.c_str(), info.Extent.width, info.Extent.height, 4, data.data(), 0);
        break;
    case OutputFormat::Tga:
        ret = stbi_write_tga(path.c_str(), info.Extent.width, info.Extent.height, 4, data.data());
        break;
    case OutputFormat::Hdr:
        ret = stbi_write_hdr(
            path.c_str(), info.Extent.width, info.Extent.height, 4,
            reinterpret_cast<const float *>(data.data())
        );
        break;
    default:
        throw error("Unsupported output format");
    }

    return ret == 1;
}

vk::Format OutputSaver::SelectImageFormat(OutputFormat format)
{
    switch (format)
    {
    case OutputFormat::Png:
    case OutputFormat::Jpg:
    case OutputFormat::Tga:
        return vk::Format::eR8G8B8A8Srgb;
    case OutputFormat::Hdr:
        return vk::Format::eR32G32B32A32Sfloat;
    default:
        throw error("Unsupported output format");
    }
}

}