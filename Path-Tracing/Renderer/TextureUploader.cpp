#include <vulkan/vulkan_format_traits.hpp>

#include "Core/Core.h"

#include "Application.h"
#include "TextureImporter.h"

#include "Renderer.h"
#include "TextureUploader.h"

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

size_t GetTextureBudget()
{
    const size_t totalMemory = Image::GetImageMemoryBudget();
    const size_t desiredMemoryBudget =
        totalMemory * Application::GetConfig().MaxTextureMemoryBudgetVramPercent / 100;
    return std::min(
        desiredMemoryBudget, static_cast<size_t>(Application::GetConfig().MaxTextureMemoryBudgetAbsolute)
    );
}

}

TextureUploader::TextureUploader(std::vector<Image> &textures, std::mutex &descriptorSetMutex)
    : m_Textures(textures), m_DescriptorSetMutex(descriptorSetMutex),
      m_LoaderThreadCount(GetLoaderThreadCount()),
      m_StagingBufferCount(GetStagingStagingBufferPerThreadCount() * m_LoaderThreadCount),
      m_FreeBuffersSemaphore(m_StagingBufferCount), m_DataBuffersSemaphore(0),
      m_TransferCommandBuffer(DeviceContext::GetTransferQueue()),
      m_MipCommandBuffer(DeviceContext::GetMipQueue()), m_UseTransferQueue(DeviceContext::HasTransferQueue())
{
    if (!DeviceContext::HasMipQueue())
        logger::warn("Secondary graphics queue wasn't found - Texture loading will be asynchronous, but it "
                     "will take up resources from the main rendering pipeline");

    if (!m_UseTransferQueue)
        logger::warn("Dedicated transfer queue for texture upload not found - using graphics queue instead");

    m_LoaderThreads.resize(m_LoaderThreadCount);
    m_FreeBuffers.reserve(m_StagingBufferCount);
    m_DataBuffers.reserve(m_StagingBufferCount);

    auto builder = BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc);

    for (int i = 0; i < m_StagingBufferCount; i++)
        m_FreeBuffers.push_back(builder.CreateHostBuffer(StagingBufferSize, "Texture Uploader Staging Buffer")
        );

    m_ImageBuilder = ImageBuilder()
                         .SetUsageFlags(
                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eSampled
                         )
                         .EnableMips();

    for (vk::Format format : ScalingFormats)
    {
        const auto blitFlags = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
        if ((DeviceContext::GetPhysical().getFormatProperties(format).optimalTilingFeatures & blitFlags) !=
            blitFlags)
            continue;

        m_ScalingImages[format] = m_ImageBuilder.SetFormat(format).CreateImage(
            MaxTextureDataSize, std::format("Scaling Image {}", vk::to_string(format))
        );
    }

    logger::info("Max Texture Data Size: {}x{}", MaxTextureDataSize.width, MaxTextureDataSize.height);
}

TextureUploader::~TextureUploader()
{
    Cancel();
}

void TextureUploader::UploadTexturesBlocking(const Scene &scene)
{
    auto textures = scene.GetTextures();
    if (textures.empty())
        return;

    DetermineMaxTextureSizes(textures.size(), scene.GetForceFullTextureSize());

    for (uint32_t i = 0; i < textures.size(); i++)
    {
        const Buffer &buffer = m_FreeBuffers.front();
        const TextureInfo &textureInfo = textures[i];

        UploadToBuffer(textureInfo, buffer);

        Renderer::s_MainCommandBuffer->Begin();
        UploadTexture(
            Renderer::s_MainCommandBuffer->Buffer, Renderer::s_MainCommandBuffer->Buffer, textureInfo, i,
            buffer
        );
        Renderer::s_MainCommandBuffer->SubmitBlocking();

        Renderer::UpdateTexture(Shaders::GetSceneTextureIndex(i));
        logger::debug("Uploaded Texture: {}", textureInfo.Name);
    }
}

