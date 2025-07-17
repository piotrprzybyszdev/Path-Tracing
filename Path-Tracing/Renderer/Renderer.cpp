#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

// TODO: Remove
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <string_view>

#include "Core/Core.h"

#include "Shaders/ShaderTypes.incl"

#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

#include "Application.h"
#include "UserInterface.h"

namespace PathTracing
{

static inline constexpr uint32_t s_MaxFramesInFlight = 10;

Shaders::RenderModeFlags Renderer::s_RenderMode = Shaders::RenderModeColor;
Shaders::EnabledTextureFlags Renderer::s_EnabledTextures = Shaders::TexturesEnableAll;

const Swapchain *Renderer::s_Swapchain = nullptr;

Renderer::CommandBuffer Renderer::s_MainCommandBuffer = { nullptr, nullptr };
vk::CommandPool Renderer::s_MainCommandPool = nullptr;

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};
std::unique_ptr<Buffer> Renderer::s_RaygenUniformBuffer = nullptr;
std::unique_ptr<Buffer> Renderer::s_ClosestHitUniformBuffer = nullptr;
std::unique_ptr<DescriptorSet> Renderer::s_DescriptorSet = nullptr;
vk::PipelineLayout Renderer::s_PipelineLayout = nullptr;
vk::Pipeline Renderer::s_Pipeline = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;
std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;

vk::Sampler Renderer::s_Sampler = nullptr;

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

    s_ShaderLibrary = std::make_unique<ShaderLibrary>();

    {
        vk::SamplerCreateInfo createInfo(vk::SamplerCreateFlags(), vk::Filter::eLinear, vk::Filter::eLinear);
        s_Sampler = DeviceContext::GetLogical().createSampler(createInfo);
        Utils::SetDebugName(s_Sampler, vk::ObjectType::eSampler, "Texture Sampler");
    }

    s_StaticSceneData.AcceleraionStructure = std::make_unique<AccelerationStructure>();
}

void Renderer::Shutdown()
{
    for (RenderingResources &res : s_RenderingResources)
    {
        DeviceContext::GetLogical().destroyCommandPool(res.CommandPool);
    }
    s_RenderingResources.clear();

    DeviceContext::GetLogical().destroyPipeline(s_Pipeline);
    DeviceContext::GetLogical().destroyPipelineLayout(s_PipelineLayout);
    s_DescriptorSet.reset();

    s_StaticSceneData.AcceleraionStructure.reset();

    s_RaygenUniformBuffer.reset();
    s_ClosestHitUniformBuffer.reset();

    DeviceContext::GetLogical().destroySampler(s_Sampler);
    s_ShaderLibrary.reset();
    s_ImageBuilder.reset();
    s_BufferBuilder.reset();

    s_MainCommandBuffer.Destroy();

    DeviceContext::GetLogical().destroyCommandPool(s_MainCommandPool);
}

void Renderer::SetScene()
{
    CreateScene();
    SetupPipeline();
}

