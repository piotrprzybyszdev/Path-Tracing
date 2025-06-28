#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>

#include "Buffer.h"

namespace PathTracing
{

class ShaderLibrary
{
public:
    ShaderLibrary();
    ~ShaderLibrary();

    ShaderLibrary(const ShaderLibrary &) = delete;
    ShaderLibrary &operator=(const ShaderLibrary &) = delete;

    void AddRaygenShader(std::filesystem::path path, std::string_view entry);
    void AddMissShader(std::filesystem::path path, std::string_view entry);
    void AddClosestHitShader(std::filesystem::path path, std::string_view entry);

    vk::Pipeline CreatePipeline(vk::PipelineLayout layout, vk::detail::DispatchLoaderDynamic loader);

    vk::StridedDeviceAddressRegionKHR GetRaygenTableEntry() const;
    vk::StridedDeviceAddressRegionKHR GetMissTableEntry() const;
    vk::StridedDeviceAddressRegionKHR GetClosestHitTableEntry() const;

private:
    uint32_t m_AlignedHandleSize;

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

    vk::StridedDeviceAddressRegionKHR CreateTableEntry(vk::DeviceAddress address) const;
};

}
