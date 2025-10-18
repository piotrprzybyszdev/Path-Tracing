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
#include "Pipeline.h"
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
    static void UpdateSpecializations(Shaders::SpecializationData data);

    static float s_Exposure;

    static std::unique_ptr<CommandBuffer> s_MainCommandBuffer;
    static std::unique_ptr<Buffer> s_StagingBuffer;

public:
    // Lock s_DescriptorSetMutex before calling (if not on main thread)
    static void UpdateTexture(uint32_t index);

private:
    static const Swapchain *s_Swapchain;

    static struct ShaderIds
    {
        ShaderId Raygen = ShaderLibrary::g_UnusedShaderId;
        ShaderId Miss = ShaderLibrary::g_UnusedShaderId;
        ShaderId ClosestHit = ShaderLibrary::g_UnusedShaderId;
        ShaderId AnyHit = ShaderLibrary::g_UnusedShaderId;
        ShaderId OcclusionMiss = ShaderLibrary::g_UnusedShaderId;
        ShaderId SkinningCompute = ShaderLibrary::g_UnusedShaderId;
    } s_Shaders;

    static struct RaytracingConfig
    {
        uint32_t RaygenGroupIndex = -1;
        uint32_t PrimaryRayMissIndex = -1;
        uint32_t OcclusionRayMissIndex = -1;
        uint32_t PrimaryRayHitIndex = -1;
        uint32_t OcclusionRayHitIndex = -1;
        uint32_t HitGroupCount = 2;
    } s_RaytracingConfig;


    static Shaders::SpecializationData s_ShaderSpecialization;

    struct RenderingResources
    {
        vk::CommandPool CommandPool;
        vk::CommandBuffer CommandBuffer;

        Image StorageImage;

        Buffer RaygenUniformBuffer;

        static inline constexpr vk::DeviceSize s_DirectionalLightOffset =
            Utils::AlignTo(sizeof(Shaders::uint), Shaders::DirectionalLightStructAlignment);
        static inline constexpr vk::DeviceSize s_LightArrayOffset = Utils::AlignTo(
            s_DirectionalLightOffset + sizeof(Shaders::DirectionalLight), Shaders::PointLightStructAlignment
        );
        Shaders::uint LightCount = 0;
        Buffer LightUniformBuffer;

        Buffer BoneTransformUniformBuffer;
        Buffer OutAnimatedVertexBuffer;
        Buffer GeometryBuffer;

        std::unique_ptr<AccelerationStructure> SceneAccelerationStructure = nullptr;
    };

    static std::vector<RenderingResources> s_RenderingResources;

    struct SceneData
    {
        std::shared_ptr<Scene> Handle = nullptr;

        Buffer VertexBuffer;
        Buffer IndexBuffer;
        
        Buffer AnimatedVertexBuffer;
        Buffer AnimatedVertexMapBuffer;
        Buffer AnimatedIndexBuffer;
        
        Buffer TransformBuffer;
        Buffer MaterialBuffer;

        Image Skybox;

        std::vector<Shaders::Vertex> OutBindPoseAnimatedVertices;
        uint32_t AnimatedGeometriesOffset = 0;
        std::vector<Shaders::Geometry> Geometries;

        std::vector<Image> Textures;
        std::vector<uint32_t> TextureMap;

        std::unique_ptr<ShaderBindingTable> SceneShaderBindingTable = nullptr;
    };

    static std::unique_ptr<SceneData> s_SceneData;

    static std::vector<Image> s_Textures;
    static std::vector<uint32_t> s_TextureMap;

    static std::mutex s_DescriptorSetMutex;
    static std::unique_ptr<TextureUploader> s_TextureUploader;
    static std::unique_ptr<CommandBuffer> s_TextureOwnershipCommandBuffer;
    static bool s_TextureOwnershipBufferHasCommands;

    static std::unique_ptr<RaytracingPipeline> s_RaytracingPipeline;
    static std::unique_ptr<ComputePipeline> s_SkinningPipeline;

    static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;

private:
    static Buffer CreateDeviceBuffer(BufferContent content, std::string &&name);

    static uint32_t AddDefaultTexture(glm::u8vec4 value, std::string &&name);

    static void CreatePipelines();

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

    static vk::Sampler s_TextureSampler;
};

}
