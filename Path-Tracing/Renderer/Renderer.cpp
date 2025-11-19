#include <vulkan/vulkan.hpp>

#include <memory>

#include "Core/Core.h"

#include "Shaders/Debug/DebugShaderTypes.incl"
#include "Shaders/ShaderRendererTypes.incl"

#include "Application.h"
#include "Resources.h"
#include "UserInterface.h"

#include "CommandBuffer.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "SceneManager.h"
#include "Utils.h"

namespace PathTracing
{

Renderer::Settings Renderer::s_Settings = {};

const Swapchain *Renderer::s_Swapchain = nullptr;

Renderer::ShaderIds Renderer::s_Shaders = {};
Renderer::ShaderConfig Renderer::s_PathTracingShaderConfig = {};
Renderer::ShaderConfig Renderer::s_DebugRayTracingShaderConfig = {};
Renderer::ShaderConfig *Renderer::s_ActiveShaderConfig = nullptr;

PathTracingPipelineConfig Renderer::s_PathTracingPipelineConfig = {};
DebugRaytracingPipelineConfig Renderer::s_DebugRayTracingPipelineConfig = {};

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};

std::unique_ptr<CommandBuffer> Renderer::s_MainCommandBuffer = nullptr;
std::unique_ptr<StagingBuffer> Renderer::s_StagingBuffer = nullptr;

std::unique_ptr<Renderer::SceneData> Renderer::s_SceneData = nullptr;

std::vector<Image> Renderer::s_Textures = {};
std::vector<uint32_t> Renderer::s_TextureMap = {};

std::mutex Renderer::s_DescriptorSetMutex = {};
std::unique_ptr<TextureUploader> Renderer::s_TextureUploader = nullptr;
std::unique_ptr<CommandBuffer> Renderer::s_TextureOwnershipCommandBuffer = nullptr;
bool Renderer::s_TextureOwnershipBufferHasCommands = false;

std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;
std::unique_ptr<RaytracingPipeline> Renderer::s_PathTracingPipeline = nullptr;
std::unique_ptr<RaytracingPipeline> Renderer::s_DebugRayTracingPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_SkinningPipeline = nullptr;
RaytracingPipeline *Renderer::s_ActiveRayTracingPipeline = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;

vk::Sampler Renderer::s_TextureSampler = nullptr;

void Renderer::Init(const Swapchain *swapchain)
{
    s_Swapchain = swapchain;

    try
    {
        CreatePipelines();
        s_ActiveRayTracingPipeline = s_PathTracingPipeline.get();
        s_ActiveShaderConfig = &s_PathTracingShaderConfig;
    }
    catch (const error &error)
    {
        logger::critical("Pipeline creation failed - Renderer can't be initialized");
        s_ShaderLibrary.reset();
        throw;
    }

    s_MainCommandBuffer = std::make_unique<CommandBuffer>(DeviceContext::GetGraphicsQueue());

    s_StagingBuffer = std::make_unique<StagingBuffer>(
        Application::GetConfig().MaxStagingBufferSize, "Main Staging Buffer", *s_MainCommandBuffer
    );

    s_BufferBuilder = std::make_unique<BufferBuilder>();
    s_ImageBuilder = std::make_unique<ImageBuilder>();

    {
        vk::SamplerCreateInfo createInfo(vk::SamplerCreateFlags(), vk::Filter::eLinear, vk::Filter::eLinear);
        createInfo.setMipmapMode(vk::SamplerMipmapMode::eLinear);
        createInfo.setMaxLod(vk::LodClampNone);
        s_TextureSampler = DeviceContext::GetLogical().createSampler(createInfo);
        Utils::SetDebugName(s_TextureSampler, "Texture Sampler");
    }

    s_TextureUploader = std::make_unique<TextureUploader>(s_Textures, s_DescriptorSetMutex);
    s_TextureOwnershipCommandBuffer = std::make_unique<CommandBuffer>(DeviceContext::GetGraphicsQueue());

    {
        uint32_t colorIndex = AddTexture(
            Shaders::DefaultTextureColor, TextureType::Color, TextureFormat::RGBAU8, { 1, 1 },
            "Default Color Texture"
        );
        uint32_t normalIndex = AddTexture(
            Shaders::DefaultTextureNormal, TextureType::Normal, TextureFormat::RGBAU8, { 1, 1 },
            "Default Normal Texture"
        );
        uint32_t roughnessIndex = AddTexture(
            Shaders::DefaultTextureRoughness, TextureType::Roughness, TextureFormat::RGBAU8, { 1, 1 },
            "Default Roughness Texture"
        );
        uint32_t metalicIndex = AddTexture(
            Shaders::DefaultTextureMetalness, TextureType::Metalic, TextureFormat::RGBAU8, { 1, 1 },
            "Default Metalic Texture"
        );
        uint32_t emissiveIndex = AddTexture(
            Shaders::DefaultTextureEmissive, TextureType::Emisive, TextureFormat::RGBAU8, { 1, 1 },
            "Default Emissive Texture"
        );
        uint32_t placeholderIndex =
            AddTexture(Resources::g_PlaceholderTextureData, TextureType::Color, "Placeholder Texture");

        s_TextureMap.resize(Shaders::SceneTextureOffset);
        s_TextureMap[Shaders::DefaultColorTextureIndex] = colorIndex;
        s_TextureMap[Shaders::DefaultNormalTextureIndex] = normalIndex;
        s_TextureMap[Shaders::DefaultRoughnessTextureIndex] = roughnessIndex;
        s_TextureMap[Shaders::DefaultMetalicTextureIndex] = metalicIndex;
        s_TextureMap[Shaders::DefaultEmissiveTextureIndex] = emissiveIndex;
        s_TextureMap[Shaders::PlaceholderTextureIndex] = placeholderIndex;
    }
}

