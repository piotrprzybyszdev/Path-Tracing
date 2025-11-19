#pragma once

#include <vulkan/vulkan.hpp>

#include <string>
#include <vector>

#include "Buffer.h"
#include "CommandBuffer.h"
#include "Image.h"

namespace PathTracing
{
    
class StagingBuffer
{
public:
    StagingBuffer(vk::DeviceSize size, const std::string &name, CommandBuffer &commandBuffer);

    StagingBuffer(const StagingBuffer &) = delete;
    StagingBuffer &operator=(const StagingBuffer &) = delete;

    void AddContent(BufferContent content, vk::Buffer destinationBuffer);
    void Flush();

    void UploadToImage(
        std::span<const BufferContent> contents, const Image &image,
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal
    );

private:
    const Buffer m_Buffer;
    CommandBuffer &m_CommandBuffer;

    std::vector<vk::DeviceSize> m_StagingBufferOffsets;
    std::vector<vk::DeviceSize> m_DestinationBufferOffsets;
    std::vector<vk::Buffer> m_DestinationBuffers;
};

}
