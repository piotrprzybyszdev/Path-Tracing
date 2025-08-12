#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <memory>
#include <ranges>
#include <set>
#include <string_view>

#include "Core/Core.h"

#include "Shaders/ShaderRendererTypes.incl"

#include "Application.h"
#include "UserInterface.h"

#include "AssetManager.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

Shaders::RenderModeFlags Renderer::s_RenderMode = Shaders::RenderModeColor;
Shaders::EnabledTextureFlags Renderer::s_EnabledTextures = Shaders::TexturesEnableAll;
Shaders::RaygenFlags Renderer::s_RaygenFlags = Shaders::RaygenFlagsNone;
Shaders::ClosestHitFlags Renderer::s_ClosestHitFlags = Shaders::ClosestHitFlagsNone;

const Swapchain *Renderer::s_Swapchain = nullptr;

Renderer::CommandBuffer Renderer::s_MainCommandBuffer = {};
vk::CommandPool Renderer::s_MainCommandPool = nullptr;

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};
std::unique_ptr<DescriptorSetBuilder> Renderer::s_DescriptorSetBuilder = nullptr;
std::unique_ptr<DescriptorSet> Renderer::s_DescriptorSet = nullptr;
vk::PipelineLayout Renderer::s_PipelineLayout = nullptr;
vk::Pipeline Renderer::s_Pipeline = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;

vk::Sampler Renderer::s_Sampler = nullptr;

std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;
Renderer::SceneData Renderer::s_StaticSceneData = {};

void Renderer::Init(const Swapchain *swapchain)
{
    s_Swapchain = swapchain;

    {
        vk::CommandPoolCreateInfo createInfo(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer, DeviceContext::GetGraphicsQueueFamilyIndex()
        );
        s_MainCommandPool = DeviceContext::GetLogical().createCommandPool(createInfo);
        s_MainCommandBuffer.Init();
    }

    s_BufferBuilder = std::make_unique<BufferBuilder>();
    s_ImageBuilder = std::make_unique<ImageBuilder>();

    {
        vk::SamplerCreateInfo createInfo(vk::SamplerCreateFlags(), vk::Filter::eLinear, vk::Filter::eLinear);
        createInfo.setMipmapMode(vk::SamplerMipmapMode::eLinear);
        createInfo.setMaxLod(vk::LodClampNone);
        s_Sampler = DeviceContext::GetLogical().createSampler(createInfo);
        Utils::SetDebugName(s_Sampler, "Texture Sampler");
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
        glm::u8vec4 defaultColor(255);
        glm::u8vec4 defaultNormal(128, 128, 255, 255);
        glm::u8vec4 defaultRoughness(0);
        glm::u8vec4 defaultMetalness(0);

        const vk::Extent2D extent(1, 1);
        const vk::Format format = vk::Format::eR8G8B8A8Unorm;      
        AddTexture(extent, format, reinterpret_cast<const std::byte *>(&defaultColor));
        AddTexture(extent, format, reinterpret_cast<const std::byte *>(&defaultNormal));
        AddTexture(extent, format, reinterpret_cast<const std::byte *>(&defaultRoughness));
        AddTexture(extent, format, reinterpret_cast<const std::byte *>(&defaultMetalness));
        Utils::SetDebugName(s_StaticSceneData.Textures[0].GetHandle(), "Default Color Texture");
        Utils::SetDebugName(s_StaticSceneData.Textures[1].GetHandle(), "Default Color Texture");
        Utils::SetDebugName(s_StaticSceneData.Textures[2].GetHandle(), "Default Color Texture");
        Utils::SetDebugName(s_StaticSceneData.Textures[3].GetHandle(), "Default Color Texture");

        static_assert(Shaders::DefaultColorTextureIndex == 0);
        static_assert(Shaders::DefaultNormalTextureIndex == 1);
        static_assert(Shaders::DefaultRoughnessTextureIndex == 2);
        static_assert(Shaders::DefaultMetalicTextureIndex == 3);
    }
}

void Renderer::Shutdown()
{
    for (RenderingResources &res : s_RenderingResources)
    {
        DeviceContext::GetLogical().destroyCommandPool(res.CommandPool);
    }
    s_RenderingResources.clear();

    s_ShaderLibrary.reset();

    DeviceContext::GetLogical().destroyPipeline(s_Pipeline);
    DeviceContext::GetLogical().destroyPipelineLayout(s_PipelineLayout);
    s_DescriptorSet.reset();
    s_DescriptorSetBuilder.reset();

    s_StaticSceneData.SceneShaderBindingTable.reset();
    s_StaticSceneData.SceneAccelerationStructure.reset();
    s_StaticSceneData.Textures.clear();
    s_StaticSceneData.MaterialBuffer.reset();
    s_StaticSceneData.GeometryBuffer.reset();
    s_StaticSceneData.TransformBuffer.reset();
    s_StaticSceneData.IndexBuffer.reset();
    s_StaticSceneData.VertexBuffer.reset();

    DeviceContext::GetLogical().destroySampler(s_Sampler);
    s_ImageBuilder.reset();
    s_BufferBuilder.reset();

    s_MainCommandBuffer.Destroy();

    DeviceContext::GetLogical().destroyCommandPool(s_MainCommandPool);
}