void Renderer::Shutdown()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();

    s_TextureUploader.reset();
    s_TextureOwnershipCommandBuffer.reset();

    for (RenderingResources &res : s_RenderingResources)
        DeviceContext::GetLogical().destroyCommandPool(res.CommandPool);
    s_RenderingResources.clear();

    s_SkinningPipeline.reset();
    s_DebugRayTracingPipeline.reset();
    s_PathTracingPipeline.reset();
    s_ShaderLibrary.reset();

    s_TextureMap.clear();
    s_Textures.clear();

    s_SceneData.reset();

    DeviceContext::GetLogical().destroySampler(s_TextureSampler);
    s_ImageBuilder.reset();
    s_BufferBuilder.reset();

    s_StagingBuffer.reset();

    s_MainCommandBuffer.reset();
}

void Renderer::UpdateSceneData(bool updated)
{
    if (updated)
        ResetAccumulationImage();

    if (s_SceneData != nullptr && s_SceneData->Handle == SceneManager::GetActiveScene())
        return;

    DeviceContext::GetGraphicsQueue().WaitIdle();
    s_TextureUploader->Cancel();
    s_SceneData = std::make_unique<SceneData>(SceneManager::GetActiveScene());

    {
        Timer timer("Mesh Upload");
        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &vertices = s_SceneData->Handle->GetVertices();
        s_SceneData->VertexBuffer = CreateDeviceBufferUnflushed(vertices, "Vertex Buffer");

        const auto &indices = s_SceneData->Handle->GetIndices();
        s_SceneData->IndexBuffer = CreateDeviceBufferUnflushed(indices, "Index Buffer");

        const auto &animatedIndices = s_SceneData->Handle->GetAnimatedIndices();
        s_SceneData->AnimatedIndexBuffer =
            CreateDeviceBufferUnflushed(animatedIndices, "Animated Index Buffer");

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &transforms = s_SceneData->Handle->GetTransforms();
        s_SceneData->TransformBuffer = CreateDeviceBufferUnflushed(transforms, "Transform Buffer");

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &animVertices = s_SceneData->Handle->GetAnimatedVertices();
        s_SceneData->AnimatedVertexBuffer =
            CreateDeviceBufferUnflushed(animVertices, "Animated Vertex Buffer");

        const auto &geometries = s_SceneData->Handle->GetGeometries();

        uint32_t animatedGeometryCount = 0;
        std::vector<uint32_t> animatedVertexMap;

        const auto &instances = s_SceneData->Handle->GetModelInstances();
        const auto &models = s_SceneData->Handle->GetModels();
        for (const auto &instance : instances)
            for (const auto &mesh : models[instance.ModelIndex].Meshes)
            {
                const auto &geometry = geometries[mesh.GeometryIndex];
                if (geometry.IsAnimated)
                {
                    for (int i = 0; i < geometry.VertexLength; i++)
                    {
                        const auto &animVertex = animVertices[geometry.VertexOffset + i];
                        s_SceneData->OutBindPoseAnimatedVertices.emplace_back(
                            animVertex.Position, animVertex.TexCoords, animVertex.Normal, animVertex.Tangent,
                            animVertex.Bitangent
                        );
                        animatedVertexMap.push_back(geometry.VertexOffset + i);
                    }

                    animatedGeometryCount++;
                }
            }

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
        );

        s_SceneData->AnimatedVertexMapBuffer =
            CreateDeviceBufferUnflushed(std::span(animatedVertexMap), "Animated Vertex Map Buffer");

        const auto &metalicRoughnessMaterials = s_SceneData->Handle->GetMetalicRoughnessMaterials();
        s_SceneData->MetalicRoughnessMaterialBuffer =
            CreateDeviceBufferUnflushed(metalicRoughnessMaterials, "MetalicRoughness Material Buffer");

        const auto &specularGlossinessMaterials = s_SceneData->Handle->GetSpecularGlossinessMaterials();
        s_SceneData->SpecularGlossinessMaterialBuffer =
            CreateDeviceBufferUnflushed(specularGlossinessMaterials, "SpecularGlossiness Material Buffer");

        // Ensure all scene buffers are flushed to device memory
        s_StagingBuffer->Flush();

        std::vector<uint32_t> geometryIndexMap;
        s_SceneData->Geometries.clear();
        s_SceneData->Geometries.reserve(geometries.size() + animatedGeometryCount);
        geometryIndexMap.resize(geometries.size() + animatedGeometryCount);

        for (int i = 0; i < geometries.size(); i++)
        {
            const auto &geometry = geometries[i];
            if (!geometry.IsAnimated)
            {
                s_SceneData->Geometries.emplace_back(
                    s_SceneData->VertexBuffer.GetDeviceAddress() +
                        geometry.VertexOffset * sizeof(Shaders::Vertex),
                    s_SceneData->IndexBuffer.GetDeviceAddress() + geometry.IndexOffset * sizeof(uint32_t)
                );
                geometryIndexMap[i] = s_SceneData->Geometries.size() - 1;
            }
        }

        // TODO: Support instancing animated meshes: map out animated meshes to source animated meshes
        s_SceneData->AnimatedGeometriesOffset = s_SceneData->Geometries.size();
        uint32_t animatedVertexOffset = 0, animatedGeometryIndex = 0;
        for (const auto &instance : instances)
        {
            for (const auto &mesh : models[instance.ModelIndex].Meshes)
            {
                if (geometries[mesh.GeometryIndex].IsAnimated)
                {
                    const auto &geometry = geometries[mesh.GeometryIndex];
                    s_SceneData->Geometries.emplace_back(
                        0 + animatedVertexOffset * sizeof(Shaders::Vertex),
                        s_SceneData->AnimatedIndexBuffer.GetDeviceAddress() +
                            geometry.IndexOffset * sizeof(uint32_t)
                    );

                    animatedVertexOffset += geometry.VertexLength;
                    geometryIndexMap[mesh.GeometryIndex] = s_SceneData->Geometries.size() - 1;
                    animatedGeometryIndex++;
                }
            }
        }

        for (int i = 0; i < s_RenderingResources.size(); i++)
            CreateSceneRenderingResources(s_RenderingResources[i], i);

        s_SceneData->SceneShaderBindingTable =
            std::make_unique<ShaderBindingTable>(s_ActiveShaderConfig->HitGroupCount);

        for (const auto &model : models)
            for (const auto &mesh : model.Meshes)
            {
                const Shaders::SBTBuffer data(
                    geometryIndexMap[mesh.GeometryIndex], mesh.MaterialIndex, mesh.TransformBufferOffset
                );

                std::array<SBTEntryInfo, 2> entries = {};
                entries[Shaders::PrimaryRayHitGroupIndex] = SBTEntryInfo {
                    .Buffer = data,
                };
                entries[Shaders::OcclusionRayHitGroupIndex] = SBTEntryInfo {
                    .HitGroupIndex = s_ActiveShaderConfig->OcclusionRayHitIndex,
                    .Buffer = data,
                };

                switch (mesh.ShaderMaterialType)
                {
                case MaterialType::MetalicRoughness:
                    entries[Shaders::PrimaryRayHitGroupIndex].HitGroupIndex =
                        s_ActiveShaderConfig->PrimaryRayMetalicRoughnessHitIndex;
                    break;
                case MaterialType::SpecularGlossiness:
                    entries[Shaders::PrimaryRayHitGroupIndex].HitGroupIndex =
                        s_ActiveShaderConfig->PrimaryRaySpecularGlossinessHitIndex;
                    break;
                }

                s_SceneData->SceneShaderBindingTable->AddRecord(entries);
            }
    }

    const auto &skybox = s_SceneData->Handle->GetSkybox();
    auto updateMissFlags = [&skybox](Shaders::SpecializationConstant &flags) {
        flags &= ~(Shaders::MissFlagsSkybox2D | Shaders::MissFlagsSkyboxCube);
        switch (skybox.index())
        {
        case 0:
            break;
        case 1:
            s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<Skybox2D>(skybox));
            flags |= Shaders::MissFlagsSkybox2D;
            break;
        case 2:
            s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<SkyboxCube>(skybox));
            flags |= Shaders::MissFlagsSkyboxCube;
            break;
        default:
            throw error("Unhandled skybox type");
        }
    };

    updateMissFlags(s_PathTracingPipelineConfig[Shaders::MissFlagsConstantId]);
    updateMissFlags(s_DebugRayTracingPipelineConfig[Shaders::DebugMissFlagsConstantId]);

    const auto &textures = s_SceneData->Handle->GetTextures();

    s_Textures.resize(Shaders::SceneTextureOffset + textures.size());
    s_TextureMap.resize(Shaders::SceneTextureOffset + textures.size());
    for (int i = 0; i < textures.size(); i++)
    {
        const TextureType type = textures[i].Type;
        s_TextureMap[Shaders::GetSceneTextureIndex(i)] = type == TextureType::Color
                                                             ? Shaders::PlaceholderTextureIndex
                                                             : Scene::GetDefaultTextureIndex(type);
    }

    UpdatePipelineSpecializations();

    RecreateDescriptorSet();

    s_TextureOwnershipCommandBuffer->Reset();
    s_TextureOwnershipBufferHasCommands = false;

    s_TextureUploader->UploadTextures(s_SceneData->Handle);
}

