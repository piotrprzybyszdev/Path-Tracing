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
#include "Utils.h"

namespace PathTracing
{

Renderer::PathTracingSettings Renderer::s_PathTracingSettings = {};
Renderer::PostProcessSettings Renderer::s_PostProcessSettings = {};
Renderer::RenderSettings Renderer::s_RenderSettings = {};
float Renderer::s_RenderTimeSeconds = 0.0f;
uint32_t Renderer::s_RenderCompletedFrames = 0;

const Swapchain *Renderer::s_Swapchain = nullptr;

Renderer::ShaderIds Renderer::s_Shaders = {};
Renderer::ShaderConfig Renderer::s_PathTracingShaderConfig = {};
Renderer::ShaderConfig Renderer::s_DebugRayTracingShaderConfig = {};
Renderer::ShaderConfig *Renderer::s_ActiveShaderConfig = nullptr;

PathTracingPipelineConfig Renderer::s_PathTracingPipelineConfig = {};
DebugRaytracingPipelineConfig Renderer::s_DebugRayTracingPipelineConfig = {};

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};

Renderer::RefreshRate Renderer::s_RefreshRate = {};

std::unique_ptr<CommandBuffer> Renderer::s_MainCommandBuffer = nullptr;
std::unique_ptr<StagingBuffer> Renderer::s_StagingBuffer = nullptr;

std::unique_ptr<Renderer::SceneData> Renderer::s_SceneData = nullptr;

std::vector<Image> Renderer::s_Textures = {};
std::vector<uint32_t> Renderer::s_TextureMap = {};

std::mutex Renderer::s_DescriptorSetMutex = {};
std::unique_ptr<TextureUploader> Renderer::s_TextureUploader = nullptr;
std::unique_ptr<CommandBuffer> Renderer::s_TextureOwnershipCommandBuffer = nullptr;
bool Renderer::s_TextureOwnershipBufferHasCommands = false;

std::unique_ptr<OutputSaver> Renderer::s_OutputSaver = nullptr;
const Image *Renderer::s_OutputImage = nullptr;

std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;
std::unique_ptr<RaytracingPipeline> Renderer::s_PathTracingPipeline = nullptr;
std::unique_ptr<RaytracingPipeline> Renderer::s_DebugRayTracingPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_SkinningPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_PostProcessPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_CompositionPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_BloomDownsamplePipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_BloomUpsamplePipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_UICompositionPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_ToneMappingPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_UIToneMappingPipeline = nullptr;
RaytracingPipeline *Renderer::s_ActiveRayTracingPipeline = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;

vk::Sampler Renderer::s_TextureSampler = nullptr;
vk::Sampler Renderer::s_BloomSampler = nullptr;

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
        createInfo.setAnisotropyEnable(vk::True);
        float maxSamplerAnisotropy = DeviceContext::GetPhysical().getProperties().limits.maxSamplerAnisotropy;
        createInfo.setMaxAnisotropy(maxSamplerAnisotropy);
        s_TextureSampler = DeviceContext::GetLogical().createSampler(createInfo);
        Utils::SetDebugName(s_TextureSampler, "Texture Sampler");
    }

    {
        vk::SamplerCreateInfo createInfo(vk::SamplerCreateFlags(), vk::Filter::eLinear, vk::Filter::eLinear);
        createInfo.setAddressModeU(vk::SamplerAddressMode::eClampToEdge);
        createInfo.setAddressModeV(vk::SamplerAddressMode::eClampToEdge);
        s_BloomSampler = DeviceContext::GetLogical().createSampler(createInfo);
        Utils::SetDebugName(s_BloomSampler, "Bloom Sampler");
    }

    s_TextureUploader = std::make_unique<TextureUploader>(s_Textures, s_DescriptorSetMutex);
    s_TextureOwnershipCommandBuffer = std::make_unique<CommandBuffer>(DeviceContext::GetGraphicsQueue());

    s_OutputSaver = std::make_unique<OutputSaver>();

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
        uint32_t metallicIndex = AddTexture(
            Shaders::DefaultTextureMetalness, TextureType::Metallic, TextureFormat::RGBAU8, { 1, 1 },
            "Default Metallic Texture"
        );
        uint32_t emissiveIndex = AddTexture(
            Shaders::DefaultTextureEmissive, TextureType::Emisive, TextureFormat::RGBAU8, { 1, 1 },
            "Default Emissive Texture"
        );
        uint32_t specularIndex = AddTexture(
            Shaders::DefaultTextureSpecular, TextureType::Specular, TextureFormat::RGBAU8, { 1, 1 },
            "Default Specular Texture"
        );
        uint32_t glossinessIndex = AddTexture(
            Shaders::DefaultTextureGlossiness, TextureType::Glossiness, TextureFormat::RGBAU8, { 1, 1 },
            "Default Glossiness Texture"
        );
        uint32_t shininessIndex = AddTexture(
            Shaders::DefaultTextureShininess, TextureType::Shininess, TextureFormat::RGBAU8, { 1, 1 },
            "Default Shininess Texture"
        ); 
        uint32_t placeholderIndex =
            AddTexture(Resources::g_PlaceholderTextureData, TextureType::Color, "Placeholder Texture");

        s_TextureMap.resize(Shaders::SceneTextureOffset);
        s_TextureMap[Shaders::DefaultColorTextureIndex] = colorIndex;
        s_TextureMap[Shaders::DefaultNormalTextureIndex] = normalIndex;
        s_TextureMap[Shaders::DefaultRoughnessTextureIndex] = roughnessIndex;
        s_TextureMap[Shaders::DefaultMetallicTextureIndex] = metallicIndex;
        s_TextureMap[Shaders::DefaultEmissiveTextureIndex] = emissiveIndex;
        s_TextureMap[Shaders::DefaultSpecularTextureIndex] = specularIndex;
        s_TextureMap[Shaders::DefaultGlossinessTextureIndex] = glossinessIndex;
        s_TextureMap[Shaders::DefaultShininessTextureIndex] = shininessIndex;
        s_TextureMap[Shaders::PlaceholderTextureIndex] = placeholderIndex;
    }
}

