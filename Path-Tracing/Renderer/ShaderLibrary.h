#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>

#include "Shaders/ShaderTypes.incl"

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

    uint32_t GetGeometryCount() const;
    void AddGeometry(const Shaders::SBTBuffer &data);

    vk::Pipeline CreatePipeline(vk::PipelineLayout layout, vk::detail::DispatchLoaderDynamic loader);

    vk::StridedDeviceAddressRegionKHR GetRaygenTableEntry() const;
    vk::StridedDeviceAddressRegionKHR GetMissTableEntry() const;
    vk::StridedDeviceAddressRegionKHR GetClosestHitTableEntry() const;

private:
    std::vector<vk::ShaderModule> m_Modules;
    std::vector<vk::PipelineShaderStageCreateInfo> m_Stages;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;

    Buffer m_RaygenShaderBindingTable;
    Buffer m_MissShaderBindingTable;
    Buffer m_ClosestHitShaderBindingTable;

    uint32_t m_AlignedHandleSize;
    // TODO: Add support for ray types
    class SBTHitRecord
    {
    public:
        SBTHitRecord(uint32_t alignedHitGroupSize, uint32_t handleSize);

        void AddBuffer(const Shaders::SBTBuffer &buffer);
        void SetHandles(std::vector<uint8_t>::const_iterator handle);

        uint32_t GetSize() const;
        uint32_t GetCount() const;
        const void *GetData() const;
        vk::StridedDeviceAddressRegionKHR GetTableEntry(vk::DeviceAddress address) const;

    private:
        const uint32_t m_AlignedHitGroupSize;
        const uint32_t m_HandleSize;
        std::vector<uint8_t> m_Data;

        uint32_t m_Size = 0;
        uint32_t m_Capacity = 0;
    };

    std::unique_ptr<SBTHitRecord> m_HitRecord;

private:
    void AddShader(
        std::filesystem::path path, std::string_view entry, vk::ShaderStageFlagBits stage,
        vk::RayTracingShaderGroupTypeKHR type, uint32_t index
    );
    vk::ShaderModule LoadShader(std::filesystem::path path);
};

}
