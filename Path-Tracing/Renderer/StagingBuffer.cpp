#include <vulkan/vulkan_format_traits.hpp>

#include "StagingBuffer.h"

namespace PathTracing
{

StagingBuffer::StagingBuffer(vk::DeviceSize size, const std::string &name, CommandBuffer &commandBuffer)
    : m_Buffer(
          BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc).CreateHostBuffer(size, name)
      ),
      m_CommandBuffer(commandBuffer), m_StagingBufferOffsets { 0 }
{
}

void StagingBuffer::AddContent(BufferContent content, vk::Buffer destinationBuffer)
{
    vk::DeviceSize destinationOffset = 0;
    vk::DeviceSize leftToUpload = content.Size;

    while (leftToUpload > 0)
    {
        const vk::DeviceSize stagingOffset = m_StagingBufferOffsets.back();
        const vk::DeviceSize space = m_Buffer.GetSize() - stagingOffset;
        const vk::DeviceSize toUpload = std::min(leftToUpload, space);

        m_Buffer.Upload(content.GetSubContent(destinationOffset, toUpload), stagingOffset);
        m_StagingBufferOffsets.push_back(stagingOffset + toUpload);
        m_DestinationBufferOffsets.push_back(destinationOffset);
        m_DestinationBuffers.push_back(destinationBuffer);

        if (space == toUpload)
            Flush();

        leftToUpload -= toUpload;
        destinationOffset += toUpload;
    }
}

void StagingBuffer::Flush()
{
    if (m_DestinationBuffers.empty())
        return;

    m_CommandBuffer.Begin();

    for (int i = 0; i < m_DestinationBuffers.size(); i++)
    {
        const vk::DeviceSize stagingOffset = m_StagingBufferOffsets[i];
        const vk::DeviceSize destinationOffset = m_DestinationBufferOffsets[i];
        const vk::DeviceSize size = m_StagingBufferOffsets[i + 1] - stagingOffset;
        m_CommandBuffer.Buffer.copyBuffer(
            m_Buffer.GetHandle(), m_DestinationBuffers[i],
            { vk::BufferCopy(stagingOffset, destinationOffset, size) }
        );
    }

    m_CommandBuffer.SubmitBlocking();

    m_StagingBufferOffsets = { 0 };
    m_DestinationBufferOffsets.clear();
    m_DestinationBuffers.clear();
}

void StagingBuffer::UploadToImage(std::span<const BufferContent> contents, const Image &image)
{
    assert(m_DestinationBuffers.empty());
    const vk::DeviceSize rowSize =
        static_cast<vk::DeviceSize>(image.GetExtent().width) * vk::blockSize(image.GetFormat());

    for (uint32_t layer = 0; layer < contents.size(); layer++)
    {
        auto content = contents[layer];

        vk::DeviceSize uploaded = 0;
        uint32_t uploadedRows = 0;
        uint32_t uploadNumber = 0;

        while (uploaded < content.Size)
        {
            const vk::DeviceSize leftToUpload = content.Size - uploaded;
            const vk::DeviceSize toUpload = std::min(leftToUpload, m_Buffer.GetSize());

            assert(toUpload % rowSize == 0);
            const uint32_t rowsToUpload = toUpload / rowSize;

            m_CommandBuffer.Begin();

            if (layer == 0 && uploadNumber == 0)
                image.Transition(
                    m_CommandBuffer.Buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal
                );

            m_Buffer.Upload(content.GetSubContent(uploaded, toUpload));
            m_CommandBuffer.Buffer.copyBufferToImage(
                m_Buffer.GetHandle(), image.GetHandle(), vk::ImageLayout::eTransferDstOptimal,
                { vk::BufferImageCopy(
                    0, 0, 0, { vk::ImageAspectFlagBits::eColor, 0, layer, 1 },
                    vk::Offset3D(0, uploadedRows, 0), vk::Extent3D(image.GetExtent().width, rowsToUpload, 1)
                ) }
            );

            uploadNumber++;
            uploaded += toUpload;
            uploadedRows += rowsToUpload;

            if (uploaded == content.Size)
                image.Transition(
                    m_CommandBuffer.Buffer, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal
                );

            m_CommandBuffer.SubmitBlocking();
        }
    }
}

}
