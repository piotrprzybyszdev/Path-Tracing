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

Shaders::RenderModeFlags Renderer::s_RenderMode = Shaders::RenderModeColor;
Shaders::EnabledTextureFlags Renderer::s_EnabledTextures = Shaders::TexturesEnableAll;
Shaders::RaygenFlags Renderer::s_RaygenFlags = Shaders::RaygenFlagsNone;
Shaders::MissFlags Renderer::s_MissFlags = Shaders::MissFlagsNone;
Shaders::ClosestHitFlags Renderer::s_ClosestHitFlags = Shaders::ClosestHitFlagsNone;
float Renderer::s_Exposure = 1.0f;

const Swapchain *Renderer::s_Swapchain = nullptr;

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};

std::unique_ptr<CommandBuffer> Renderer::s_MainCommandBuffer = nullptr;

std::unique_ptr<Renderer::SceneData> Renderer::s_SceneData = nullptr;

std::vector<Image> Renderer::s_Textures = {};
std::vector<uint32_t> Renderer::s_TextureMap = {};

std::unique_ptr<DescriptorSetBuilder> Renderer::s_DescriptorSetBuilder = nullptr;
std::unique_ptr<DescriptorSet> Renderer::s_DescriptorSet = nullptr;
std::mutex Renderer::s_DescriptorSetMutex = {};
std::unique_ptr<TextureUploader> Renderer::s_TextureUploader = nullptr;

vk::PipelineLayout Renderer::s_PipelineLayout = nullptr;
vk::Pipeline Renderer::s_Pipeline = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;

std::unique_ptr<Buffer> Renderer::s_StagingBuffer = nullptr;

vk::Sampler Renderer::s_TextureSampler = nullptr;

std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;

