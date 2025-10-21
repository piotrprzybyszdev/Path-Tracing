#include <vulkan/vulkan_format_traits.hpp>

#include "Core/Core.h"

#include "Application.h"
#include "AssetImporter.h"

#include "Renderer.h"
#include "TextureUploader.h"
#include "Utils.h"

namespace PathTracing
{

namespace
{

uint32_t GetLoaderThreadCount()
{
    const uint32_t desiredLoaderThreadCount = std::thread::hardware_concurrency() / 2;
    return std::min(Application::GetConfig().MaxTextureLoaderThreads, desiredLoaderThreadCount);
}

uint32_t GetStagingStagingBufferPerThreadCount()
{
    const uint32_t desiredStagingBufferCount = 2;
    return std::min(Application::GetConfig().MaxBuffersPerLoaderThread, desiredStagingBufferCount);
}

}

TextureUploader::TextureUploader(std::vector<Image> &textures, std::mutex &descriptorSetMutex)
    : m_Textures(textures), m_DescriptorSetMutex(descriptorSetMutex),
      m_LoaderThreadCount(GetLoaderThreadCount()),
      m_StagingBufferCount(GetStagingStagingBufferPerThreadCount() * m_LoaderThreadCount),
      m_FreeBuffersSemaphore(m_StagingBufferCount), m_DataBuffersSemaphore(0),
      m_TransferCommandBuffer(DeviceContext::GetTransferQueue()),
      m_MipCommandBuffer(DeviceContext::GetMipQueue()), m_UseTransferQueue(DeviceContext::HasTransferQueue()),
      m_ColorTextureScalingSupported(CheckBlitSupported(GetTextureDataFormat(TextureType::Color))),
      m_OtherTextureScalingSupported(CheckBlitSupported(GetTextureDataFormat(TextureType::Normal)))
{
    if (!DeviceContext::HasMipQueue())
        logger::warn("Secondary graphics queue wasn't found - Texture loading will be asynchronous, but it "
                     "will take up resources from the main rendering pipeline");

    if (!m_UseTransferQueue)
        logger::warn("Dedicated transfer queue for texture upload not found - using graphics queue instead");

    if (!m_ColorTextureScalingSupported)
        logger::warn(
            "Blit operation is not supported on {} format. Color textures with size above {}x{} are not "
            "supported",
            vk::to_string(GetTextureDataFormat(TextureType::Color)), MaxTextureSize.width,
            MaxTextureSize.height
        );

    if (!m_OtherTextureScalingSupported)
        logger::warn(
            "Blit operation is not supported on {} format. Textures with size above {}x{} are not "
            "supported",
            vk::to_string(GetTextureDataFormat(TextureType::Normal)), MaxTextureSize.width,
            MaxTextureSize.height
        );

    m_LoaderThreads.resize(m_LoaderThreadCount);
    m_FreeBuffers.reserve(m_StagingBufferCount);
    m_DataBuffers.reserve(m_StagingBufferCount);

    auto builder = BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc);

    for (int i = 0; i < m_StagingBufferCount; i++)
        m_FreeBuffers.push_back(builder.CreateHostBuffer(StagingBufferSize, "Texture Uploader Staging Buffer")
        );

    m_ImageBuilder = ImageBuilder()
                         .SetFormat(GetTextureDataFormat(TextureType::Color))
                         .SetUsageFlags(
                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eSampled
                         )
                         .EnableMips();

    m_TemporaryColorImage =
        m_ImageBuilder.CreateImage(MaxTextureDataSize, "Texture Uploader Color Staging Image");

