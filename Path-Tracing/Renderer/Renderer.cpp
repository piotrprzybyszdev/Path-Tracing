#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_format_traits.hpp>

#include <algorithm>
#include <bit>
#include <memory>
#include <ranges>
#include <set>

#include "Core/Core.h"

#include "Shaders/ShaderRendererTypes.incl"

#include "Application.h"
#include "UserInterface.h"

#include "AssetImporter.h"
#include "CommandBuffer.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "SceneManager.h"
#include "Utils.h"

namespace PathTracing
{

float Renderer::s_Exposure = 1.0f;

const Swapchain *Renderer::s_Swapchain = nullptr;

Renderer::ShaderIds Renderer::s_Shaders = {};
Renderer::RaytracingConfig Renderer::s_RaytracingConfig = {};

Shaders::SpecializationData Renderer::s_ShaderSpecialization = {};

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};

std::unique_ptr<CommandBuffer> Renderer::s_MainCommandBuffer = nullptr;

std::unique_ptr<Renderer::SceneData> Renderer::s_SceneData = nullptr;

std::vector<Image> Renderer::s_Textures = {};
std::vector<uint32_t> Renderer::s_TextureMap = {};

std::mutex Renderer::s_DescriptorSetMutex = {};
std::unique_ptr<TextureUploader> Renderer::s_TextureUploader = nullptr;
std::unique_ptr<CommandBuffer> Renderer::s_TextureOwnershipCommandBuffer = nullptr;
bool Renderer::s_TextureOwnershipBufferHasCommands = false;

std::unique_ptr<RaytracingPipeline> Renderer::s_RaytracingPipeline = nullptr;
std::unique_ptr<ComputePipeline> Renderer::s_SkinningPipeline = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;

std::unique_ptr<StagingBuffer> Renderer::s_StagingBuffer = nullptr;

vk::Sampler Renderer::s_TextureSampler = nullptr;

std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;

void Renderer::Init(const Swapchain *swapchain)
{
    s_Swapchain = swapchain;

    try
    {
        CreatePipelines();
    }
    catch (const error &error)
    {
        logger::critical("Pipeline creation failed - Renderer can't be initialized");
        s_ShaderLibrary.reset();
        throw error;
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
        auto tou8vec4 = [](float value) { return glm::u8vec4(value * 255.0f); };

        uint32_t colorIndex = AddDefaultTexture(glm::u8vec4(255), "Default Color Texture");
        uint32_t normalIndex = AddDefaultTexture(glm::u8vec4(128, 128, 255, 255), "Default Normal Texture");
        uint32_t roughnessIndex =
            AddDefaultTexture(tou8vec4(Shaders::DefaultRoughness), "Default Roughness Texture");
        uint32_t metalicIndex =
            AddDefaultTexture(tou8vec4(Shaders::DefaultMetalness), "Default Metalic Texture");

        s_TextureMap.resize(4);
        s_TextureMap[Shaders::DefaultColorTextureIndex] = colorIndex;
        s_TextureMap[Shaders::DefaultNormalTextureIndex] = normalIndex;
        s_TextureMap[Shaders::DefaultRoughnessTextureIndex] = roughnessIndex;
        s_TextureMap[Shaders::DefaultMetalicTextureIndex] = metalicIndex;
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
    s_RaytracingPipeline.reset();
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

void Renderer::UpdateSceneData()
{
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

        const auto &texturedMaterials = s_SceneData->Handle->GetTexturedMaterials();
        s_SceneData->TexturedMaterialBuffer =
            CreateDeviceBufferUnflushed(texturedMaterials, "Textured Material Buffer");

        const auto &solidColorMaterials = s_SceneData->Handle->GetSolidColorMaterials();
        s_SceneData->SolidColorMaterialBuffer =
            CreateDeviceBufferUnflushed(solidColorMaterials, "Solid Color Material Buffer");

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
            std::make_unique<ShaderBindingTable>(s_RaytracingConfig.HitGroupCount);

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
                    .HitGroupIndex = s_RaytracingConfig.OcclusionRayHitIndex,
                    .Buffer = data,
                };

                switch (mesh.ShaderMaterialType)
                {
                case MaterialType::Textured:
                    entries[Shaders::PrimaryRayHitGroupIndex].HitGroupIndex =
                        s_RaytracingConfig.PrimaryRayTexturedHitIndex;
                    break;
                case MaterialType::SolidColor:
                    entries[Shaders::PrimaryRayHitGroupIndex].HitGroupIndex =
                        s_RaytracingConfig.PrimaryRaySolidColorHitIndex;
                    break;
                }

                s_SceneData->SceneShaderBindingTable->AddRecord(entries);
            }
    }

    const auto &skybox = s_SceneData->Handle->GetSkybox();
    s_ShaderSpecialization.MissFlags &= ~(Shaders::MissFlagsSkybox2D | Shaders::MissFlagsSkyboxCube);
    switch (skybox.index())
    {
    case 0:
        break;
    case 1:
        s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<Skybox2D>(skybox));
        s_ShaderSpecialization.MissFlags |= Shaders::MissFlagsSkybox2D;
        break;
    case 2:
        s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<SkyboxCube>(skybox));
        s_ShaderSpecialization.MissFlags |= Shaders::MissFlagsSkyboxCube;
        break;
    default:
        throw error("Unhandled skybox type");
    }

    const auto &textures = s_SceneData->Handle->GetTextures();

    s_Textures.resize(Shaders::SceneTextureOffset + textures.size());
    s_TextureMap.resize(Shaders::SceneTextureOffset + textures.size());
    for (int i = 0; i < textures.size(); i++)
    {
        const uint32_t mapIndex = Shaders::GetSceneTextureIndex(i);
        s_TextureMap[mapIndex] = Scene::GetDefaultTextureIndex(textures[i].Type);
    }

    UpdateSpecializations(s_ShaderSpecialization);

    RecreateDescriptorSet();

    s_TextureOwnershipCommandBuffer->Reset();
    s_TextureOwnershipBufferHasCommands = false;

    s_TextureUploader->UploadTextures(s_SceneData->Handle);
}