void Renderer::SetScene(const Scene &scene)
{
    DeviceContext::GetLogical().waitIdle();

    {
        Timer timer("Mesh Upload");
        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &vertices = scene.GetVertices();
        s_StaticSceneData.VertexBuffer = s_BufferBuilder->CreateDeviceBufferUnique(vertices, "Vertex Buffer");

        const auto &indices = scene.GetIndices();
        s_StaticSceneData.IndexBuffer = s_BufferBuilder->CreateDeviceBufferUnique(indices, "Index Buffer");

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
        s_StaticSceneData.TransformBuffer =
            s_BufferBuilder->CreateDeviceBufferUnique(std::span(transforms2), "Transform Buffer");

        s_BufferBuilder->ResetFlags().SetUsageFlags(
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
        );

        const auto &geometries = scene.GetGeometries();
        std::vector<Shaders::Geometry> geometries2 = {};
        for (const auto &geometry : geometries)
            geometries2.emplace_back(
                s_StaticSceneData.VertexBuffer->GetDeviceAddress() +
                    geometry.VertexOffset * sizeof(Shaders::Vertex),
                s_StaticSceneData.IndexBuffer->GetDeviceAddress() + geometry.IndexOffset * sizeof(uint32_t)
            );
        s_StaticSceneData.GeometryBuffer =
            s_BufferBuilder->CreateDeviceBufferUnique(std::span(geometries2), "Geometry Buffer");

        const auto &materials = scene.GetMaterials();
        s_StaticSceneData.MaterialBuffer =
            s_BufferBuilder->CreateDeviceBufferUnique(materials, "Material Buffer");
    }

    {
        Timer timer("Texture Upload");
        s_StaticSceneData.Textures.resize(Shaders::SceneTextureOffset);
        const auto &textures = scene.GetTextures();
        for (const auto &texture : textures)
            AddTexture(texture);
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

    bool isRecreated = SetupPipeline();
    s_StaticSceneData.SceneShaderBindingTable->Upload(s_Pipeline);

    if (isRecreated)
        RecreateDescriptorSet();
}

void Renderer::AddTexture(vk::Extent2D extent, vk::Format format, const std::byte *data)
{
    auto IsPowerOf2 = [](uint32_t num) { return (num & (num - 1)) == 0; };
    assert(IsPowerOf2(extent.width) && IsPowerOf2(extent.height));

    ImageBuilder builder;
    builder.SetFormat(format)
        .SetUsageFlags(
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eTransferSrc
        )
        .EnableMips();

    Image image = builder.CreateImage(std::min(extent, s_MaxTextureSize));
    image.UploadStaging(data, extent, vk::ImageLayout::eShaderReadOnlyOptimal);

    s_StaticSceneData.Textures.push_back(std::move(image));
}

void Renderer::AddTexture(Texture texture)
{
    TextureData data = AssetManager::LoadTextureData(texture);

    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    vk::Extent2D extent = { static_cast<uint32_t>(data.Width), static_cast<uint32_t>(data.Height) };
    AddTexture(extent, format, data.Data.data());

    AssetManager::ReleaseTextureData(data);
}

void Renderer::AddTexture(Texture texture, const std::string &name)
{
    AddTexture(texture);
    Utils::SetDebugName(s_StaticSceneData.Textures.back().GetHandle(), name);
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
        .SetDescriptor({ 4, vk::DescriptorType::eCombinedImageSampler,
                         static_cast<uint32_t>(s_StaticSceneData.Textures.size()),
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR })
        .SetDescriptor({ 5, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR })
        .SetDescriptor({ 6, vk::DescriptorType::eStorageBuffer, 1,
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR })
        .SetDescriptor({ 7, vk::DescriptorType::eStorageBuffer, 1,
                         vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eAnyHitKHR });

    bool isRecreated = s_PipelineLayout != nullptr;
    if (isRecreated)
        DeviceContext::GetLogical().destroyPipelineLayout(s_PipelineLayout);

    std::vector<vk::DescriptorSetLayout> layouts = { s_DescriptorSetBuilder->CreateLayout() };
    vk::PipelineLayoutCreateInfo createInfo(vk::PipelineLayoutCreateFlags(), layouts);
    s_PipelineLayout = DeviceContext::GetLogical().createPipelineLayout(createInfo);

    s_Pipeline = s_ShaderLibrary->CreatePipeline(s_PipelineLayout);
    if (s_Pipeline == nullptr)
        throw error("Failed to create pipeline.");

    return isRecreated;
}

void Renderer::ReloadShaders()
{
    DeviceContext::GetLogical().waitIdle();
    DeviceContext::GetLogical().destroyPipeline(s_Pipeline);
    s_Pipeline = s_ShaderLibrary->CreatePipeline(s_PipelineLayout);
    s_StaticSceneData.SceneShaderBindingTable->Upload(s_Pipeline);
}

void Renderer::RecordCommandBuffer(const RenderingResources &resources)
{
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Image image = s_Swapchain->GetCurrentFrame().Image;
    vk::ImageView imageView = s_Swapchain->GetCurrentFrame().ImageView;
    vk::Extent2D extent = s_Swapchain->GetExtent();

    commandBuffer.begin(vk::CommandBufferBeginInfo());
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

        resources.StorageImage->Transition(
            commandBuffer, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal
        );

        vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        vk::Offset3D offset(0, 0, 0);
        vk::ImageCopy copy(subresource, offset, subresource, offset, vk::Extent3D(extent, 1));

        commandBuffer.copyImage(
            resources.StorageImage->GetHandle(), vk::ImageLayout::eTransferSrcOptimal, image,
            vk::ImageLayout::eTransferDstOptimal, { copy }
        );

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eColorAttachmentOptimal
        );

        resources.StorageImage->Transition(
            commandBuffer, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral
        );
    }

    {
        Utils::DebugLabel label(commandBuffer, "UI pass", { 0.24f, 0.34f, 0.93f, 1.0f });

        std::vector<vk::RenderingAttachmentInfo> colorAttachments = {
            vk::RenderingAttachmentInfo(imageView, vk::ImageLayout::eColorAttachmentOptimal)
        };

        commandBuffer.beginRendering(
            vk::RenderingInfo(vk::RenderingFlags(), vk::Rect2D({}, extent), 1, 0, colorAttachments)
        );
        UserInterface::Render(commandBuffer);
        commandBuffer.endRendering();

        Image::Transition(
            commandBuffer, image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR
        );
    }
    commandBuffer.end();
}