void Renderer::Init(const Swapchain *swapchain)
{
    s_Swapchain = swapchain;

    s_MainCommandBuffer = std::make_unique<CommandBuffer>(DeviceContext::GetGraphicsQueue());

    s_StagingBuffer = BufferBuilder()
                          .SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc)
                          .CreateHostBufferUnique(128ull * 1024 * 1024, "Main Staging Buffer");

    s_BufferBuilder = std::make_unique<BufferBuilder>();
    s_ImageBuilder = std::make_unique<ImageBuilder>();

    {
        vk::SamplerCreateInfo createInfo(vk::SamplerCreateFlags(), vk::Filter::eLinear, vk::Filter::eLinear);
        createInfo.setMipmapMode(vk::SamplerMipmapMode::eLinear);
        createInfo.setMaxLod(vk::LodClampNone);
        s_TextureSampler = DeviceContext::GetLogical().createSampler(createInfo);
        Utils::SetDebugName(s_TextureSampler, "Texture Sampler");
    }

    {
        s_ShaderLibrary = std::make_unique<ShaderLibrary>();

        const ShaderId raygenId =
            s_ShaderLibrary->AddShader("Shaders/raygen.rgen", vk::ShaderStageFlagBits::eRaygenKHR);
        const ShaderId missId =
            s_ShaderLibrary->AddShader("Shaders/miss.rmiss", vk::ShaderStageFlagBits::eMissKHR);
        const ShaderId closestHitId =
            s_ShaderLibrary->AddShader("Shaders/closesthit.rchit", vk::ShaderStageFlagBits::eClosestHitKHR);
        const ShaderId anyHitId =
            s_ShaderLibrary->AddShader("Shaders/anyhit.rahit", vk::ShaderStageFlagBits::eAnyHitKHR);

        s_ShaderLibrary->AddGeneralGroup(ShaderBindingTable::RaygenGroupIndex, raygenId);
        s_ShaderLibrary->AddGeneralGroup(ShaderBindingTable::MissGroupIndex, missId);
        s_ShaderLibrary->AddHitGroup(ShaderBindingTable::HitGroupIndex, closestHitId, anyHitId);
    }

    {
#ifndef NDEBUG
        const uint32_t loaderThreadCount = 2;
        const uint32_t bufferPerLoaderThread = 1;
#else
        const uint32_t loaderThreadCount = std::thread::hardware_concurrency() / 2;
        const uint32_t bufferPerLoaderThread = 2;
#endif
        const size_t stagingMemoryLimit =
            TextureUploader::GetStagingMemoryRequirement(loaderThreadCount * bufferPerLoaderThread);

        s_TextureUploader = std::make_unique<TextureUploader>(
            loaderThreadCount, stagingMemoryLimit, s_Textures, s_DescriptorSetMutex
        );
    }

    {
        uint32_t colorIndex = AddDefaultTexture(glm::u8vec4(255), "Default Color Texture");
        uint32_t normalIndex = AddDefaultTexture(glm::u8vec4(128, 128, 255, 255), "Default Normal Texture");
        uint32_t roughnessIndex = AddDefaultTexture(glm::u8vec4(0), "Default Roughness Texture");
        uint32_t metalicIndex = AddDefaultTexture(glm::u8vec4(0), "Default Metalic Texture");

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

    for (RenderingResources &res : s_RenderingResources)
        DeviceContext::GetLogical().destroyCommandPool(res.CommandPool);
    s_RenderingResources.clear();

    s_ShaderLibrary.reset();

    DeviceContext::GetLogical().destroyPipeline(s_Pipeline);
    DeviceContext::GetLogical().destroyPipelineLayout(s_PipelineLayout);
    s_DescriptorSet.reset();
    s_DescriptorSetBuilder.reset();

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
    DeviceContext::GetGraphicsQueue().WaitIdle();
    s_TextureUploader->Cancel();
    s_SceneData = std::make_unique<SceneData>(SceneManager::GetActiveScene());

    {
        Timer timer("Mesh Upload");
        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &vertices = s_SceneData->Scene->GetVertices();
        s_SceneData->VertexBuffer = CreateDeviceBuffer(vertices, "Vertex Buffer");

        const auto &indices = s_SceneData->Scene->GetIndices();
        s_SceneData->IndexBuffer = CreateDeviceBuffer(indices, "Index Buffer");

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &transforms = s_SceneData->Scene->GetTransforms();
        std::vector<vk::TransformMatrixKHR> transforms2;
        transforms2.reserve(transforms.size());
        for (const auto &transform : transforms)
            transforms2.push_back(TrivialCopy<glm::mat3x4, vk::TransformMatrixKHR>(transform));

        s_SceneData->TransformBuffer = CreateDeviceBuffer(std::span(transforms2), "Transform Buffer");

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &geometries = s_SceneData->Scene->GetGeometries();
        std::vector<Shaders::Geometry> geometries2 = {};
        geometries2.reserve(geometries.size());
        for (const auto &geometry : geometries)
            geometries2.emplace_back(
                s_SceneData->VertexBuffer.GetDeviceAddress() +
                    geometry.VertexOffset * sizeof(Shaders::Vertex),
                s_SceneData->IndexBuffer.GetDeviceAddress() + geometry.IndexOffset * sizeof(uint32_t)
            );

        s_SceneData->GeometryBuffer = CreateDeviceBuffer(std::span(geometries2), "Geometry Buffer");

        const auto &materials = s_SceneData->Scene->GetMaterials();
        s_SceneData->MaterialBuffer = CreateDeviceBuffer(materials, "Material Buffer");
    }

    for (int i = 0; i < s_RenderingResources.size(); i++)
    {
        RenderingResources &res = s_RenderingResources[i];

        s_BufferBuilder->ResetFlags().SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer);
        res.LightCount = s_SceneData->Scene->GetLights().size();
        res.LightUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            s_SceneData->Scene->GetLights(), std::format("Light Uniform Buffer {}", i)
        );

        res.SceneAccelerationStructure = std::make_unique<AccelerationStructure>(
            s_SceneData->VertexBuffer, s_SceneData->IndexBuffer, s_SceneData->TransformBuffer,
            s_SceneData->Scene
        );
    }

    auto models = s_SceneData->Scene->GetModels();
    s_SceneData->SceneShaderBindingTable = std::make_unique<ShaderBindingTable>();
    for (const auto &model : models)
        for (const auto &mesh : model.Meshes)
            s_SceneData->SceneShaderBindingTable->AddRecord({ mesh.GeometryIndex, mesh.MaterialIndex,
                                                              mesh.TransformBufferOffset });

    const auto &skybox = s_SceneData->Scene->GetSkybox();
    s_MissFlags &= ~(Shaders::MissFlagsSkybox2D | Shaders::MissFlagsSkyboxCube);
    switch (skybox.index())
    {
    case 0:
        break;
    case 1:
        s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<Skybox2D>(skybox));
        s_MissFlags |= Shaders::MissFlagsSkybox2D;
        break;
    case 2:
        s_SceneData->Skybox = s_TextureUploader->UploadSkyboxBlocking(std::get<SkyboxCube>(skybox));
        s_MissFlags |= Shaders::MissFlagsSkyboxCube;
        break;
    default:
        throw error("Unhandled skybox type");
    }

    const auto &textures = s_SceneData->Scene->GetTextures();

    s_Textures.resize(Shaders::SceneTextureOffset + textures.size());
    s_TextureMap.resize(Shaders::SceneTextureOffset + textures.size());
    for (int i = 0; i < textures.size(); i++)
    {
        const uint32_t mapIndex = Shaders::GetSceneTextureIndex(i);
        s_TextureMap[mapIndex] = Scene::GetDefaultTextureIndex(textures[i].Type);
    }

    if (SetupPipeline())
        RecreateDescriptorSet();

    s_TextureUploader->UploadTextures(s_SceneData->Scene);

    s_SceneData->SceneShaderBindingTable->Upload(s_Pipeline);
}

