#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <string_view>

#include "Core/Core.h"

#include "DeviceContext.h"
#include "Renderer.h"

#include "Application.h"
#include "UserInterface.h"

namespace PathTracing
{

static inline constexpr uint32_t s_MaxFramesInFlight = 10;

const Swapchain *Renderer::s_Swapchain = nullptr;
vk::Extent2D Renderer::s_Extent = {};

Renderer::CommandBuffer Renderer::s_MainCommandBuffer = { nullptr, nullptr };
vk::CommandPool Renderer::s_MainCommandPool = nullptr;

std::vector<Renderer::RenderingResources> Renderer::s_RenderingResources = {};
std::unique_ptr<Buffer> Renderer::s_UniformBuffer = nullptr;
vk::DescriptorSetLayout Renderer::s_DescriptorSetLayout = nullptr;
vk::PipelineLayout Renderer::s_PipelineLayout = nullptr;
vk::Pipeline Renderer::s_Pipeline = nullptr;
vk::DescriptorPool Renderer::s_DescriptorPool = nullptr;

std::unique_ptr<BufferBuilder> Renderer::s_BufferBuilder = nullptr;
std::unique_ptr<ImageBuilder> Renderer::s_ImageBuilder = nullptr;
std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = nullptr;

Renderer::SceneData Renderer::s_StaticSceneData = {};

void Renderer::Init(const Swapchain *swapchain)
{
    s_Swapchain = swapchain;

    vk::CommandPoolCreateInfo createInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, DeviceContext::GetGraphicsQueueFamilyIndex()
    );
    s_MainCommandPool = DeviceContext::GetLogical().createCommandPool(createInfo);

    s_MainCommandBuffer.Init();

    s_BufferBuilder = std::make_unique<BufferBuilder>();
    s_ImageBuilder = std::make_unique<ImageBuilder>();

    s_ShaderLibrary = std::make_unique<ShaderLibrary>();

    CreateScene();
    SetupPipeline();
}

void Renderer::Shutdown()
{
    for (RenderingResources &res : s_RenderingResources)
    {
        DeviceContext::GetLogical().destroyCommandPool(res.CommandPool);
    }
    s_RenderingResources.clear();

    DeviceContext::GetLogical().destroyDescriptorPool(s_DescriptorPool);
    DeviceContext::GetLogical().destroyPipeline(s_Pipeline);

    DeviceContext::GetLogical().destroyPipelineLayout(s_PipelineLayout);
    DeviceContext::GetLogical().destroyDescriptorSetLayout(s_DescriptorSetLayout);

    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        s_StaticSceneData.TopLevelAccelerationStructure, nullptr, Application::GetDispatchLoader()
    );

    s_StaticSceneData.TopLevelAccelerationStructureBuffer.reset();

    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        s_StaticSceneData.BottomLevelAccelerationStructure, nullptr, Application::GetDispatchLoader()
    );
    s_StaticSceneData.BottomLevelAccelerationStructureBuffer.reset();

    s_UniformBuffer.reset();
    s_StaticSceneData.TransformMatrixBuffer.reset();
    s_StaticSceneData.IndexBuffer.reset();
    s_StaticSceneData.VertexBuffer.reset();

    s_ShaderLibrary.reset();
    s_ImageBuilder.reset();
    s_BufferBuilder.reset();

    s_MainCommandBuffer.Destroy();

    DeviceContext::GetLogical().destroyCommandPool(s_MainCommandPool);
}

