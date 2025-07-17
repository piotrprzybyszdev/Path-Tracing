#include "AccelerationStructure.h"
#include "Application.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

AccelerationStructure::AccelerationStructure()
{
}

AccelerationStructure::~AccelerationStructure()
{
    for (const auto &blas : m_Blases)
        DeviceContext::GetLogical().destroyAccelerationStructureKHR(
            blas, nullptr, Application::GetDispatchLoader()
        );
    DeviceContext::GetLogical().destroyAccelerationStructureKHR(
        m_Tlas, nullptr, Application::GetDispatchLoader()
    );
}

uint32_t AccelerationStructure::AddGeometry(
    std::span<const Shaders::Vertex> vertices, std::span<const uint32_t> indices
)
{
    assert(indices.size() % 3 == 0);

    m_Geometries.emplace_back(
        static_cast<uint32_t>(m_Vertices.size()), static_cast<uint32_t>(vertices.size()),
        static_cast<uint32_t>(m_Indices.size()), static_cast<uint32_t>(indices.size())
    );

    m_Vertices.insert(m_Vertices.end(), vertices.begin(), vertices.end());
    m_Indices.insert(m_Indices.end(), indices.begin(), indices.end());

    return m_Geometries.size() - 1;
}

uint32_t AccelerationStructure::AddModel(
    const std::vector<uint32_t> &geometryIndices, std::optional<std::string_view> name
)
{
    m_Models.emplace_back(geometryIndices, std::nullopt, name);
    return m_Models.size() - 1;
}

uint32_t AccelerationStructure::AddModel(
    const std::vector<uint32_t> &geometryIndices, const std::vector<glm::mat4> &transforms,
    std::optional<std::string_view> name
)
{
    assert(geometryIndices.size() == transforms.size());

    const uint32_t index = AddModel(geometryIndices, name);

    m_Models.back().TransformBufferOffset = static_cast<uint32_t>(transforms.size());

    for (const glm::mat4 &transform : transforms)
        m_Transforms.push_back(ToTransformMatrix(transform));

    return index;
}

void AccelerationStructure::AddModelInstance(uint32_t modelIndex, glm::mat4 transform, uint32_t sbtOffset)
{
    m_ModelInstances.emplace_back(modelIndex, transform, sbtOffset);
}

void AccelerationStructure::Build()
{
    // TODO: These buffers should be device local
    Timer timer("Build Total");

    auto &builder =
        BufferBuilder()
            .SetUsageFlags(
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress
            )
            .SetMemoryFlags(
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            )
            .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

    m_VertexBuffer = builder.CreateBufferUnique(m_Vertices.size() * sizeof(Shaders::Vertex), "Vertex Buffer");
    m_VertexBuffer->Upload(m_Vertices.data());
    m_IndexBuffer = builder.CreateBufferUnique(m_Indices.size() * sizeof(uint32_t), "Index Buffer");
    m_IndexBuffer->Upload(m_Indices.data());

    if (m_Transforms.size() > 0)
    {
        m_TransformBuffer = builder.CreateBufferUnique(
            m_Transforms.size() * sizeof(vk::TransformMatrixKHR), "Transform Buffer"
        );
        m_TransformBuffer->Upload(m_Transforms.data());
    }

    std::vector<Shaders::Geometry> geometries = {};
    for (const GeometryInfo &geometry : m_Geometries)
        geometries.emplace_back(
            m_VertexBuffer->GetDeviceAddress() + geometry.VertexBufferOffset * sizeof(Shaders::Vertex),
            m_IndexBuffer->GetDeviceAddress() + geometry.IndexBufferOffset * sizeof(uint32_t)
        );

    builder.SetUsageFlags(vk::BufferUsageFlagBits::eStorageBuffer);
    m_GeometryBuffer = builder.CreateBufferUnique(geometries.size() * sizeof(Shaders::Geometry));
    m_GeometryBuffer->Upload(geometries.data());

    {
        Timer timer("AC Build");
        BuildBlases();
        BuildTlas();
    }
}

vk::AccelerationStructureKHR AccelerationStructure::GetTlas() const
{
    return m_Tlas;
}

const Buffer &AccelerationStructure::GetGeometryBuffer() const
{
    return *m_GeometryBuffer;
}

