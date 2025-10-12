#include "Core/Core.h"

#include "AccelerationStructure.h"
#include "Application.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

AccelerationStructure::AccelerationStructure(
    vk::DeviceAddress vertexBufferAddress, vk::DeviceAddress indexBufferAddress,
    vk::DeviceAddress animatedVertexBufferAddress, vk::DeviceAddress animatedIndexBufferAddress,
    vk::DeviceAddress transformBufferAddress, std::shared_ptr<const Scene> scene, uint32_t hitGroupCount
)
    : m_VertexBufferAddress(vertexBufferAddress), m_IndexBufferAddress(indexBufferAddress),
      m_AnimatedVertexBufferAddress(animatedVertexBufferAddress),
      m_AnimatedIndexBufferAddress(animatedIndexBufferAddress),
      m_TransformBufferAddress(transformBufferAddress), m_Scene(std::move(scene)),
      m_HitGroupCount(hitGroupCount),
      m_ScratchOffsetAlignment(
          DeviceContext::GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment
      )
{
    Timer timer("Acceleration Structure Build");

    Renderer::s_MainCommandBuffer->Begin();
    CreateBlases();
    BuildBlases(Renderer::s_MainCommandBuffer->Buffer, vk::BuildAccelerationStructureModeKHR::eBuild);
    AddBuildSyncBarrier(Renderer::s_MainCommandBuffer->Buffer);
    CreateTlas();
    BuildTlas(Renderer::s_MainCommandBuffer->Buffer, vk::BuildAccelerationStructureModeKHR::eBuild);
    Renderer::s_MainCommandBuffer->SubmitBlocking();
}

AccelerationStructure::~AccelerationStructure()
{
    for (const auto &blas : m_Blases)
        DeviceContext::GetLogical().destroyAccelerationStructureKHR(
            blas.Handle, nullptr, Application::GetDispatchLoader()
        );
    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        m_Tlas, nullptr, Application::GetDispatchLoader()
    );
}

void AccelerationStructure::Update(vk::CommandBuffer commandBuffer)
{
    if (!m_Scene->HasAnimations())
        return;

    Timer timer("Acceleration Structure Update");
    BuildBlases(commandBuffer, vk::BuildAccelerationStructureModeKHR::eUpdate);
    AddBuildSyncBarrier(commandBuffer);
    BuildTlas(commandBuffer, vk::BuildAccelerationStructureModeKHR::eUpdate);
    AddTraceBarrier(commandBuffer);
}

vk::AccelerationStructureKHR AccelerationStructure::GetTlas() const
{
    return m_Tlas;
}