    m_ImageBuilder.SetFormat(GetTextureDataFormat(TextureType::Normal));
    m_TemporaryOtherImage =
        m_ImageBuilder.CreateImage(MaxTextureDataSize, "Texture Uploader Other Staging Image");

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

void TextureUploader::UploadTextures(const std::shared_ptr<const Scene> &scene)
{
    Cancel();

    Application::AddBackgroundTask(BackgroundTaskType::TextureUpload, scene->GetTextures().size());
    StartLoaderThreads(scene);
    StartSubmitThread(scene);
}

void TextureUploader::Cancel()
{
    logger::trace("Texture Uploader cancellation requested");
    if (!m_SubmitThread.joinable())
        return;

    for (auto &thread : m_LoaderThreads)
        thread.request_stop();
    for (auto &thread : m_LoaderThreads)
        thread.join();

    m_SubmitThread.request_stop();
    m_SubmitThread.join();

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

    Application::ResetBackgroundTask(BackgroundTaskType::TextureUpload);
}

Image TextureUploader::UploadDefault(glm::u8vec4 value, std::string &&name)
{
    vk::Extent2D extent = { 1, 1 };
    vk::Format format = vk::Format::eR8G8B8A8Unorm;

    Image image = ImageBuilder()
                      .SetFormat(format)
                      .SetUsageFlags(
                          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                          vk::ImageUsageFlagBits::eTransferSrc
                      )
                      .EnableMips()
                      .CreateImage(extent, name);

    const Buffer &buffer = m_FreeBuffers.front();

    buffer.Upload(ToByteSpan(value));

    assert(m_TemporaryOtherImage.GetFormat() == vk::Format::eR8G8B8A8Unorm);
    Renderer::s_MainCommandBuffer->Begin();
    image.UploadStaging(
        Renderer::s_MainCommandBuffer->Buffer, Renderer::s_MainCommandBuffer->Buffer, buffer,
        m_TemporaryOtherImage, extent, vk::ImageLayout::eShaderReadOnlyOptimal
    );
    Renderer::s_MainCommandBuffer->SubmitBlocking();

    return image;
}

Image TextureUploader::UploadSkyboxBlocking(const Skybox2D &skybox)
{
    vk::Extent2D extent(skybox.Content.Width, skybox.Content.Height);

    assert(std::has_single_bit(extent.width) && std::has_single_bit(extent.height));
    assert(skybox.Content.Type == TextureType::Skybox || skybox.Content.Type == TextureType::SkyboxHDR);

    vk::Format format = SelectTextureFormat(skybox.Content.Type);

    auto image = ImageBuilder()
                     .SetFormat(format)
                     .SetUsageFlags(
                         vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                         vk::ImageUsageFlagBits::eTransferSrc
                     )
                     .EnableMips()
                     .CreateImage(extent, "Skybox 2D");

    const Buffer &buffer = *Renderer::s_StagingBuffer;
    const TextureInfo &textureInfo = skybox.Content;
    UploadToBuffer(skybox.Content, buffer);
    SubmitBlocking(image, buffer, extent);

    return image;
}

Image TextureUploader::UploadSkyboxBlocking(const SkyboxCube &skybox)
{
    std::array<const TextureInfo *, 6> textureInfos = { &skybox.Front, &skybox.Back, &skybox.Up,
                                                        &skybox.Down,  &skybox.Left, &skybox.Right };

    TextureType type = textureInfos[0]->Type;
    assert(type == TextureType::Skybox || type == TextureType::SkyboxHDR);
    vk::Format format = SelectTextureFormat(type);
    vk::Extent2D extent(textureInfos[0]->Width, textureInfos[0]->Height);

    assert(std::has_single_bit(extent.width) && std::has_single_bit(extent.height));
    assert(format == GetTextureDataFormat(type));

    auto image = ImageBuilder()
                     .SetFormat(format)
                     .SetUsageFlags(
                         vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                         vk::ImageUsageFlagBits::eTransferSrc
                     )
                     .EnableMips()
                     .EnableCube()
                     .CreateImage(extent, "Skybox Cube");

    const Buffer &buffer = *Renderer::s_StagingBuffer;
    const vk::DeviceSize layerSize = Image::GetByteSize(extent, format);
    for (int i = 0; i < textureInfos.size(); i++)
    {
        assert(textureInfos[i]->Width == extent.width && textureInfos[i]->Height == extent.height);
        assert(textureInfos[i]->Type == type);
        UploadToBuffer(*textureInfos[i], buffer, i * layerSize);
    }

    SubmitBlocking(image, buffer, extent);
    return image;
}

void TextureUploader::SubmitBlocking(const Image &image, const Buffer &buffer, vk::Extent2D extent)
{
    Renderer::s_MainCommandBuffer->Begin();
    image.UploadStaging(
        Renderer::s_MainCommandBuffer->Buffer, Renderer::s_MainCommandBuffer->Buffer, buffer, Image(), extent,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );
    Renderer::s_MainCommandBuffer->SubmitBlocking();
}

void TextureUploader::StartLoaderThreads(const std::shared_ptr<const Scene> &scene)
{
    for (auto &thread : m_LoaderThreads)
    {
        thread = std::jthread([scene, this](std::stop_token stopToken) {
            auto textures = scene->GetTextures();
            while (!stopToken.stop_requested() && m_TextureIndex < textures.size())
            {
                const uint32_t textureIndex = m_TextureIndex++;
                const TextureInfo &textureInfo = textures[textureIndex];

                if (!CheckCanUpload(textureInfo))
                {
                    m_RejectedCount++;
                    Application::IncrementBackgroundTaskDone(BackgroundTaskType::TextureUpload);
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

void TextureUploader::StartSubmitThread(const std::shared_ptr<const Scene> &scene)
{
    m_SubmitThread = std::jthread([scene, this](std::stop_token stopToken) {
        auto textures = scene->GetTextures();
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

            Application::IncrementBackgroundTaskDone(
                BackgroundTaskType::TextureUpload, textureIndices.size()
            );
            uploadedCount += textureIndices.size();
            buffers.clear();
            textureIndices.clear();
        }

        uint32_t rejectedCount = m_RejectedCount;
        if (uploadedCount == textures.size() - rejectedCount)
            logger::info("Done uploading scene textures");
        else
            logger::trace("Texture upload submit thread cancelled");
        m_FreeBuffersSemaphore.release(rejectedCount);

        if (rejectedCount > 0)
            logger::warn("{} texture(s) weren't uploaded", rejectedCount);
    });
}

void TextureUploader::UploadToBuffer(
    const TextureInfo &textureInfo, const Buffer &buffer, vk::DeviceSize offset
)
{
    std::byte *data = AssetImporter::LoadTextureData(textureInfo);

    const vk::Extent2D extent(textureInfo.Width, textureInfo.Height);
    const size_t dataSize = Image::GetByteSize(extent, GetTextureDataFormat(textureInfo.Type));

    assert(extent <= MaxTextureDataSize);
    assert(dataSize <= buffer.GetSize());

    buffer.Upload(std::span(data, dataSize), offset);
    AssetImporter::ReleaseTextureData(data);
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
    const uint32_t width = std::max(texture.Width / scale, 1u);
    const uint32_t height = std::max(texture.Height / scale, 1u);
    const vk::Extent2D extent(width, height);

    Image image = m_ImageBuilder.CreateImage(extent, texture.Path.string());

    image.UploadStaging(
        mipBuffer, transferBuffer, buffer, GetTemporaryImage(texture.Type),
        vk::Extent2D(texture.Width, texture.Height), vk::ImageLayout::eShaderReadOnlyOptimal
    );

    // Release barrier
    image.TransitionWithQueueChange(
        mipBuffer, nullptr, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits2::eAllCommands, vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eNone, DeviceContext::GetMipQueue().FamilyIndex,
        DeviceContext::GetGraphicsQueue().FamilyIndex
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
    if (!IsScalingSupported(info.Type) && vk::Extent2D(info.Width, info.Height) > MaxTextureSize)
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
    // TODO: Consider compressed texture formats
    switch (type)
    {
    case TextureType::Color:
    case TextureType::Skybox:
        return vk::Format::eR8G8B8A8Srgb;
    case TextureType::SkyboxHDR:
        return vk::Format::eR32G32B32A32Sfloat;
    default:
        return vk::Format::eR8G8B8A8Unorm;
    }
}

bool TextureUploader::IsScalingSupported(TextureType type) const
{
    switch (type)
    {
    case TextureType::Color:
    case TextureType::Skybox:
        return m_ColorTextureScalingSupported;
    case TextureType::SkyboxHDR:
        throw error("HDR texture scaling is not supported");
    default:
        return m_OtherTextureScalingSupported;
    }
}

const Image &TextureUploader::GetTemporaryImage(TextureType type) const
{
    switch (type)
    {
    case TextureType::Color:
    case TextureType::Skybox:
        return m_TemporaryColorImage;
    case TextureType::SkyboxHDR:
        throw error("HDR texture scaling is not supported");
    default:
        return m_TemporaryOtherImage;
    }
}

vk::Format TextureUploader::GetTextureDataFormat(TextureType type)
{
    switch (type)
    {
    case TextureType::Color:
    case TextureType::Skybox:
        return vk::Format::eR8G8B8A8Srgb;
    case TextureType::SkyboxHDR:
        return vk::Format::eR32G32B32A32Sfloat;
    default:
        return vk::Format::eR8G8B8A8Unorm;
    }
}

bool TextureUploader::CheckBlitSupported(vk::Format format)
{
    const auto flags = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
    return (DeviceContext::GetFormatProperties(format).formatProperties.bufferFeatures & flags) != flags;
}

}
