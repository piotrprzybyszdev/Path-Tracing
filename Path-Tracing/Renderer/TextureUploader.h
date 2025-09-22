#pragma once

#include <vulkan/vulkan.hpp>

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
    TextureUploader(
        uint32_t loaderThreadCount, size_t stagingMemoryLimit, std::vector<Image> &textures,
        std::mutex &descriptorSetMutex
    );
    ~TextureUploader();

    TextureUploader(const TextureUploader &) = delete;
    TextureUploader &operator=(const TextureUploader &) = delete;

    void UploadTexturesBlocking(const Scene &scene);
    void UploadTextures(const std::shared_ptr<const Scene> &scene);
    void Cancel();

    Image UploadDefault(glm::u8vec4 value, std::string &&name);
    Image UploadSkyboxBlocking(const Skybox2D &skybox);
    Image UploadSkyboxBlocking(const SkyboxCube &skybox);

public:
    [[nodiscard]] static size_t GetStagingMemoryRequirement(uint32_t numBuffers);

private:
    std::vector<Image> &m_Textures;
    std::mutex &m_DescriptorSetMutex;

    const uint32_t m_LoaderThreadCount, m_StagingBufferCount;

    CommandBuffer m_TransferCommandBuffer;
    CommandBuffer m_MipCommandBuffer;

    const bool m_UseTransferQueue;
    const bool m_ColorTextureScalingSupported;
    const bool m_OtherTextureScalingSupported;

    Image m_TemporaryColorImage;  // for scaling down color textures
    Image m_TemporaryOtherImage;  // for scaling down other textures

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
    static inline constexpr vk::Extent2D MaxTextureSize = { 512u, 512u };
    static inline constexpr vk::Extent2D MaxTextureDataSize = { 4096u, 4096u };
    static inline constexpr size_t StagingBufferSize =
        4ull * MaxTextureDataSize.width * MaxTextureDataSize.height;
    static inline constexpr vk::Format IntermediateColorTextureFormat = vk::Format::eR8G8B8A8Srgb;
    static inline constexpr vk::Format IntermediateOtherTextureFormat = vk::Format::eR8G8B8A8Unorm;

private:
    void SubmitBlocking(const Image &image, const Buffer &buffer, vk::Extent2D extent, TextureType type);

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

    bool CheckCanUpload(const TextureInfo &info);

    bool IsScalingSupported(TextureType type) const;
    const Image &GetTemporaryImage(TextureType type) const;

    static vk::Format SelectTextureFormat(TextureType type);
    static bool CheckBlitSupported(vk::Format format);
};

}