void AccelerationStructure::CreateBlases()
{
    vk::DeviceSize totalBlasBufferSize = 0;
    vk::DeviceSize totalBlasScratchBufferSize = 0;

    // Gather info about the BLASes
    for (const auto &model : m_Scene->GetModels())
    {
        std::vector<uint32_t> primitiveCounts = {};

        BlasInfo &blasInfo = m_BlasInfos.emplace_back();
        blasInfo.Ranges.reserve(model.Meshes.size());
        blasInfo.Geometries.reserve(model.Meshes.size());

        for (const auto &mesh : model.Meshes)
        {
            const Geometry geometry = m_Scene->GetGeometries()[mesh.GeometryIndex];
            const bool hasTransform = mesh.TransformBufferOffset != SceneBuilder::IdentityTransformIndex;

            vk::DeviceAddress vertexBufferAddress =
                geometry.IsAnimated ? m_AnimatedVertexBufferAddress : m_VertexBufferAddress;
            vk::DeviceAddress indexBufferAddress =
                geometry.IsAnimated ? m_AnimatedIndexBufferAddress : m_IndexBufferAddress;

            vk::AccelerationStructureGeometryTrianglesDataKHR geometryData(
                vk::Format::eR32G32B32Sfloat, vertexBufferAddress, sizeof(Shaders::Vertex),
                geometry.VertexLength - 1, vk::IndexType::eUint32, indexBufferAddress,
                hasTransform ? m_TransformBufferAddress : vk::DeviceOrHostAddressConstKHR()
            );

            blasInfo.Geometries.emplace_back(
                vk::GeometryTypeKHR::eTriangles, geometryData,
                geometry.IsOpaque ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagsKHR()
            );

            blasInfo.IsAnimated |= geometry.IsAnimated;
            m_IsOpaque &= geometry.IsOpaque;
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
            vk::AccelerationStructureTypeKHR::eBottomLevel, GetFlags(blasInfo.IsAnimated)
        );
        blasInfo.BuildInfo.setGeometries(blasInfo.Geometries);

        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
            DeviceContext::GetLogical().getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, blasInfo.BuildInfo, primitiveCounts,
                Application::GetDispatchLoader()
            );

        blasInfo.BlasBufferOffset = totalBlasBufferSize;
        blasInfo.BlasScratchBufferOffset = totalBlasScratchBufferSize;
        blasInfo.BlasBufferSize = buildSizesInfo.accelerationStructureSize;
        totalBlasBufferSize += Utils::AlignTo(buildSizesInfo.accelerationStructureSize, 256);
        totalBlasScratchBufferSize +=
            Utils::AlignTo(buildSizesInfo.buildScratchSize, m_ScratchOffsetAlignment);
    }

    auto builder = BufferBuilder().SetUsageFlags(
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
        vk::BufferUsageFlagBits::eShaderDeviceAddress
    );

    m_BlasBuffer = builder.CreateDeviceBuffer(totalBlasBufferSize, "BLAS Buffer");

    m_BlasScratchBuffer = builder.SetAlignment(m_ScratchOffsetAlignment)
                              .CreateDeviceBuffer(totalBlasScratchBufferSize, "BLAS Scratch Buffer");

    // Create the BLASes
    for (uint32_t i = 0; i < m_BlasInfos.size(); i++)
    {
        BlasInfo &blasInfo = m_BlasInfos[i];

        vk::AccelerationStructureCreateInfoKHR createInfo(
            vk::AccelerationStructureCreateFlagsKHR(), m_BlasBuffer.GetHandle(), blasInfo.BlasBufferOffset,
            blasInfo.BlasBufferSize, vk::AccelerationStructureTypeKHR::eBottomLevel
        );

        vk::AccelerationStructureKHR blas = DeviceContext::GetLogical().createAccelerationStructureKHR(
            createInfo, nullptr, Application::GetDispatchLoader()
        );
        blasInfo.BuildInfo.setSrcAccelerationStructure(blas).setDstAccelerationStructure(blas).setScratchData(
            m_BlasScratchBuffer.GetDeviceAddress() + blasInfo.BlasScratchBufferOffset
        );
        vk::DeviceAddress address = DeviceContext::GetLogical().getAccelerationStructureAddressKHR(
            { blas }, Application::GetDispatchLoader()
        );

        m_Blases.emplace_back(blas, address);

        Utils::SetDebugName(blas, std::format("BLAS {}", i));
    }
}

void AccelerationStructure::CreateTlas()
{
    const uint32_t instanceCount = m_Scene->GetModelInstances().size();
    const vk::DeviceSize instancesSize = instanceCount * sizeof(vk::AccelerationStructureInstanceKHR);

    m_InstanceBuffer = BufferBuilder()
                           .SetUsageFlags(
                               vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                               vk::BufferUsageFlagBits::eShaderDeviceAddress
                           )
                           .CreateHostBuffer(instancesSize, "Instance Buffer");

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData(
        vk::False, m_InstanceBuffer.GetDeviceAddress()
    );

    vk::AccelerationStructureGeometryKHR geometry(vk::GeometryTypeKHR::eInstances, instancesData);

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo(
        vk::AccelerationStructureTypeKHR::eTopLevel, GetFlags(m_Scene->HasAnimations())
    );
    buildInfo.setGeometries({ geometry });

    const uint32_t primitiveCount[] = { instanceCount };
    vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo =
        DeviceContext::GetLogical().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, primitiveCount,
            Application::GetDispatchLoader()
        );

    m_TlasBuffer = BufferBuilder()
                       .SetUsageFlags(
                           vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                           vk::BufferUsageFlagBits::eShaderDeviceAddress
                       )
                       .CreateDeviceBuffer(
                           buildSizesInfo.accelerationStructureSize, "Top Level Acceleration Structure Buffer"
                       );

    vk::AccelerationStructureCreateInfoKHR createInfo(
        vk::AccelerationStructureCreateFlagsKHR(), m_TlasBuffer.GetHandle(), 0,
        buildSizesInfo.accelerationStructureSize
    );
    m_Tlas = DeviceContext::GetLogical().createAccelerationStructureKHR(
        createInfo, nullptr, Application::GetDispatchLoader()
    );
    Utils::SetDebugName(m_Tlas, "Top Level Acceleration Structure");

    const vk::DeviceSize scratchSize =
        std::max(buildSizesInfo.buildScratchSize, buildSizesInfo.updateScratchSize);

    m_TlasScratchBuffer = BufferBuilder()
                              .SetAlignment(m_ScratchOffsetAlignment)
                              .SetUsageFlags(
                                  vk::BufferUsageFlagBits::eStorageBuffer |
                                  vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                  vk::BufferUsageFlagBits::eShaderDeviceAddress
                              )
                              .CreateDeviceBuffer(scratchSize, "Scratch Buffer (TLAS)");
}