void Renderer::UpdateTexture(uint32_t index)
{
    s_TextureMap[index] = index;

    if (s_DescriptorSet == nullptr)
        return;

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
        s_DescriptorSet->UpdateImage(
            4, frameIndex, s_Textures[index], s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal, index
        );
}

Buffer Renderer::CreateDeviceBuffer(BufferContent content, std::string &&name)
{
    auto buffer = s_BufferBuilder->CreateDeviceBuffer(content.Size, name);
    s_StagingBuffer->Upload(content);

    s_MainCommandBuffer->Begin();
    buffer.UploadStaging(s_MainCommandBuffer->Buffer, *s_StagingBuffer);
    s_MainCommandBuffer->SubmitBlocking();

    return buffer;
}

uint32_t Renderer::AddDefaultTexture(glm::u8vec4 value, std::string &&name)
{
    auto image = s_TextureUploader->UploadDefault(value, std::move(name));
    s_Textures.push_back(std::move(image));
    return s_Textures.size() - 1;
}

bool Renderer::SetupPipeline()
{
    s_DescriptorSetBuilder = std::make_unique<DescriptorSetBuilder>();
    s_DescriptorSetBuilder
        ->SetDescriptor({ 0, vk::DescriptorType::eAccelerationStructureKHR, 1,
                          vk::ShaderStageFlagBits::eRaygenKHR })
        .SetDescriptor({ 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR })
        .SetDescriptor({ 2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR })
        .SetDescriptor({ 3, vk::DescriptorType::eUniformBuffer, 1,
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR })
        .SetDescriptor(
            { 4, vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(s_TextureMap.size()),
              vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR },
            true
        )
        .SetDescriptor({ 5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR })
        .SetDescriptor({ 6, vk::DescriptorType::eStorageBuffer, 1,
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR })
        .SetDescriptor({ 7, vk::DescriptorType::eStorageBuffer, 1,
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR })
        .SetDescriptor({ 8, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR })
        .SetDescriptor({ 9, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eMissKHR })
        .SetDescriptor(
            { 10, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eMissKHR }, true
        )
        .SetDescriptor(
            { 11, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eMissKHR }, true
        );

    bool isRecreated = s_PipelineLayout != nullptr;
    if (isRecreated)
    {
        DeviceContext::GetLogical().destroyPipeline(s_Pipeline);
        DeviceContext::GetLogical().destroyPipelineLayout(s_PipelineLayout);
    }

    std::array<vk::DescriptorSetLayout, 1> layouts = { s_DescriptorSetBuilder->CreateLayout() };
    vk::PipelineLayoutCreateInfo createInfo(vk::PipelineLayoutCreateFlags(), layouts);
    s_PipelineLayout = DeviceContext::GetLogical().createPipelineLayout(createInfo);

    s_Pipeline = s_ShaderLibrary->CreatePipeline(s_PipelineLayout);
    if (s_Pipeline == nullptr)
        throw error("Failed to create pipeline");

    return isRecreated;
}

