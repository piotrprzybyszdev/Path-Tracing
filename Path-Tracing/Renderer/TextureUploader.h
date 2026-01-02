#pragma once

#include <vulkan/vulkan.hpp>

#include <array>
#include <memory>
#include <mutex>
#include <semaphore>
#include <thread>

#include "Scene.h"

#include "Buffer.h"
#include "CommandBuffer.h"
#include "Image.h"

namespace PathTracing
{

class TextureUploader
{
public:
    TextureUploader(std::vector<Image> &textures, std::mutex &descriptorSetMutex);
    ~TextureUploader();

    TextureUploader(const TextureUploader &) = delete;
    TextureUploader &operator=(const TextureUploader &) = delete;

    void UploadTexturesBlocking(const Scene &scene);
    void UploadTextures(const std::shared_ptr<const Scene> &scene);
    void Cancel();

    // These use the Renderer's command buffer and staging buffer
    Image UploadFromRawContentBlocking(
        std::span<const std::byte> data, TextureType type, TextureFormat format, vk::Extent2D extent,
        std::string &&name
    );
    Image UploadSingleBlocking(TextureSourceVariant source, TextureType type, std::string &&name);
    Image UploadSkyboxBlocking(const Skybox2D &skybox);
    Image UploadSkyboxBlocking(const SkyboxCube &skybox);

private:
    std::vector<Image> &m_Textures;
    std::mutex &m_DescriptorSetMutex;

    const uint32_t m_LoaderThreadCount;
    const uint32_t m_StagingBufferCount;

    CommandBuffer m_TransferCommandBuffer;
    CommandBuffer m_MipCommandBuffer;

    const bool m_UseTransferQueue;

    std::unordered_map<vk::Format, Image> m_ScalingImages;
    std::unordered_map<vk::Format, vk::Extent2D> m_MaxTextureSize;

    std::jthread m_SubmitThread;
    ImageBuilder m_ImageBuilder;

    std::vector<std::jthread> m_LoaderThreads;
    std::atomic<uint32_t> m_TextureIndex = 0;
    std::atomic<uint32_t> m_RejectedCount = 0;

    std::counting_semaphore<> m_FreeBuffersSemaphore;  // How many Free Buffers there are
    std::mutex m_FreeBuffersMutex;
    std::vector<Buffer> m_FreeBuffers;

    std::binary_semaphore m_DataBuffersSemaphore;  // Is there at least one Data Buffer
    std::mutex m_DataBuffersMutex;
    std::vector<Buffer> m_DataBuffers;
    std::vector<uint32_t> m_TextureIndices;

private:
    static inline constexpr vk::Extent2D MaxTextureDataSize = { 4096u, 4096u };
    static inline constexpr size_t StagingBufferSize =
        4ull * MaxTextureDataSize.width * MaxTextureDataSize.height;

    static inline constexpr std::array<vk::Format, 2> ScalingFormats = { vk::Format::eR8G8B8A8Unorm,
                                                                         vk::Format::eR8G8B8A8Srgb };
    static inline constexpr std::array<vk::Format, 7> SupportedFormats = {
        vk::Format::eR8G8B8A8Unorm,     vk::Format::eR8G8B8A8Srgb,     vk::Format::eR32G32B32A32Sfloat,
        vk::Format::eBc1RgbaUnormBlock, vk::Format::eBc1RgbaSrgbBlock, vk::Format::eBc3SrgbBlock,
        vk::Format::eBc5UnormBlock
    };

private:
    void DetermineMaxTextureSizes(size_t textureCount);

    void StartLoaderThreads(const std::shared_ptr<const Scene> &scene);
    void StartSubmitThread(const std::shared_ptr<const Scene> &scene);

    void UploadToBuffer(const TextureInfo &textureInfo, const Buffer &buffer, vk::DeviceSize offset = 0);
    void UploadTexture(
        vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const TextureInfo &texture,
        uint32_t textureIndex, const Buffer &buffer
    );

    void UploadBuffers(
        std::span<const TextureInfo> textures, std::span<const uint32_t> textureIndices,
        std::span<const Buffer> buffers
    );
    void UploadBuffersWithTransfer(
        std::span<const TextureInfo> textures, std::span<const uint32_t> textureIndices,
        std::span<const Buffer> buffers
    );

    vk::Format GetImageFormat(TextureType type, TextureFormat format);
};

}