void Renderer::CreateScene()
{
    struct Vertex
    {
        float pos[3];
    };
    std::vector<Vertex> vertices = { { { 1.0f, 1.0f, 0.0f } },
                                     { { -1.0f, 1.0f, 0.0f } },
                                     { { 1.0f, -1.0f, 0.0f } },
                                     { { -1.0f, -1.0f, 0.0f } } };
    std::vector<uint32_t> indices = { 0, 1, 2, 3, 1, 2 };

    vk::TransformMatrixKHR matrix(std::array<std::array<float, 4>, 3>({
        { { 1.0f, 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 1.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 1.0f, 0.0f } },
    }));

    {
        s_BufferBuilder->ResetFlags()
            .SetUsageFlags(
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress
            )
            .SetMemoryFlags(
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            )
            .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

        s_StaticSceneData.VertexBuffer =
            s_BufferBuilder->CreateBufferUnique(vertices.size() * sizeof(Vertex));
        s_StaticSceneData.VertexBuffer->Upload(vertices.data());

        s_StaticSceneData.IndexBuffer =
            s_BufferBuilder->CreateBufferUnique(indices.size() * sizeof(uint32_t));
        s_StaticSceneData.IndexBuffer->Upload(indices.data());

        s_StaticSceneData.TransformMatrixBuffer = s_BufferBuilder->CreateBufferUnique(sizeof(matrix));
        s_StaticSceneData.TransformMatrixBuffer->Upload(matrix.matrix.data());

        s_BufferBuilder->SetUsageFlags(
            vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
        );
        s_UniformBuffer = s_BufferBuilder->CreateBufferUnique(2 * sizeof(glm::mat4));
    }

    {
        vk::AccelerationStructureGeometryTrianglesDataKHR geometryData(
            vk::Format::eR32G32B32Sfloat, s_StaticSceneData.VertexBuffer->GetDeviceAddress(), sizeof(Vertex),
            vertices.size() - 1, vk::IndexType::eUint32, s_StaticSceneData.IndexBuffer->GetDeviceAddress(),
            s_StaticSceneData.TransformMatrixBuffer->GetDeviceAddress()
        );

        vk::AccelerationStructureGeometryKHR geometry(
            vk::GeometryTypeKHR::eTriangles, geometryData, vk::GeometryFlagBitsKHR::eOpaque
        );

        vk::AccelerationStructureBuildGeometryInfoKHR bottomBuildInfo =
            vk::AccelerationStructureBuildGeometryInfoKHR(
                vk::AccelerationStructureTypeKHR::eBottomLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
                .setGeometries({ geometry });

        const uint32_t primitiveCount[] = { 2 };
        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
            DeviceContext::GetLogical().getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, bottomBuildInfo, primitiveCount,
                Application::GetDispatchLoader()
            );

        s_StaticSceneData.BottomLevelAccelerationStructureBuffer =
            s_BufferBuilder->ResetFlags()
                .SetUsageFlags(
                    vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress
                )
                .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
                .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress)
                .CreateBufferUnique(buildSizesInfo.accelerationStructureSize);

        Buffer scratchBuffer = s_BufferBuilder->CreateBuffer(buildSizesInfo.buildScratchSize);

        vk::AccelerationStructureCreateInfoKHR createInfo(
            vk::AccelerationStructureCreateFlagsKHR(),
            s_StaticSceneData.BottomLevelAccelerationStructureBuffer->GetHandle(), 0,
            buildSizesInfo.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel
        );

        s_StaticSceneData.BottomLevelAccelerationStructure =
            DeviceContext::GetLogical().createAccelerationStructureKHR(
                createInfo, nullptr, Application::GetDispatchLoader()
            );

        bottomBuildInfo.setDstAccelerationStructure(s_StaticSceneData.BottomLevelAccelerationStructure)
            .setScratchData(scratchBuffer.GetDeviceAddress());

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo(2, 0, 0, 0);

        s_MainCommandBuffer.Begin();
        s_MainCommandBuffer.CommandBuffer.buildAccelerationStructuresKHR(
            { bottomBuildInfo }, { &rangeInfo }, Application::GetDispatchLoader()
        );
        s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo(
            s_StaticSceneData.BottomLevelAccelerationStructure
        );
        s_StaticSceneData.BottomLevelAccelerationStructureAddress =
            DeviceContext::GetLogical().getAccelerationStructureAddressKHR(
                addressInfo, Application::GetDispatchLoader()
            );
    }

    {
        vk::AccelerationStructureInstanceKHR instance(
            matrix, 0, 0xff, 0, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable,
            s_StaticSceneData.BottomLevelAccelerationStructureAddress
        );

        Buffer instanceBuffer =
            s_BufferBuilder->ResetFlags()
                .SetUsageFlags(
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress
                )
                .SetMemoryFlags(
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
                )
                .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress)
                .CreateBuffer(sizeof(vk::AccelerationStructureInstanceKHR));

        instanceBuffer.Upload(&instance);

        vk::AccelerationStructureGeometryInstancesDataKHR instancesData(
            vk::False, instanceBuffer.GetDeviceAddress()
        );

        vk::AccelerationStructureGeometryKHR geometry(
            vk::GeometryTypeKHR::eInstances, instancesData, vk::GeometryFlagBitsKHR::eOpaque
        );

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo =
            vk::AccelerationStructureBuildGeometryInfoKHR(
                vk::AccelerationStructureTypeKHR::eTopLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
                .setGeometries({ geometry });

        const uint32_t primitiveCount[] = { 1 };
        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
            DeviceContext::GetLogical().getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, primitiveCount,
                Application::GetDispatchLoader()
            );

        s_StaticSceneData.TopLevelAccelerationStructureBuffer =
            s_BufferBuilder->ResetFlags()
                .SetUsageFlags(
                    vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress
                )
                .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
                .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress)
                .CreateBufferUnique(buildSizesInfo.accelerationStructureSize);

        vk::AccelerationStructureCreateInfoKHR createInfo(
            vk::AccelerationStructureCreateFlagsKHR(),
            s_StaticSceneData.TopLevelAccelerationStructureBuffer->GetHandle(), 0,
            buildSizesInfo.accelerationStructureSize
        );

        s_StaticSceneData.TopLevelAccelerationStructure =
            DeviceContext::GetLogical().createAccelerationStructureKHR(
                createInfo, nullptr, Application::GetDispatchLoader()
            );

        Buffer scratchBuffer = s_BufferBuilder->CreateBuffer(buildSizesInfo.buildScratchSize);

        vk::AccelerationStructureBuildGeometryInfoKHR geometryInfo =
            vk::AccelerationStructureBuildGeometryInfoKHR(
                vk::AccelerationStructureTypeKHR::eTopLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
                .setGeometries({ geometry })
                .setDstAccelerationStructure(s_StaticSceneData.TopLevelAccelerationStructure)
                .setScratchData(scratchBuffer.GetDeviceAddress());

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo(1, 0, 0, 0);

        s_MainCommandBuffer.Begin();
        s_MainCommandBuffer.CommandBuffer.buildAccelerationStructuresKHR(
            { geometryInfo }, { &rangeInfo }, Application::GetDispatchLoader()
        );
        s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo(
            s_StaticSceneData.TopLevelAccelerationStructure
        );
        s_StaticSceneData.TopLevelAccelerationStructureAddress =
            DeviceContext::GetLogical().getAccelerationStructureAddressKHR(
                addressInfo, Application::GetDispatchLoader()
            );
    }
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
        vk::DescriptorSetLayoutBinding structure(
            0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR
        );

        vk::DescriptorSetLayoutBinding result(
            1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR
        );

        vk::DescriptorSetLayoutBinding uniform(
            2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR
        );

        std::vector<vk::DescriptorSetLayoutBinding> bindings = { structure, result, uniform };

        vk::DescriptorSetLayoutCreateInfo createInfo(vk::DescriptorSetLayoutCreateFlags(), bindings);
        s_DescriptorSetLayout = DeviceContext::GetLogical().createDescriptorSetLayout(createInfo);
    }

    {
        vk::PipelineLayoutCreateInfo createInfo(vk::PipelineLayoutCreateFlags(), { s_DescriptorSetLayout });
        s_PipelineLayout = DeviceContext::GetLogical().createPipelineLayout(createInfo);
    }

    {
        s_ShaderLibrary->AddRaygenShader("Shaders/raygen.spv", "main");
        s_ShaderLibrary->AddMissShader("Shaders/miss.spv", "main");
        s_ShaderLibrary->AddClosestHitShader("Shaders/closesthit.spv", "main");
        s_Pipeline = s_ShaderLibrary->CreatePipeline(s_PipelineLayout, Application::GetDispatchLoader());
    }

    {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR, 1),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1),
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1)
        };

        vk::DescriptorPoolCreateInfo createInfo(
            vk::DescriptorPoolCreateFlags(), s_MaxFramesInFlight, poolSizes
        );
        s_DescriptorPool = DeviceContext::GetLogical().createDescriptorPool(createInfo);
    }
}

