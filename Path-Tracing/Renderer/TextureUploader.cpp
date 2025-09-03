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
      m_TransferCommandBuffer(
          DeviceContext::GetTransferQueueFamilyIndex(), DeviceContext::GetTransferQueue()
      ),
      m_MipCommandBuffer(DeviceContext::GetGraphicsQueueFamilyIndex(), DeviceContext::GetMipQueue()),
      m_AlwaysBlock(!DeviceContext::HasMipQueue()), m_UseTransferQueue(DeviceContext::HasTransferQueue())
{
    m_LoaderThreads.resize(m_LoaderThreadCount);
    m_FreeBuffers.reserve(m_StagingBufferCount);
    m_DataBuffers.reserve(m_StagingBufferCount);

    auto builder = BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc);

    for (int i = 0; i < m_StagingBufferCount; i++)
        m_FreeBuffers.push_back(builder.CreateHostBuffer(StagingBufferSize, "TextureUploader Staging Buffer")
        );

    m_ImageBuilder = ImageBuilder()
                         .SetUsageFlags(
                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eSampled
                         )
                         .SetFormat(vk::Format::eR8G8B8A8Unorm)
                         .EnableMips();

    m_TemporaryImage = m_ImageBuilder.CreateImage(MaxTextureDataSize, "Texture Uploader Staging Image");

    Stats::AddStat(
        "Max Texture Size", "Max Texture Size: {}x{}", MaxTextureSize.width, MaxTextureSize.height
    );
    Stats::AddStat(
        "Max Texture Data Size", "Max Texture Data Size: {}x{}", MaxTextureDataSize.width,
        MaxTextureDataSize.height
    );
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
        const TextureInfo textureInfo = textures[i];

        TextureSpec textureSpec = UploadToBuffer(textureInfo, buffer);

        Renderer::s_MainCommandBuffer->Begin();
        UploadTexture(
            Renderer::s_MainCommandBuffer->Buffer, Renderer::s_MainCommandBuffer->Buffer, textureInfo,
            UploadInfo(i, textureSpec), buffer
        );
        Renderer::s_MainCommandBuffer->SubmitBlocking();

        Renderer::UpdateTexture(Shaders::GetSceneTextureIndex(i));
        logger::debug("Uploaded Texture: {}", textureInfo.Path.string());
    }
}

void TextureUploader::UploadTextures(const Scene &scene)
{
    if (m_AlwaysBlock)
    {
        UploadTexturesBlocking(scene);
        return;
    }

    Cancel();

    m_TextureIndex = 0;
    m_FreeBuffersSemaphore.release(m_DataBuffers.size());
    m_FreeBuffers.insert(
        m_FreeBuffers.end(), std::make_move_iterator(m_DataBuffers.begin()),
        std::make_move_iterator(m_DataBuffers.end())
    );
    if (!m_DataBuffers.empty())
        m_DataBuffersSemaphore.acquire();
    m_DataBuffers.clear();
    m_UploadInfos.clear();

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

                const uint32_t textureIndex = m_TextureIndex++;
                TextureSpec spec = UploadToBuffer(textures[textureIndex], buffer);

                {
                    std::lock_guard lock(m_DataBuffersMutex);

                    m_DataBuffers.push_back(std::move(buffer));
                    m_UploadInfos.emplace_back(textureIndex, spec);
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
        std::vector<UploadInfo> uploadInfos;
        buffers.reserve(m_StagingBufferCount);
        uploadInfos.reserve(m_StagingBufferCount);

        while (!stopToken.stop_requested() && uploadedCount < textures.size())
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
                uploadInfos.swap(m_UploadInfos);
            }

            if (m_UseTransferQueue)
                UploadBuffersWithTransfer(textures, uploadInfos, buffers);
            else
                UploadBuffers(textures, uploadInfos, buffers);

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
                for (const auto &info : uploadInfos)
                {
                    Renderer::UpdateTexture(Shaders::GetSceneTextureIndex(info.TextureIndex));
                    logger::debug("Uploaded Texture: {}", textures[info.TextureIndex].Path.string());
                }
            }

            uploadedCount += uploadInfos.size();
            buffers.clear();
            uploadInfos.clear();
        }

        if (uploadedCount == textures.size())
            logger::info("Done uploading scene textures");
        else
            logger::debug("Texture upload cancelled");
    });
}

TextureUploader::TextureSpec TextureUploader::UploadToBuffer(
    const TextureInfo &textureInfo, const Buffer &buffer
)
{
    const Texture texture = AssetManager::LoadTexture(textureInfo.Path);

    const vk::Extent2D extent(texture.Width, texture.Height);
    const size_t dataSize = Image::GetByteSize(extent, vk::Format::eR8G8B8A8Unorm);

    assert(extent <= MaxTextureDataSize);
    assert(dataSize <= StagingBufferSize);

    buffer.Upload(std::span(texture.Data, dataSize));
    AssetManager::ReleaseTexture(texture);

    return { extent, textureInfo.Type };
}

void TextureUploader::UploadTexture(
    vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const TextureInfo &texture,
    const UploadInfo &info, const Buffer &buffer
)
{
    assert(std::has_single_bit(info.Spec.Extent.width) && std::has_single_bit(info.Spec.Extent.height));

    // TODO: Pick format depending on TextureType
    Image image =
        m_ImageBuilder.CreateImage(std::min(info.Spec.Extent, MaxTextureSize), texture.Path.string());

    image.UploadStaging(
        mipBuffer, transferBuffer, buffer, m_TemporaryImage, info.Spec.Extent,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );

    m_Textures[Shaders::GetSceneTextureIndex(info.TextureIndex)] = std::move(image);
}

void TextureUploader::UploadBuffers(
    std::span<const TextureInfo> textures, std::span<const UploadInfo> infos, std::span<const Buffer> buffers
)
{
    m_MipCommandBuffer.Begin();
    for (int i = 0; i < buffers.size(); i++)
        UploadTexture(
            m_MipCommandBuffer.Buffer, m_MipCommandBuffer.Buffer, textures[i], infos[i], buffers[i]
        );
    m_MipCommandBuffer.SubmitBlocking();
}

void TextureUploader::UploadBuffersWithTransfer(
    std::span<const TextureInfo> textures, std::span<const UploadInfo> infos, std::span<const Buffer> buffers
)
{
    for (int i = 0; i < buffers.size(); i++)
    {
        m_TransferCommandBuffer.Begin();
        vk::Semaphore semaphore = m_TransferCommandBuffer.Signal();
        m_MipCommandBuffer.Begin(semaphore, vk::PipelineStageFlagBits::eTransfer);

        UploadTexture(
            m_MipCommandBuffer.Buffer, m_TransferCommandBuffer.Buffer, textures[i], infos[i], buffers[i]
        );

        m_TransferCommandBuffer.Submit();
        m_MipCommandBuffer.SubmitBlocking();
    }
}

}
