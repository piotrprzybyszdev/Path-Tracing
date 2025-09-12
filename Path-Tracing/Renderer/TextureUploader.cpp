#include <vulkan/vulkan_format_traits.hpp>

#include "Core/Core.h"

#include "AssetManager.h"

#include "Renderer.h"
#include "TextureUploader.h"
#include "Utils.h"

namespace PathTracing
{

size_t TextureUploader::GetStagingMemoryRequirement(uint32_t numBuffers)
{
    return numBuffers * StagingBufferSize;
}

TextureUploader::TextureUploader(
    uint32_t loaderThreadCount, size_t stagingMemoryLimit, std::vector<Image> &textures,
    std::mutex &descriptorSetMutex
)
    : m_Textures(textures), m_DescriptorSetMutex(descriptorSetMutex), m_LoaderThreadCount(loaderThreadCount),
      m_StagingBufferCount(std::floor(stagingMemoryLimit / StagingBufferSize)),
      m_FreeBuffersSemaphore(m_StagingBufferCount), m_DataBuffersSemaphore(0),
      m_TransferCommandBuffer(DeviceContext::GetTransferQueue()),
      m_MipCommandBuffer(DeviceContext::GetMipQueue()), m_UseTransferQueue(DeviceContext::HasTransferQueue()),
      m_TextureScalingSupported(CheckBlitSupported(IntermediateTextureFormat))
{
    if (!DeviceContext::HasMipQueue())
        logger::warn("Secondary graphics queue wasn't found - Texture loading will be asynchronous, but it "
                     "will take up resources from the main rendering pipeline");

    if (!m_UseTransferQueue)
        logger::warn("Dedicated transfer queue for texture upload not found - using graphics queue instead");

    if (!m_TextureScalingSupported)
        logger::warn(
            "Blit operation is not supported on {} format. Textures with size above {}x{} are not supported",
            vk::to_string(IntermediateTextureFormat), MaxTextureSize.width, MaxTextureSize.height
        );

    m_LoaderThreads.resize(m_LoaderThreadCount);
    m_FreeBuffers.reserve(m_StagingBufferCount);
    m_DataBuffers.reserve(m_StagingBufferCount);

    auto builder = BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc);

    for (int i = 0; i < m_StagingBufferCount; i++)
        m_FreeBuffers.push_back(builder.CreateHostBuffer(StagingBufferSize, "Texture Uploader Staging Buffer")
        );

    m_ImageBuilder = ImageBuilder()
                         .SetFormat(IntermediateTextureFormat)
                         .SetUsageFlags(
                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eSampled
                         )
                         .EnableMips();

    m_TemporaryImage = m_ImageBuilder.CreateImage(MaxTextureDataSize, "Texture Uploader Staging Image");

    logger::info("Max Texture Size: {}x{}", MaxTextureSize.width, MaxTextureSize.height);
    logger::info("Max Texture Data Size: {}x{}", MaxTextureDataSize.width, MaxTextureDataSize.height);
}

TextureUploader::~TextureUploader()
{
    Cancel();
}

void TextureUploader::UploadTexturesBlocking(const Scene &scene)
{
    auto textures = scene.GetTextures();
    for (uint32_t i = 0; i < textures.size(); i++)
    {
        const Buffer &buffer = m_FreeBuffers.front();
        const TextureInfo &textureInfo = textures[i];

        if (!CheckCanUpload(textureInfo))
            continue;

        UploadToBuffer(textureInfo, buffer);

        Renderer::s_MainCommandBuffer->Begin();
        UploadTexture(
            Renderer::s_MainCommandBuffer->Buffer, Renderer::s_MainCommandBuffer->Buffer, textureInfo, i,
            buffer
        );
        Renderer::s_MainCommandBuffer->SubmitBlocking();

        Renderer::UpdateTexture(Shaders::GetSceneTextureIndex(i));
        logger::debug("Uploaded Texture: {}", textureInfo.Path.string());
    }
}

