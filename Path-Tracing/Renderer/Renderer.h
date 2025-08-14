#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

#include "Core/Camera.h"
#include "Core/Core.h"

#include "AccelerationStructure.h"
#include "Buffer.h"
#include "DescriptorSet.h"
#include "Image.h"
#include "Scene.h"
#include "ShaderBindingTable.h"
#include "ShaderLibrary.h"
#include "Swapchain.h"

namespace PathTracing
{

class Renderer
{
public:
    static void Init(const Swapchain *swapchain);
    static void Shutdown();

    static void SetScene(const Scene &scene);

    static void OnResize(vk::Extent2D extent);
    static void OnUpdate(float timeStep);
    static void Render(const Camera &camera);

    static void ReloadShaders();

    static inline constexpr vk::Extent2D s_MaxTextureSize = { 512u, 512u };

    static Shaders::RenderModeFlags s_RenderMode;
    static Shaders::EnabledTextureFlags s_EnabledTextures;
    static Shaders::RaygenFlags s_RaygenFlags;
    static Shaders::MissFlags s_MissFlags;
    static Shaders::ClosestHitFlags s_ClosestHitFlags;

    // Blocking one time submission buffer
    static struct CommandBuffer
    {
        vk::CommandBuffer CommandBuffer;
        vk::Fence Fence;

        void Init();
        void Destroy();

        void Begin() const;
        void Submit(vk::Queue queue) const;
    } s_MainCommandBuffer;

private:
    static const Swapchain *s_Swapchain;

    static vk::CommandPool s_MainCommandPool;

    struct RenderingResources
    {
        vk::CommandPool CommandPool;
        vk::CommandBuffer CommandBuffer;

        std::unique_ptr<Image> StorageImage;

        std::unique_ptr<Buffer> RaygenUniformBuffer;
        std::unique_ptr<Buffer> ClosestHitUniformBuffer;
        std::unique_ptr<Buffer> MissUniformBuffer;
    };

    static std::vector<RenderingResources> s_RenderingResources;

    static struct SceneData
    {
        std::unique_ptr<Buffer> VertexBuffer = nullptr;
        std::unique_ptr<Buffer> IndexBuffer = nullptr;
        std::unique_ptr<Buffer> TransformBuffer = nullptr;

        std::unique_ptr<Buffer> GeometryBuffer = nullptr;
        std::unique_ptr<Buffer> MaterialBuffer = nullptr;

        std::vector<Image> Textures;

        std::unique_ptr<Image> Skybox = nullptr;

        std::unique_ptr<ShaderBindingTable> SceneShaderBindingTable = nullptr;
        std::unique_ptr<AccelerationStructure> SceneAccelerationStructure = nullptr;
    } s_StaticSceneData;

    static std::unique_ptr<DescriptorSetBuilder> s_DescriptorSetBuilder;
    static std::unique_ptr<DescriptorSet> s_DescriptorSet;

    static vk::PipelineLayout s_PipelineLayout;
    static vk::Pipeline s_Pipeline;

    static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;

private:
    static void AddTexture(vk::Extent2D extent, vk::Format format, const std::byte *data);
    static void AddTexture(TextureInfo textureInfo);
    static void AddTexture(TextureInfo textureInfo, const std::string &name);
    
    static void AddSkybox(const Skybox2D &skybox);
    static void AddSkybox(const SkyboxCube &skybox);
    
    static bool SetupPipeline();

    static void RecordCommandBuffer(const RenderingResources &resources);

    static std::unique_ptr<Image> CreateStorageImage(vk::Extent2D extent);
    static void OnInFlightCountChange();
    static void RecreateDescriptorSet();

private:
    static std::unique_ptr<BufferBuilder> s_BufferBuilder;
    static std::unique_ptr<ImageBuilder> s_ImageBuilder;

    static vk::Sampler s_TextureSampler;
};

}
