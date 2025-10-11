#pragma once

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_reflect.hpp>
#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include "Core/Cache.h"

#include "Shaders/ShaderRendererTypes.incl"

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
    Includer();

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

    const uint32_t m_MaxIncludeDepth;
    LRUCache<std::filesystem::path, FileInfo> m_Cache;
};

using ShaderId = uint32_t;

class Shader
{
public:
    Shader(std::filesystem::path path, vk::ShaderStageFlagBits stage);
    ~Shader() noexcept;

    Shader(Shader &&shader) noexcept;

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    [[nodiscard]] vk::ShaderStageFlagBits GetStage() const;
    [[nodiscard]] const std::filesystem::path &GetPath() const;
    [[nodiscard]] bool HasChanged() const;

    bool RecompileIfChanged(
        const shaderc::Compiler &compiler, const shaderc::CompileOptions &options, Includer *includer
    );
    [[nodiscard]] vk::PipelineShaderStageCreateInfo GetStageCreateInfo() const;
    [[nodiscard]] std::span<const vk::DescriptorSetLayoutBinding> GetSetLayoutBindings() const;
    [[nodiscard]] vk::PushConstantRange GetPushConstants() const;
    [[nodiscard]] std::span<const uint32_t> GetSpecializationConstantIds() const;

private:
    std::filesystem::path m_Path;
    std::filesystem::path m_OutputPath;
    vk::ShaderStageFlagBits m_Stage;

    std::set<std::filesystem::path> m_IncludedPaths;

    std::filesystem::file_time_type m_UpdateTime = std::filesystem::file_time_type::min();
    std::vector<char> m_Code;
    vk::ShaderModule m_Module;

    std::vector<vk::DescriptorSetLayoutBinding> m_SetLayoutBindings;
    vk::PushConstantRange m_PushConstants;
    std::vector<uint32_t> m_SpecializationConstantIds;

    bool m_IsMoved = false;

private:
    std::filesystem::file_time_type ComputeUpdateTime() const;
    void UpdateModule(std::span<const uint32_t> code, std::filesystem::file_time_type updateTime);
    
    void Reflect(std::span<const uint32_t> code);
    void Reflect(
        const spirv_cross::Compiler &compiler, std::span<const spirv_cross::Resource> resources,
        vk::DescriptorType descriptorType
    );

private:
    static std::filesystem::path ToOutputPath(const std::filesystem::path &path);
};

class ShaderLibrary
{
public:
    ShaderLibrary();
    ~ShaderLibrary();

    ShaderLibrary(const ShaderLibrary &) = delete;
    ShaderLibrary &operator=(const ShaderLibrary &) = delete;

    ShaderId AddShader(std::filesystem::path path, vk::ShaderStageFlagBits stage);
    [[nodiscard]] const Shader &GetShader(ShaderId id) const;

    void CompileShaders();
    [[nodiscard]] std::vector<bool> RecompileChanged(std::span<const ShaderId> shaderIds);

public:
    static inline constexpr ShaderId g_UnusedShaderId = vk::ShaderUnusedKHR;
    
private:
    shaderc::Compiler m_Compiler;
    shaderc::CompileOptions m_Options;
    Includer *m_Includer;

    std::vector<Shader> m_Shaders;

    std::set<ShaderId> m_RaytracingShaderIds;
};

}
