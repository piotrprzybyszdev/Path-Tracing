#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <string_view>
#include <vector>

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

    vk::Pipeline CreatePipeline(vk::PipelineLayout layout);

private:
    std::vector<vk::ShaderModule> m_Modules;
    std::vector<vk::PipelineShaderStageCreateInfo> m_Stages;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;

private:
    void AddShader(
        std::filesystem::path path, std::string_view entry, vk::ShaderStageFlagBits stage,
        vk::RayTracingShaderGroupTypeKHR type, uint32_t index
    );
    vk::ShaderModule LoadShader(std::filesystem::path path);

    static inline constexpr uint32_t RaygenShaderIndex = 0;
    static inline constexpr uint32_t MissShaderIndex = 1;
    static inline constexpr uint32_t ClosestHitShaderIndex = 2;
};

}
