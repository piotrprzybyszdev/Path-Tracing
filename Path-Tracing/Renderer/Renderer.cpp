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

#include "AssetManager.h"
#include "CommandBuffer.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

Shaders::RenderModeFlags Renderer::s_RenderMode = Shaders::RenderModeColor;
Shaders::EnabledTextureFlags Renderer::s_EnabledTextures = Shaders::TexturesEnableAll;
Shaders::RaygenFlags Renderer::s_RaygenFlags = Shaders::RaygenFlagsNone;
Shaders::MissFlags Renderer::s_MissFlags = Shaders::MissFlagsNone;
Shaders::ClosestHitFlags Renderer::s_ClosestHitFlags = Shaders::ClosestHitFlagsNone;

const Swapchain *Renderer::s_Swapchain = nullptr;

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};

std::unique_ptr<CommandBuffer> Renderer::s_MainCommandBuffer = nullptr;

std::unique_ptr<DescriptorSetBuilder> Renderer::s_DescriptorSetBuilder = nullptr;
std::unique_ptr<DescriptorSet> Renderer::s_DescriptorSet = nullptr;
std::mutex Renderer::s_DescriptorSetMutex = {};
std::unique_ptr<TextureUploader> Renderer::s_TextureUploader = nullptr;

vk::PipelineLayout Renderer::s_PipelineLayout = nullptr;
vk::Pipeline Renderer::s_Pipeline = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;

std::unique_ptr<Image> Renderer::s_StagingImage = nullptr;
std::unique_ptr<Buffer> Renderer::s_StagingBuffer = nullptr;

vk::Sampler Renderer::s_TextureSampler = nullptr;

std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;
Renderer::SceneData Renderer::s_StaticSceneData = {};

void Renderer::Init(const Swapchain *swapchain)
{
    s_Swapchain = swapchain;

    s_MainCommandBuffer = std::make_unique<CommandBuffer>(DeviceContext::GetGraphicsQueue());

    s_StagingImage = ImageBuilder()
                         .SetUsageFlags(
                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                             vk::ImageUsageFlagBits::eSampled
                         )
                         .SetFormat(vk::Format::eR8G8B8A8Unorm)
                         .EnableMips()
                         .CreateImageUnique(vk::Extent2D(4096, 4096), "Main Staging Image");

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
        uint32_t colorIndex = AddDefaultTexture(glm::u8vec4(255), "Default Color Texture");
        uint32_t normalIndex = AddDefaultTexture(glm::u8vec4(128, 128, 255, 255), "Default Normal Texture");
        uint32_t roughnessIndex = AddDefaultTexture(glm::u8vec4(0), "Default Roughness Texture");
        uint32_t metalicIndex = AddDefaultTexture(glm::u8vec4(0), "Default Metalic Texture");

        s_StaticSceneData.TextureMap.resize(4);
        s_StaticSceneData.TextureMap[Shaders::DefaultColorTextureIndex] = colorIndex;
        s_StaticSceneData.TextureMap[Shaders::DefaultNormalTextureIndex] = normalIndex;
        s_StaticSceneData.TextureMap[Shaders::DefaultRoughnessTextureIndex] = roughnessIndex;
        s_StaticSceneData.TextureMap[Shaders::DefaultMetalicTextureIndex] = metalicIndex;
    }

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
        loaderThreadCount, stagingMemoryLimit, s_StaticSceneData.Textures, s_DescriptorSetMutex
    );
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

    s_StaticSceneData.SceneShaderBindingTable.reset();
    s_StaticSceneData.SceneAccelerationStructure.reset();
    s_StaticSceneData.Skybox.reset();
    s_StaticSceneData.Textures.clear();
    s_StaticSceneData.MaterialBuffer.reset();
    s_StaticSceneData.GeometryBuffer.reset();
    s_StaticSceneData.TransformBuffer.reset();
    s_StaticSceneData.IndexBuffer.reset();
    s_StaticSceneData.VertexBuffer.reset();

    DeviceContext::GetLogical().destroySampler(s_TextureSampler);
    s_ImageBuilder.reset();
    s_BufferBuilder.reset();

    s_StagingImage.reset();
    s_StagingBuffer.reset();

    s_MainCommandBuffer.reset();
}

