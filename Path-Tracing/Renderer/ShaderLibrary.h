#pragma once

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.hpp>

#include <filesystem>
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
    ~Shader();

    Shader(Shader &&shader) noexcept;

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    [[nodiscard]] vk::ShaderModule GetModule(
        const shaderc::Compiler &compiler, const shaderc::CompileOptions &options, Includer *includer
    );

private:
    const std::filesystem::path m_Path;
    const std::filesystem::path m_OutputPath;
    const vk::ShaderStageFlagBits m_Stage;

    std::set<std::filesystem::path> m_IncludedPaths;

    std::filesystem::file_time_type m_UpdateTime;
    std::vector<char> m_Code;
    vk::ShaderModule m_Module;

    bool m_IsMoved = false;

private:
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
    void AddGeneralGroup(uint32_t groupIndex, ShaderId shaderId);
    void AddHitGroup(uint32_t groupIndex, ShaderId closestHitId, ShaderId anyHitId);

    vk::Pipeline CreatePipeline(vk::PipelineLayout layout);

    static inline constexpr uint32_t RaygenGroupIndex = 0;
    static inline constexpr uint32_t MissGroupIndex = 1;
    static inline constexpr uint32_t HitGroupIndex = 2;

private:
    shaderc::Compiler m_Compiler;
    shaderc::CompileOptions m_Options;
    Includer *m_Includer;

    std::vector<Shader> m_Shaders;
    std::vector<vk::PipelineShaderStageCreateInfo> m_Stages;
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> m_Groups;

private:
    void ResizeGroups(uint32_t size);
};

}