void Renderer::UpdateTexture(uint32_t index)
{
    s_TextureMap[index] = index;

    if (s_ActiveRayTracingPipeline->GetDescriptorSet() == nullptr)
        return;

    if (s_TextureOwnershipBufferHasCommands == false)
    {
        s_TextureOwnershipCommandBuffer->Begin();
        s_TextureOwnershipBufferHasCommands = true;
    }

    // Acquire barrier
    s_Textures[index].TransitionWithQueueChange(
        nullptr, s_TextureOwnershipCommandBuffer->Buffer, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eAllCommands,
        vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eNone,
        DeviceContext::GetMipQueue().FamilyIndex, DeviceContext::GetGraphicsQueue().FamilyIndex
    );

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
    {
        s_PathTracingPipeline->GetDescriptorSet()->UpdateImage(
            3, frameIndex, s_Textures[index], s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal, index
        );
        s_DebugRayTracingPipeline->GetDescriptorSet()->UpdateImage(
            3, frameIndex, s_Textures[index], s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal, index
        );
    }
}

Buffer Renderer::CreateDeviceBufferUnflushed(BufferContent content, std::string &&name)
{
    if (content.Size == 0)
        return Buffer();

    auto buffer = s_BufferBuilder->CreateDeviceBuffer(content.Size, name);
    s_StagingBuffer->AddContent(content, buffer.GetHandle());

    return buffer;
}