void Renderer::SetScene(const Scene &scene)
{
    DeviceContext::GetGraphicsQueue().WaitIdle();

    {
        Timer timer("Mesh Upload");
        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &vertices = scene.GetVertices();
        s_StagingBuffer->Upload(vertices);
        s_MainCommandBuffer->Begin();
        s_StaticSceneData.VertexBuffer = s_BufferBuilder->CreateDeviceBufferUnique(
            s_MainCommandBuffer->Buffer, *s_StagingBuffer, "Vertex Buffer"
        );
        s_MainCommandBuffer->SubmitBlocking();

        const auto &indices = scene.GetIndices();
        s_StagingBuffer->Upload(indices);
        s_MainCommandBuffer->Begin();
        s_StaticSceneData.IndexBuffer = s_BufferBuilder->CreateDeviceBufferUnique(
            s_MainCommandBuffer->Buffer, *s_StagingBuffer, "Index Buffer"
        );
        s_MainCommandBuffer->SubmitBlocking();

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &transforms = scene.GetTransforms();
        std::vector<vk::TransformMatrixKHR> transforms2;
        transforms2.reserve(transforms.size());
        for (const auto &transform : transforms)
            transforms2.push_back(TrivialCopy<glm::mat3x4, vk::TransformMatrixKHR>(transform));
        s_MainCommandBuffer->Begin();
        s_StagingBuffer->Upload(std::span(transforms2));
        s_StaticSceneData.TransformBuffer = s_BufferBuilder->CreateDeviceBufferUnique(
            s_MainCommandBuffer->Buffer, *s_StagingBuffer, "Transform Buffer"
        );
        s_MainCommandBuffer->SubmitBlocking();

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &geometries = scene.GetGeometries();
        std::vector<Shaders::Geometry> geometries2 = {};
        geometries2.reserve(geometries.size());
        for (const auto &geometry : geometries)
            geometries2.emplace_back(
                s_StaticSceneData.VertexBuffer->GetDeviceAddress() +
                    geometry.VertexOffset * sizeof(Shaders::Vertex),
                s_StaticSceneData.IndexBuffer->GetDeviceAddress() + geometry.IndexOffset * sizeof(uint32_t)
            );

        s_MainCommandBuffer->Begin();
        s_StagingBuffer->Upload(std::span(geometries2));
        s_StaticSceneData.GeometryBuffer = s_BufferBuilder->CreateDeviceBufferUnique(
            s_MainCommandBuffer->Buffer, *s_StagingBuffer, "Geometry Buffer"
        );
        s_MainCommandBuffer->SubmitBlocking();

        const auto &materials = scene.GetMaterials();
        s_MainCommandBuffer->Begin();
        s_StagingBuffer->Upload(materials);
        s_StaticSceneData.MaterialBuffer = s_BufferBuilder->CreateDeviceBufferUnique(
            s_MainCommandBuffer->Buffer, *s_StagingBuffer, "Material Buffer"
        );
        s_MainCommandBuffer->SubmitBlocking();
    }

    // Setup AC and SBT
    auto models = scene.GetModels();
    auto instances = scene.GetModelInstances();
    s_StaticSceneData.SceneAccelerationStructure = std::make_unique<AccelerationStructure>(
        *s_StaticSceneData.VertexBuffer, *s_StaticSceneData.IndexBuffer, *s_StaticSceneData.TransformBuffer,
        scene
    );

    s_StaticSceneData.SceneAccelerationStructure->Build();

    s_StaticSceneData.SceneShaderBindingTable = std::make_unique<ShaderBindingTable>();
    for (const auto &model : models)
        for (const auto &mesh : model.Meshes)
            s_StaticSceneData.SceneShaderBindingTable->AddRecord({ mesh.GeometryIndex, mesh.MaterialIndex,
                                                                   mesh.TransformBufferOffset });

    const auto &skybox = scene.GetSkybox();
    switch (skybox.index())
    {
    case 0:
        break;
    case 1:
        AddSkybox(std::get<Skybox2D>(skybox));
        break;
    case 2:
        AddSkybox(std::get<SkyboxCube>(skybox));
        break;
    default:
        throw error("Unhandled skybox type");
    }

    const auto &textures = scene.GetTextures();

    s_StaticSceneData.Textures.resize(Shaders::SceneTextureOffset + textures.size());
    s_StaticSceneData.TextureMap.resize(Shaders::SceneTextureOffset + textures.size());
    for (int i = 0; i < textures.size(); i++)
    {
        const uint32_t mapIndex = Shaders::SceneTextureOffset + i;
        switch (textures[i].Type)
        {
        case TextureType::Color:
            s_StaticSceneData.TextureMap[mapIndex] = Shaders::DefaultColorTextureIndex;
            break;
        case TextureType::Normal:
            s_StaticSceneData.TextureMap[mapIndex] = Shaders::DefaultNormalTextureIndex;
            break;
        case TextureType::Roughness:
            s_StaticSceneData.TextureMap[mapIndex] = Shaders::DefaultRoughnessTextureIndex;
            break;
        case TextureType::Metalic:
            s_StaticSceneData.TextureMap[mapIndex] = Shaders::DefaultMetalicTextureIndex;
            break;
        default:
            throw error("Unsupported Texture Type");
        }
    }

    if (SetupPipeline())
        RecreateDescriptorSet();

    s_TextureUploader->UploadTextures(scene);

    s_StaticSceneData.SceneShaderBindingTable->Upload(s_Pipeline);
}