void Renderer::ReloadShaders()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();
    DeviceContext::GetLogical().destroyPipeline(s_Pipeline);
    s_Pipeline = s_ShaderLibrary->CreatePipeline(s_PipelineLayout);

    s_SceneData->SceneShaderBindingTable->Upload(s_Pipeline);
}

void Renderer::RecordCommandBuffer(const RenderingResources &resources)
{
    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Image image = s_Swapchain->GetCurrentFrame().Image;
    vk::ImageView linearImageView = s_Swapchain->GetCurrentFrame().LinearImageView;
    vk::ImageView nonLinearImageView = s_Swapchain->GetCurrentFrame().NonLinearImageView;
    vk::Extent2D extent = s_Swapchain->GetExtent();

    commandBuffer.reset();
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    {
        Utils::DebugLabel label(commandBuffer, "Path tracing pass", { 0.35f, 0.9f, 0.29f, 1.0f });

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, s_Pipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR, s_PipelineLayout, 0,
            { s_DescriptorSet->GetSet(s_Swapchain->GetCurrentFrameInFlightIndex()) }, {}
        );

        commandBuffer.traceRaysKHR(
            s_SceneData->SceneShaderBindingTable->GetRaygenTableEntry(),
            s_SceneData->SceneShaderBindingTable->GetMissTableEntry(),
            s_SceneData->SceneShaderBindingTable->GetClosestHitTableEntry(),
            vk::StridedDeviceAddressRegionKHR(), extent.width, extent.height, 1,
            Application::GetDispatchLoader()
        );

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal
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
        UserInterface::Render(commandBuffer);
        commandBuffer.endRendering();

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR
        );
    }
    commandBuffer.end();
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

void Renderer::OnResize(vk::Extent2D extent)
{
    for (int i = 0; i < s_RenderingResources.size(); i++)
    {
        RenderingResources &res = s_RenderingResources[i];
        res.StorageImage = CreateStorageImage(extent);
        res.StorageImage.SetDebugName(std::format("Storage Image {}", i));

        std::lock_guard lock(s_DescriptorSetMutex);
        s_DescriptorSet->UpdateImage(1, i, res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral);
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
        res.MissUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            sizeof(Shaders::MissUniformData), std::format("Miss Uniform Buffer {}", frameIndex)
        );
        res.ClosestHitUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            sizeof(Shaders::ClosestHitUniformData), std::format("Closest Hit Uniform Buffer {}", frameIndex)
        );

        res.LightCount = s_SceneData->Scene->GetLights().size();
        res.LightUniformBuffer = s_BufferBuilder->CreateHostBuffer(
            s_SceneData->Scene->GetLights(), std::format("Light Uniform Buffer {}", frameIndex)
        );

        res.SceneAccelerationStructure = std::make_unique<AccelerationStructure>(
            s_SceneData->VertexBuffer, s_SceneData->IndexBuffer, s_SceneData->TransformBuffer,
            s_SceneData->Scene
        );

        s_RenderingResources.push_back(std::move(res));
    }

    RecreateDescriptorSet();
}