Buffer Renderer::CreateDeviceBuffer(BufferContent content, std::string &&name)
{
    Buffer buffer = CreateDeviceBufferUnflushed(content, std::move(name));
    s_StagingBuffer->Flush();
    return buffer;
}

uint32_t Renderer::AddTexture(
    uint32_t data, TextureType type, TextureFormat format, vk::Extent2D extent, std::string &&name
)
{
    auto image = s_TextureUploader->UploadFromRawContentBlocking(
        ToByteSpan(data), type, format, extent, std::move(name)
    );
    s_Textures.push_back(std::move(image));
    return s_Textures.size() - 1;
}

uint32_t Renderer::AddTexture(std::span<const uint8_t> data, TextureType type, std::string &&name)
{
    auto image = s_TextureUploader->UploadSingleBlocking(data, type, std::move(name));
    s_Textures.push_back(std::move(image));
    return s_Textures.size() - 1;
}

void Renderer::CreatePipelines()
{
    Timer timer("Pipeline Create");

    s_ShaderLibrary = std::make_unique<ShaderLibrary>();

    s_Shaders.Raygen = s_ShaderLibrary->AddShader("raygen.rgen", vk::ShaderStageFlagBits::eRaygenKHR);
    s_Shaders.Miss = s_ShaderLibrary->AddShader("miss.rmiss", vk::ShaderStageFlagBits::eMissKHR);
    s_Shaders.MetalicRoughnessClosestHit =
        s_ShaderLibrary->AddShader("metalicRoughness.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);
    s_Shaders.SpecularGlossinessClosestHit =
        s_ShaderLibrary->AddShader("specularGlossiness.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);
    s_Shaders.AnyHit = s_ShaderLibrary->AddShader("anyhit.rahit", vk::ShaderStageFlagBits::eAnyHitKHR);
    s_Shaders.OcclusionMiss =
        s_ShaderLibrary->AddShader("occlusion.rmiss", vk::ShaderStageFlagBits::eMissKHR);
    s_Shaders.SkinningCompute =
        s_ShaderLibrary->AddShader("skinning.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.DebugRaygen =
        s_ShaderLibrary->AddShader("Debug/debugRaygen.rgen", vk::ShaderStageFlagBits::eRaygenKHR);
    s_Shaders.DebugMiss =
        s_ShaderLibrary->AddShader("Debug/debugMiss.rmiss", vk::ShaderStageFlagBits::eMissKHR);
    s_Shaders.DebugClosestHit =
        s_ShaderLibrary->AddShader("Debug/debugClosestHit.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);

    s_ShaderLibrary->CompileShaders();

    {
        RaytracingPipelineBuilder builder(*s_ShaderLibrary);

        s_PathTracingShaderConfig.RaygenGroupIndex = builder.AddGeneralGroup(s_Shaders.Raygen);
        s_PathTracingShaderConfig.PrimaryRayMissIndex = builder.AddGeneralGroup(s_Shaders.Miss);
        s_PathTracingShaderConfig.OcclusionRayMissIndex = builder.AddGeneralGroup(s_Shaders.OcclusionMiss);
        s_PathTracingShaderConfig.PrimaryRayMetalicRoughnessHitIndex =
            builder.AddHitGroup(s_Shaders.MetalicRoughnessClosestHit, s_Shaders.AnyHit);
        s_PathTracingShaderConfig.PrimaryRaySpecularGlossinessHitIndex =
            builder.AddHitGroup(s_Shaders.SpecularGlossinessClosestHit, s_Shaders.AnyHit);
        s_PathTracingShaderConfig.OcclusionRayHitIndex =
            builder.AddHitGroup(ShaderLibrary::g_UnusedShaderId, s_Shaders.AnyHit);

        builder.AddHintIsPartial(3, true);
        builder.AddHintIsPartial(6, true);
        builder.AddHintIsPartial(7, true);
        builder.AddHintIsPartial(9, true);
        builder.AddHintIsPartial(10, true);
        builder.AddHintSize(3, Shaders::MaxTextureCount);

        static PathTracingPipelineConfig maxPathTracingConfig = {};
        maxPathTracingConfig[Shaders::MissFlagsConstantId] = Shaders::MissFlagsAll;

        RaytracingPipelineData data(
            Shaders::MaxPayloadSize, Shaders::MaxHitAttributeSize, Shaders::MaxRecursionDepth
        );

        s_PathTracingPipeline = builder.CreatePipelineUnique(maxPathTracingConfig, data);
    }

    {
        RaytracingPipelineBuilder builder(*s_ShaderLibrary);

        s_DebugRayTracingShaderConfig.RaygenGroupIndex = builder.AddGeneralGroup(s_Shaders.DebugRaygen);
        s_DebugRayTracingShaderConfig.PrimaryRayMissIndex = builder.AddGeneralGroup(s_Shaders.DebugMiss);
        s_DebugRayTracingShaderConfig.OcclusionRayMissIndex =
            builder.AddGeneralGroup(s_Shaders.OcclusionMiss);
        s_DebugRayTracingShaderConfig.PrimaryRayMetalicRoughnessHitIndex =
            builder.AddHitGroup(s_Shaders.DebugClosestHit, s_Shaders.AnyHit);
        s_DebugRayTracingShaderConfig.PrimaryRaySpecularGlossinessHitIndex =
            builder.AddHitGroup(s_Shaders.DebugClosestHit, s_Shaders.AnyHit);
        s_DebugRayTracingShaderConfig.OcclusionRayHitIndex =
            builder.AddHitGroup(ShaderLibrary::g_UnusedShaderId, s_Shaders.AnyHit);

        builder.AddHintIsPartial(3, true);
        builder.AddHintIsPartial(6, true);
        builder.AddHintIsPartial(7, true);
        builder.AddHintIsPartial(9, true);
        builder.AddHintIsPartial(10, true);
        builder.AddHintSize(3, Shaders::MaxTextureCount);

        static DebugRaytracingPipelineConfig maxDebugRaytracingConfig = {};
        maxDebugRaytracingConfig[Shaders::DebugRenderModeConstantId] = Shaders::RenderModeMax;
        maxDebugRaytracingConfig[Shaders::DebugRaygenFlagsConstantId] = Shaders::RaygenFlagsAll;
        maxDebugRaytracingConfig[Shaders::DebugMissFlagsConstantId] = Shaders::MissFlagsAll;
        maxDebugRaytracingConfig[Shaders::DebugHitGroupFlagsConstantId] = Shaders::HitGroupFlagsAll;

        RaytracingPipelineData data(
            Shaders::MaxDebugPayloadSize, Shaders::MaxDebugHitAttributeSize, Shaders::MaxDebugRecursionDepth
        );

        s_DebugRayTracingPipeline = builder.CreatePipelineUnique(maxDebugRaytracingConfig, data);
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.SkinningCompute);
        static SkinningPipelineConfig maxSkinningConfig = {};
        s_SkinningPipeline = builder.CreatePipelineUnique(maxSkinningConfig);
    }
}

void Renderer::UpdateShaderBindingTable()
{
    std::array<uint32_t, 2> missGroupIndices = {};
    missGroupIndices[Shaders::PrimaryRayMissGroupIndex] = s_ActiveShaderConfig->PrimaryRayMissIndex;
    missGroupIndices[Shaders::OcclusionRayMissGroupIndex] = s_ActiveShaderConfig->OcclusionRayMissIndex;
    s_SceneData->SceneShaderBindingTable->Upload(
        s_ActiveRayTracingPipeline->GetHandle(), s_ActiveShaderConfig->RaygenGroupIndex, missGroupIndices
    );
}

void Renderer::UpdatePipelineSpecializations()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();
    s_ActiveRayTracingPipeline->CancelUpdate();
    s_SkinningPipeline->CancelUpdate();
    Application::ResetBackgroundTask(BackgroundTaskType::ShaderCompilation);

    if (s_ActiveRayTracingPipeline == s_PathTracingPipeline.get())
        s_ActiveRayTracingPipeline->Update(s_PathTracingPipelineConfig);
    else
        s_ActiveRayTracingPipeline->Update(s_DebugRayTracingPipelineConfig);

    s_SkinningPipeline->Update(SkinningPipelineConfig());
    UpdateShaderBindingTable();
    ResetAccumulationImage();
}

void Renderer::ReloadShaders()
{
    UpdatePipelineSpecializations();
}

void Renderer::SetPathTracingPipeline(PathTracingPipelineConfig config)
{
    s_ActiveRayTracingPipeline = s_PathTracingPipeline.get();
    config[Shaders::MissFlagsConstantId] = s_PathTracingPipelineConfig[Shaders::MissFlagsConstantId];
    s_PathTracingPipelineConfig = config;
    UpdatePipelineSpecializations();
}

void Renderer::SetDebugRaytracingPipeline(DebugRaytracingPipelineConfig config)
{
    s_ActiveRayTracingPipeline = s_DebugRayTracingPipeline.get();
    config[Shaders::DebugMissFlagsConstantId] =
        s_DebugRayTracingPipelineConfig[Shaders::DebugMissFlagsConstantId];
    s_DebugRayTracingPipelineConfig = config;
    UpdatePipelineSpecializations();
}

void Renderer::ResetAccumulationImage()
{
    for (RenderingResources &res : s_RenderingResources)
        res.TotalSamples = 0;
}

void Renderer::SetSettings(const Settings &settings)
{
    s_Settings = settings;
    ResetAccumulationImage();
}

void Renderer::RecordCommandBuffer(const RenderingResources &resources)
{
    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Image image = s_Swapchain->GetCurrentFrame().Image;
    vk::ImageView linearImageView = s_Swapchain->GetCurrentFrame().LinearImageView;
    vk::ImageView nonLinearImageView = s_Swapchain->GetCurrentFrame().NonLinearImageView;
    vk::Extent2D extent = s_Swapchain->GetExtent();

    {
        Utils::DebugLabel label(commandBuffer, "Path tracing pass", { 0.35f, 0.9f, 0.29f, 1.0f });

        commandBuffer.bindPipeline(
            vk::PipelineBindPoint::eRayTracingKHR, s_ActiveRayTracingPipeline->GetHandle()
        );
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR, s_ActiveRayTracingPipeline->GetLayout(), 0,
            { s_ActiveRayTracingPipeline->GetDescriptorSet()->GetSet(
                s_Swapchain->GetCurrentFrameInFlightIndex()
            ) },
            {}
        );

        commandBuffer.traceRaysKHR(
            s_SceneData->SceneShaderBindingTable->GetRaygenTableEntry(),
            s_SceneData->SceneShaderBindingTable->GetMissTableEntry(),
            s_SceneData->SceneShaderBindingTable->GetClosestHitTableEntry(),
            vk::StridedDeviceAddressRegionKHR(), extent.width, extent.height, 1,
            Application::GetDispatchLoader()
        );

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eTransferWrite
        );

        resources.StorageImage.Transition(
            commandBuffer, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal
        );

        auto area = Image::GetMipLevelArea(extent);
        vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        vk::ImageBlit2 imageBlit(subresource, area, subresource, area);

        vk::BlitImageInfo2 blitInfo(
            resources.StorageImage.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, image,
            vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eLinear
        );

        commandBuffer.blitImage2(blitInfo);

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eAttachmentOptimal
        );

        resources.StorageImage.Transition(
            commandBuffer, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral
        );
    }

    {
        Utils::DebugLabel label(commandBuffer, "UI pass", { 0.24f, 0.34f, 0.93f, 1.0f });

        std::array<vk::RenderingAttachmentInfo, 1> colorAttachments = {
            vk::RenderingAttachmentInfo(linearImageView, vk::ImageLayout::eAttachmentOptimal)
        };

        commandBuffer.beginRendering(
            vk::RenderingInfo(vk::RenderingFlags(), vk::Rect2D({}, extent), 1, 0, colorAttachments)
        );
        UserInterface::OnRender(commandBuffer);
        commandBuffer.endRendering();

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR
        );
    }
}