void Renderer::UpdateTexture(uint32_t index)
{
    s_StaticSceneData.TextureMap[index] = index;

    if (s_DescriptorSet == nullptr)
        return;

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
        s_DescriptorSet->UpdateImage(
            4, frameIndex, s_StaticSceneData.Textures[index], s_TextureSampler,
            vk::ImageLayout::eShaderReadOnlyOptimal, index
        );
}

uint32_t Renderer::AddDefaultTexture(glm::u8vec4 value, std::string &&name)
{
    vk::Extent2D extent = { 1, 1 };
    vk::Format format = vk::Format::eR8G8B8A8Unorm;

    Image image = ImageBuilder()
                      .SetFormat(format)
                      .SetUsageFlags(
                          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                          vk::ImageUsageFlagBits::eTransferSrc
                      )
                      .EnableMips()
                      .CreateImage(extent, name);

    s_StagingBuffer->Upload(ToByteSpan(value));

    s_MainCommandBuffer->Begin();
    image.UploadStaging(
        s_MainCommandBuffer->Buffer, s_MainCommandBuffer->Buffer, *s_StagingBuffer, *s_StagingImage, extent,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );
    s_MainCommandBuffer->SubmitBlocking();

    s_StaticSceneData.Textures.push_back(std::move(image));
    return s_StaticSceneData.Textures.size() - 1;
}

void Renderer::AddSkybox(const Skybox2D &skybox)
{
    // TOOD: HDR
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    vk::Extent2D extent(skybox.Content.Width, skybox.Content.Height);

    assert(std::has_single_bit(extent.width) && std::has_single_bit(extent.height));

    s_StaticSceneData.Skybox =
        ImageBuilder()
            .SetFormat(format)
            .SetUsageFlags(
                vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eTransferSrc
            )
            .EnableMips()
            .CreateImageUnique(extent);

    std::byte *data = AssetManager::LoadTextureData(skybox.Content);
    s_StagingBuffer->Upload(std::span(data, Image::GetByteSize(extent, format)));

    s_MainCommandBuffer->Begin();
    s_StaticSceneData.Skybox->UploadStaging(
        s_MainCommandBuffer->Buffer, s_MainCommandBuffer->Buffer, *s_StagingBuffer, *s_StagingImage, extent,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );
    s_MainCommandBuffer->SubmitBlocking();

    AssetManager::ReleaseTextureData(data);
    s_MissFlags = Shaders::MissFlagsSkybox2D;
}

void Renderer::AddSkybox(const SkyboxCube &skybox)
{
    std::array<const TextureInfo *, 6> textureInfos = { &skybox.Front, &skybox.Back, &skybox.Up,
                                                        &skybox.Down,  &skybox.Left, &skybox.Right };

    // TOOD: HDR
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    vk::Extent2D extent(textureInfos[0]->Width, textureInfos[0]->Height);

    assert(std::has_single_bit(extent.width) && std::has_single_bit(extent.height));

    s_StaticSceneData.Skybox =
        ImageBuilder()
            .SetFormat(format)
            .SetUsageFlags(
                vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eTransferSrc
            )
            .EnableMips()
            .EnableCube()
            .CreateImageUnique(extent);

    for (int i = 0; i < textureInfos.size(); i++)
    {
        assert(textureInfos[i]->Width == extent.width && textureInfos[i]->Height == extent.height);

        std::byte *data = AssetManager::LoadTextureData(*textureInfos[i]);
        
        const vk::DeviceSize layerSize = Image::GetByteSize(extent, format);
        s_StagingBuffer->Upload(std::span(data, layerSize), i * layerSize);

        AssetManager::ReleaseTextureData(data);
    }

    s_MainCommandBuffer->Begin();
    s_StaticSceneData.Skybox->UploadStaging(
        s_MainCommandBuffer->Buffer, s_MainCommandBuffer->Buffer, *s_StagingBuffer, *s_StagingImage, extent,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );
    s_MainCommandBuffer->SubmitBlocking();

    s_MissFlags = Shaders::MissFlagsSkyboxCube;
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
            { 4, vk::DescriptorType::eCombinedImageSampler,
              static_cast<uint32_t>(s_StaticSceneData.TextureMap.size()),
              vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR },
            true
        )
        .SetDescriptor({ 5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR })
        .SetDescriptor({ 6, vk::DescriptorType::eStorageBuffer, 1,
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR })
        .SetDescriptor({ 7, vk::DescriptorType::eStorageBuffer, 1,
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR })
        .SetDescriptor({ 8, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eMissKHR })
        .SetDescriptor(
            { 9, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eMissKHR }, true
        )
        .SetDescriptor(
            { 10, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eMissKHR }, true
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

    s_StaticSceneData.SceneShaderBindingTable->Upload(s_Pipeline);
}