void Renderer::Shutdown()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();

    s_OutputSaver.reset();

    s_TextureUploader.reset();
    s_TextureOwnershipCommandBuffer.reset();

    for (RenderingResources &res : s_RenderingResources)
    {
        DeviceContext::GetLogical().destroyCommandPool(res.CommandPool);
        for (auto view : res.BloomImageViews)
            DeviceContext::GetLogical().destroyImageView(view);
    }
    s_RenderingResources.clear();

    s_UIToneMappingPipeline.reset();
    s_ToneMappingPipeline.reset();
    s_PostProcessPipeline.reset();
    s_UICompositionPipeline.reset();
    s_CompositionPipeline.reset();
    s_BloomDownsamplePipeline.reset();
    s_BloomUpsamplePipeline.reset();
    s_SkinningPipeline.reset();
    s_DebugRayTracingPipeline.reset();
    s_PathTracingPipeline.reset();
    s_ShaderLibrary.reset();

    s_TextureMap.clear();
    s_Textures.clear();

    s_SceneData.reset();

    DeviceContext::GetLogical().destroySampler(s_BloomSampler);
    DeviceContext::GetLogical().destroySampler(s_TextureSampler);
    s_ImageBuilder.reset();
    s_BufferBuilder.reset();

    s_StagingBuffer.reset();

    s_MainCommandBuffer.reset();
}

uint32_t Renderer::GetPreferredImageCount()
{
    if (s_ActiveRayTracingPipeline == s_DebugRayTracingPipeline.get() &&
        s_Swapchain->GetPresentMode() == vk::PresentModeKHR::eMailbox)
        return 3;
    return 2;
}

uint32_t Renderer::GetRenderFramerate()
{
    return s_RenderSettings.Output.Framerate;
}

bool Renderer::CanRenderVideo()
{
    return s_OutputSaver->CanOutputVideo();
}

