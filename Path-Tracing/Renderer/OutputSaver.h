#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <thread>

#include "Buffer.h"
#include "Image.h"

struct subprocess_s;

namespace PathTracing
{

enum class OutputFormat
{
    Png, Jpg, Tga, Hdr, Mp4
};

struct OutputInfo
{
    std::filesystem::path Path;
    vk::Extent2D Extent;
    uint32_t Framerate;
    OutputFormat Format;
};

/*
 * As a caller do:
 * 1. Register output to allocate resources
 * 2. Submit your work with the signal semaphore
 * 3. Call StartOutputWait every frame
 * 4. Call EndOutput after submitting all frames
 */
class OutputSaver
{
public:
    OutputSaver();
    ~OutputSaver();

    [[nodiscard]] vk::Semaphore GetSignalSemaphore() const;
    [[nodiscard]] bool CanOutputVideo() const;

    [[nodiscard]] const Image *RegisterOutput(const OutputInfo &info);
    void StartOutputWait();
    void EndOutput();
    void CancelOutput();

private:
    Image m_Image;
    Image m_LinearImage;
    Buffer m_Buffer;
    OutputInfo m_Info;
    vk::Semaphore m_Semaphore;
    vk::Fence m_Fence;
    vk::CommandPool m_CommandPool;
    vk::CommandBuffer m_CommandBuffer;

    std::jthread m_Thread;
    bool m_HasFFmpeg = false;
    subprocess_s *m_FFmpegSubprocess = nullptr;

private:
    bool WriteImage(const OutputInfo &info, std::span<const std::byte> data);
    static vk::Format SelectImageFormat(OutputFormat format);
};

}