void AccelerationStructure::BuildBlases(
    vk::CommandBuffer commandBuffer, vk::BuildAccelerationStructureModeKHR mode
)
{
    if (m_BlasInfos.empty())
        return;

    // Build all BLASes at once
    std::vector<const vk::AccelerationStructureBuildRangeInfoKHR *> outRanges = {};
    std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> outBuild = {};
    for (const BlasInfo &info : m_BlasInfos)
    {
        outRanges.push_back(info.Ranges.data());
        outBuild.push_back(info.BuildInfo);
    }
    
    {
        Utils::DebugLabel label(commandBuffer, "BLAS Build", { 0.96f, 0.95f, 0.48f, 1.0f });
        commandBuffer.buildAccelerationStructuresKHR(outBuild, outRanges, Application::GetDispatchLoader());
    }

    // Prepare BLASes for updates
    if (mode == vk::BuildAccelerationStructureModeKHR::eBuild)
    {
        std::erase_if(m_BlasInfos, [](auto &info) { return !info.IsAnimated; });

        for (BlasInfo &info : m_BlasInfos)
            info.BuildInfo.setMode(vk::BuildAccelerationStructureModeKHR::eUpdate);
    }
}

void AccelerationStructure::BuildTlas(
    vk::CommandBuffer commandBuffer, vk::BuildAccelerationStructureModeKHR mode
)
{
    std::vector<vk::AccelerationStructureInstanceKHR> instances = {};
    for (int i = 0; i < m_Scene->GetModelInstances().size(); i++)
    {
        const auto &instance = m_Scene->GetModelInstances()[i];
        const uint32_t customIndex = 0;

        instances.emplace_back(
            TrivialCopy<glm::mat3x4, vk::TransformMatrixKHR>(instance.Transform), customIndex, 0xff,
            m_Scene->GetModels()[instance.ModelIndex].MeshOffset * m_HitGroupCount,
            vk::GeometryInstanceFlagsKHR(), m_Blases[instance.ModelIndex].Address
        );
    }

    m_InstanceBuffer.Upload(instances.data());

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData(
        vk::False, m_InstanceBuffer.GetDeviceAddress()
    );

    vk::AccelerationStructureGeometryKHR geometry(
        vk::GeometryTypeKHR::eInstances, instancesData,
        m_IsOpaque ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagsKHR()
    );

    vk::AccelerationStructureBuildGeometryInfoKHR geometryInfo(
        vk::AccelerationStructureTypeKHR::eTopLevel, GetFlags(m_Scene->HasAnimations()), mode, m_Tlas, m_Tlas,
        geometry, {}, m_TlasScratchBuffer.GetDeviceAddress()
    );

    vk::AccelerationStructureBuildRangeInfoKHR rangeInfo(m_Scene->GetModelInstances().size(), 0, 0, 0);

    {
        Utils::DebugLabel label(commandBuffer, "TLAS Build", { 0.89f, 0.96f, 0.13f, 1.0f });
        commandBuffer.buildAccelerationStructuresKHR(
            { geometryInfo }, { &rangeInfo }, Application::GetDispatchLoader()
        );
    }
}

void AccelerationStructure::AddBuildSyncBarrier(vk::CommandBuffer commandBuffer)
{
    m_BlasBuffer.AddBarrier(
        commandBuffer, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR
    );
}

void AccelerationStructure::AddTraceBarrier(vk::CommandBuffer commandBuffer)
{
    m_TlasBuffer.AddBarrier(
        commandBuffer, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        vk::PipelineStageFlagBits2::eRayTracingShaderKHR
    );
}

vk::BuildAccelerationStructureFlagsKHR AccelerationStructure::GetFlags(bool isAnimated) const
{
    return m_Scene->HasAnimations() ? vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild |
                                          vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate
                                    : vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
}

}