void Renderer::UpdateSceneData(const std::shared_ptr<Scene> &scene, bool updated)
{
    if (updated)
        ResetAccumulationImage();

    if (s_SceneData != nullptr && s_SceneData->Handle == scene)
        return;

    DeviceContext::GetGraphicsQueue().WaitIdle();
    s_TextureUploader->Cancel();
    s_SceneData = std::make_unique<SceneData>(scene);

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

        const auto &metallicRoughnessMaterials = s_SceneData->Handle->GetMetallicRoughnessMaterials();
        s_SceneData->MetallicRoughnessMaterialBuffer =
            CreateDeviceBufferUnflushed(metallicRoughnessMaterials, "MetallicRoughness Material Buffer");

        const auto &specularGlossinessMaterials = s_SceneData->Handle->GetSpecularGlossinessMaterials();
        s_SceneData->SpecularGlossinessMaterialBuffer =
            CreateDeviceBufferUnflushed(specularGlossinessMaterials, "SpecularGlossiness Material Buffer");

        const auto &phongMaterials = s_SceneData->Handle->GetPhongMaterials();
        s_SceneData->PhongMaterialBuffer =
            CreateDeviceBufferUnflushed(phongMaterials, "Phong Material Buffer");

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
                    .HitGroupIndex = s_ActiveShaderConfig->PrimaryRayHitIndex,
                    .Buffer = data,
                };
                entries[Shaders::OcclusionRayHitGroupIndex] = SBTEntryInfo {
                    .HitGroupIndex = s_ActiveShaderConfig->OcclusionRayHitIndex,
                    .Buffer = data,
                };

                s_SceneData->SceneShaderBindingTable->AddRecord(entries);
            }
    }

    const auto &skybox = s_SceneData->Handle->GetSkybox();
    switch (skybox.index())
    {
    case 0:
        break;
    case 1:
        s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<Skybox2D>(skybox));
        break;
    case 2:
        s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<SkyboxCube>(skybox));
        break;
    default:
        throw error("Unhandled skybox type");
    }

    UpdateScenePipelineConfig();

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
    s_Shaders.ClosestHit =
        s_ShaderLibrary->AddShader("closestHit.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);
    s_Shaders.AnyHit = s_ShaderLibrary->AddShader("anyhit.rahit", vk::ShaderStageFlagBits::eAnyHitKHR);
    s_Shaders.OcclusionMiss =
        s_ShaderLibrary->AddShader("occlusion.rmiss", vk::ShaderStageFlagBits::eMissKHR);
    s_Shaders.OcclusionAnyHit =
        s_ShaderLibrary->AddShader("occlusionAnyhit.rahit", vk::ShaderStageFlagBits::eAnyHitKHR);
    s_Shaders.SkinningCompute =
        s_ShaderLibrary->AddShader("skinning.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.PostProcessCompute =
        s_ShaderLibrary->AddShader("postprocess.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.CompositionCompute =
        s_ShaderLibrary->AddShader("composition.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.BloomDownsampleCompute =
        s_ShaderLibrary->AddShader("bloomDownsample.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.BloomUpsampleCompute =
        s_ShaderLibrary->AddShader("bloomUpsample.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.UICompositionCompute =
        s_ShaderLibrary->AddShader("uiComposition.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.TonemappingCompute =
        s_ShaderLibrary->AddShader("toneMapping.comp", vk::ShaderStageFlagBits::eCompute);
    s_Shaders.DebugRaygen =
        s_ShaderLibrary->AddShader("Debug/debugRaygen.rgen", vk::ShaderStageFlagBits::eRaygenKHR);
    s_Shaders.DebugMiss =
        s_ShaderLibrary->AddShader("Debug/debugMiss.rmiss", vk::ShaderStageFlagBits::eMissKHR);
    s_Shaders.DebugClosestHit =
        s_ShaderLibrary->AddShader("Debug/debugClosestHit.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);
    s_Shaders.DebugAnyHit =
        s_ShaderLibrary->AddShader("Debug/debugAnyhit.rahit", vk::ShaderStageFlagBits::eAnyHitKHR);

    s_ShaderLibrary->CompileShaders();

    {
        RaytracingPipelineBuilder builder(*s_ShaderLibrary);

        s_PathTracingShaderConfig.RaygenGroupIndex = builder.AddGeneralGroup(s_Shaders.Raygen);
        s_PathTracingShaderConfig.PrimaryRayMissIndex = builder.AddGeneralGroup(s_Shaders.Miss);
        s_PathTracingShaderConfig.OcclusionRayMissIndex = builder.AddGeneralGroup(s_Shaders.OcclusionMiss);
        s_PathTracingShaderConfig.PrimaryRayHitIndex =
            builder.AddHitGroup(s_Shaders.ClosestHit, s_Shaders.AnyHit);
        s_PathTracingShaderConfig.OcclusionRayHitIndex =
            builder.AddHitGroup(ShaderLibrary::g_UnusedShaderId, s_Shaders.OcclusionAnyHit);

        builder.AddHintIsPartial(3, true);
        builder.AddHintIsPartial(6, true);
        builder.AddHintIsPartial(7, true);
        builder.AddHintIsPartial(8, true);
        builder.AddHintIsPartial(10, true);
        builder.AddHintIsPartial(11, true);
        builder.AddHintSize(3, Shaders::MaxTextureCount);

        static PathTracingPipelineConfig maxPathTracingConfig = {};
        maxPathTracingConfig[Shaders::MissFlagsConstantId] = Shaders::MissFlagsAll;
        maxPathTracingConfig[Shaders::HitFlagsConstantId] = Shaders::HitFlagsAll;

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
        s_DebugRayTracingShaderConfig.PrimaryRayHitIndex =
            builder.AddHitGroup(s_Shaders.DebugClosestHit, s_Shaders.DebugAnyHit);
        s_DebugRayTracingShaderConfig.OcclusionRayHitIndex =
            builder.AddHitGroup(ShaderLibrary::g_UnusedShaderId, s_Shaders.OcclusionAnyHit);

        builder.AddHintIsPartial(3, true);
        builder.AddHintIsPartial(6, true);
        builder.AddHintIsPartial(7, true);
        builder.AddHintIsPartial(8, true);
        builder.AddHintIsPartial(10, true);
        builder.AddHintIsPartial(11, true);
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

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.PostProcessCompute);
        static PostProcessPipelineConfig maxPostProcessConfig = {};
        s_PostProcessPipeline = builder.CreatePipelineUnique(maxPostProcessConfig);
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.CompositionCompute);
        static CompositionPipelineConfig maxCompositionConfig = {};
        s_CompositionPipeline = builder.CreatePipelineUnique(maxCompositionConfig);
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.BloomDownsampleCompute);
        builder.AddHintSize(0, Shaders::MaxBloomMipmapLevel + 1);
        builder.AddHintSize(1, Shaders::MaxBloomMipmapLevel + 1);
        static BloomDownsamplePipelineConfig maxBloomDownsampleConfig = {};
        s_BloomDownsamplePipeline = builder.CreatePipelineUnique(maxBloomDownsampleConfig);
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.BloomUpsampleCompute);
        builder.AddHintSize(0, Shaders::MaxBloomMipmapLevel + 1);
        builder.AddHintSize(1, Shaders::MaxBloomMipmapLevel + 1);
        static BloomUpsamplePipelineConfig maxBloomUpsampleConfig = {};
        s_BloomUpsamplePipeline = builder.CreatePipelineUnique(maxBloomUpsampleConfig);
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.UICompositionCompute);
        static UICompositionPipelineConfig maxUICompositionConfig = { Shaders::ToneMappingModeMax };
        s_UICompositionPipeline = builder.CreatePipelineUnique(maxUICompositionConfig);
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.TonemappingCompute);
        builder.AddHintIsPartial(0, true);
        static ToneMappingPipelineConfig maxTonemappingConfig = { Shaders::ToneMappingModeMax };
        s_ToneMappingPipeline = builder.CreatePipelineUnique(maxTonemappingConfig);
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.TonemappingCompute);
        static ToneMappingPipelineConfig maxTonemappingConfig = { Shaders::ToneMappingModeMax };
        s_UIToneMappingPipeline = builder.CreatePipelineUnique(maxTonemappingConfig);
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

void Renderer::UpdateScenePipelineConfig()
{
    auto updateMissFlags = [](Shaders::SpecializationConstant &flags) {
        const auto &skybox = s_SceneData->Handle->GetSkybox();
        flags &= ~(Shaders::MissFlagsSkybox2D | Shaders::MissFlagsSkyboxCube);
        switch (skybox.index())
        {
        case 0:
            break;
        case 1:
            flags |= Shaders::MissFlagsSkybox2D;
            break;
        case 2:
            flags |= Shaders::MissFlagsSkyboxCube;
            break;
        default:
            throw error("Unhandled skybox type");
        }
    };

    updateMissFlags(s_PathTracingPipelineConfig[Shaders::MissFlagsConstantId]);
    updateMissFlags(s_DebugRayTracingPipelineConfig[Shaders::DebugMissFlagsConstantId]);

    s_PathTracingPipelineConfig[Shaders::HitFlagsConstantId] &= ~Shaders::HitFlagsDxNormalTextures;
    s_DebugRayTracingPipelineConfig[Shaders::DebugHitGroupFlagsConstantId] &=
        ~Shaders::HitGroupFlagsDxNormalTextures;

    if (s_SceneData->Handle->HasDxNormalTextures())
    {
        s_PathTracingPipelineConfig[Shaders::HitFlagsConstantId] |= Shaders::HitFlagsDxNormalTextures;
        s_DebugRayTracingPipelineConfig[Shaders::DebugHitGroupFlagsConstantId] |=
            Shaders::HitGroupFlagsDxNormalTextures;
    }
}

void Renderer::UpdatePipelineSpecializations()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();
    s_PathTracingPipeline->CancelUpdate();
    s_DebugRayTracingPipeline->CancelUpdate();
    s_SkinningPipeline->CancelUpdate();
    s_PostProcessPipeline->CancelUpdate();
    s_CompositionPipeline->CancelUpdate();
    s_UICompositionPipeline->CancelUpdate();
    s_BloomDownsamplePipeline->CancelUpdate();
    s_BloomUpsamplePipeline->CancelUpdate();
    s_ToneMappingPipeline->CancelUpdate();
    s_UIToneMappingPipeline->CancelUpdate();
    Application::ResetBackgroundTask(BackgroundTaskType::ShaderCompilation);

    if (s_ActiveRayTracingPipeline == s_PathTracingPipeline.get())
        s_ActiveRayTracingPipeline->Update(s_PathTracingPipelineConfig);
    else
        s_ActiveRayTracingPipeline->Update(s_DebugRayTracingPipelineConfig);

    ToneMappingPipelineConfig toneMappingConfig = {
        s_RenderSettings.Output.Format == OutputFormat::Hdr ? Shaders::ToneMappingModeHDR
                                                            : Shaders::ToneMappingModeSDR,
    };

    ToneMappingPipelineConfig uiToneMappingConfig = {
        s_Swapchain->IsHdr() ? Shaders::ToneMappingModeHDR : Shaders::ToneMappingModeSDR,
    };

    UICompositionPipelineConfig uiCompositionConfig = {
        s_Swapchain->IsHdr() ? Shaders::ToneMappingModeHDR : Shaders::ToneMappingModeSDR,
    };

    s_PostProcessPipeline->Update(PostProcessPipelineConfig());
    s_CompositionPipeline->Update(CompositionPipelineConfig());
    s_BloomDownsamplePipeline->Update(BloomDownsamplePipelineConfig());
    s_BloomUpsamplePipeline->Update(BloomUpsamplePipelineConfig());
    s_SkinningPipeline->Update(SkinningPipelineConfig());
    s_ToneMappingPipeline->Update(toneMappingConfig);
    s_UIToneMappingPipeline->Update(uiToneMappingConfig);
    s_UICompositionPipeline->Update(uiCompositionConfig);
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
    s_PathTracingPipelineConfig = config;
    UpdateScenePipelineConfig();
    UpdatePipelineSpecializations();
}

void Renderer::SetDebugRaytracingPipeline(DebugRaytracingPipelineConfig config)
{
    s_ActiveRayTracingPipeline = s_DebugRayTracingPipeline.get();
    s_DebugRayTracingPipelineConfig = config;
    UpdateScenePipelineConfig();
    UpdatePipelineSpecializations();
}

void Renderer::UpdateHdr()
{
    s_ToneMappingPipeline->CancelUpdate();
    s_UIToneMappingPipeline->CancelUpdate();
    s_UICompositionPipeline->CancelUpdate();

    ToneMappingPipelineConfig toneMappingConfig = {
        s_RenderSettings.Output.Format == OutputFormat::Hdr ? Shaders::ToneMappingModeHDR
                                                            : Shaders::ToneMappingModeSDR,
    };

    ToneMappingPipelineConfig uiToneMappingConfig = {
        s_Swapchain->IsHdr() ? Shaders::ToneMappingModeHDR : Shaders::ToneMappingModeSDR,
    };

    UICompositionPipelineConfig uiCompositionConfig = {
        s_Swapchain->IsHdr() ? Shaders::ToneMappingModeHDR : Shaders::ToneMappingModeSDR,
    };

    s_ToneMappingPipeline->Update(toneMappingConfig);
    s_UIToneMappingPipeline->Update(uiToneMappingConfig);
    s_UICompositionPipeline->Update(uiCompositionConfig);
}

void Renderer::ResetAccumulationImage()
{
    s_RenderTimeSeconds = 0.0f;
    s_RefreshRate.SamplesPerFrame = 1;
    s_RefreshRate.SinceResetSeconds = 0.0f;
    for (RenderingResources &res : s_RenderingResources)
        res.TotalSamples = 0;
}

void Renderer::CancelRendering()
{
    ResetAccumulationImage();
    s_RenderCompletedFrames = 0;
    s_OutputSaver->CancelOutput();

    Application::EndOfflineRendering();
    Application::SetBackgroundTaskDone(BackgroundTaskType::Rendering);

    logger::info("Render cancelled");

    DeviceContext::GetGraphicsQueue().WaitIdle();
    OnResize(s_Swapchain->GetExtent());
}

void Renderer::SetSettings(const PathTracingSettings &settings)
{
    s_PathTracingSettings = settings;
    ResetAccumulationImage();
}

void Renderer::SetSettings(const PostProcessSettings &settings)
{
    s_PostProcessSettings = settings;
}

void Renderer::SetSettings(const RenderSettings &settings)
{
    s_RenderSettings = settings;
    DeviceContext::GetGraphicsQueue().WaitIdle();
    for (int i = 0; i < s_RenderingResources.size(); i++)
        CreateImageResourcesInternal(s_RenderingResources[i], i, s_RenderSettings.Output.Extent);
    OnResize(s_Swapchain->GetExtent());
    s_OutputImage = s_OutputSaver->RegisterOutput(s_RenderSettings.Output);
    for (int i = 0; i < s_RenderingResources.size(); i++)
        s_ToneMappingPipeline->GetDescriptorSet()->UpdateImage(
            0, i, *s_OutputImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
    Application::ResetBackgroundTask(BackgroundTaskType::Rendering);
    Application::AddBackgroundTask(
        BackgroundTaskType::Rendering, settings.MaxSampleCount * settings.FrameCount
    );
}

void Renderer::RecordSkinningCommands(const RenderingResources &resources)
{
    assert(s_SceneData->Handle->HasSkeletalAnimations());

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

void Renderer::RecordPathTracingCommands(const RenderingResources &resources)
{
    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Extent2D storageExtent = resources.AccumulationImage.GetExtent();

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
            vk::StridedDeviceAddressRegionKHR(), storageExtent.width, storageExtent.height, 1,
            Application::GetDispatchLoader()
        );

        Image::Transition(
            commandBuffer, resources.AccumulationImage.GetHandle(), vk::ImageLayout::eGeneral,
            vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::AccessFlagBits2::eShaderStorageRead
        );
    }
}

void Renderer::RecordPostProcessCommands(const RenderingResources &resources)
{
    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Image screenImage = resources.ScreenImage.GetHandle();
    vk::Extent2D screenExtent = resources.ScreenImage.GetExtent();
    vk::Extent2D storageExtent = resources.AccumulationImage.GetExtent();
    assert(storageExtent == resources.PostProcessImage.GetExtent());
    assert(screenExtent == s_Swapchain->GetExtent());

    {
        Utils::DebugLabel label(commandBuffer, "Post Processing pass", { 0.92f, 0.05f, 0.16f, 1.0f });

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, s_PostProcessPipeline->GetHandle());
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, s_PostProcessPipeline->GetLayout(), 0,
            { s_PostProcessPipeline->GetDescriptorSet()->GetSet(
                s_Swapchain->GetCurrentFrameInFlightIndex()
            ) },
            {}
        );

        const uint32_t groupSizeX =
            std::ceil(static_cast<float>(storageExtent.width) / Shaders::PostProcessShaderGroupSizeX);
        const uint32_t groupSizeY =
            std::ceil(static_cast<float>(storageExtent.height) / Shaders::PostProcessShaderGroupSizeY);
        commandBuffer.dispatch(groupSizeX, groupSizeY, 1);

        const uint32_t maxMipLevel =
            std::min(resources.BloomImage.GetMipLevels() - 3, Shaders::MaxBloomMipmapLevel);

        for (uint32_t i = 0; i < maxMipLevel - 1; i++)
        {
            const auto flags = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderStorageWrite;

            Image::Transition(
                commandBuffer, resources.BloomImage.GetHandle(), vk::ImageLayout::eGeneral,
                vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eComputeShader, flags, flags, i
            );

            Image::Transition(
                commandBuffer, resources.BloomImage.GetHandle(), vk::ImageLayout::eGeneral,
                vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eComputeShader, flags, flags, i + 1
            );

            commandBuffer.bindPipeline(
                vk::PipelineBindPoint::eCompute, s_BloomDownsamplePipeline->GetHandle()
            );
            commandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, s_BloomDownsamplePipeline->GetLayout(), 0,
                { s_BloomDownsamplePipeline->GetDescriptorSet()->GetSet(
                    s_Swapchain->GetCurrentFrameInFlightIndex()
                ) },
                {}
            );

            commandBuffer.pushConstants(
                s_BloomDownsamplePipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0u,
                sizeof(uint32_t), &i
            );

            vk::Extent2D mipmapExtent = resources.BloomImage.GetMipExtent(i + 1);

            const uint32_t mipGroupSizeX =
                std::ceil(static_cast<float>(mipmapExtent.width) / Shaders::PostProcessShaderGroupSizeX);
            const uint32_t mipGroupSizeY =
                std::ceil(static_cast<float>(mipmapExtent.height) / Shaders::PostProcessShaderGroupSizeY);

            commandBuffer.dispatch(mipGroupSizeX, mipGroupSizeY, 1);
        }

        for (uint32_t i = maxMipLevel - 1; i > 0; i--)
        {
            const auto flags = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderStorageWrite;

            Image::Transition(
                commandBuffer, resources.BloomImage.GetHandle(), vk::ImageLayout::eGeneral,
                vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eComputeShader, flags, flags, i
            );

            Image::Transition(
                commandBuffer, resources.BloomImage.GetHandle(), vk::ImageLayout::eGeneral,
                vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                vk::PipelineStageFlagBits2::eComputeShader, flags, flags, i - 1
            );

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, s_BloomUpsamplePipeline->GetHandle());
            commandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, s_BloomUpsamplePipeline->GetLayout(), 0,
                { s_BloomUpsamplePipeline->GetDescriptorSet()->GetSet(
                    s_Swapchain->GetCurrentFrameInFlightIndex()
                ) },
                {}
            );

            commandBuffer.pushConstants(
                s_BloomUpsamplePipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0u, sizeof(uint32_t),
                &i
            );

            vk::Extent2D mipmapExtent = resources.BloomImage.GetMipExtent(i - 1);

            const uint32_t mipGroupSizeX =
                std::ceil(static_cast<float>(mipmapExtent.width) / Shaders::PostProcessShaderGroupSizeX);
            const uint32_t mipGroupSizeY =
                std::ceil(static_cast<float>(mipmapExtent.height) / Shaders::PostProcessShaderGroupSizeY);

            commandBuffer.dispatch(mipGroupSizeX, mipGroupSizeY, 1);
        }

        Image::Transition(
            commandBuffer, resources.BloomImage.GetHandle(), vk::ImageLayout::eGeneral,
            vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::AccessFlagBits2::eShaderStorageRead, 0
        );

        Image::Transition(
            commandBuffer, resources.PostProcessImage.GetHandle(), vk::ImageLayout::eGeneral,
            vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
        );

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, s_CompositionPipeline->GetHandle());
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, s_CompositionPipeline->GetLayout(), 0,
            { s_CompositionPipeline->GetDescriptorSet()->GetSet(
                s_Swapchain->GetCurrentFrameInFlightIndex()
            ) },
            {}
        );

        commandBuffer.dispatch(groupSizeX, groupSizeY, 1);

        Image::Transition(
            commandBuffer, screenImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eTransferWrite
        );

        resources.PostProcessImage.Transition(
            commandBuffer, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral
        );

        auto srcarea = Image::GetMipLevelArea(storageExtent);
        auto dstarea = Image::GetMipLevelArea(screenExtent);
        vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        vk::ImageBlit2 imageBlit(subresource, srcarea, subresource, dstarea);

        vk::BlitImageInfo2 blitInfo(
            resources.PostProcessImage.GetHandle(), vk::ImageLayout::eGeneral, screenImage,
            vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eLinear
        );

        commandBuffer.blitImage2(blitInfo);
    }
}