void Renderer::UpdateTexture(uint32_t index)
{
    s_TextureMap[index] = index;

    if (s_RaytracingPipeline->GetDescriptorSet() == nullptr)
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
        s_RaytracingPipeline->GetDescriptorSet()->UpdateImage(
            3, frameIndex, s_Textures[index], s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal, index
        );
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

uint32_t Renderer::AddDefaultTexture(glm::u8vec4 value, std::string &&name)
{
    auto image = s_TextureUploader->UploadDefault(value, std::move(name));
    s_Textures.push_back(std::move(image));
    return s_Textures.size() - 1;
}

void Renderer::CreatePipelines()
{
    Timer timer("Pipeline Create");

    s_ShaderLibrary = std::make_unique<ShaderLibrary>();

    s_Shaders.Raygen = s_ShaderLibrary->AddShader("raygen.rgen", vk::ShaderStageFlagBits::eRaygenKHR);
    s_Shaders.Miss = s_ShaderLibrary->AddShader("miss.rmiss", vk::ShaderStageFlagBits::eMissKHR);
    s_Shaders.TexturedClosestHit =
        s_ShaderLibrary->AddShader("textured.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);
    s_Shaders.SolidColorClosestHit =
        s_ShaderLibrary->AddShader("solidColor.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);
    s_Shaders.AnyHit = s_ShaderLibrary->AddShader("anyhit.rahit", vk::ShaderStageFlagBits::eAnyHitKHR);
    s_Shaders.OcclusionMiss =
        s_ShaderLibrary->AddShader("occlusion.rmiss", vk::ShaderStageFlagBits::eMissKHR);
    s_Shaders.SkinningCompute =
        s_ShaderLibrary->AddShader("skinning.comp", vk::ShaderStageFlagBits::eCompute);

    s_ShaderLibrary->CompileShaders();

    {
        RaytracingPipelineBuilder builder(*s_ShaderLibrary);

        s_RaytracingConfig.RaygenGroupIndex = builder.AddGeneralGroup(s_Shaders.Raygen);
        s_RaytracingConfig.PrimaryRayMissIndex = builder.AddGeneralGroup(s_Shaders.Miss);
        s_RaytracingConfig.OcclusionRayMissIndex = builder.AddGeneralGroup(s_Shaders.OcclusionMiss);
        s_RaytracingConfig.PrimaryRayTexturedHitIndex =
            builder.AddHitGroup(s_Shaders.TexturedClosestHit, s_Shaders.AnyHit);
        s_RaytracingConfig.PrimaryRaySolidColorHitIndex =
            builder.AddHitGroup(s_Shaders.SolidColorClosestHit, s_Shaders.AnyHit);
        s_RaytracingConfig.OcclusionRayHitIndex =
            builder.AddHitGroup(ShaderLibrary::g_UnusedShaderId, s_Shaders.AnyHit);

        builder.AddHintIsPartial(3, true);
        builder.AddHintIsPartial(6, true);
        builder.AddHintIsPartial(7, true);
        builder.AddHintIsPartial(9, true);
        builder.AddHintIsPartial(10, true);
        builder.AddHintSize(3, Shaders::MaxTextureCount);

        s_RaytracingPipeline = builder.CreatePipelineUnique();
    }

    {
        ComputePipelineBuilder builder(*s_ShaderLibrary, s_Shaders.SkinningCompute);
        s_SkinningPipeline = builder.CreatePipelineUnique();
    }
}

void Renderer::UpdateShaderBindingTable()
{
    std::array<uint32_t, 2> missGroupIndices = {};
    missGroupIndices[Shaders::PrimaryRayMissGroupIndex] = s_RaytracingConfig.PrimaryRayMissIndex;
    missGroupIndices[Shaders::OcclusionRayMissGroupIndex] = s_RaytracingConfig.OcclusionRayMissIndex;
    s_SceneData->SceneShaderBindingTable->Upload(
        s_RaytracingPipeline->GetHandle(), s_RaytracingConfig.RaygenGroupIndex, missGroupIndices
    );
}

void Renderer::UpdatePipelineSpecializations()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();
    s_RaytracingPipeline->CancelUpdate();
    s_SkinningPipeline->CancelUpdate();
    Application::ResetBackgroundTask(BackgroundTaskType::ShaderCompilation);
    s_RaytracingPipeline->Update(s_ShaderSpecialization);
    s_SkinningPipeline->Update(s_ShaderSpecialization);
    UpdateShaderBindingTable();
}

void Renderer::ReloadShaders()
{
    UpdatePipelineSpecializations();
}

void Renderer::UpdateSpecializations(Shaders::SpecializationData data)
{
    data.MissFlags = s_ShaderSpecialization.MissFlags;
    s_ShaderSpecialization = data;
    UpdatePipelineSpecializations();
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

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, s_RaytracingPipeline->GetHandle());
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR, s_RaytracingPipeline->GetLayout(), 0,
            { s_RaytracingPipeline->GetDescriptorSet()->GetSet(s_Swapchain->GetCurrentFrameInFlightIndex()) },
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
                      .SetFormat(vk::Format::eR8G8B8A8Unorm)
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
        getAddress(s_SceneData->TransformBuffer), s_SceneData->Handle, s_RaytracingConfig.HitGroupCount
    );
}