void TextureUploader::UploadTextures(const Scene &scene)
{
    Cancel();

    m_TextureIndex = 0;
    m_RejectedCount = 0;
    m_FreeBuffersSemaphore.release(m_DataBuffers.size());
    m_FreeBuffers.insert(
        m_FreeBuffers.end(), std::make_move_iterator(m_DataBuffers.begin()),
        std::make_move_iterator(m_DataBuffers.end())
    );
    if (!m_DataBuffers.empty())
        m_DataBuffersSemaphore.acquire();
    m_DataBuffers.clear();
    m_TextureIndices.clear();

    StartLoaderThreads(scene);
    StartSubmitThread(scene);
}

void TextureUploader::Cancel()
{
    logger::trace("Texture Uploader cancellation requested");
    if (!m_SubmitThread.joinable())
        return;

    m_SubmitThread.request_stop();
    for (auto &thread : m_LoaderThreads)
        thread.request_stop();

    m_SubmitThread.join();
    for (auto &thread : m_LoaderThreads)
        thread.join();
}

void TextureUploader::StartLoaderThreads(const Scene &scene)
{
    for (auto &thread : m_LoaderThreads)
    {
        thread = std::jthread([=, this](std::stop_token stopToken) {
            auto textures = scene.GetTextures();
            while (!stopToken.stop_requested() && m_TextureIndex < textures.size())
            {
                const uint32_t textureIndex = m_TextureIndex++;
                const TextureInfo &textureInfo = textures[textureIndex];

                if (!CheckCanUpload(textureInfo))
                {
                    m_RejectedCount++;
                    continue;
                }

                m_FreeBuffersSemaphore.acquire();
                if (stopToken.stop_requested())
                {
                    m_FreeBuffersSemaphore.release();
                    break;
                }

                Buffer buffer;
                {
                    std::lock_guard lock(m_FreeBuffersMutex);
                    buffer = std::move(m_FreeBuffers.back());
                    m_FreeBuffers.pop_back();
                }

                UploadToBuffer(textureInfo, buffer);

                {
                    std::lock_guard lock(m_DataBuffersMutex);

                    m_DataBuffers.push_back(std::move(buffer));
                    m_TextureIndices.push_back(textureIndex);
                }

                m_DataBuffersSemaphore.release();
            }
        });
    }
}

void TextureUploader::StartSubmitThread(const Scene &scene)
{
    m_SubmitThread = std::jthread([=, this](std::stop_token stopToken) {
        auto textures = scene.GetTextures();
        uint32_t uploadedCount = 0;

        std::vector<Buffer> buffers;
        std::vector<uint32_t> textureIndices;
        buffers.reserve(m_StagingBufferCount);
        textureIndices.reserve(m_StagingBufferCount);

        while (!stopToken.stop_requested() && uploadedCount < textures.size() - m_RejectedCount)
        {
            m_DataBuffersSemaphore.acquire();
            if (stopToken.stop_requested())
            {
                m_DataBuffersSemaphore.release();
                break;
            }

            {
                std::lock_guard lock(m_DataBuffersMutex);
                buffers.swap(m_DataBuffers);
                textureIndices.swap(m_TextureIndices);
            }

            if (m_UseTransferQueue)
                UploadBuffersWithTransfer(textures, textureIndices, buffers);
            else
                UploadBuffers(textures, textureIndices, buffers);

            {
                std::lock_guard lock(m_FreeBuffersMutex);

                m_FreeBuffersSemaphore.release(buffers.size());
                m_FreeBuffers.insert(
                    m_FreeBuffers.end(), std::make_move_iterator(buffers.begin()),
                    std::make_move_iterator(buffers.end())
                );
            }

            {
                std::lock_guard lock(m_DescriptorSetMutex);
                for (uint32_t textureIndex : textureIndices)
                {
                    Renderer::UpdateTexture(Shaders::GetSceneTextureIndex(textureIndex));
                    logger::debug("Uploaded Texture: {}", textures[textureIndex].Path.string());
                }
            }

            uploadedCount += textureIndices.size();
            buffers.clear();
            textureIndices.clear();
        }

        uint32_t rejectedcount = m_RejectedCount;
        if (uploadedCount == textures.size() - rejectedcount)
            logger::info("Done uploading scene textures");
        else
            logger::debug("Texture upload cancelled");

        if (rejectedcount > 0)
            logger::warn("{} texture(s) weren't uploaded", rejectedcount);
    });
}

