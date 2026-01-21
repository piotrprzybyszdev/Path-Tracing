#include <vulkan/vulkan_format_traits.hpp>
#include <stb_image_write.h>
#include <subprocess.h>

#include <limits>
#include <utility>
#include <vector>

#include "Core/Core.h"

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

    const char *cmd[] = { "ffmpeg", nullptr };
    subprocess_s test;
    int result = subprocess_create(cmd, 0, &test);
    m_HasFFmpeg = result == 0;

    if (!m_HasFFmpeg)
        logger::warn("FFmpeg not found - video output will be disabled");

    if (result == 0)
    {
        result = subprocess_destroy(&test);
        assert(result == 0);
    }
}

OutputSaver::~OutputSaver()
{
    EndOutput();

    DeviceContext::GetLogical().destroyCommandPool(m_CommandPool);
    DeviceContext::GetLogical().destroyFence(m_Fence);
    DeviceContext::GetLogical().destroySemaphore(m_Semaphore);
}

vk::Semaphore OutputSaver::GetSignalSemaphore() const
{
    return m_Semaphore;
}

bool OutputSaver::CanOutputVideo() const
{
    return m_HasFFmpeg;
}

const Image *OutputSaver::RegisterOutput(const OutputInfo &info)
{
    EndOutput();

    m_Image = ImageBuilder()
                  .SetUsageFlags(
                      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eTransferSrc
                  )
                  .SetFormat(SelectImageFormat(info.Format))
                  .CreateImage(info.Extent, "Output Image");

    m_LinearImage = ImageBuilder()
                        .SetUsageFlags(
                            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc |
                            vk::ImageUsageFlagBits::eStorage
                        )
                .SetFormat(vk::Format::eR16G16B16A16Sfloat)
                .CreateImage(info.Extent, "Linear Output Image");

    m_Buffer = BufferBuilder()
                   .SetUsageFlags(vk::BufferUsageFlagBits::eTransferDst)
                   .CreateHostBuffer(m_Image.GetMipSize(0), "Output Read Buffer");

    if (info.Format == OutputFormat::Mp4)
    {
        const std::string framerate = std::to_string(info.Framerate);
        const std::string size = std::format("{}x{}", info.Extent.width, info.Extent.height);
        const std::string path = info.Path.string();
        const char *cmd[] = {
            "ffmpeg", "-r",       framerate.c_str(), "-f",       "rawvideo", "-pix_fmt",
            "rgba",   "-s",       size.c_str(),      "-i",       "-",        "-y",
            "-an",    "-vcodec",  "libx264",         "-preset",  "veryslow", "-crf",
            "17",     "-pix_fmt", "yuv420p",         "-threads", "0",        path.c_str(),
            nullptr,
        };

        m_FFmpegSubprocess = new subprocess_s;
        int result = subprocess_create(cmd, 0, m_FFmpegSubprocess);
        assert(result == 0);
        fclose(subprocess_stdout(m_FFmpegSubprocess));
        fclose(subprocess_stderr(m_FFmpegSubprocess));
    }

    m_Info = info;

    return &m_LinearImage;
}

void OutputSaver::StartOutputWait()
{
    if (m_Thread.joinable())
        m_Thread.join();

    m_CommandBuffer.reset();
    m_CommandBuffer.begin(vk::CommandBufferBeginInfo());

    vk::BufferImageCopy imageCopy(
        0, 0, 0, { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, vk::Offset3D(0, 0, 0),
        vk::Extent3D(m_Image.GetExtent(), 1)
    );

    m_LinearImage.Transition(
        m_CommandBuffer, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal
    );

    m_Image.Transition(m_CommandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

    auto area = Image::GetMipLevelArea(m_Image.GetExtent());
    vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    vk::ImageBlit2 imageBlit(subresource, area, subresource, area);

    vk::BlitImageInfo2 blitInfo(
        m_LinearImage.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, m_Image.GetHandle(),
        vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eLinear
    );

    m_CommandBuffer.blitImage2(blitInfo);

    m_Image.Transition(
        m_CommandBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal
    );

    m_CommandBuffer.copyImageToBuffer(
        m_Image.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, m_Buffer.GetHandle(), imageCopy
    );

    m_CommandBuffer.end();

    vk::CommandBufferSubmitInfo cmdInfo(m_CommandBuffer);
    vk::SemaphoreSubmitInfo waitInfo(
        m_Semaphore, 0, vk::PipelineStageFlagBits2::eAllCommands
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
            logger::info(std::format("Successfully encoded frame to file {}", m_Info.Path.string()));
        else
            logger::error(std::format("Could not encode frame to file {}", m_Info.Path.string()));
    });
}

void OutputSaver::EndOutput()
{
    if (m_Thread.joinable())
        m_Thread.join();

    if (m_FFmpegSubprocess != nullptr)
    {
        logger::info("Flushing output file {}", m_Info.Path.string());
        
        int result = subprocess_join(m_FFmpegSubprocess, nullptr);
        assert(result == 0);

        result = subprocess_destroy(m_FFmpegSubprocess);
        assert(result == 0);

        delete m_FFmpegSubprocess;
        m_FFmpegSubprocess = nullptr;
        logger::info("Done flushing output file {}", m_Info.Path.string());
    }
}

void OutputSaver::CancelOutput()
{
    if (m_Thread.joinable())
        m_Thread.join();

    if (m_FFmpegSubprocess != nullptr)
    {
        int result = subprocess_terminate(m_FFmpegSubprocess);
        assert(result == 0);

        result = subprocess_join(m_FFmpegSubprocess, nullptr);
        assert(result == 0);

        result = subprocess_destroy(m_FFmpegSubprocess);
        assert(result == 0);

        delete m_FFmpegSubprocess;
        m_FFmpegSubprocess = nullptr;
    }

    std::filesystem::remove(m_Info.Path);
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
    case OutputFormat::Mp4:
        ret = fwrite(data.data(), data.size(), 1, subprocess_stdin(m_FFmpegSubprocess));
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
    case OutputFormat::Mp4:
        return vk::Format::eR8G8B8A8Srgb;
    default:
        throw error("Unsupported output format");
    }
}

}