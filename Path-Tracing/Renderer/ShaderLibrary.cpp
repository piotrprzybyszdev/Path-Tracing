#include <fstream>

#include "LogicalDevice.h"
#include "ShaderLibrary.h"

namespace PathTracing
{

ShaderLibrary::ShaderLibrary(const LogicalDevice &logicalDevice, const PhysicalDevice &physicalDevice)
    : m_LogicalDevice(logicalDevice), m_Device(logicalDevice.GetHandle()),
      m_AlignedHandleSize(physicalDevice.GetAlignedShaderGroupHandleSize())
{
}

ShaderLibrary::~ShaderLibrary()
{
    for (vk::ShaderModule module : m_Modules)
        m_Device.destroyShaderModule(module);
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

vk::Pipeline ShaderLibrary::CreatePipeline(
    vk::PipelineLayout layout, vk::detail::DispatchLoaderDynamic loader
)
{
    vk::RayTracingPipelineCreateInfoKHR createInfo(vk::PipelineCreateFlags(), m_Stages, m_Groups, 1);
    createInfo.setLayout(layout);

    vk::ResultValue<vk::Pipeline> result =
        m_Device.createRayTracingPipelineKHR(nullptr, nullptr, createInfo, nullptr, loader);
    assert(result.result == vk::Result::eSuccess);

    const uint32_t shaderGroupCount = 3;

    BufferBuilder builder = m_LogicalDevice.CreateBufferBuilder();
    builder
        .SetUsageFlags(
            vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eTransferSrc |
            vk::BufferUsageFlagBits::eShaderDeviceAddress
        )
        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
        .SetAllocateFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);

    m_RaygenShaderBindingTable = builder.CreateBuffer(m_AlignedHandleSize);
    m_MissShaderBindingTable = builder.CreateBuffer(m_AlignedHandleSize);
    m_ClosestHitShaderBindingTable = builder.CreateBuffer(m_AlignedHandleSize);

    std::vector<uint8_t> shaderHandles = m_Device.getRayTracingShaderGroupHandlesKHR<uint8_t>(
        result.value, 0, shaderGroupCount, m_AlignedHandleSize * shaderGroupCount, loader
    );

    m_RaygenShaderBindingTable.Upload(shaderHandles.data());
    m_MissShaderBindingTable.Upload(shaderHandles.data() + m_AlignedHandleSize);
    m_ClosestHitShaderBindingTable.Upload(shaderHandles.data() + 2 * m_AlignedHandleSize);

    return result.value;
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::GetRaygenTableEntry()
{
    return CreateTableEntry(m_RaygenShaderBindingTable.GetDeviceAddress());
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::GetMissTableEntry()
{
    return CreateTableEntry(m_MissShaderBindingTable.GetDeviceAddress());
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::GetClosestHitTableEntry()
{
    return CreateTableEntry(m_ClosestHitShaderBindingTable.GetDeviceAddress());
}

void ShaderLibrary::AddShader(
    std::filesystem::path path, std::string_view entry, vk::ShaderStageFlagBits stage,
    vk::RayTracingShaderGroupTypeKHR type, uint32_t index
)
{
    vk::ShaderModule module = LoadShader(path);
    m_Stages.push_back({
        vk::PipelineShaderStageCreateFlags(),
        stage,
        module,
        entry.data(),
    });

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

    assert(file.is_open());

    size_t size = file.tellg();
    std::vector<char> buffer(size);

    file.seekg(0);
    file.read(buffer.data(), size);

    file.close();

    vk::ShaderModuleCreateInfo createInfo(vk::ShaderModuleCreateFlags(), size, (uint32_t *)buffer.data());
    return m_Device.createShaderModule(createInfo);
}

vk::StridedDeviceAddressRegionKHR ShaderLibrary::CreateTableEntry(vk::DeviceAddress address)
{
    return vk::StridedDeviceAddressRegionKHR(address, m_AlignedHandleSize, m_AlignedHandleSize);
}

}