void Renderer::OnResize(vk::Extent2D extent)
{
    for (int i = 0; i < s_RenderingResources.size(); i++)
    {
        RenderingResources &res = s_RenderingResources[i];
        res.StorageImage = CreateStorageImage(extent);
        res.StorageImage.SetDebugName(std::format("Storage Image {}", i));

        std::lock_guard lock(s_DescriptorSetMutex);
        s_RaytracingPipeline->GetDescriptorSet()->UpdateImage(
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
    s_RaytracingPipeline->CreateDescriptorSet(s_RenderingResources.size());
    s_SkinningPipeline->CreateDescriptorSet(s_RenderingResources.size());
    DescriptorSet *raytracingDescriptorSet = s_RaytracingPipeline->GetDescriptorSet();
    DescriptorSet *skinningDescriptorSet = s_SkinningPipeline->GetDescriptorSet();

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
    {
        const RenderingResources &res = s_RenderingResources[frameIndex];

        raytracingDescriptorSet->UpdateAccelerationStructures(
            0, frameIndex, { res.SceneAccelerationStructure->GetTlas() }
        );
        raytracingDescriptorSet->UpdateImage(
            1, frameIndex, res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        raytracingDescriptorSet->UpdateBuffer(2, frameIndex, res.RaygenUniformBuffer);
        raytracingDescriptorSet->UpdateImageArray(
            3, frameIndex, s_Textures, s_TextureMap, s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal
        );
        raytracingDescriptorSet->UpdateBuffer(4, frameIndex, s_SceneData->TransformBuffer);
        raytracingDescriptorSet->UpdateBuffer(5, frameIndex, res.GeometryBuffer);
        if (s_SceneData->TexturedMaterialBuffer.GetHandle() != nullptr)
            raytracingDescriptorSet->UpdateBuffer(6, frameIndex, s_SceneData->TexturedMaterialBuffer);
        if (s_SceneData->SolidColorMaterialBuffer.GetHandle() != nullptr)
            raytracingDescriptorSet->UpdateBuffer(7, frameIndex, s_SceneData->SolidColorMaterialBuffer);
        raytracingDescriptorSet->UpdateBuffer(8, frameIndex, res.LightUniformBuffer);
        if ((s_ShaderSpecialization.MissFlags & Shaders::MissFlagsSkybox2D) != Shaders::MissFlagsNone)
            raytracingDescriptorSet->UpdateImage(
                9, frameIndex, s_SceneData->Skybox, s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal
            );
        if ((s_ShaderSpecialization.MissFlags & Shaders::MissFlagsSkyboxCube) != Shaders::MissFlagsNone)
            raytracingDescriptorSet->UpdateImage(
                10, frameIndex, s_SceneData->Skybox, s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal
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
        }
        s_RaytracingPipeline->GetDescriptorSet()->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    }

    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    const RenderingResources &res = s_RenderingResources[s_Swapchain->GetCurrentFrameInFlightIndex()];

    Camera &camera = s_SceneData->Handle->GetActiveCamera();
    camera.OnResize(res.StorageImage.GetExtent().width, res.StorageImage.GetExtent().height);
    Shaders::RaygenUniformData rgenData = { camera.GetInvViewMatrix(), camera.GetInvProjectionMatrix(),
                                            s_Exposure };

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
