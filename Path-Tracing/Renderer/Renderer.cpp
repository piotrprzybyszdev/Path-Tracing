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
std::unique_ptr<MaterialSystem> Renderer::s_MaterialSystem = nullptr;

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
    s_MaterialSystem = std::make_unique<MaterialSystem>();

    {
        vk::SamplerCreateInfo createInfo(vk::SamplerCreateFlags(), vk::Filter::eLinear, vk::Filter::eLinear);
        s_Sampler = DeviceContext::GetLogical().createSampler(createInfo);
    }
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

    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        s_StaticSceneData.TopLevelAccelerationStructure, nullptr, Application::GetDispatchLoader()
    );

    s_StaticSceneData.TopLevelAccelerationStructureBuffer.reset();

    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        s_StaticSceneData.BottomLevelAccelerationStructure1, nullptr, Application::GetDispatchLoader()
    );
    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        s_StaticSceneData.BottomLevelAccelerationStructure2, nullptr, Application::GetDispatchLoader()
    );
    s_StaticSceneData.BottomLevelAccelerationStructureBuffer1.reset();
    s_StaticSceneData.BottomLevelAccelerationStructureBuffer2.reset();

    s_RaygenUniformBuffer.reset();
    s_ClosestHitUniformBuffer.reset();
    s_StaticSceneData.TransformMatrixBuffer.reset();
    s_StaticSceneData.GeometryBuffer.reset();
    s_StaticSceneData.IndexBuffer.reset();
    s_StaticSceneData.VertexBuffer.reset();

    DeviceContext::GetLogical().destroySampler(s_Sampler);
    s_MaterialSystem.reset();
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
    std::vector<Shaders::Geometry> geometries = {};
    for (int i = 0; i < 6; i++)
    {
        const uint32_t verticesPerGeometry = 4;
        const uint32_t indicesPerGeometry = 6;

        std::vector<uint32_t> temp = { 0, 1, 2, 2, 3, 0 };
        for (auto x : temp)
            indices.push_back(x);

        geometries.push_back({ i * verticesPerGeometry, i * indicesPerGeometry });
    }

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

        // TODO: Use when implementing model loading
        // s_StaticSceneData.TransformMatrixBuffer = s_BufferBuilder->CreateBufferUnique(sizeof(matrix));
        // s_StaticSceneData.TransformMatrixBuffer->Upload(matrix.matrix.data());

        s_BufferBuilder->SetUsageFlags(
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer
        );

        s_StaticSceneData.VertexBuffer =
            s_BufferBuilder->CreateBufferUnique(vertices.size() * sizeof(Shaders::Vertex));
        s_StaticSceneData.VertexBuffer->Upload(vertices.data());

        s_StaticSceneData.IndexBuffer =
            s_BufferBuilder->CreateBufferUnique(indices.size() * sizeof(uint32_t));
        s_StaticSceneData.IndexBuffer->Upload(indices.data());

        s_BufferBuilder->SetUsageFlags(vk::BufferUsageFlagBits::eStorageBuffer);

        s_StaticSceneData.GeometryBuffer =
            s_BufferBuilder->CreateBufferUnique(geometries.size() * sizeof(Shaders::Geometry));
        s_StaticSceneData.GeometryBuffer->Upload(geometries.data());

        s_BufferBuilder->SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer);
        s_RaygenUniformBuffer = s_BufferBuilder->CreateBufferUnique(sizeof(Shaders::RaygenUniformData));
        s_ClosestHitUniformBuffer =
            s_BufferBuilder->CreateBufferUnique(sizeof(Shaders::ClosestHitUniformData));
    }

    {
        std::vector<vk::AccelerationStructureGeometryKHR> geometries = {};
        std::vector<uint32_t> primitiveCounts = {};
        for (uint32_t i = 0; i < 6; i++)
        {
            vk::AccelerationStructureGeometryTrianglesDataKHR geometryData(
                vk::Format::eR32G32B32Sfloat, s_StaticSceneData.VertexBuffer->GetDeviceAddress(),
                sizeof(Shaders::Vertex), 3, vk::IndexType::eUint32,
                s_StaticSceneData.IndexBuffer->GetDeviceAddress()  //,
                // s_StaticSceneData.TransformMatrixBuffer->GetDeviceAddress()
                // TODO: ^^^ Use when implementing model loading
            );

            geometries.emplace_back(vk::AccelerationStructureGeometryKHR(
                vk::GeometryTypeKHR::eTriangles, geometryData, vk::GeometryFlagBitsKHR::eOpaque
            ));
            primitiveCounts.push_back(2);

            s_ShaderLibrary->AddGeometry({ i, i / 2 });
        }

        for (uint32_t i = 0; i < 6; i++)
            s_ShaderLibrary->AddGeometry({ i, 0 });

        vk::AccelerationStructureBuildGeometryInfoKHR bottomBuildInfo =
            vk::AccelerationStructureBuildGeometryInfoKHR(
                vk::AccelerationStructureTypeKHR::eBottomLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
                .setGeometries(geometries);

        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
            DeviceContext::GetLogical().getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, bottomBuildInfo, primitiveCounts,
                Application::GetDispatchLoader()
            );

        s_BufferBuilder->ResetFlags()
            .SetUsageFlags(
                vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress
            )
            .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
            .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);
        s_StaticSceneData.BottomLevelAccelerationStructureBuffer1 =
            s_BufferBuilder->CreateBufferUnique(buildSizesInfo.accelerationStructureSize);
        s_StaticSceneData.BottomLevelAccelerationStructureBuffer2 =
            s_BufferBuilder->CreateBufferUnique(buildSizesInfo.accelerationStructureSize);

        Buffer scratchBuffer1 = s_BufferBuilder->CreateBuffer(buildSizesInfo.buildScratchSize);
        Buffer scratchBuffer2 = s_BufferBuilder->CreateBuffer(buildSizesInfo.buildScratchSize);

        vk::AccelerationStructureCreateInfoKHR createInfo(
            vk::AccelerationStructureCreateFlagsKHR(),
            s_StaticSceneData.BottomLevelAccelerationStructureBuffer1->GetHandle(), 0,
            buildSizesInfo.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel
        );

        s_StaticSceneData.BottomLevelAccelerationStructure1 =
            DeviceContext::GetLogical().createAccelerationStructureKHR(
                createInfo, nullptr, Application::GetDispatchLoader()
            );

        createInfo.setBuffer(s_StaticSceneData.BottomLevelAccelerationStructureBuffer2->GetHandle());

        s_StaticSceneData.BottomLevelAccelerationStructure2 =
            DeviceContext::GetLogical().createAccelerationStructureKHR(
                createInfo, nullptr, Application::GetDispatchLoader()
            );

        bottomBuildInfo.setDstAccelerationStructure(s_StaticSceneData.BottomLevelAccelerationStructure1)
            .setScratchData(scratchBuffer1.GetDeviceAddress());

        auto bottomBuildInfo2 = bottomBuildInfo;
        bottomBuildInfo2.setDstAccelerationStructure(s_StaticSceneData.BottomLevelAccelerationStructure2)
            .setScratchData(scratchBuffer2.GetDeviceAddress());

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfos[] = { { 2, 0, 0, 0 },
                                                                    { 2, 6 * sizeof(uint32_t), 4, 0 },
                                                                    { 2, 12 * sizeof(uint32_t), 8, 0 },
                                                                    { 2, 18 * sizeof(uint32_t), 12, 0 },
                                                                    { 2, 24 * sizeof(uint32_t), 16, 0 },
                                                                    { 2, 30 * sizeof(uint32_t), 20, 0 } };

        s_MainCommandBuffer.Begin();
        s_MainCommandBuffer.CommandBuffer.buildAccelerationStructuresKHR(
            { bottomBuildInfo, bottomBuildInfo2 }, { rangeInfos, rangeInfos },
            Application::GetDispatchLoader()
        );
        s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo(
            s_StaticSceneData.BottomLevelAccelerationStructure1
        );
        s_StaticSceneData.BottomLevelAccelerationStructureAddress1 =
            DeviceContext::GetLogical().getAccelerationStructureAddressKHR(
                addressInfo, Application::GetDispatchLoader()
            );

        addressInfo.setAccelerationStructure(s_StaticSceneData.BottomLevelAccelerationStructure2);
        s_StaticSceneData.BottomLevelAccelerationStructureAddress2 =
            DeviceContext::GetLogical().getAccelerationStructureAddressKHR(
                addressInfo, Application::GetDispatchLoader()
            );
    }

    {
        // TODO: Get rid of this abomination
        glm::mat3x4 mat = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f)));
        vk::TransformMatrixKHR matrix = {};
        memcpy(matrix.matrix.data(), &mat[0][0], 3 * 4 * sizeof(float));

        vk::AccelerationStructureInstanceKHR instance(
            matrix, 0, 0xff, 0, vk::GeometryInstanceFlagsKHR(),
            s_StaticSceneData.BottomLevelAccelerationStructureAddress1
        );

        std::vector<vk::AccelerationStructureInstanceKHR> instances = {};
        instances.push_back(instance);
        mat = glm::transpose(glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -1.0f, -3.0f)), glm::vec3(2.0f, 1.0f, 0.3f)
        ));
        memcpy(matrix.matrix.data(), &mat[0][0], 3 * 4 * sizeof(float));
        instance.setAccelerationStructureReference(s_StaticSceneData.BottomLevelAccelerationStructureAddress2
        );
        instance.setTransform(matrix);
        instance.setInstanceShaderBindingTableRecordOffset(6);  // Because first instance has 6 geometries
        instances.push_back(instance);

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
                .CreateBuffer(sizeof(vk::AccelerationStructureInstanceKHR) * instances.size());

        instanceBuffer.Upload(instances.data());

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

        const uint32_t primitiveCount[] = { static_cast<uint32_t>(instances.size()) };
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

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo(2, 0, 0, 0);

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
        DescriptorSetBuilder builder(s_MaxFramesInFlight);
        builder
            .AddDescriptor(
                vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR
            )
            .AddDescriptor(vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR, true)
            .AddDescriptor(vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR)
            .AddDescriptor(vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR)
            .AddDescriptor(vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR)
            .AddDescriptor(vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR)
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

    commandBuffer.begin(vk::CommandBufferBeginInfo());

    vk::StridedDeviceAddressRegionKHR callableShaderEntry;

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, s_Pipeline);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR, s_PipelineLayout, 0,
        { s_DescriptorSet->GetSet(s_Swapchain->GetCurrentFrameInFlightIndex()) }, {}
    );

    vk::Extent2D extent = s_Swapchain->GetExtent();
    commandBuffer.traceRaysKHR(
        s_ShaderLibrary->GetRaygenTableEntry(), s_ShaderLibrary->GetMissTableEntry(),
        s_ShaderLibrary->GetClosestHitTableEntry(), callableShaderEntry, extent.width, extent.height, 1,
        Application::GetDispatchLoader()
    );

    Image::Transition(commandBuffer, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

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
        commandBuffer, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal
    );

    resources.StorageImage->Transition(
        commandBuffer, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral
    );

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

        vk::Extent2D extent = s_Swapchain->GetExtent();
        res.StorageImage = CreateStorageImage(extent);

        const uint32_t frameIndex = s_RenderingResources.size();
        s_DescriptorSet->UpdateAccelerationStructures(
            0, frameIndex, { s_StaticSceneData.TopLevelAccelerationStructure }
        );
        s_DescriptorSet->UpdateImage(
            1, frameIndex, *res.StorageImage, vk::Sampler(), vk::ImageLayout::eGeneral
        );
        s_DescriptorSet->UpdateBuffer(2, frameIndex, *s_RaygenUniformBuffer);
        s_DescriptorSet->UpdateBuffer(3, frameIndex, *s_ClosestHitUniformBuffer);
        s_DescriptorSet->UpdateBuffer(4, frameIndex, *s_StaticSceneData.VertexBuffer);
        s_DescriptorSet->UpdateBuffer(5, frameIndex, *s_StaticSceneData.IndexBuffer);
        s_DescriptorSet->UpdateImageArray(
            6, frameIndex, MaterialSystem::GetTextures(), s_Sampler, vk::ImageLayout::eShaderReadOnlyOptimal
        );
        s_DescriptorSet->UpdateBuffer(7, frameIndex, *s_StaticSceneData.GeometryBuffer);
        s_DescriptorSet->UpdateBuffer(8, frameIndex, MaterialSystem::GetBuffer());

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