void Renderer::RecordCommandBuffer(const RenderingResources &resources)
{
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

    vk::CommandBuffer commandBuffer = resources.CommandBuffer;
    vk::Image image = s_Swapchain->GetCurrentFrame().Image;
    vk::ImageView imageView = s_Swapchain->GetCurrentFrame().ImageView;

    commandBuffer.begin(vk::CommandBufferBeginInfo());

    vk::StridedDeviceAddressRegionKHR callableShaderEntry;

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, s_Pipeline);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR, s_PipelineLayout, 0, { resources.DescriptorSet }, {}
    );

    vk::Extent2D extent = s_Swapchain->GetExtent();
    commandBuffer.traceRaysKHR(
        s_ShaderLibrary->GetRaygenTableEntry(), s_ShaderLibrary->GetMissTableEntry(),
        s_ShaderLibrary->GetClosestHitTableEntry(), callableShaderEntry, extent.width, extent.height, 1,
        Application::GetDispatchLoader()
    );

    ImageTransition(
        commandBuffer, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer
    );

    ImageTransition(
        commandBuffer, resources.StorageImage->GetHandle(), vk::ImageLayout::eGeneral,
        vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferRead,
        vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer
    );

    vk::ImageSubresourceLayers subresource(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    vk::Offset3D offset(0, 0, 0);
    vk::ImageCopy copy(subresource, offset, subresource, offset, vk::Extent3D(extent, 1));

    commandBuffer.copyImage(
        resources.StorageImage->GetHandle(), vk::ImageLayout::eTransferSrcOptimal, image,
        vk::ImageLayout::eTransferDstOptimal, { copy }
    );

    ImageTransition(
        commandBuffer, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal,
        vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eAllCommands
    );

    ImageTransition(
        commandBuffer, resources.StorageImage->GetHandle(), vk::ImageLayout::eTransferSrcOptimal,
        vk::ImageLayout::eGeneral, vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eNone,
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe
    );

    std::vector<vk::RenderingAttachmentInfo> colorAttachments = {
        vk::RenderingAttachmentInfo(imageView, vk::ImageLayout::eColorAttachmentOptimal)
    };
    commandBuffer.beginRendering(
        vk::RenderingInfo(vk::RenderingFlags(), vk::Rect2D({}, extent), 1, 0, colorAttachments)
    );
    UserInterface::Render(commandBuffer);
    commandBuffer.endRendering();

    ImageTransition(
        commandBuffer, image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eNone,
        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe
    );

    commandBuffer.end();
}

