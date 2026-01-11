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

void StagingBuffer::UploadToImage(
    std::span<const BufferContent> contents, const Image &image, vk::ImageLayout layout
)
{
    assert(m_DestinationBuffers.empty());

    const bool is3D = image.Is3D();
    const uint32_t width = is3D ? image.GetExtent3D().width : image.GetExtent().width;
    const uint32_t height = is3D ? image.GetExtent3D().height : image.GetExtent().height;
    const uint32_t depth = is3D ? image.GetExtent3D().depth : 1;
    const vk::DeviceSize rowSize = static_cast<vk::DeviceSize>(width) * vk::blockSize(image.GetFormat());

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

            if (is3D)
            {
                // For 3D images: rows span across depth slices
                const uint32_t rowsPerSlice = height;
                const uint32_t startSlice = uploadedRows / rowsPerSlice;
                const uint32_t startRow = uploadedRows % rowsPerSlice;
                
                // Calculate how many complete slices we can fit in this upload
                const uint32_t rowsInFirstSlice = std::min(rowsPerSlice - startRow, rowsToUpload);
                const uint32_t remainingRows = rowsToUpload - rowsInFirstSlice;
                const uint32_t additionalSlices = remainingRows / rowsPerSlice;
                const uint32_t rowsInLastSlice = remainingRows % rowsPerSlice;
                const uint32_t totalSlices = 1 + additionalSlices + (rowsInLastSlice > 0 ? 1 : 0);

                // Determine actual extent height for this upload
                uint32_t extentHeight;
                if (totalSlices == 1)
                {
                    // Single slice upload
                    extentHeight = rowsToUpload;
                }
                else
                {
                    // Multi-slice upload: fill remaining rows in first slice
                    extentHeight = rowsInFirstSlice;
                }

                // Depth extent: how many complete slices we're uploading
                const uint32_t extentDepth = (startRow == 0 && rowsToUpload >= rowsPerSlice) 
                    ? std::min(rowsToUpload / rowsPerSlice, depth - startSlice)
                    : 1;

                // Adjust height if we're doing multi-slice upload
                if (extentDepth > 1)
                {
                    extentHeight = rowsPerSlice;
                }

                m_CommandBuffer.Buffer.copyBufferToImage(
                    m_Buffer.GetHandle(), image.GetHandle(), vk::ImageLayout::eTransferDstOptimal,
                    { vk::BufferImageCopy(
                        0, 0, 0, { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                        vk::Offset3D(0, startRow, startSlice),
                        vk::Extent3D(width, extentHeight, extentDepth)
                    ) }
                );
            }
            else
            {
                // For 2D images: use layer parameter
                m_CommandBuffer.Buffer.copyBufferToImage(
                    m_Buffer.GetHandle(), image.GetHandle(), vk::ImageLayout::eTransferDstOptimal,
                    { vk::BufferImageCopy(
                        0, 0, 0, { vk::ImageAspectFlagBits::eColor, 0, layer, 1 },
                        vk::Offset3D(0, uploadedRows, 0), vk::Extent3D(width, rowsToUpload, 1)
                    ) }
                );
            }

            uploadNumber++;
            uploaded += toUpload;
            uploadedRows += rowsToUpload;

            if (uploaded == content.Size)
                image.Transition(m_CommandBuffer.Buffer, vk::ImageLayout::eTransferDstOptimal, layout);

            m_CommandBuffer.SubmitBlocking();
        }
    }
}

}