void Renderer::RecordUICommands(const RenderingResources &resources)
{
    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Extent2D screenExtent = resources.ScreenImage.GetExtent();
    vk::Image screenImage = resources.ScreenImage.GetHandle();
    vk::ImageView uiImageView = resources.UIImage.GetView();
    vk::Image swapchainImage = s_Swapchain->GetCurrentFrame().Image;

    {
        Utils::DebugLabel label(commandBuffer, "UI pass", { 0.24f, 0.34f, 0.93f, 1.0f });

        Image::Transition(
            commandBuffer, resources.UIImage.GetHandle(), vk::ImageLayout::eGeneral,
            vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eClear, vk::AccessFlagBits2::eNone,
            vk::AccessFlagBits2::eTransferWrite
        );

        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        commandBuffer.clearColorImage(
            resources.UIImage.GetHandle(), vk::ImageLayout::eTransferDstOptimal,
            vk::ClearColorValue(0, 0, 0, 0), range
        );

        Image::Transition(
            commandBuffer, resources.UIImage.GetHandle(), vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eClear,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eTransferWrite,
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite
        );

        std::array<vk::RenderingAttachmentInfo, 1> colorAttachments = {
            vk::RenderingAttachmentInfo(uiImageView, vk::ImageLayout::eGeneral)
        };

        commandBuffer.beginRendering(
            vk::RenderingInfo(vk::RenderingFlags(), vk::Rect2D({}, screenExtent), 1, 0, colorAttachments)
        );
        UserInterface::OnRender(commandBuffer);
        commandBuffer.endRendering();

        resources.UIImage.Transition(
            commandBuffer, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral
        );

        resources.ScreenImage.Transition(
            commandBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral
        );

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, s_UIToneMappingPipeline->GetHandle());
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, s_UIToneMappingPipeline->GetLayout(), 0,
            { s_UIToneMappingPipeline->GetDescriptorSet()->GetSet(
                s_Swapchain->GetCurrentFrameInFlightIndex()
            ) },
            {}
        );

        const uint32_t groupSizeX =
            std::ceil(static_cast<float>(screenExtent.width) / Shaders::PostProcessShaderGroupSizeX);
        const uint32_t groupSizeY =
            std::ceil(static_cast<float>(screenExtent.height) / Shaders::PostProcessShaderGroupSizeY);
        commandBuffer.dispatch(groupSizeX, groupSizeY, 1);

        Image::Transition(
            commandBuffer, swapchainImage, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eComputeShader, vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite
        );

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, s_UICompositionPipeline->GetHandle());
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, s_UICompositionPipeline->GetLayout(), 0,
            { s_UICompositionPipeline->GetDescriptorSet()->GetSet(
                s_Swapchain->GetCurrentFrameInFlightIndex()
            ) },
            {}
        );
        commandBuffer.dispatch(groupSizeX, groupSizeY, 1);

        resources.ScreenImage.Transition(
            commandBuffer, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal
        );

        {
            auto area = Image::GetMipLevelArea(screenExtent);
            vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            vk::ImageBlit2 imageBlit(subresource, area, subresource, area);

            Image::Transition(
                commandBuffer, swapchainImage, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eNone,
                vk::AccessFlagBits2::eTransferWrite
            );

            vk::BlitImageInfo2 blitInfo(
                resources.ScreenImage.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, swapchainImage,
                vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eLinear
            );

            commandBuffer.blitImage2(blitInfo);

            resources.ScreenImage.Transition(
                commandBuffer, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral
            );

            Image::Transition(
                commandBuffer, swapchainImage, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::ePresentSrcKHR
            );
        }
    }
}