void Renderer::RecordCommandBuffer(const RenderingResources &resources)
{
    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Image image = s_Swapchain->GetCurrentFrame().Image;
    vk::ImageView imageView = s_Swapchain->GetCurrentFrame().ImageView;
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
            s_StaticSceneData.SceneShaderBindingTable->GetRaygenTableEntry(),
            s_StaticSceneData.SceneShaderBindingTable->GetMissTableEntry(),
            s_StaticSceneData.SceneShaderBindingTable->GetClosestHitTableEntry(),
            vk::StridedDeviceAddressRegionKHR(), extent.width, extent.height, 1,
            Application::GetDispatchLoader()
        );

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal
        );

        resources.StorageImage.Transition(
            commandBuffer, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal
        );

        vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        vk::Offset3D offset(0, 0, 0);
        vk::ImageCopy copy(subresource, offset, subresource, offset, vk::Extent3D(extent, 1));

        commandBuffer.copyImage(
            resources.StorageImage.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, image,
            vk::ImageLayout::eTransferDstOptimal, { copy }
        );

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
            vk::RenderingAttachmentInfo(imageView, vk::ImageLayout::eAttachmentOptimal)
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
            0, frameIndex, { s_StaticSceneData.SceneAccelerationStructure->GetTlas() }
        );
        s_DescriptorSet->UpdateImage(
            1, frameIndex, res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_DescriptorSet->UpdateBuffer(2, frameIndex, res.RaygenUniformBuffer);
        s_DescriptorSet->UpdateBuffer(3, frameIndex, res.ClosestHitUniformBuffer);
        s_DescriptorSet->UpdateImageArray(
            4, frameIndex, s_StaticSceneData.Textures, s_StaticSceneData.TextureMap, s_TextureSampler,
            vk::ImageLayout::eShaderReadOnlyOptimal
        );
        s_DescriptorSet->UpdateBuffer(5, frameIndex, *s_StaticSceneData.TransformBuffer);
        s_DescriptorSet->UpdateBuffer(6, frameIndex, *s_StaticSceneData.GeometryBuffer);
        s_DescriptorSet->UpdateBuffer(7, frameIndex, *s_StaticSceneData.MaterialBuffer);
        s_DescriptorSet->UpdateBuffer(8, frameIndex, res.MissUniformBuffer);
        if ((s_MissFlags & Shaders::MissFlagsSkybox2D) != Shaders::MissFlagsNone)
            s_DescriptorSet->UpdateImage(
                9, frameIndex, *s_StaticSceneData.Skybox, s_TextureSampler,
                vk::ImageLayout::eShaderReadOnlyOptimal
            );
        if ((s_MissFlags & Shaders::MissFlagsSkyboxCube) != Shaders::MissFlagsNone)
            s_DescriptorSet->UpdateImage(
                10, frameIndex, *s_StaticSceneData.Skybox, s_TextureSampler,
                vk::ImageLayout::eShaderReadOnlyOptimal
            );
    }
}

void Renderer::OnUpdate(float /* timeStep */)
{
    if (s_RenderingResources.size() < s_Swapchain->GetInFlightCount())
        OnInFlightCountChange();

    {
        std::lock_guard lock(s_DescriptorSetMutex);
        s_DescriptorSet->FlushUpdate(s_Swapchain->GetCurrentFrameInFlightIndex());
    }
}

void Renderer::Render(const Camera &camera)
{
    Shaders::RaygenUniformData rgenData = { camera.GetInvViewMatrix(), camera.GetInvProjectionMatrix(),
                                            s_RaygenFlags };
    Shaders::ClosestHitUniformData rchitData = { s_RenderMode, s_EnabledTextures, s_ClosestHitFlags };
    Shaders::MissUniformData missData = { s_MissFlags };

    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    const RenderingResources &res = s_RenderingResources[s_Swapchain->GetCurrentFrameInFlightIndex()];

    res.RaygenUniformBuffer.Upload(&rgenData);
    res.MissUniformBuffer.Upload(&missData);
    res.ClosestHitUniformBuffer.Upload(&rchitData);

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
