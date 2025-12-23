#pragma once

#include <vulkan/vulkan.hpp>

#include <thread>

#include "Buffer.h"
#include "Image.h"

namespace PathTracing
{

enum class OutputFormat
{
    Png, Jpg, Tga, Hdr
};

struct OutputInfo
{
    std::filesystem::path Path;
    vk::Extent2D Extent;
    OutputFormat Format;
};

/*
 * As a caller do:
 * 1. Register output to allocate resources
 * 2. Submit your work with the signal semaphore
 * 3. Call StartOutputWait
 */
class OutputSaver
{
public:
    OutputSaver();
    ~OutputSaver();

    [[nodiscard]] vk::Semaphore GetSignalSemaphore() const;

    vk::Image RegisterOutput(const OutputInfo &info);
    void StartOutputWait();

private:
    Image m_Image;
    Buffer m_Buffer;
    OutputInfo m_Info;
    vk::Semaphore m_Semaphore;
    vk::Fence m_Fence;
    vk::CommandPool m_CommandPool;
    vk::CommandBuffer m_CommandBuffer;

    std::jthread m_Thread;

private:
    static bool WriteImage(const OutputInfo &info, std::span<const std::byte> data);
    static vk::Format SelectImageFormat(OutputFormat format);
};

}