void Renderer::UpdateAnimatedVertices(const RenderingResources &resources)
{
    if (!s_SceneData->Handle->HasSkeletalAnimations())
        return;

    resources.BoneTransformUniformBuffer.Upload(s_SceneData->Handle->GetBoneTransforms());

    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    {
        Utils::DebugLabel label(commandBuffer, "Compute Skinning pass", { 0.32f, 0.20f, 0.92f, 1.0f });

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, s_SkinningPipeline->GetHandle());

        Shaders::SkinningPushConstants pushConstants = {
            s_SceneData->AnimatedVertexBuffer.GetDeviceAddress(),
            resources.OutAnimatedVertexBuffer.GetDeviceAddress(),
        };

        commandBuffer.pushConstants(
            s_SkinningPipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0u,
            sizeof(Shaders::SkinningPushConstants), &pushConstants
        );
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, s_SkinningPipeline->GetLayout(), 0,
            { s_SkinningPipeline->GetDescriptorSet()->GetSet(s_Swapchain->GetCurrentFrameInFlightIndex()) },
            {}
        );

        const uint32_t groupCount = std::ceil(
            s_SceneData->OutBindPoseAnimatedVertices.size() /
            static_cast<float>(Shaders::SkinningShaderGroupSizeX)
        );
        commandBuffer.dispatch(groupCount, 1, 1);

        resources.OutAnimatedVertexBuffer.AddBarrier(
            commandBuffer, vk::PipelineStageFlagBits2::eComputeShader,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR
        );
    }
}

