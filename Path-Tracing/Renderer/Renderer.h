#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>
#include <mutex>
#include <vector>

#include "Core/Camera.h"
#include "Core/Core.h"

#include "AccelerationStructure.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "DescriptorSet.h"
#include "Image.h"
#include "Scene.h"
#include "ShaderBindingTable.h"
#include "ShaderLibrary.h"
#include "Swapchain.h"
#include "TextureUploader.h"

namespace PathTracing
{

class Renderer
{
public:
    static void Init(const Swapchain *swapchain);
    static void Shutdown();

    static void UpdateSceneData();

    static void OnResize(vk::Extent2D extent);
    static void OnUpdate(float timeStep);
    static void Render(const Camera &camera);

    static void ReloadShaders();

    static Shaders::RenderModeFlags s_RenderMode;
    static Shaders::EnabledTextureFlags s_EnabledTextures;
    static Shaders::RaygenFlags s_RaygenFlags;
    static Shaders::MissFlags s_MissFlags;
    static Shaders::ClosestHitFlags s_ClosestHitFlags;
    static float s_Exposure;

    static std::unique_ptr<CommandBuffer> s_MainCommandBuffer;

public:
    // Lock s_DescriptorSetMutex before calling (if not on main thread)
    static void UpdateTexture(uint32_t index);

private:
    static const Swapchain *s_Swapchain;

    struct RenderingResources
    {
        vk::CommandPool CommandPool;
        vk::CommandBuffer CommandBuffer;

        Image StorageImage;

        Buffer RaygenUniformBuffer;
        Buffer MissUniformBuffer;
        Buffer ClosestHitUniformBuffer;

        uint32_t LightCount = 0;
        Buffer LightUniformBuffer;

        Buffer BoneTransformUniformBuffer;
        Buffer OutAnimatedVertexBuffer;
        Buffer GeometryBuffer;

        std::unique_ptr<AccelerationStructure> SceneAccelerationStructure = nullptr;
    };

    static std::vector<RenderingResources> s_RenderingResources;

    struct SceneData
    {
        std::shared_ptr<Scene> Scene = nullptr;

        Buffer VertexBuffer;
        Buffer IndexBuffer;
        
        Buffer AnimatedVertexBuffer;
        Buffer AnimatedVertexMapBuffer;
        Buffer AnimatedIndexBuffer;
        
        Buffer TransformBuffer;
        Buffer MaterialBuffer;

        Image Skybox;

        uint32_t OutAnimatedVertexCount = 0;
        uint32_t AnimatedGeometriesOffset = 0;
        std::vector<Shaders::Geometry> Geometries;

        std::vector<Image> Textures;
        std::vector<uint32_t> TextureMap;

        std::unique_ptr<ShaderBindingTable> SceneShaderBindingTable = nullptr;
    };

    static std::unique_ptr<SceneData> s_SceneData;

    static std::vector<Image> s_Textures;
    static std::vector<uint32_t> s_TextureMap;

    static ShaderId s_SkinningShaderId;

    static std::unique_ptr<DescriptorSetBuilder> s_DescriptorSetBuilder;
    static std::unique_ptr<DescriptorSetBuilder> s_SkinningDescriptorSetBuilder;
    static std::unique_ptr<DescriptorSet> s_DescriptorSet;
    static std::unique_ptr<DescriptorSet> s_SkinningDescriptorSet;

    static std::mutex s_DescriptorSetMutex;
    static std::unique_ptr<TextureUploader> s_TextureUploader;

    static vk::PipelineLayout s_PipelineLayout;
    static vk::PipelineLayout s_SkinningPipelineLayout;
    static vk::Pipeline s_Pipeline;
    static vk::Pipeline s_SkinningPipeline;

    static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;

private:
    static Buffer CreateDeviceBuffer(BufferContent content, std::string &&name);

    static uint32_t AddDefaultTexture(glm::u8vec4 value, std::string &&name);

    static bool SetupPipeline();

    static void RecordCommandBuffer(const RenderingResources &resources);
    static void UpdateAnimatedVertices(const RenderingResources &resources);

    static void CreateSceneRenderingResources(RenderingResources &res, uint32_t frameIndex);
    static Image CreateStorageImage(vk::Extent2D extent);
    static void CreateGeometryBuffer(RenderingResources &resources);
    static void CreateAccelerationStructure(RenderingResources &resources);
    
    static void OnInFlightCountChange();
    static void RecreateDescriptorSet();

private:
    static std::unique_ptr<BufferBuilder> s_BufferBuilder;
    static std::unique_ptr<ImageBuilder> s_ImageBuilder;

    static std::unique_ptr<Buffer> s_StagingBuffer;

    static vk::Sampler s_TextureSampler;
};

}