void AccelerationStructure::BuildBlases()
{
    struct BlasInfo
    {
        std::vector<vk::AccelerationStructureBuildRangeInfoKHR> Ranges = {};
        std::vector<vk::AccelerationStructureGeometryKHR> Geometries = {};
        vk::AccelerationStructureBuildGeometryInfoKHR BuildInfo;

        vk::DeviceSize BlasBufferOffset = 0;
        vk::DeviceSize BlasScratchBufferOffset = 0;
        vk::DeviceSize BlasBufferSize = 0;

        std::optional<std::string_view> Name;
    };

    std::vector<BlasInfo> blasInfos = {};

    vk::DeviceSize totalBlasBufferSize = 0;
    vk::DeviceSize totalBlasScratchBufferSize = 0;

    // Gather info about the BLASes
    for (const ModelInfo &model : m_Models)
    {
        std::vector<uint32_t> primitiveCounts = {};

        BlasInfo &blasInfo = blasInfos.emplace_back();
        blasInfo.Name = model.Name;
        blasInfo.Ranges.reserve(model.GeometryIndices.size());
        blasInfo.Geometries.reserve(model.GeometryIndices.size());

        for (int i = 0; i < model.GeometryIndices.size(); i++)
        {
            const GeometryInfo &geometryInfo = m_Geometries[model.GeometryIndices[i]];

            const vk::DeviceAddress transformBuffer =
                model.TransformBufferOffset.has_value() ? m_TransformBuffer->GetDeviceAddress() : 0;
            const uint32_t transformBufferOffset =
                model.TransformBufferOffset.has_value() ? model.TransformBufferOffset.value() + i : 0;

            vk::AccelerationStructureGeometryTrianglesDataKHR geometryData(
                vk::Format::eR32G32B32Sfloat, m_VertexBuffer->GetDeviceAddress(), sizeof(Shaders::Vertex),
                geometryInfo.VertexBufferLength - 1, vk::IndexType::eUint32,
                m_IndexBuffer->GetDeviceAddress(), transformBuffer
            );

            blasInfo.Geometries.emplace_back(vk::AccelerationStructureGeometryKHR(
                vk::GeometryTypeKHR::eTriangles, geometryData, vk::GeometryFlagBitsKHR::eOpaque
            ));

            primitiveCounts.push_back(geometryInfo.IndexBufferLength / 3);

            blasInfo.Ranges.emplace_back(
                geometryInfo.IndexBufferLength / 3,
                geometryInfo.IndexBufferOffset * static_cast<uint32_t>(sizeof(uint32_t)),
                geometryInfo.VertexBufferOffset, transformBufferOffset
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
        totalBlasBufferSize += buildSizesInfo.accelerationStructureSize;
        totalBlasScratchBufferSize += buildSizesInfo.buildScratchSize;
    }

    auto &builder = BufferBuilder()
                        .SetUsageFlags(
                            vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                            vk::BufferUsageFlagBits::eShaderDeviceAddress
                        )
                        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
                        .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

    Buffer blasScratchBuffer = builder.CreateBuffer(totalBlasScratchBufferSize, "BLAS Scratch Buffer");
    m_BlasBuffer = builder.CreateBufferUnique(totalBlasBufferSize, "BLAS Buffer");

    // Create the BLASes
    for (BlasInfo &blasInfo : blasInfos)
    {
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

        if (blasInfo.Name.has_value())
        {
            Utils::SetDebugName(
                blas, vk::ObjectType::eAccelerationStructureKHR,
                std::format("BLAS: {}", blasInfo.Name.value())
            );
        }
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
    for (const ModelInstanceInfo &instance : m_ModelInstances)
    {
        vk::DeviceAddress address = DeviceContext::GetLogical().getAccelerationStructureAddressKHR(
            { m_Blases[instance.ModelIndex] }, Application::GetDispatchLoader()
        );

        vk::TransformMatrixKHR matrix = ToTransformMatrix(instance.Transform);

        instances.push_back({ matrix, 0, 0xff, instance.SbtOffset, vk::GeometryInstanceFlagsKHR(), address });
    }

    auto &builder =
        BufferBuilder()
            .SetUsageFlags(
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress
            )
            .SetMemoryFlags(
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            )
            .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

    Buffer instanceBuffer = builder.CreateBuffer(
        sizeof(vk::AccelerationStructureInstanceKHR) * instances.size(), "Instance Buffer"
    );
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

    m_TlasBuffer = builder.ResetFlags()
                       .SetUsageFlags(
                           vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                           vk::BufferUsageFlagBits::eShaderDeviceAddress
                       )
                       .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
                       .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress)
                       .CreateBufferUnique(
                           buildSizesInfo.accelerationStructureSize, "Top Level Acceleration Structure Buffer"
                       );

    vk::AccelerationStructureCreateInfoKHR createInfo(
        vk::AccelerationStructureCreateFlagsKHR(), m_TlasBuffer->GetHandle(), 0,
        buildSizesInfo.accelerationStructureSize
    );

    m_Tlas = DeviceContext::GetLogical().createAccelerationStructureKHR(
        createInfo, nullptr, Application::GetDispatchLoader()
    );
    Utils::SetDebugName(
        m_Tlas, vk::ObjectType::eAccelerationStructureKHR, "Top Level Acceleration Structure"
    );

    Buffer scratchBuffer = builder.CreateBuffer(buildSizesInfo.buildScratchSize, "Scratch Buffer (TLAS)");

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

vk::TransformMatrixKHR AccelerationStructure::ToTransformMatrix(const glm::mat3x4 &matrix)
{
    vk::TransformMatrixKHR out = {};
    memcpy(out.matrix.data(), &matrix[0][0], sizeof(glm::mat3x4));
    return out;
}

}