void Renderer::CreateSceneRenderingResources(RenderingResources &res, uint32_t frameIndex)
{
    s_BufferBuilder->ResetFlags().SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer);
    res.LightCount = s_SceneData->Handle->GetPointLights().size();
    res.LightUniformBuffer = s_BufferBuilder->CreateHostBuffer(
        RenderingResources::s_LightArrayOffset + s_SceneData->Handle->GetPointLights().size_bytes(),
        std::format("Light Uniform Buffer {}", frameIndex)
    );

    if (s_SceneData->Handle->HasSkeletalAnimations())
    {
        res.BoneTransformUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            s_SceneData->Handle->GetBoneTransforms(), std::format("Bone Transform Buffer {}", frameIndex)
        );

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst
        );

        res.OutAnimatedVertexBuffer = CreateDeviceBuffer(
            std::span(s_SceneData->OutBindPoseAnimatedVertices),
            std::format("Out Animated Vertex Buffer {}", frameIndex)
        );
    }

    CreateGeometryBuffer(res);
    res.GeometryBuffer.SetDebugName(std::format("Geometry Buffer {}", frameIndex));
    CreateAccelerationStructure(res);
}

Image Renderer::CreateStorageImage(vk::Extent2D extent)
{
    Image image = s_ImageBuilder->ResetFlags()
                      .SetFormat(vk::Format::eR32G32B32A32Sfloat)
                      .SetUsageFlags(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage)
                      .CreateImage(extent);

    s_MainCommandBuffer->Begin();
    image.Transition(s_MainCommandBuffer->Buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
    s_MainCommandBuffer->SubmitBlocking();

    return image;
}

void Renderer::CreateGeometryBuffer(RenderingResources &resources)
{
    auto modifyGeometries = [](vk::DeviceAddress address, int sign) {
        for (int i = s_SceneData->AnimatedGeometriesOffset; i < s_SceneData->Geometries.size(); i++)
            s_SceneData->Geometries[i].Vertices += sign * address;
    };

    if (s_SceneData->Handle->HasSkeletalAnimations())
        modifyGeometries(resources.OutAnimatedVertexBuffer.GetDeviceAddress(), 1);

    s_BufferBuilder->ResetFlags().SetUsageFlags(
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
    );
    resources.GeometryBuffer = CreateDeviceBuffer(std::span(s_SceneData->Geometries), "Geometry Buffer");

    if (s_SceneData->Handle->HasSkeletalAnimations())
        modifyGeometries(resources.OutAnimatedVertexBuffer.GetDeviceAddress(), -1);
}

void Renderer::CreateAccelerationStructure(RenderingResources &resources)
{
    auto getAddress = [](const Buffer &buffer) {
        return buffer.GetHandle() == nullptr ? 0 : buffer.GetDeviceAddress();
    };

    resources.SceneAccelerationStructure = std::make_unique<AccelerationStructure>(
        getAddress(s_SceneData->VertexBuffer), getAddress(s_SceneData->IndexBuffer),
        getAddress(resources.OutAnimatedVertexBuffer), getAddress(s_SceneData->AnimatedIndexBuffer),
        getAddress(s_SceneData->TransformBuffer), s_SceneData->Handle, s_ActiveShaderConfig->HitGroupCount
    );
}

void Renderer::OnResize(vk::Extent2D extent)
{
    for (int i = 0; i < s_RenderingResources.size(); i++)
    {
        RenderingResources &res = s_RenderingResources[i];
        res.TotalSamples = 0;
        res.StorageImage = CreateStorageImage(extent);
        res.StorageImage.SetDebugName(std::format("Storage Image {}", i));

        std::lock_guard lock(s_DescriptorSetMutex);
        s_PathTracingPipeline->GetDescriptorSet()->UpdateImage(
            1, i, res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_DebugRayTracingPipeline->GetDescriptorSet()->UpdateImage(
            1, i, res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
    }
}

void Renderer::OnInFlightCountChange()
{
    while (s_RenderingResources.size() < s_Swapchain->GetInFlightCount())
    {
        RenderingResources res;

        vk::CommandPoolCreateInfo commandPoolCreateInfo(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer, DeviceContext::GetGraphicsQueue().FamilyIndex
        );
        res.CommandPool = DeviceContext::GetLogical().createCommandPool(commandPoolCreateInfo);

        vk::CommandBufferAllocateInfo allocateCommandBufferInfo(
            res.CommandPool, vk::CommandBufferLevel::ePrimary, 1
        );
        res.CommandBuffer = DeviceContext::GetLogical().allocateCommandBuffers(allocateCommandBufferInfo)[0];

        const uint32_t frameIndex = s_RenderingResources.size();

        vk::Extent2D extent = s_Swapchain->GetExtent();
        res.StorageImage = CreateStorageImage(extent);
        res.StorageImage.SetDebugName(std::format("Storage image {}", frameIndex));

        s_BufferBuilder->ResetFlags().SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer);
        res.RaygenUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            sizeof(Shaders::RaygenUniformData), std::format("Raygen Uniform Buffer {}", frameIndex)
        );

        CreateSceneRenderingResources(res, frameIndex);

        s_RenderingResources.push_back(std::move(res));
    }

    RecreateDescriptorSet();
}

void Renderer::RecreateDescriptorSet()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();

    if (s_RenderingResources.empty())
        return;

    std::lock_guard lock(s_DescriptorSetMutex);
    s_PathTracingPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_DebugRayTracingPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_SkinningPipeline->CreateDescriptorSet(s_RenderingResources.size());
    DescriptorSet *skinningDescriptorSet = s_SkinningPipeline->GetDescriptorSet();

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
    {
        const RenderingResources &res = s_RenderingResources[frameIndex];

        auto updateRaytracingDescriptorSet = [&res, frameIndex](
                                                 DescriptorSet *set, Shaders::SpecializationConstant missFlags
                                             ) {
            set->UpdateAccelerationStructures(0, frameIndex, { res.SceneAccelerationStructure->GetTlas() });
            set->UpdateImage(1, frameIndex, res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral);
            set->UpdateBuffer(2, frameIndex, res.RaygenUniformBuffer);
            set->UpdateImageArray(
                3, frameIndex, s_Textures, s_TextureMap, s_TextureSampler,
                vk::ImageLayout::eShaderReadOnlyOptimal
            );
            set->UpdateBuffer(4, frameIndex, s_SceneData->TransformBuffer);
            set->UpdateBuffer(5, frameIndex, res.GeometryBuffer);
            if (s_SceneData->MetalicRoughnessMaterialBuffer.GetHandle() != nullptr)
                set->UpdateBuffer(6, frameIndex, s_SceneData->MetalicRoughnessMaterialBuffer);
            if (s_SceneData->SpecularGlossinessMaterialBuffer.GetHandle() != nullptr)
                set->UpdateBuffer(7, frameIndex, s_SceneData->SpecularGlossinessMaterialBuffer);
            set->UpdateBuffer(8, frameIndex, res.LightUniformBuffer);
            if ((missFlags & Shaders::MissFlagsSkybox2D) != Shaders::MissFlagsNone)
                set->UpdateImage(
                    9, frameIndex, s_SceneData->Skybox, s_TextureSampler,
                    vk::ImageLayout::eShaderReadOnlyOptimal
                );
            if ((missFlags & Shaders::MissFlagsSkyboxCube) != Shaders::MissFlagsNone)
                set->UpdateImage(
                    10, frameIndex, s_SceneData->Skybox, s_TextureSampler,
                    vk::ImageLayout::eShaderReadOnlyOptimal
                );
        };

        updateRaytracingDescriptorSet(
            s_PathTracingPipeline->GetDescriptorSet(),
            s_PathTracingPipelineConfig[Shaders::MissFlagsConstantId]
        );
        updateRaytracingDescriptorSet(
            s_DebugRayTracingPipeline->GetDescriptorSet(),
            s_DebugRayTracingPipelineConfig[Shaders::DebugMissFlagsConstantId]
        );

        if (s_SceneData->Handle->HasSkeletalAnimations())
        {
            skinningDescriptorSet->UpdateBuffer(0, frameIndex, res.BoneTransformUniformBuffer);
            skinningDescriptorSet->UpdateBuffer(1, frameIndex, s_SceneData->AnimatedVertexMapBuffer);
        }
    }
}