void Renderer::RecordSaveOutputCommands(const RenderingResources &resources)
{
    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Extent2D storageExtent = resources.AccumulationImage.GetExtent();

    auto area = Image::GetMipLevelArea(storageExtent);
    vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    vk::ImageBlit2 imageBlit(subresource, area, subresource, area);

    Image::Transition(
        commandBuffer, s_OutputImage->GetHandle(), vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands,
        vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eTransferWrite
    );

    vk::BlitImageInfo2 blitInfo(
        resources.PostProcessImage.GetHandle(), vk::ImageLayout::eGeneral,
        s_OutputImage->GetHandle(), vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eLinear
    );

    commandBuffer.blitImage2(blitInfo);

    Image::Transition(
        commandBuffer, s_OutputImage->GetHandle(), vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eBlit,
        vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eTransferWrite,
        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite
    );

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, s_ToneMappingPipeline->GetHandle());
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, s_ToneMappingPipeline->GetLayout(), 0,
        { s_ToneMappingPipeline->GetDescriptorSet()->GetSet(s_Swapchain->GetCurrentFrameInFlightIndex()) },
        {}
    );

    const uint32_t groupSizeX =
        std::ceil(static_cast<float>(storageExtent.width) / Shaders::PostProcessShaderGroupSizeX);
    const uint32_t groupSizeY =
        std::ceil(static_cast<float>(storageExtent.height) / Shaders::PostProcessShaderGroupSizeY);
    commandBuffer.dispatch(groupSizeX, groupSizeY, 1);
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