std::unique_ptr<Image> Renderer::CreateStorageImage(vk::Extent2D extent)
{
    s_ImageBuilder->ResetFlags()
        .SetFormat(vk::Format::eR8G8B8A8Unorm)
        .SetUsageFlags(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage);

    auto image = s_ImageBuilder->CreateImageUnique(extent);

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::ImageMemoryBarrier barrier;
    barrier.setNewLayout(vk::ImageLayout::eGeneral).setImage(image->GetHandle()).setSubresourceRange(range);

    s_MainCommandBuffer.Begin();
    s_MainCommandBuffer.CommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands,
        vk::DependencyFlags(), {}, {}, { barrier }
    );
    s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());

    return image;
}

void Renderer::OnResize(vk::Extent2D extent)
{
    for (int i = 0; i < s_RenderingResources.size(); i++)
    {
        RenderingResources &res = s_RenderingResources[i];
        res.StorageImage = CreateStorageImage(extent);
        res.StorageImage->SetDebugName(std::format("Storage Image {}", i));
        s_DescriptorSet->UpdateImage(1, i, *res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral);
    }
}

void Renderer::OnInFlightCountChange()
{
    while (s_RenderingResources.size() < s_Swapchain->GetInFlightCount())
    {
        RenderingResources res;

        vk::CommandPoolCreateInfo commandPoolCreateInfo(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer, DeviceContext::GetGraphicsQueueFamilyIndex()
        );
        res.CommandPool = DeviceContext::GetLogical().createCommandPool(commandPoolCreateInfo);

        vk::CommandBufferAllocateInfo allocateCommandBufferInfo(
            res.CommandPool, vk::CommandBufferLevel::ePrimary, 1
        );
        res.CommandBuffer = DeviceContext::GetLogical().allocateCommandBuffers(allocateCommandBufferInfo)[0];

        const uint32_t frameIndex = s_RenderingResources.size();

        vk::Extent2D extent = s_Swapchain->GetExtent();
        res.StorageImage = CreateStorageImage(extent);
        res.StorageImage->SetDebugName(std::format("Storage image {}", frameIndex));

        s_BufferBuilder->SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer);
        res.RaygenUniformBuffer = s_BufferBuilder->CreateHostBufferUnique(
            sizeof(Shaders::RaygenUniformData), std::format("Raygen Uniform Buffer {}", frameIndex)
        );
        res.ClosestHitUniformBuffer = s_BufferBuilder->CreateHostBufferUnique(
            sizeof(Shaders::ClosestHitUniformData), std::format("Closest Hit Uniform Buffer {}", frameIndex)
        );

        s_RenderingResources.push_back(std::move(res));
    }

    RecreateDescriptorSet();
}