void Renderer::CreateScene()
{
    std::vector<Shaders::Vertex> vertices = {
        { { -1, -1, 1 }, { 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, -1, 1 }, { 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 1, 1 }, { 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },

        { { 1, -1, -1 }, { 0, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { -1, -1, -1 }, { 1, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { -1, 1, -1 }, { 1, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 1, -1 }, { 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },

        { { -1, -1, -1 }, { 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, -1, 1 }, { 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 1, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, -1 }, { 0, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },

        { { 1, -1, 1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 1, -1, -1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 1, 1, -1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 1, 1, 1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },

        { { -1, 1, 1 }, { 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, 1 }, { 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, -1 }, { 1, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { -1, 1, -1 }, { 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },

        { { -1, -1, -1 }, { 0, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 1, -1, -1 }, { 1, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 1, -1, 1 }, { 1, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { -1, -1, 1 }, { 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
    };

    // TODO: Move to model loading
    std::vector<uint32_t> indices = {};
    for (int i = 0; i < 6; i++)
    {
        const uint32_t verticesPerGeometry = 4;
        const uint32_t indicesPerGeometry = 6;

        std::vector<uint32_t> temp = { 0, 1, 2, 2, 3, 0 };
        for (auto x : temp)
            indices.push_back(x);
    }

    s_BufferBuilder->ResetFlags()
        .SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer)
        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
        .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

    s_RaygenUniformBuffer =
        s_BufferBuilder->CreateBufferUnique(sizeof(Shaders::RaygenUniformData), "Raygen Uniform Buffer");
    s_ClosestHitUniformBuffer = s_BufferBuilder->CreateBufferUnique(
        sizeof(Shaders::ClosestHitUniformData), "Closest Hit Uniform Buffer"
    );

    auto vertexIterator = vertices.begin();
    auto indexIterator = indices.begin();
    for (uint32_t i = 0; i < 6; i++)
    {
        s_StaticSceneData.AcceleraionStructure->AddGeometry(
            std::span(vertexIterator, 4), std::span(indexIterator, 6)
        );
        std::advance(vertexIterator, 4);
        std::advance(indexIterator, 6);
    }

    const uint32_t cubeModelIndex =
        s_StaticSceneData.AcceleraionStructure->AddModel({ 0, 1, 2, 3, 4, 5 }, "Cube");

    // Cube ver1 (1st instance)
    s_StaticSceneData.AcceleraionStructure->AddModelInstance(
        cubeModelIndex, glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f))),
        s_ShaderLibrary->GetGeometryCount()
    );

    // Cube ver1 (2nd instance)
    s_StaticSceneData.AcceleraionStructure->AddModelInstance(
        cubeModelIndex, glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f))),
        s_ShaderLibrary->GetGeometryCount()
    );

    for (uint32_t i = 0; i < 6; i++)
        s_ShaderLibrary->AddGeometry({ i, i / 2 });

    // Cube ver2 (1st instance)
    s_StaticSceneData.AcceleraionStructure->AddModelInstance(
        cubeModelIndex,
        glm::transpose(glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -1.0f, -3.0f)), glm::vec3(2.0f, 1.0f, 0.3f)
        )),
        s_ShaderLibrary->GetGeometryCount()
    );

    for (uint32_t i = 0; i < 6; i++)
        s_ShaderLibrary->AddGeometry({ i, 0 });

    s_StaticSceneData.AcceleraionStructure->Build();
}

std::unique_ptr<Image> Renderer::CreateStorageImage(vk::Extent2D extent)
{
    s_ImageBuilder->ResetFlags()
        .SetFormat(vk::Format::eR8G8B8A8Unorm)
        .SetUsageFlags(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage)
        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal);

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

void Renderer::SetupPipeline()
{
    {
        DescriptorSetBuilder builder(s_MaxFramesInFlight);
        builder
            .AddDescriptor(
                vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR
            )
            .AddDescriptor(vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR, true)
            .AddDescriptor(vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR)
            .AddDescriptor(vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR)
            .AddDescriptor(
                vk::DescriptorType::eCombinedImageSampler, MaterialSystem::GetTextures().size(),
                vk::ShaderStageFlagBits::eClosestHitKHR
            )
            .AddDescriptor(vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR)
            .AddDescriptor(vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR);

        s_DescriptorSet = builder.CreateSetUnique();
    }

    {
        std::vector<vk::DescriptorSetLayout> layouts = { s_DescriptorSet->GetLayout() };
        vk::PipelineLayoutCreateInfo createInfo(vk::PipelineLayoutCreateFlags(), layouts);
        s_PipelineLayout = DeviceContext::GetLogical().createPipelineLayout(createInfo);

        s_ShaderLibrary->AddRaygenShader("Shaders/raygen.spv", "main");
        s_ShaderLibrary->AddMissShader("Shaders/miss.spv", "main");
        s_ShaderLibrary->AddClosestHitShader("Shaders/closesthit.spv", "main");
        s_Pipeline = s_ShaderLibrary->CreatePipeline(s_PipelineLayout, Application::GetDispatchLoader());
    }
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
            s_ShaderLibrary->GetRaygenTableEntry(), s_ShaderLibrary->GetMissTableEntry(),
            s_ShaderLibrary->GetClosestHitTableEntry(), vk::StridedDeviceAddressRegionKHR(), extent.width,
            extent.height, 1, Application::GetDispatchLoader()
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

void Renderer::OnUpdate(float timeStep)
{
    assert(s_Swapchain->GetInFlightCount() < s_MaxFramesInFlight);
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

        s_DescriptorSet->UpdateAccelerationStructures(
            0, frameIndex, { s_StaticSceneData.AcceleraionStructure->GetTlas() }
        );
        s_DescriptorSet->UpdateImage(
            1, frameIndex, *res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_DescriptorSet->UpdateBuffer(2, frameIndex, *s_RaygenUniformBuffer);
        s_DescriptorSet->UpdateBuffer(3, frameIndex, *s_ClosestHitUniformBuffer);
        s_DescriptorSet->UpdateImageArray(
            4, frameIndex, MaterialSystem::GetTextures(), s_Sampler, vk::ImageLayout::eShaderReadOnlyOptimal
        );
        s_DescriptorSet->UpdateBuffer(
            5, frameIndex, s_StaticSceneData.AcceleraionStructure->GetGeometryBuffer()
        );
        s_DescriptorSet->UpdateBuffer(6, frameIndex, MaterialSystem::GetBuffer());

        s_RenderingResources.emplace_back(std::move(res));
    }

    s_DescriptorSet->FlushUpdate();
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

    s_DescriptorSet->FlushUpdate();
}

void Renderer::Render(const Camera &camera)
{
    Shaders::RaygenUniformData rgenData = { camera.GetInvViewMatrix(), camera.GetInvProjectionMatrix() };
    s_RaygenUniformBuffer->Upload(&rgenData);

    Shaders::ClosestHitUniformData rchitData = { s_RenderMode, s_EnabledTextures };
    s_ClosestHitUniformBuffer->Upload(&rchitData);

    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    const RenderingResources &res = s_RenderingResources[s_Swapchain->GetCurrentFrameInFlightIndex()];

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
    s_MainCommandBuffer.CommandBuffer = DeviceContext::GetLogical().allocateCommandBuffers(allocateInfo)[0];
    s_MainCommandBuffer.Fence = DeviceContext::GetLogical().createFence(vk::FenceCreateInfo());
}

void Renderer::CommandBuffer::Destroy()
{
    DeviceContext::GetLogical().freeCommandBuffers(s_MainCommandPool, { s_MainCommandBuffer.CommandBuffer });
    DeviceContext::GetLogical().destroyFence(s_MainCommandBuffer.Fence);
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
    catch (vk::SystemError err)
    {
        throw PathTracing::error(err.what());
    }
}

}