void Renderer::CreateImageResourcesInternal(RenderingResources &res, uint32_t frameIndex, vk::Extent2D extent)
{
    s_ImageBuilder->ResetFlags();

    res.AccumulationImage =
        s_ImageBuilder->SetFormat(vk::Format::eR32G32B32A32Sfloat)
            .SetUsageFlags(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst)
            .CreateImage(extent, std::format("Accumulation Image {}", frameIndex));
    res.TotalSamples = 0;

    res.PostProcessImage =
        s_ImageBuilder->SetFormat(vk::Format::eR16G16B16A16Sfloat)
            .SetUsageFlags(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)
            .CreateImage(extent, std::format("Post-process Image {}", frameIndex));

    res.BloomImage = s_ImageBuilder->SetFormat(vk::Format::eR16G16B16A16Sfloat)
                         .SetUsageFlags(
                             vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                             vk::ImageUsageFlagBits::eTransferSrc
                         )
                         .EnableMips()
                         .CreateImage(extent, std::format("Bloom Image {}", frameIndex));

    for (auto view : res.BloomImageViews)
        DeviceContext::GetLogical().destroyImageView(view);
    res.BloomImageViews.clear();

    for (uint32_t level = 0; level < res.BloomImage.GetMipLevels(); level++)
    {
        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, level, 1, 0, 1);
        vk::ImageViewCreateInfo viewCreateInfo = vk::ImageViewCreateInfo(
                                                     vk::ImageViewCreateFlags(), res.BloomImage.GetHandle(),
                                                     vk::ImageViewType::e2D, res.BloomImage.GetFormat()
        )
                                                     .setSubresourceRange(range);

        res.BloomImageViews.push_back(DeviceContext::GetLogical().createImageView(viewCreateInfo));
    }

    s_MainCommandBuffer->Begin();
    res.AccumulationImage.Transition(
        s_MainCommandBuffer->Buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral
    );
    res.PostProcessImage.Transition(
        s_MainCommandBuffer->Buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral
    );

    res.BloomImage.Transition(
        s_MainCommandBuffer->Buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral
    );
    s_MainCommandBuffer->SubmitBlocking();
}

