#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>
#include <mutex>
#include <queue>
#include <vector>

#include "Shaders/ShaderRendererTypes.incl"

#include "AccelerationStructure.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "Image.h"
#include "Pipeline.h"
#include "Scene.h"
#include "ShaderBindingTable.h"
#include "ShaderLibrary.h"
#include "StagingBuffer.h"
#include "Swapchain.h"
#include "TextureUploader.h"

namespace PathTracing
{

using PathTracingPipelineConfig = PipelineConfig<2>;
using DebugRaytracingPipelineConfig = PipelineConfig<4>;
using SkinningPipelineConfig = PipelineConfig<0>;
using PostProcessPipelineConfig = PipelineConfig<0>;

class Renderer
{
public:
    static void Init(const Swapchain *swapchain);
    static void Shutdown();

    static uint32_t GetPreferredImageCount();
    static void UpdateSceneData(const std::shared_ptr<Scene> &scene, bool updated);

    static void OnResize(vk::Extent2D extent);
    static void OnUpdate(float timeStep);
    static void Render();

    static void ReloadShaders();
    static void SetPathTracingPipeline(PathTracingPipelineConfig config);
    static void SetDebugRaytracingPipeline(DebugRaytracingPipelineConfig config);

    struct PathTracingSettings
    {
        uint32_t BounceCount = 4;
    };

    struct PostProcessSettings
    {
        float Exposure = 1.0f;
    };

    static void SetSettings(const PathTracingSettings &settings);
    static void SetSettings(const PostProcessSettings &settings);

    static std::unique_ptr<CommandBuffer> s_MainCommandBuffer;
    static std::unique_ptr<StagingBuffer> s_StagingBuffer;

public:
    // Lock s_DescriptorSetMutex before calling (if not on main thread)
    static void UpdateTexture(uint32_t index);

private:
    static const Swapchain *s_Swapchain;

    static struct ShaderIds
    {
        ShaderId Raygen = ShaderLibrary::g_UnusedShaderId;
        ShaderId Miss = ShaderLibrary::g_UnusedShaderId;
        ShaderId MetallicRoughnessClosestHit = ShaderLibrary::g_UnusedShaderId;
        ShaderId SpecularGlossinessClosestHit = ShaderLibrary::g_UnusedShaderId;
        ShaderId AnyHit = ShaderLibrary::g_UnusedShaderId;
        ShaderId OcclusionMiss = ShaderLibrary::g_UnusedShaderId;
        ShaderId OcclusionAnyHit = ShaderLibrary::g_UnusedShaderId;
        ShaderId SkinningCompute = ShaderLibrary::g_UnusedShaderId;
        ShaderId PostProcessCompute = ShaderLibrary::g_UnusedShaderId;

        ShaderId DebugRaygen = ShaderLibrary::g_UnusedShaderId;
        ShaderId DebugMiss = ShaderLibrary::g_UnusedShaderId;
        ShaderId DebugClosestHit = ShaderLibrary::g_UnusedShaderId;
        ShaderId DebugAnyHit = ShaderLibrary::g_UnusedShaderId;
    } s_Shaders;

    static struct ShaderConfig
    {
        uint32_t RaygenGroupIndex = -1;
        uint32_t PrimaryRayMissIndex = -1;
        uint32_t OcclusionRayMissIndex = -1;
        uint32_t PrimaryRayMetallicRoughnessHitIndex = -1;
        uint32_t PrimaryRaySpecularGlossinessHitIndex = -1;
        uint32_t OcclusionRayHitIndex = -1;
        uint32_t HitGroupCount = 2;
    } s_PathTracingShaderConfig, s_DebugRayTracingShaderConfig;

    static ShaderConfig *s_ActiveShaderConfig;

    static PathTracingPipelineConfig s_PathTracingPipelineConfig;
    static DebugRaytracingPipelineConfig s_DebugRayTracingPipelineConfig;

    struct RenderingResources
    {
        vk::CommandPool CommandPool;
        vk::CommandBuffer CommandBuffer;

        uint32_t TotalSamples = 0;
        Image AccumulationImage;
        Image PostProcessImage;

        Buffer RaygenUniformBuffer;
        Buffer PostProcessUniformBuffer;

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

    static struct RefreshRate
    {
        std::queue<float> Timings;
        float TimeSum = 0.0f;
        float SinceResetSeconds = 0.0f;
        float IncraseThresholdSeconds = 2.0f;
        float DecreaseThresholdSeconds = 1.0f;
        uint32_t SamplesPerFrame = 1;
    } s_RefreshRate;

    static PathTracingSettings s_PathTracingSettings;
    static PostProcessSettings s_PostProcessSettings;

    struct SceneData
    {
        std::shared_ptr<Scene> Handle = nullptr;

        Buffer VertexBuffer;
        Buffer IndexBuffer;
        
        Buffer AnimatedVertexBuffer;
        Buffer AnimatedVertexMapBuffer;
        Buffer AnimatedIndexBuffer;
        
        Buffer TransformBuffer;
        Buffer MetallicRoughnessMaterialBuffer;
        Buffer SpecularGlossinessMaterialBuffer;

        Image Skybox;

        std::vector<Shaders::Vertex> OutBindPoseAnimatedVertices;
        uint32_t AnimatedGeometriesOffset = 0;
        std::vector<Shaders::Geometry> Geometries;

        std::unique_ptr<ShaderBindingTable> SceneShaderBindingTable = nullptr;
    };

    static std::unique_ptr<SceneData> s_SceneData;

    static std::vector<Image> s_Textures;
    static std::vector<uint32_t> s_TextureMap;

    static std::mutex s_DescriptorSetMutex;
    static std::unique_ptr<TextureUploader> s_TextureUploader;
    static std::unique_ptr<CommandBuffer> s_TextureOwnershipCommandBuffer;
    static bool s_TextureOwnershipBufferHasCommands;

    static std::unique_ptr<RaytracingPipeline> s_PathTracingPipeline;
    static std::unique_ptr<RaytracingPipeline> s_DebugRayTracingPipeline;
    static std::unique_ptr<ComputePipeline> s_SkinningPipeline;
    static std::unique_ptr<ComputePipeline> s_PostProcessPipeline;

    static RaytracingPipeline *s_ActiveRayTracingPipeline;

    static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;

private:
    static Buffer CreateDeviceBufferUnflushed(BufferContent content, std::string &&name);
    static Buffer CreateDeviceBuffer(BufferContent content, std::string &&name);

    static uint32_t AddTexture(
        uint32_t data, TextureType type, TextureFormat format, vk::Extent2D extent, std::string &&name
    );
    static uint32_t AddTexture(std::span<const uint8_t> data, TextureType type, std::string &&name);

    static void UpdateScenePipelineConfig();
    static void UpdatePipelineSpecializations();
    static void CreatePipelines();
    static void UpdateShaderBindingTable();
    static void ResetAccumulationImage();

    static void RecordCommandBuffer(const RenderingResources &resources);
    static void UpdateAnimatedVertices(const RenderingResources &resources);

    static void CreateSceneRenderingResources(RenderingResources &res, uint32_t frameIndex);
    static void CreateImageResources(RenderingResources &res, uint32_t frameIndex, vk::Extent2D extent);
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