void TextureUploader::UploadToBuffer(const TextureInfo &textureInfo, const Buffer &buffer)
{
    std::byte *data = AssetManager::LoadTextureData(textureInfo);

    const vk::Extent2D extent(textureInfo.Width, textureInfo.Height);
    const size_t dataSize = Image::GetByteSize(extent, IntermediateTextureFormat);

    assert(extent <= MaxTextureDataSize);
    assert(dataSize <= StagingBufferSize);

    buffer.Upload(std::span(data, dataSize));
    AssetManager::ReleaseTextureData(data);
}

void TextureUploader::UploadTexture(
    vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const TextureInfo &texture,
    uint32_t textureIndex, const Buffer &buffer
)
{
    assert(std::has_single_bit(texture.Width) && std::has_single_bit(texture.Height));

    const vk::Format format = SelectTextureFormat(texture.Type);
    m_ImageBuilder.SetFormat(format);
    m_ImageBuilder.EnableMips(CheckBlitSupported(format));

    const uint32_t scale =
        std::max(1u, std::max(texture.Width / MaxTextureSize.width, texture.Height / MaxTextureSize.height));
    const vk::Extent2D extent(texture.Width / scale, texture.Height / scale);

    Image image = m_ImageBuilder.CreateImage(extent, texture.Path.string());

    image.UploadStaging(
        mipBuffer, transferBuffer, buffer, m_TemporaryImage, vk::Extent2D(texture.Width, texture.Height),
        vk::ImageLayout::eShaderReadOnlyOptimal
    );

    m_Textures[Shaders::GetSceneTextureIndex(textureIndex)] = std::move(image);
}

void TextureUploader::UploadBuffers(
    std::span<const TextureInfo> textures, std::span<const uint32_t> textureIndices,
    std::span<const Buffer> buffers
)
{
    m_MipCommandBuffer.Begin();
    for (int i = 0; i < buffers.size(); i++)
        UploadTexture(
            m_MipCommandBuffer.Buffer, m_MipCommandBuffer.Buffer, textures[textureIndices[i]],
            textureIndices[i], buffers[i]
        );
    m_MipCommandBuffer.SubmitBlocking();
}

void TextureUploader::UploadBuffersWithTransfer(
    std::span<const TextureInfo> textures, std::span<const uint32_t> textureIndices,
    std::span<const Buffer> buffers
)
{
    for (int i = 0; i < buffers.size(); i++)
    {
        m_TransferCommandBuffer.Begin();
        vk::Semaphore semaphore = m_TransferCommandBuffer.Signal();
        m_MipCommandBuffer.Begin(semaphore, vk::PipelineStageFlagBits2::eTransfer);

        UploadTexture(
            m_MipCommandBuffer.Buffer, m_TransferCommandBuffer.Buffer, textures[textureIndices[i]],
            textureIndices[i], buffers[i]
        );

        m_TransferCommandBuffer.Submit();
        m_MipCommandBuffer.SubmitBlocking();
    }
}

bool TextureUploader::CheckCanUpload(const TextureInfo &info)
{
    if (!m_TextureScalingSupported && vk::Extent2D(info.Width, info.Height) > MaxTextureSize)
    {
        logger::error(
            "Cannot load texture {} because Texture Scaling is not supported and the texture size {}x{} is "
            "larger than the MaxTextureSize {}x{}",
            info.Path.string(), info.Width, info.Height, MaxTextureSize.width, MaxTextureSize.height
        );
        return false;
    }

    return true;
}

vk::Format TextureUploader::SelectTextureFormat(TextureType type)
{
    // TODO: Select format depending on TextureType
    return vk::Format::eR8G8B8A8Unorm;
}

bool TextureUploader::CheckBlitSupported(vk::Format format)
{
    const auto flags = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
    return (DeviceContext::GetFormatProperties(format).formatProperties.bufferFeatures & flags) != flags;
}

}
