#include "Core/Core.h"

#include "AccelerationStructure.h"
#include "Application.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

AccelerationStructure::AccelerationStructure(
    const Buffer &vertexBuffer, const Buffer &indexBuffer, const Buffer &transformBuffer, const Scene &scene
)
    : m_VertexBuffer(vertexBuffer), m_IndexBuffer(indexBuffer), m_TransformBuffer(transformBuffer),
      m_Scene(scene),
      m_ScratchOffsetAlignment(
          DeviceContext::GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment
      )
{
}

AccelerationStructure::~AccelerationStructure()
{
    for (vk::AccelerationStructureKHR blas : m_Blases)
        DeviceContext::GetLogical().destroyAccelerationStructureKHR(
            blas, nullptr, Application::GetDispatchLoader()
        );
    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        m_Tlas, nullptr, Application::GetDispatchLoader()
    );
}

void AccelerationStructure::Build()
{
    Timer timer("Acceleration Structure Build");
    BuildBlases();
    BuildTlas();
}

vk::AccelerationStructureKHR AccelerationStructure::GetTlas() const
{
    return m_Tlas;
}

void AccelerationStructure::BuildBlases()
{
    struct BlasInfo
    {
        std::vector<vk::AccelerationStructureBuildRangeInfoKHR> Ranges;
        std::vector<vk::AccelerationStructureGeometryKHR> Geometries;
        vk::AccelerationStructureBuildGeometryInfoKHR BuildInfo;

        vk::DeviceSize BlasBufferOffset = 0;
        vk::DeviceSize BlasScratchBufferOffset = 0;
        vk::DeviceSize BlasBufferSize = 0;
    };

    std::vector<BlasInfo> blasInfos = {};

    vk::DeviceSize totalBlasBufferSize = 0;
    vk::DeviceSize totalBlasScratchBufferSize = 0;

    // Gather info about the BLASes
    for (const auto &model : m_Scene.GetModels())
    {
        std::vector<uint32_t> primitiveCounts = {};

        BlasInfo &blasInfo = blasInfos.emplace_back();
        blasInfo.Ranges.reserve(model.Meshes.size());
        blasInfo.Geometries.reserve(model.Meshes.size());

        for (const auto &mesh : model.Meshes)
        {
            const Geometry geometry = m_Scene.GetGeometries()[mesh.GeometryIndex];
            const bool hasTransform = mesh.TransformBufferOffset != Scene::IdentityTransformIndex;

            vk::AccelerationStructureGeometryTrianglesDataKHR geometryData(
                vk::Format::eR32G32B32Sfloat, m_VertexBuffer.GetDeviceAddress(), sizeof(Shaders::Vertex),
                geometry.VertexLength - 1, vk::IndexType::eUint32, m_IndexBuffer.GetDeviceAddress(),
                hasTransform ? m_TransformBuffer.GetDeviceAddress() : vk::DeviceOrHostAddressConstKHR()
            );

            blasInfo.Geometries.emplace_back(
                vk::GeometryTypeKHR::eTriangles, geometryData,
                geometry.IsOpaque ? vk::GeometryFlagBitsKHR::eOpaque
                                  : vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation
            );

            primitiveCounts.push_back(geometry.IndexLength / 3);

            blasInfo.Ranges.emplace_back(
                geometry.IndexLength / 3, geometry.IndexOffset * static_cast<uint32_t>(sizeof(uint32_t)),
                geometry.VertexOffset,
                static_cast<uint32_t>(
                    hasTransform ? mesh.TransformBufferOffset * sizeof(vk::TransformMatrixKHR) : 0
                )
            );
        }

        blasInfo.BuildInfo = vk::AccelerationStructureBuildGeometryInfoKHR(
                                 vk::AccelerationStructureTypeKHR::eBottomLevel,
                                 vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
        )
                                 .setGeometries(blasInfo.Geometries);

        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
            DeviceContext::GetLogical().getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, blasInfo.BuildInfo, primitiveCounts,
                Application::GetDispatchLoader()
            );

        blasInfo.BlasBufferOffset = totalBlasBufferSize;
        blasInfo.BlasScratchBufferOffset = totalBlasScratchBufferSize;
        blasInfo.BlasBufferSize = buildSizesInfo.accelerationStructureSize;
        totalBlasBufferSize += Utils::AlignTo(buildSizesInfo.accelerationStructureSize, 256);
        totalBlasScratchBufferSize += Utils::AlignTo(buildSizesInfo.buildScratchSize, m_ScratchOffsetAlignment);
    }

    auto builder = BufferBuilder().SetUsageFlags(
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
        vk::BufferUsageFlagBits::eShaderDeviceAddress
    );
    m_BlasBuffer = builder.CreateDeviceBufferUnique(totalBlasBufferSize, "BLAS Buffer");

    builder.SetAlignment(m_ScratchOffsetAlignment);
    Buffer blasScratchBuffer = builder.CreateDeviceBuffer(totalBlasScratchBufferSize, "BLAS Scratch Buffer");

    // Create the BLASes
    for (uint32_t i = 0; i < blasInfos.size(); i++)
    {
        BlasInfo &blasInfo = blasInfos[i];

        vk::AccelerationStructureCreateInfoKHR createInfo(
            vk::AccelerationStructureCreateFlagsKHR(), m_BlasBuffer->GetHandle(), blasInfo.BlasBufferOffset,
            blasInfo.BlasBufferSize, vk::AccelerationStructureTypeKHR::eBottomLevel
        );

        vk::AccelerationStructureKHR blas = DeviceContext::GetLogical().createAccelerationStructureKHR(
            createInfo, nullptr, Application::GetDispatchLoader()
        );
        blasInfo.BuildInfo.setDstAccelerationStructure(blas).setScratchData(
            blasScratchBuffer.GetDeviceAddress() + blasInfo.BlasScratchBufferOffset
        );
        m_Blases.push_back(blas);

        Utils::SetDebugName(blas, std::format("BLAS: {}", m_Scene.ModelNames.Get(i)));
    }

    // Build all BLASes at once
    std::vector<const vk::AccelerationStructureBuildRangeInfoKHR *> outRanges = {};
    std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> outBuild = {};
    for (const BlasInfo &info : blasInfos)
    {
        outRanges.push_back(info.Ranges.data());
        outBuild.push_back(info.BuildInfo);
    }

    {
        Utils::DebugLabel label(
            Renderer::s_MainCommandBuffer.CommandBuffer, "BLAS Build", { 0.96f, 0.95f, 0.48f, 1.0f }
        );
        Renderer::s_MainCommandBuffer.Begin();
        Renderer::s_MainCommandBuffer.CommandBuffer.buildAccelerationStructuresKHR(
            outBuild, outRanges, Application::GetDispatchLoader()
        );
        Renderer::s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());
    }
}

