#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>

#include "Buffer.h"

namespace PathTracing
{

class PhysicalDevice;
class LogicalDevice;

class ShaderLibrary
{
public:
    ShaderLibrary(const LogicalDevice &logicalDevice, const PhysicalDevice &physicalDevice);
    ~ShaderLibrary();

    void AddRaygenShader(std::filesystem::path path, std::string_view entry);
    void AddMissShader(std::filesystem::path path, std::string_view entry);
    void AddClosestHitShader(std::filesystem::path path, std::string_view entry);

    vk::Pipeline CreatePipeline(vk::PipelineLayout layout, vk::detail::DispatchLoaderDynamic loader);

    vk::StridedDeviceAddressRegionKHR GetRaygenTableEntry();
    vk::StridedDeviceAddressRegionKHR GetMissTableEntry();
    vk::StridedDeviceAddressRegionKHR GetClosestHitTableEntry();

private:
    const uint32_t m_AlignedHandleSize;
    const LogicalDevice &m_LogicalDevice;
    vk::Device m_Device;

    std::vector<vk::ShaderModule> m_Modules;
    std::vector<vk::PipelineShaderStageCreateInfo> m_Stages;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;

    Buffer m_RaygenShaderBindingTable;
    Buffer m_MissShaderBindingTable;
    Buffer m_ClosestHitShaderBindingTable;

private:
    void AddShader(
        std::filesystem::path path, std::string_view entry, vk::ShaderStageFlagBits stage,
        vk::RayTracingShaderGroupTypeKHR type, uint32_t index
    );
    vk::ShaderModule LoadShader(std::filesystem::path path);

    vk::StridedDeviceAddressRegionKHR CreateTableEntry(vk::DeviceAddress address);
};

}
