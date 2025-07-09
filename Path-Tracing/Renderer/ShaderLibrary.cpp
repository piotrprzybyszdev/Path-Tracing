#include <fstream>

#include "Core/Core.h"

#include "Buffer.h"
#include "DeviceContext.h"
#include "ShaderLibrary.h"

namespace PathTracing
{

ShaderLibrary::ShaderLibrary()
{
    auto properties =
        DeviceContext::GetPhysical()
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>(
            )
            .get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

    auto align = [](uint32_t size, uint32_t alignment) { return (size + alignment - 1) & ~(alignment - 1); };

    // TODO: Make sure that the alignment is fulfilled when integrating VMA
    assert(properties.shaderGroupBaseAlignment % properties.shaderGroupHandleAlignment == 0);

    const uint32_t alignment = properties.shaderGroupHandleAlignment;
    const uint32_t hitGroupSize = properties.shaderGroupHandleSize + sizeof(Shaders::SBTBuffer);

    assert(hitGroupSize < properties.maxShaderGroupStride);

    m_AlignedHandleSize = align(properties.shaderGroupHandleSize, alignment);
    const uint32_t alignedHitGroupSize = align(hitGroupSize, alignment);

    logger::debug("Handle size: {}", properties.shaderGroupHandleSize);
    logger::debug("Aligned hit group size: {}", alignedHitGroupSize);

    m_HitRecord = std::make_unique<SBTHitRecord>(alignedHitGroupSize, properties.shaderGroupHandleSize);
}

ShaderLibrary::~ShaderLibrary()
{
    for (vk::ShaderModule module : m_Modules)
        DeviceContext::GetLogical().destroyShaderModule(module);
}

void ShaderLibrary::AddRaygenShader(std::filesystem::path path, std::string_view entry)
{
    AddShader(
        path, entry, vk::ShaderStageFlagBits::eRaygenKHR, vk::RayTracingShaderGroupTypeKHR::eGeneral, 0
    );
}

void ShaderLibrary::AddMissShader(std::filesystem::path path, std::string_view entry)
{
    AddShader(path, entry, vk::ShaderStageFlagBits::eMissKHR, vk::RayTracingShaderGroupTypeKHR::eGeneral, 1);
}

void ShaderLibrary::AddClosestHitShader(std::filesystem::path path, std::string_view entry)
{
    AddShader(
        path, entry, vk::ShaderStageFlagBits::eClosestHitKHR,
        vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, 2
    );
}

uint32_t ShaderLibrary::GetGeometryCount() const
{
    return m_HitRecord->GetCount();
}

void ShaderLibrary::AddGeometry(const Shaders::SBTBuffer &data)
{
    return m_HitRecord->AddBuffer(data);
}

vk::Pipeline ShaderLibrary::CreatePipeline(
    vk::PipelineLayout layout, vk::detail::DispatchLoaderDynamic loader
)
{
    vk::RayTracingPipelineCreateInfoKHR createInfo(vk::PipelineCreateFlags(), m_Stages, m_Groups, 1);
    createInfo.setLayout(layout);

    vk::ResultValue<vk::Pipeline> result = DeviceContext::GetLogical().createRayTracingPipelineKHR(
        nullptr, nullptr, createInfo, nullptr, loader
    );
    assert(result.result == vk::Result::eSuccess);

    const uint32_t shaderGroupCount = 3;

    BufferBuilder builder;
    builder
        .SetUsageFlags(
            vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eTransferSrc |
            vk::BufferUsageFlagBits::eShaderDeviceAddress
        )
        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
        .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

    m_RaygenShaderBindingTable = builder.CreateBuffer(m_AlignedHandleSize);
    m_MissShaderBindingTable = builder.CreateBuffer(m_AlignedHandleSize);
    m_ClosestHitShaderBindingTable = builder.CreateBuffer(m_HitRecord->GetSize());

    std::vector<uint8_t> shaderHandles =
        DeviceContext::GetLogical().getRayTracingShaderGroupHandlesKHR<uint8_t>(
            result.value, 0, shaderGroupCount, m_AlignedHandleSize * shaderGroupCount, loader
        );

    m_RaygenShaderBindingTable.Upload(shaderHandles.data());
    m_MissShaderBindingTable.Upload(shaderHandles.data() + m_AlignedHandleSize);
    m_HitRecord->SetHandles(shaderHandles.begin() + m_AlignedHandleSize * 2);
    m_ClosestHitShaderBindingTable.Upload(m_HitRecord->GetData());

    return result.value;
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::GetRaygenTableEntry() const
{
    return { m_RaygenShaderBindingTable.GetDeviceAddress(), m_AlignedHandleSize, m_AlignedHandleSize };
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::GetMissTableEntry() const
{
    return { m_MissShaderBindingTable.GetDeviceAddress(), m_AlignedHandleSize, m_AlignedHandleSize };
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::GetClosestHitTableEntry() const
{
    return m_HitRecord->GetTableEntry(m_ClosestHitShaderBindingTable.GetDeviceAddress());
}

void ShaderLibrary::AddShader(
    std::filesystem::path path, std::string_view entry, vk::ShaderStageFlagBits stage,
    vk::RayTracingShaderGroupTypeKHR type, uint32_t index
)
{
    vk::ShaderModule module = LoadShader(path);
    m_Stages.push_back({ vk::PipelineShaderStageCreateFlags(), stage, module, entry.data() });

    if (stage == vk::ShaderStageFlagBits::eClosestHitKHR)
    {
        vk::RayTracingShaderGroupCreateInfoKHR createInfo(type);
        createInfo.setClosestHitShader(index);
        m_Groups.push_back(createInfo);
    }
    else
        m_Groups.push_back({ type, index });

    m_Modules.push_back(module);
}

vk::ShaderModule ShaderLibrary::LoadShader(std::filesystem::path path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
        throw error(std::format("Shader file {} cannot be opened", path.string()));

    size_t size = file.tellg();
    std::vector<char> buffer(size);

    file.seekg(0);
    file.read(buffer.data(), size);

    file.close();

    vk::ShaderModuleCreateInfo createInfo(
        vk::ShaderModuleCreateFlags(), size, reinterpret_cast<uint32_t *>(buffer.data())
    );
    return DeviceContext::GetLogical().createShaderModule(createInfo);
}

ShaderLibrary::SBTHitRecord::SBTHitRecord(uint32_t alignedHitGroupSize, uint32_t handleSize)
    : m_AlignedHitGroupSize(alignedHitGroupSize), m_HandleSize(handleSize)
{
}

void ShaderLibrary::SBTHitRecord::SetHandles(std::vector<uint8_t>::const_iterator handle)
{
    for (int i = 0; i < m_Size; i++)
    {
        auto it = m_Data.begin();
        std::advance(it, i * m_AlignedHitGroupSize);
        std::copy(handle, handle + m_HandleSize, it);
    }
}

void ShaderLibrary::SBTHitRecord::AddBuffer(const Shaders::SBTBuffer &buffer)
{
    static_assert(std::is_trivially_copyable_v<Shaders::SBTBuffer>);

    if (m_Size == m_Capacity)
    {
        m_Capacity = m_Capacity == 0 ? 1 : m_Capacity * 2;
        m_Data.resize(m_Capacity * m_AlignedHitGroupSize);
    }

    auto it = m_Data.begin();
    std::advance(it, m_Size * m_AlignedHitGroupSize + m_HandleSize);
    std::copy(reinterpret_cast<const uint8_t *>(&buffer), reinterpret_cast<const uint8_t *>(&buffer + 1), it);

    m_Size++;
}

uint32_t ShaderLibrary::SBTHitRecord::GetSize() const
{
    return m_Size * m_AlignedHitGroupSize;
}

uint32_t ShaderLibrary::SBTHitRecord::GetCount() const
{
    return m_Size;
}

const void *ShaderLibrary::SBTHitRecord::GetData() const
{
    return m_Data.data();
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::SBTHitRecord::GetTableEntry(vk::DeviceAddress address) const
{
    return { address, m_AlignedHitGroupSize, m_HandleSize + sizeof(Shaders::SBTBuffer) };
}

}