void Renderer::OnUpdate(float /* timeStep */)
{
    if (s_RenderingResources.size() < s_Swapchain->GetInFlightCount())
        OnInFlightCountChange();
}

void Renderer::Render()
{
    s_SkinningPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());

    {
        std::lock_guard lock(s_DescriptorSetMutex);
        if (s_TextureOwnershipBufferHasCommands)
        {
            s_TextureOwnershipCommandBuffer->SubmitBlocking();
            s_TextureOwnershipBufferHasCommands = false;
            ResetAccumulationImage();
        }
        s_ActiveRayTracingPipeline->GetDescriptorSet()->FlushUpdate(
            s_Swapchain->GetCurrentFrameInFlightIndex()
        );
    }

    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    RenderingResources &res = s_RenderingResources[s_Swapchain->GetCurrentFrameInFlightIndex()];

    Camera &camera = s_SceneData->Handle->GetActiveCamera();
    camera.OnResize(res.StorageImage.GetExtent().width, res.StorageImage.GetExtent().height);
    Shaders::RaygenUniformData rgenData = { camera.GetInvViewMatrix(), camera.GetInvProjectionMatrix(),
                                            s_Settings.BounceCount,    s_Settings.SampleCount,
                                            res.TotalSamples,          s_Settings.Exposure };

    res.TotalSamples += s_Settings.SampleCount;
    res.RaygenUniformBuffer.Upload(&rgenData);
    res.LightUniformBuffer.Upload(ToByteSpan(res.LightCount));
    res.LightUniformBuffer.Upload(
        ToByteSpan(s_SceneData->Handle->GetDirectionalLight()), RenderingResources::s_DirectionalLightOffset
    );
    if (res.LightCount > 0)
        res.LightUniformBuffer.Upload(
            s_SceneData->Handle->GetPointLights(), RenderingResources::s_LightArrayOffset
        );

    res.CommandBuffer.reset();
    res.CommandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    UpdateAnimatedVertices(res);

    res.SceneAccelerationStructure->Update(res.CommandBuffer);

    RecordCommandBuffer(res);

    res.CommandBuffer.end();

    vk::CommandBufferSubmitInfo cmdInfo(res.CommandBuffer);
    vk::SemaphoreSubmitInfo waitInfo(
        sync.ImageAcquiredSemaphore, 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );
    vk::SemaphoreSubmitInfo signalInfo(
        sync.RenderCompleteSemaphore, 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );
    vk::SubmitInfo2 submitInfo(vk::SubmitFlags(), waitInfo, cmdInfo, signalInfo);

    {
        auto lock = DeviceContext::GetGraphicsQueue().GetLock();
        DeviceContext::GetGraphicsQueue().Handle.submit2({ submitInfo }, sync.InFlightFence);
    }
}

}