void Renderer::RecreateDescriptorSet()
{
    DeviceContext::GetLogical().waitIdle();
    s_DescriptorSet = s_DescriptorSetBuilder->CreateSetUnique(s_RenderingResources.size());

    for (uint32_t frameIndex = 0; frameIndex < s_RenderingResources.size(); frameIndex++)
    {
        const RenderingResources &res = s_RenderingResources[frameIndex];

        s_DescriptorSet->UpdateAccelerationStructures(
            0, frameIndex, { s_StaticSceneData.SceneAccelerationStructure->GetTlas() }
        );
        s_DescriptorSet->UpdateImage(
            1, frameIndex, *res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_DescriptorSet->UpdateBuffer(2, frameIndex, *res.RaygenUniformBuffer);
        s_DescriptorSet->UpdateBuffer(3, frameIndex, *res.ClosestHitUniformBuffer);
        s_DescriptorSet->UpdateImageArray(
            4, frameIndex, s_StaticSceneData.Textures, s_Sampler, vk::ImageLayout::eShaderReadOnlyOptimal
        );
        s_DescriptorSet->UpdateBuffer(5, frameIndex, *s_StaticSceneData.TransformBuffer);
        s_DescriptorSet->UpdateBuffer(6, frameIndex, *s_StaticSceneData.GeometryBuffer);
        s_DescriptorSet->UpdateBuffer(7, frameIndex, *s_StaticSceneData.MaterialBuffer);
    }
}

void Renderer::OnUpdate(float /* timeStep */)
{
    if (s_RenderingResources.size() < s_Swapchain->GetInFlightCount())
        OnInFlightCountChange();

    s_DescriptorSet->FlushUpdate();
}

void Renderer::Render(const Camera &camera)
{
    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    const RenderingResources &res = s_RenderingResources[s_Swapchain->GetCurrentFrameInFlightIndex()];

    Shaders::RaygenUniformData rgenData = { camera.GetInvViewMatrix(), camera.GetInvProjectionMatrix() };
    res.RaygenUniformBuffer->Upload(&rgenData);

    Shaders::ClosestHitUniformData rchitData = { s_RenderMode, s_EnabledTextures };
    res.ClosestHitUniformBuffer->Upload(&rchitData);


    RecordCommandBuffer(res);

    std::vector<vk::PipelineStageFlags> stages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    std::vector<vk::CommandBuffer> commandBuffers = { res.CommandBuffer };

    vk::SubmitInfo submitInfo(
        { sync.ImageAcquiredSemaphore }, stages, commandBuffers, { sync.RenderCompleteSemaphore }
    );

    DeviceContext::GetGraphicsQueue().submit({ submitInfo }, sync.InFlightFence);
}

void Renderer::CommandBuffer::Init()
{
    vk::CommandBufferAllocateInfo allocateInfo(s_MainCommandPool, vk::CommandBufferLevel::ePrimary, 1);
    CommandBuffer = DeviceContext::GetLogical().allocateCommandBuffers(allocateInfo)[0];
    Fence = DeviceContext::GetLogical().createFence(vk::FenceCreateInfo());
}

void Renderer::CommandBuffer::Destroy()
{
    DeviceContext::GetLogical().freeCommandBuffers(s_MainCommandPool, { CommandBuffer });
    DeviceContext::GetLogical().destroyFence(Fence);
}

void Renderer::CommandBuffer::Begin() const
{
    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    CommandBuffer.begin(beginInfo);
}

void Renderer::CommandBuffer::Submit(vk::Queue queue) const
{
    CommandBuffer.end();
    vk::SubmitInfo submitInfo = {};
    submitInfo.setCommandBuffers({ CommandBuffer });
    DeviceContext::GetLogical().resetFences({ Fence });
    queue.submit({ submitInfo }, Fence);

    try
    {
        vk::Result result = DeviceContext::GetLogical().waitForFences(
            { Fence }, vk::True, std::numeric_limits<uint64_t>::max()
        );
        assert(result == vk::Result::eSuccess);
    }
    catch (const vk::SystemError &err)
    {
        throw PathTracing::error(err.what());
    }
}

}