void Renderer::CreateImageResources(RenderingResources &res, uint32_t frameIndex, vk::Extent2D extent)
{
    if (!Application::IsRendering())
        CreateImageResourcesInternal(res, frameIndex, extent);

    res.UIImage = s_ImageBuilder->ResetFlags()
                      .SetFormat(vk::Format::eR8G8B8A8Unorm)
                      .SetUsageFlags(
                          vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment |
                          vk::ImageUsageFlagBits::eStorage
                      )
                      .CreateImage(s_Swapchain->GetExtent(), std::format("UI Image {}", frameIndex));

    res.ScreenImage = s_ImageBuilder->ResetFlags()
                          .SetFormat(vk::Format::eR16G16B16A16Sfloat)
                          .SetUsageFlags(
                              vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc |
                              vk::ImageUsageFlagBits::eTransferDst
                          )
                          .CreateImage(s_Swapchain->GetExtent(), std::format("Screen Image {}", frameIndex));

    s_MainCommandBuffer->Begin();
    res.UIImage.Transition(
        s_MainCommandBuffer->Buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral
    );

    res.ScreenImage.Transition(
        s_MainCommandBuffer->Buffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral
    );

    s_MainCommandBuffer->SubmitBlocking();
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

        assert(res.AccumulationImage.GetExtent() == res.PostProcessImage.GetExtent());

        CreateImageResources(res, i, extent);

        std::lock_guard lock(s_DescriptorSetMutex);
        s_PathTracingPipeline->GetDescriptorSet()->UpdateImage(
            1, i, res.AccumulationImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_DebugRayTracingPipeline->GetDescriptorSet()->UpdateImage(
            1, i, res.AccumulationImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_PostProcessPipeline->GetDescriptorSet()->UpdateImage(
            0, i, res.AccumulationImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_PostProcessPipeline->GetDescriptorSet()->UpdateImage(
            1, i, res.PostProcessImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_PostProcessPipeline->GetDescriptorSet()->UpdateImage(
            2, i, res.BloomImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_CompositionPipeline->GetDescriptorSet()->UpdateImage(
            0, i, res.PostProcessImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_CompositionPipeline->GetDescriptorSet()->UpdateImage(
            1, i, res.BloomImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_UICompositionPipeline->GetDescriptorSet()->UpdateImage(
            0, i, res.UIImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_UICompositionPipeline->GetDescriptorSet()->UpdateImage(
            1, i, res.ScreenImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_UIToneMappingPipeline->GetDescriptorSet()->UpdateImage(
            0, i, res.ScreenImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_BloomDownsamplePipeline->GetDescriptorSet()->UpdateImageArrayFromViews(
            0, i, res.BloomImageViews, s_BloomSampler, vk::ImageLayout::eGeneral
        );
        s_BloomDownsamplePipeline->GetDescriptorSet()->UpdateImageArrayFromViews(
            1, i, res.BloomImageViews, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_BloomUpsamplePipeline->GetDescriptorSet()->UpdateImageArrayFromViews(
            0, i, res.BloomImageViews, s_BloomSampler, vk::ImageLayout::eGeneral
        );
        s_BloomUpsamplePipeline->GetDescriptorSet()->UpdateImageArrayFromViews(
            1, i, res.BloomImageViews, vk::Sampler(), vk::ImageLayout::eGeneral
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

        CreateImageResources(res, frameIndex, s_Swapchain->GetExtent());

        s_BufferBuilder->ResetFlags().SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer);
        res.RaygenUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            sizeof(Shaders::RaygenUniformData), std::format("Raygen Uniform Buffer {}", frameIndex)
        );
        res.PostProcessUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            sizeof(Shaders::PostProcessingUniformData),
            std::format("Post-process Uniform Buffer {}", frameIndex)
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
    s_PostProcessPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_CompositionPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_UICompositionPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_ToneMappingPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_UIToneMappingPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_BloomDownsamplePipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_BloomUpsamplePipeline->CreateDescriptorSet(s_RenderingResources.size());
    DescriptorSet *skinningDescriptorSet = s_SkinningPipeline->GetDescriptorSet();
    DescriptorSet *postProcessDescriptorSet = s_PostProcessPipeline->GetDescriptorSet();
    DescriptorSet *compositionDescriptorSet = s_CompositionPipeline->GetDescriptorSet();
    DescriptorSet *uiCompositionDescriptorSet = s_UICompositionPipeline->GetDescriptorSet();
    DescriptorSet *toneMappingDescriptorSet = s_ToneMappingPipeline->GetDescriptorSet();
    DescriptorSet *uiToneMappingDescriptorSet = s_UIToneMappingPipeline->GetDescriptorSet();
    DescriptorSet *bloomDownsampleDescriptorSet = s_BloomDownsamplePipeline->GetDescriptorSet();
    DescriptorSet *bloomUpsampleDescriptorSet = s_BloomUpsamplePipeline->GetDescriptorSet();

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
    {
        const RenderingResources &res = s_RenderingResources[frameIndex];

        auto updateRaytracingDescriptorSet = [&res, frameIndex](
                                                 DescriptorSet *set, Shaders::SpecializationConstant missFlags
                                             ) {
            set->UpdateAccelerationStructures(0, frameIndex, { res.SceneAccelerationStructure->GetTlas() });
            set->UpdateImage(1, frameIndex, res.AccumulationImage, vk::Sampler(), vk::ImageLayout::eGeneral);
            set->UpdateBuffer(2, frameIndex, res.RaygenUniformBuffer);
            set->UpdateImageArray(
                3, frameIndex, s_Textures, s_TextureMap, s_TextureSampler,
                vk::ImageLayout::eShaderReadOnlyOptimal
            );
            set->UpdateBuffer(4, frameIndex, s_SceneData->TransformBuffer);
            set->UpdateBuffer(5, frameIndex, res.GeometryBuffer);
            if (s_SceneData->MetallicRoughnessMaterialBuffer.GetHandle() != nullptr)
                set->UpdateBuffer(6, frameIndex, s_SceneData->MetallicRoughnessMaterialBuffer);
            if (s_SceneData->SpecularGlossinessMaterialBuffer.GetHandle() != nullptr)
                set->UpdateBuffer(7, frameIndex, s_SceneData->SpecularGlossinessMaterialBuffer);
            if (s_SceneData->PhongMaterialBuffer.GetHandle() != nullptr)
                set->UpdateBuffer(8, frameIndex, s_SceneData->PhongMaterialBuffer);
            set->UpdateBuffer(9, frameIndex, res.LightUniformBuffer);
            if ((missFlags & Shaders::MissFlagsSkybox2D) != Shaders::MissFlagsNone)
                set->UpdateImage(
                    10, frameIndex, s_SceneData->Skybox, s_TextureSampler,
                    vk::ImageLayout::eShaderReadOnlyOptimal
                );
            if ((missFlags & Shaders::MissFlagsSkyboxCube) != Shaders::MissFlagsNone)
                set->UpdateImage(
                    11, frameIndex, s_SceneData->Skybox, s_TextureSampler,
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

        postProcessDescriptorSet->UpdateImage(
            0, frameIndex, res.AccumulationImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        postProcessDescriptorSet->UpdateImage(
            1, frameIndex, res.PostProcessImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        postProcessDescriptorSet->UpdateImage(
            2, frameIndex, res.BloomImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        postProcessDescriptorSet->UpdateBuffer(3, frameIndex, res.PostProcessUniformBuffer);

        compositionDescriptorSet->UpdateImage(
            0, frameIndex, res.PostProcessImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        compositionDescriptorSet->UpdateImage(
            1, frameIndex, res.BloomImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        compositionDescriptorSet->UpdateBuffer(2, frameIndex, res.PostProcessUniformBuffer);

        bloomDownsampleDescriptorSet->UpdateImageArrayFromViews(
            0, frameIndex, res.BloomImageViews, s_BloomSampler, vk::ImageLayout::eGeneral
        );
        bloomDownsampleDescriptorSet->UpdateImageArrayFromViews(
            1, frameIndex, res.BloomImageViews, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        bloomUpsampleDescriptorSet->UpdateImageArrayFromViews(
            0, frameIndex, res.BloomImageViews, s_BloomSampler, vk::ImageLayout::eGeneral
        );
        bloomUpsampleDescriptorSet->UpdateImageArrayFromViews(
            1, frameIndex, res.BloomImageViews, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        uiCompositionDescriptorSet->UpdateImage(
            0, frameIndex, res.UIImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        uiCompositionDescriptorSet->UpdateImage(
            1, frameIndex, res.ScreenImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        if (s_OutputImage != nullptr)
            toneMappingDescriptorSet->UpdateImage(
                0, frameIndex, *s_OutputImage, vk::Sampler(), vk::ImageLayout::eGeneral
            );
        uiToneMappingDescriptorSet->UpdateImage(
            0, frameIndex, res.ScreenImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
    }
}

void Renderer::OnUpdate(float timeStep)
{
    if (s_RenderingResources.size() < s_Swapchain->GetInFlightCount())
        OnInFlightCountChange();

    s_RenderTimeSeconds += timeStep;

    // Keep the last MinRefreshRate frame times and their sum
    if (s_RefreshRate.Timings.size() == Application::GetConfig().MinRefreshRate)
    {
        s_RefreshRate.TimeSum -= s_RefreshRate.Timings.front();
        s_RefreshRate.Timings.pop();
    }
    s_RefreshRate.TimeSum += timeStep;
    s_RefreshRate.Timings.push(timeStep);

    const float threshold =
        1.0f * (Application::GetConfig().MinRefreshRate + 1) / Application::GetConfig().MinRefreshRate;

    if (s_RefreshRate.SinceResetSeconds > s_RefreshRate.IncraseThresholdSeconds &&
        s_RefreshRate.TimeSum < threshold &&
        s_RefreshRate.SamplesPerFrame < Application::GetConfig().MaxSamplesPerFrame)
    {
        // If the framerate has stabilized above the desired threshold
        // We increase the samples per frame, the next increase can happen after 2 seconds
        s_RefreshRate.IncraseThresholdSeconds = 2.0f;
        s_RefreshRate.SamplesPerFrame++;
        s_RefreshRate.SinceResetSeconds = 0.0f;
    }
    else if (s_RefreshRate.SinceResetSeconds > s_RefreshRate.DecreaseThresholdSeconds &&
             s_RefreshRate.TimeSum > threshold && s_RefreshRate.SamplesPerFrame > 1)
    {
        // If the framerate stabilized below the desired threshold
        // We decrease the samples per frame, the next increase can happen after 10 seconds
        s_RefreshRate.IncraseThresholdSeconds = 10.0f;
        s_RefreshRate.SamplesPerFrame--;
        s_RefreshRate.SinceResetSeconds = 0.0f;
    }
    else
        s_RefreshRate.SinceResetSeconds += timeStep;

    Stats::AddStat("Samples Per Frame", "Samples Per Frame: {}", s_RefreshRate.SamplesPerFrame);
}

void Renderer::Render()
{
    s_SkinningPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    s_PostProcessPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    s_CompositionPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    s_UICompositionPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    s_ToneMappingPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    s_UIToneMappingPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    s_BloomDownsamplePipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    s_BloomUpsamplePipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());

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
    camera.OnResize(res.AccumulationImage.GetExtent().width, res.AccumulationImage.GetExtent().height);
    Shaders::RaygenUniformData rgenData = { camera.GetInvViewMatrix(),
                                            camera.GetInvProjectionMatrix(),
                                            s_PathTracingSettings.BounceCount,
                                            s_PathTracingSettings.LensRadius,
                                            s_PathTracingSettings.FocalDistance,
                                            s_RefreshRate.SamplesPerFrame,
                                            res.TotalSamples };

    bool resetAccumulationImage = false, saveOutput = false;
    if (s_ActiveRayTracingPipeline != s_DebugRayTracingPipeline.get())
    {
        resetAccumulationImage |= res.TotalSamples == 0;
        res.TotalSamples += s_RefreshRate.SamplesPerFrame;
        if (Application::IsRendering())
        {
            saveOutput |= res.TotalSamples >= s_RenderSettings.MaxSampleCount;
            saveOutput |= s_RenderTimeSeconds >= s_RenderSettings.MaxTime.count();
            Application::IncrementBackgroundTaskDone(
                BackgroundTaskType::Rendering, s_RefreshRate.SamplesPerFrame
            );
        }
    }
    else
        res.TotalSamples = 1;

    Shaders::PostProcessingUniformData postprocessData = { res.TotalSamples, s_PostProcessSettings.Exposure,
                                                           s_PostProcessSettings.BloomThreshold,
                                                           s_PostProcessSettings.BloomIntensity };

    res.RaygenUniformBuffer.Upload(&rgenData);
    res.PostProcessUniformBuffer.Upload(&postprocessData);
    res.LightUniformBuffer.Upload(ToByteSpan(res.LightCount));
    res.LightUniformBuffer.Upload(
        ToByteSpan(s_SceneData->Handle->GetDirectionalLight()), RenderingResources::s_DirectionalLightOffset
    );
    if (res.LightCount > 0)
        res.LightUniformBuffer.Upload(
            s_SceneData->Handle->GetPointLights(), RenderingResources::s_LightArrayOffset
        );

    if (s_SceneData->Handle->HasSkeletalAnimations())
        res.BoneTransformUniformBuffer.Upload(s_SceneData->Handle->GetBoneTransforms());

    res.CommandBuffer.reset();
    res.CommandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    if (resetAccumulationImage)
    {
        vk::ImageSubresourceRange subresource(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        res.CommandBuffer.clearColorImage(
            res.AccumulationImage.GetHandle(), vk::ImageLayout::eGeneral,
            vk::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f), subresource
        );

        Image::Transition(
            res.CommandBuffer, res.AccumulationImage.GetHandle(), vk::ImageLayout::eGeneral,
            vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands,
            vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eNone,
            vk::AccessFlagBits2::eShaderStorageWrite
        );
    }

    if (s_SceneData->Handle->HasSkeletalAnimations())
        RecordSkinningCommands(res);

    if (s_SceneData->Handle->HasAnimations())
        res.SceneAccelerationStructure->RecordUpdateCommands(res.CommandBuffer);

    RecordPathTracingCommands(res);
    RecordPostProcessCommands(res);

    if (saveOutput)
        RecordSaveOutputCommands(res);

    RecordUICommands(res);

    res.CommandBuffer.end();

    vk::CommandBufferSubmitInfo cmdInfo(res.CommandBuffer);
    vk::SemaphoreSubmitInfo waitInfo(
        sync.ImageAcquiredSemaphore, 0, vk::PipelineStageFlagBits2::eAllCommands
    );
    std::array<vk::SemaphoreSubmitInfo, 2> signalInfo = {
        vk::SemaphoreSubmitInfo(sync.RenderCompleteSemaphore, 0, vk::PipelineStageFlagBits2::eAllCommands),
        vk::SemaphoreSubmitInfo(
            s_OutputSaver->GetSignalSemaphore(), 0, vk::PipelineStageFlagBits2::eAllCommands
        ),
    };
    vk::SubmitInfo2 submitInfo(vk::SubmitFlags(), waitInfo, cmdInfo, signalInfo);

    submitInfo.setSignalSemaphoreInfoCount(saveOutput ? 2 : 1);

    {
        auto lock = DeviceContext::GetGraphicsQueue().GetLock();
        DeviceContext::GetGraphicsQueue().Handle.submit2({ submitInfo }, sync.InFlightFence);
    }

    if (saveOutput)
    {
        s_OutputSaver->StartOutputWait();
        s_RenderCompletedFrames++;
        Application::AdvanceFrameOfflineRendering();
        Application::IncrementBackgroundTaskDone(
            BackgroundTaskType::Rendering, s_RenderSettings.MaxSampleCount - res.TotalSamples
        );
        logger::info("Total Time: {}s", s_RenderTimeSeconds);
        logger::info("Total Samples: {}", res.TotalSamples);
        s_RenderTimeSeconds = 0.0f;
        res.TotalSamples = 0;

        if (s_RenderCompletedFrames == s_RenderSettings.FrameCount)
        {
            s_RenderCompletedFrames = 0;
            s_OutputSaver->EndOutput();

            Application::EndOfflineRendering();
            Application::SetBackgroundTaskDone(BackgroundTaskType::Rendering);
            DeviceContext::GetGraphicsQueue().WaitIdle();
            OnResize(s_Swapchain->GetExtent());
        }
    }
}

}
