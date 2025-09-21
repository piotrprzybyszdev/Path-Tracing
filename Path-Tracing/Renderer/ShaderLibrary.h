#pragma once

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string_view>
#include <vector>

namespace PathTracing
{

struct FileInfo
{
    std::filesystem::file_time_type Time;
    std::string Path;
    std::vector<char> Buffer;
};

class Includer : public shaderc::CompileOptions::IncluderInterface
{
public:
    shaderc_include_result *GetInclude(
        const char *requested_source, shaderc_include_type type, const char *requesting_source,
        size_t include_depth
    ) override;
    void ReleaseInclude(shaderc_include_result *data) override;

    std::vector<std::filesystem::path> SystemIncludePaths;
    std::set<std::filesystem::path> IncludedFiles;

private:
    std::filesystem::path GetFilePath(
        const char *requested_source, shaderc_include_type type, const char *requesting_source
    );

    std::map<std::filesystem::path, FileInfo> m_Cache;
};

class Shader
{
public:
    Shader(std::filesystem::path path, vk::ShaderStageFlagBits stage);
    ~Shader() noexcept;

    Shader(Shader &&shader) noexcept;

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    [[nodiscard]] vk::PipelineShaderStageCreateInfo GetStageCreateInfo(
        const shaderc::Compiler &compiler, const shaderc::CompileOptions &options, Includer *includer
    );

private:
    std::filesystem::path m_Path;
    std::filesystem::path m_OutputPath;
    vk::ShaderStageFlagBits m_Stage;

    std::set<std::filesystem::path> m_IncludedPaths;

    std::filesystem::file_time_type m_UpdateTime = std::filesystem::file_time_type::min();
    std::vector<char> m_Code;
    vk::ShaderModule m_Module;

    bool m_IsMoved = false;

private:
    [[nodiscard]] vk::ShaderModule GetModule(
        const shaderc::Compiler &compiler, const shaderc::CompileOptions &options, Includer *includer
    );

    [[nodiscard]] std::filesystem::file_time_type ComputeUpdateTime() const;
    void UpdateModule(std::span<const uint32_t> code, std::filesystem::file_time_type updateTime);

    static std::filesystem::path ToOutputPath(const std::filesystem::path &path);
};

using ShaderId = uint32_t;

class ShaderLibrary
{
public:
    ShaderLibrary();
    ~ShaderLibrary();

    ShaderLibrary(const ShaderLibrary &) = delete;
    ShaderLibrary &operator=(const ShaderLibrary &) = delete;

    ShaderId AddShader(std::filesystem::path path, vk::ShaderStageFlagBits stage);
    uint32_t AddGeneralGroup(ShaderId shaderId);
    uint32_t AddHitGroup(ShaderId closestHitId, ShaderId anyHitId);

    vk::Pipeline CreateRaytracingPipeline(vk::PipelineLayout layout);
    vk::Pipeline CreateComputePipeline(vk::PipelineLayout layout, ShaderId shaderId);

public:
    static inline constexpr ShaderId g_UnusedShaderId = vk::ShaderUnusedKHR;

private:
    shaderc::Compiler m_Compiler;
    shaderc::CompileOptions m_Options;
    Includer *m_Includer;

    std::vector<Shader> m_Shaders;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;

    std::set<ShaderId> m_RaytracingShaderIds;
};

}