void TextureUploader::UploadTextures(const std::shared_ptr<const Scene> &scene)
{
    Cancel();

    auto textures = scene->GetTextures();
    if (textures.empty())
        return;

    DetermineMaxTextureSizes(textures.size(), scene->GetForceFullTextureSize());
    Application::AddBackgroundTask(BackgroundTaskType::TextureUpload, textures.size());
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

Image TextureUploader::UploadFromRawContentBlocking(
    std::span<const std::byte> content, TextureType type, TextureFormat format, vk::Extent2D extent,
    std::string &&name
)
{
    Image image = ImageBuilder()
                      .SetFormat(GetImageFormat(type, format))
                      .SetUsageFlags(
                          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                          vk::ImageUsageFlagBits::eTransferSrc
                      )
                      .EnableMips()
                      .CreateImage(extent, name);

    std::array<BufferContent, 1> contents = { content };
    Renderer::s_StagingBuffer->UploadToImage(contents, image, vk::ImageLayout::eTransferDstOptimal);
    Renderer::s_MainCommandBuffer->Begin();
    image.GenerateFullMips(Renderer::s_MainCommandBuffer->Buffer, vk::ImageLayout::eShaderReadOnlyOptimal);
    Renderer::s_MainCommandBuffer->SubmitBlocking();

    return image;
}

Image TextureUploader::UploadSingleBlocking(TextureSourceVariant source, TextureType type, std::string &&name)
{
    TextureInfo info = TextureImporter::GetTextureInfo(std::move(source), type, std::move(name));
    assert(info.Levels == 1);

    TextureData data = TextureImporter::LoadTextureData(info);
    vk::Extent2D extent(info.Width, info.Height);

    Image image = UploadFromRawContentBlocking(
        data, type, info.Format, extent, std::move(info.Name)
    );

    TextureImporter::ReleaseTextureData(info, data);

    return image;
}

Image TextureUploader::UploadSkyboxBlocking(const Skybox2D &skybox)
{
    vk::Extent2D extent(skybox.Content.Width, skybox.Content.Height);

    assert(std::has_single_bit(extent.width) && std::has_single_bit(extent.height));
    assert(skybox.Content.Type == TextureType::Skybox);

    vk::Format format = GetImageFormat(skybox.Content.Type, skybox.Content.Format);

    auto image = ImageBuilder()
                     .SetFormat(format)
                     .SetUsageFlags(
                         vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                         vk::ImageUsageFlagBits::eTransferSrc
                     )
                     .CreateImage(extent, "Skybox 2D");

    const TextureInfo &textureInfo = skybox.Content;
    assert(textureInfo.Levels == 1);

    TextureData data = TextureImporter::LoadTextureData(textureInfo);

    std::array<BufferContent, 1> contents = { data };

    Renderer::s_StagingBuffer->UploadToImage(contents, image);
    TextureImporter::ReleaseTextureData(textureInfo, data);

    return image;
}

Image TextureUploader::UploadSkyboxBlocking(const SkyboxCube &skybox)
{
    std::array<const TextureInfo *, 6> textureInfos = { &skybox.Front, &skybox.Back, &skybox.Up,
                                                        &skybox.Down,  &skybox.Left, &skybox.Right };

    TextureType type = textureInfos[0]->Type;
    assert(type == TextureType::Skybox);
    vk::Format format = GetImageFormat(textureInfos[0]->Type, textureInfos[0]->Format);

    vk::Extent2D extent(textureInfos[0]->Width, textureInfos[0]->Height);
    assert(std::has_single_bit(extent.width) && std::has_single_bit(extent.height));

    auto image = ImageBuilder()
                     .SetFormat(format)
                     .SetUsageFlags(
                         vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                         vk::ImageUsageFlagBits::eTransferSrc
                     )
                     .EnableCube()
                     .CreateImage(extent, "Skybox Cube");

    std::array<TextureData, 6> data = {};
    std::array<BufferContent, 6> contents = {};

    for (int i = 0; i < textureInfos.size(); i++)
    {
        assert(textureInfos[i]->Width == extent.width && textureInfos[i]->Height == extent.height);
        assert(textureInfos[i]->Type == type);
        assert(textureInfos[i]->Levels == 0);
        data[i] = TextureImporter::LoadTextureData(*textureInfos[i]);
        contents[i] = data[i];
    }

    Renderer::s_StagingBuffer->UploadToImage(contents, image);

    for (int i = 0; i < contents.size(); i++)
        TextureImporter::ReleaseTextureData(*textureInfos[i], data[i]);

    return image;
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
                    const uint32_t sceneTextureIndex = Shaders::GetSceneTextureIndex(textureIndex);
                    if (m_Textures[sceneTextureIndex].GetHandle() == nullptr)  // Upload failed
                        continue;
                    Renderer::UpdateTexture(sceneTextureIndex);
                    logger::debug("Uploaded Texture: {}", textures[textureIndex].Name);
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
    TextureData data = TextureImporter::LoadTextureData(textureInfo);

    const vk::Extent2D extent(textureInfo.Width, textureInfo.Height);

    assert(Utils::LteExtent(extent, MaxTextureDataSize));
    assert(data.size() <= buffer.GetSize());

    buffer.Upload(data);

    TextureImporter::ReleaseTextureData(textureInfo, data);
}

void TextureUploader::UploadTexture(
    vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const TextureInfo &texture,
    uint32_t textureIndex, const Buffer &buffer
)
{
    const vk::Format format = GetImageFormat(texture.Type, texture.Format);
    m_ImageBuilder.SetFormat(format);

    const vk::Extent2D maxExtent = m_MaxTextureSize.at(format);
    const uint32_t scale = std::max(
        std::ceil(static_cast<float>(texture.Width) / maxExtent.width),
        std::ceil(static_cast<float>(texture.Height) / maxExtent.height)
    );
    const uint32_t width = std::max(texture.Width / scale, 1u);
    const uint32_t height = std::max(texture.Height / scale, 1u);

    const vk::Extent2D originalExtent(texture.Width, texture.Height);
    const vk::Extent2D extent(width, height);

    Image image = m_ImageBuilder.CreateImage(extent, texture.Name);
    if (Utils::LteExtent(originalExtent, maxExtent))
    {
        if (texture.Levels != image.GetMipLevels() && !m_ScalingImages.contains(format) && texture.Levels == 1)
        {
            logger::warn(
                "Texture {} has only one mip map and mips can't be generated for it since it's format "
                "doesn't support it", texture.Name
            );
            image = ImageBuilder()
                        .SetUsageFlags(
                            vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                            vk::ImageUsageFlagBits::eSampled
                        )
                        .SetFormat(format)
                        .CreateImage(extent, texture.Name);
            image.UploadFromBuffer(transferBuffer, buffer, 0, originalExtent, 0, texture.Levels);
        }
        else
        {
            image.UploadFromBuffer(transferBuffer, buffer, 0, originalExtent, 0, texture.Levels);

            if (texture.Levels != image.GetMipLevels())
            {
                if (!m_ScalingImages.contains(format))
                {
                    logger::error(
                        "Could not upload texture {} because it requires generating mip maps and "
                        "it's format doesn't support the blit operation",
                        texture.Name
                    );
                    m_RejectedCount++;
                    Application::IncrementBackgroundTaskDone(BackgroundTaskType::TextureUpload);
                    return;
                }

                image.GenerateFullMips(mipBuffer, vk::ImageLayout::eTransferDstOptimal);
            }
        }
    }
    else
    {
        if (texture.Levels == 1)
        {
            if (!m_ScalingImages.contains(format))
            {
                logger::error(
                    "Could not upload texture {} it requires scaling since "
                    "it's size ({}x{}) is greater than the max texture size ({}x{}) "
                    "it's format ({}) doesn't support the blit operation",
                    texture.Name, originalExtent.width, originalExtent.height, maxExtent.width,
                    maxExtent.height, vk::to_string(format)
                );
                m_RejectedCount++;
                Application::IncrementBackgroundTaskDone(BackgroundTaskType::TextureUpload);
                return;
            }

            const Image &temporary = m_ScalingImages.at(format);
            const uint32_t fromMip = temporary.GetMip(originalExtent), toMip = temporary.GetMip(extent);
            temporary.UploadFromBuffer(transferBuffer, buffer, 0, originalExtent, fromMip, 1);
            temporary.TransitionWithQueueChange(
                transferBuffer, mipBuffer, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands,
                vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eNone,
                DeviceContext::GetTransferQueue().FamilyIndex, DeviceContext::GetMipQueue().FamilyIndex
            );
            temporary.GenerateMips(mipBuffer, vk::ImageLayout::eTransferSrcOptimal, fromMip, toMip);

            temporary.CopyMipTo(mipBuffer, image, toMip);
            image.GenerateFullMips(mipBuffer, vk::ImageLayout::eTransferDstOptimal);
        }
        else
        {
            assert(texture.Levels >= image.GetMipLevels());
            vk::DeviceSize offset = 0;
            for (uint32_t mip = 0; mip < texture.Levels - image.GetMipLevels(); mip++)
                offset += Image::GetSize(Image::GetMipExtent(originalExtent, mip), image.GetFormat());

            image.UploadFromBuffer(
                transferBuffer, buffer, offset, image.GetExtent(), 0, image.GetMipLevels()
            );
        }
    }

    // Release barrier
    image.TransitionWithQueueChange(
        mipBuffer, nullptr, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
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

void TextureUploader::DetermineMaxTextureSizes(size_t textureCount, bool forceFullSize)
{
    const size_t textureBudget = GetTextureBudget();
    const size_t perTextureBudget = textureBudget / textureCount;

    for (vk::Format format : SupportedFormats)
    {
        vk::Extent2D maxExtent = MaxTextureDataSize;
        if (!forceFullSize)
        {
            while (Image::GetTextureMemoryRequirement(maxExtent, format) > perTextureBudget)
            {
                maxExtent.height /= 2;
                maxExtent.width /= 2;
            }
        }
        m_MaxTextureSize[format] = maxExtent;
    }
}

vk::Format TextureUploader::GetImageFormat(TextureType type, TextureFormat format)
{
    // We assume that color textures are in srgb space
    // We assume all other textures to be in linear space
    auto isColorTexture = [](TextureType type) {
        return type == TextureType::Color || type == TextureType::Emisive || type == TextureType::Skybox;
    };

    switch (format)
    {
    case TextureFormat::RGBAU8:
        return isColorTexture(type) ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
    case TextureFormat::RGBAF32:
        return vk::Format::eR32G32B32A32Sfloat;
    case TextureFormat::BC1:
        return isColorTexture(type) ? vk::Format::eBc1RgbaSrgbBlock : vk::Format::eBc1RgbaUnormBlock;
    case TextureFormat::BC3:
        return vk::Format::eBc3SrgbBlock;
    case TextureFormat::BC5:
        return vk::Format::eBc5UnormBlock;
    default:
        throw error("Unsupported texture format");
    }
}

}