void AccelerationStructure::BuildTlas()
{
    std::vector<vk::AccelerationStructureInstanceKHR> instances = {};
    for (const auto &instance : m_Scene.GetModelInstances())
    {
        vk::DeviceAddress address = DeviceContext::GetLogical().getAccelerationStructureAddressKHR(
            { m_Blases[instance.ModelIndex] }, Application::GetDispatchLoader()
        );

        instances.emplace_back(
            TrivialCopy<glm::mat3x4, vk::TransformMatrixKHR>(instance.Transform), 0, 0xff,
            m_Scene.GetModels()[instance.ModelIndex].SbtOffset,
            vk::GeometryInstanceFlagsKHR(), address
        );
    }

    auto builder = BufferBuilder().SetUsageFlags(
        vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
        vk::BufferUsageFlagBits::eShaderDeviceAddress
    );

    Buffer instanceBuffer = builder.CreateHostBuffer(std::span(instances), "Instance Buffer");

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData(
        vk::False, instanceBuffer.GetDeviceAddress()
    );

    vk::AccelerationStructureGeometryKHR geometry(
        vk::GeometryTypeKHR::eInstances, instancesData, vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation
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

    m_TlasBuffer = builder.ResetFlags()
                       .SetUsageFlags(
                           vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                           vk::BufferUsageFlagBits::eShaderDeviceAddress
                       )
                       .CreateDeviceBufferUnique(
                           buildSizesInfo.accelerationStructureSize, "Top Level Acceleration Structure Buffer"
                       );

    vk::AccelerationStructureCreateInfoKHR createInfo(
        vk::AccelerationStructureCreateFlagsKHR(), m_TlasBuffer->GetHandle(), 0,
        buildSizesInfo.accelerationStructureSize
    );

    m_Tlas = DeviceContext::GetLogical().createAccelerationStructureKHR(
        createInfo, nullptr, Application::GetDispatchLoader()
    );
    Utils::SetDebugName(m_Tlas, "Top Level Acceleration Structure");

    builder.SetAlignment(m_ScratchOffsetAlignment);
    Buffer scratchBuffer =
        builder.CreateDeviceBuffer(buildSizesInfo.buildScratchSize, "Scratch Buffer (TLAS)");

    vk::AccelerationStructureBuildGeometryInfoKHR geometryInfo =
        vk::AccelerationStructureBuildGeometryInfoKHR(
            vk::AccelerationStructureTypeKHR::eTopLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
        )
            .setGeometries({ geometry })
            .setDstAccelerationStructure(m_Tlas)
            .setScratchData(scratchBuffer.GetDeviceAddress());

    vk::AccelerationStructureBuildRangeInfoKHR rangeInfo(instances.size(), 0, 0, 0);

    {
        Utils::DebugLabel label(
            Renderer::s_MainCommandBuffer.CommandBuffer, "TLAS Build", { 0.89f, 0.96f, 0.13f, 1.0f }
        );
        Renderer::s_MainCommandBuffer.Begin();
        Renderer::s_MainCommandBuffer.CommandBuffer.buildAccelerationStructuresKHR(
            { geometryInfo }, { &rangeInfo }, Application::GetDispatchLoader()
        );
        Renderer::s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());
    }
}

}