void Renderer::RecreateDescriptorSet()
{
    DeviceContext::GetGraphicsQueue().WaitIdle();

    std::lock_guard lock(s_DescriptorSetMutex);
    s_DescriptorSet = s_DescriptorSetBuilder->CreateSetUnique(s_RenderingResources.size());

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
    {
        const RenderingResources &res = s_RenderingResources[frameIndex];

        s_DescriptorSet->UpdateAccelerationStructures(
            0, frameIndex, { res.SceneAccelerationStructure->GetTlas() }
        );
        s_DescriptorSet->UpdateImage(
            1, frameIndex, res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_DescriptorSet->UpdateBuffer(2, frameIndex, res.RaygenUniformBuffer);
        s_DescriptorSet->UpdateBuffer(3, frameIndex, res.ClosestHitUniformBuffer);
        s_DescriptorSet->UpdateImageArray(
            4, frameIndex, s_Textures, s_TextureMap, s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal
        );
        s_DescriptorSet->UpdateBuffer(5, frameIndex, s_SceneData->TransformBuffer);
        s_DescriptorSet->UpdateBuffer(6, frameIndex, s_SceneData->GeometryBuffer);
        s_DescriptorSet->UpdateBuffer(7, frameIndex, s_SceneData->MaterialBuffer);
        s_DescriptorSet->UpdateBuffer(8, frameIndex, res.LightUniformBuffer);
        s_DescriptorSet->UpdateBuffer(9, frameIndex, res.MissUniformBuffer);
        if ((s_MissFlags & Shaders::MissFlagsSkybox2D) != Shaders::MissFlagsNone)
            s_DescriptorSet->UpdateImage(
                10, frameIndex, s_SceneData->Skybox, s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal
            );
        if ((s_MissFlags & Shaders::MissFlagsSkyboxCube) != Shaders::MissFlagsNone)
            s_DescriptorSet->UpdateImage(
                11, frameIndex, s_SceneData->Skybox, s_TextureSampler, vk::ImageLayout::eShaderReadOnlyOptimal
            );
    }
}

void Renderer::OnUpdate(float /* timeStep */)
{
    if (s_RenderingResources.size() < s_Swapchain->GetInFlightCount())
        OnInFlightCountChange();

    const RenderingResources &res = s_RenderingResources[s_Swapchain->GetCurrentFrameInFlightIndex()];
    res.SceneAccelerationStructure->Update();

    {
        std::lock_guard lock(s_DescriptorSetMutex);
        s_DescriptorSet->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    }
}

void Renderer::Render(const Camera &camera)
{
    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    const RenderingResources &res = s_RenderingResources[s_Swapchain->GetCurrentFrameInFlightIndex()];

    Shaders::RaygenUniformData rgenData = { camera.GetInvViewMatrix(), camera.GetInvProjectionMatrix(),
                                            s_RaygenFlags, s_Exposure };
    Shaders::ClosestHitUniformData rchitData = { s_RenderMode, s_EnabledTextures, s_ClosestHitFlags,
                                                 res.LightCount };
    Shaders::MissUniformData missData = { s_MissFlags };

    res.RaygenUniformBuffer.Upload(&rgenData);
    res.MissUniformBuffer.Upload(&missData);
    res.ClosestHitUniformBuffer.Upload(&rchitData);
    res.LightUniformBuffer.Upload(s_SceneData->Scene->GetLights());

    RecordCommandBuffer(res);

    vk::CommandBufferSubmitInfo cmdInfo(res.CommandBuffer);
    vk::SemaphoreSubmitInfo waitInfo(
        sync.ImageAcquiredSemaphore, 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );
    vk::SemaphoreSubmitInfo signalInfo(sync.RenderCompleteSemaphore);
    vk::SubmitInfo2 submitInfo(vk::SubmitFlags(), waitInfo, cmdInfo, signalInfo);

    {
        auto lock = DeviceContext::GetGraphicsQueue().GetLock();
        DeviceContext::GetGraphicsQueue().Handle.submit2({ submitInfo }, sync.InFlightFence);
    }
}

}