void Renderer::OnUpdate(float timeStep)
{
    if (s_Swapchain->GetExtent() != s_Extent)
    {
        s_Extent = s_Swapchain->GetExtent();
        OnResize();
    }

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

        vk::Extent2D extent = s_Swapchain->GetExtent();
        res.StorageImage = CreateStorageImage(extent);

        vk::DescriptorSetAllocateInfo allocateInfo(s_DescriptorPool, { s_DescriptorSetLayout });
        res.DescriptorSet = DeviceContext::GetLogical().allocateDescriptorSets(allocateInfo)[0];

        std::vector<vk::AccelerationStructureKHR> accelerationStructures = {
            s_StaticSceneData.TopLevelAccelerationStructure
        };
        vk::WriteDescriptorSetAccelerationStructureKHR structureInfo(accelerationStructures);

        vk::WriteDescriptorSet structureWrite =
            vk::WriteDescriptorSet(res.DescriptorSet, 0, 0, 1, vk::DescriptorType::eAccelerationStructureKHR)
                .setPNext(&structureInfo);

        vk::DescriptorImageInfo imageInfo =
            vk::DescriptorImageInfo(vk::Sampler(), res.StorageImage->GetView(), vk::ImageLayout::eGeneral);
        vk::WriteDescriptorSet imageWrite =
            vk::WriteDescriptorSet(res.DescriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage)
                .setPImageInfo(&imageInfo);

        vk::DescriptorBufferInfo bufferInfo =
            vk::DescriptorBufferInfo(s_UniformBuffer->GetHandle(), 0, s_UniformBuffer->GetSize());
        vk::WriteDescriptorSet bufferWrite =
            vk::WriteDescriptorSet(res.DescriptorSet, 2, 0, 1, vk::DescriptorType::eUniformBuffer)
                .setPBufferInfo(&bufferInfo);

        std::vector<vk::WriteDescriptorSet> sets = { structureWrite, imageWrite, bufferWrite };

        DeviceContext::GetLogical().updateDescriptorSets(sets, {});

        s_RenderingResources.emplace_back(std::move(res));
    }
}

void Renderer::OnResize()
{
    for (RenderingResources &res : s_RenderingResources)
    {
        res.StorageImage = CreateStorageImage(s_Extent);

        vk::DescriptorImageInfo imageInfo =
            vk::DescriptorImageInfo(vk::Sampler(), res.StorageImage->GetView(), vk::ImageLayout::eGeneral);

        vk::WriteDescriptorSet imageWrite =
            vk::WriteDescriptorSet(res.DescriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage)
                .setPImageInfo(&imageInfo);

        DeviceContext::GetLogical().updateDescriptorSets({ imageWrite }, {});
    }
}

void Renderer::Render(uint32_t frameInFlightIndex, const Camera &camera)
{
    glm::mat4 uniformData[2] = { camera.GetInvViewMatrix(), camera.GetInvProjectionMatrix() };
    s_UniformBuffer->Upload(uniformData);

    const Swapchain::SynchronizationObjects &sync = s_Swapchain->GetCurrentSyncObjects();
    const RenderingResources &res = s_RenderingResources[frameInFlightIndex];

    RecordCommandBuffer(res);

    std::vector<vk::PipelineStageFlags> stages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    std::vector<vk::CommandBuffer> commandBuffers = { res.CommandBuffer };

    vk::SubmitInfo submitInfo(
        { sync.ImageAcquiredSemaphore }, stages, commandBuffers, { sync.RenderCompleteSemaphore }
    );

    DeviceContext::GetGraphicsQueue().submit({ submitInfo }, sync.InFlightFence);
}

void Renderer::ImageTransition(
    vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
    vk::AccessFlagBits accessFrom, vk::AccessFlagBits accessTo, vk::PipelineStageFlagBits stageFrom,
    vk::PipelineStageFlagBits stageTo
)
{
    vk::ImageMemoryBarrier barrier(
        accessFrom, accessTo, layoutFrom, layoutTo, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image,
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    );

    buffer.pipelineBarrier(stageFrom, stageTo, vk::DependencyFlags(), {}, {}, { barrier });
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
