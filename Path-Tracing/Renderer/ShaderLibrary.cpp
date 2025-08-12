#include <fstream>

#include "Core/Core.h"

#include "Application.h"
#include "DeviceContext.h"
#include "ShaderLibrary.h"
#include "Utils.h"

namespace PathTracing
{

namespace
{

shaderc_shader_kind ToShaderKind(vk::ShaderStageFlagBits stage)
{
    switch (stage)
    {
    case vk::ShaderStageFlagBits::eRaygenKHR:
        return shaderc_raygen_shader;
    case vk::ShaderStageFlagBits::eClosestHitKHR:
        return shaderc_closesthit_shader;
    case vk::ShaderStageFlagBits::eMissKHR:
        return shaderc_miss_shader;
    case vk::ShaderStageFlagBits::eAnyHitKHR:
        return shaderc_anyhit_shader;
    default:
        throw error("Unsupported shader stage");
    }
}

vk::RayTracingShaderGroupTypeKHR ToShaderGroupType(vk::ShaderStageFlagBits stage)
{
    switch (stage)
    {
    case vk::ShaderStageFlagBits::eRaygenKHR:
        return vk::RayTracingShaderGroupTypeKHR::eGeneral;
    case vk::ShaderStageFlagBits::eMissKHR:
        return vk::RayTracingShaderGroupTypeKHR::eGeneral;
    case vk::ShaderStageFlagBits::eClosestHitKHR:
        return vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
    default:
        throw error("Unsupported shader stage");
    }
}

FileInfo ReadFile(std::filesystem::path path)
{
    FileInfo fileInfo = { .Time = std::filesystem::last_write_time(path), .Path = path.string() };

    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
        throw error(std::format("Shader file {} cannot be opened", fileInfo.Path));

    size_t size = file.tellg();
    fileInfo.Buffer.resize(size);

    file.seekg(0);
    file.read(fileInfo.Buffer.data(), size);

    file.close();
    return fileInfo;
}

}

std::filesystem::path Shader::ToOutputPath(const std::filesystem::path &path)
{
    std::filesystem::path outputPath = path;
#ifndef NDEBUG
    outputPath.replace_extension(".spvd");
#else
    outputPath.replace_extension(".spv");
#endif
    return outputPath;
}

Shader::Shader(std::filesystem::path path, vk::ShaderStageFlagBits stage)
    : m_Path(path), m_OutputPath(ToOutputPath(path)), m_Stage(stage)
{
}

Shader::Shader(Shader &&shader) noexcept
    : m_Path(shader.m_Path), m_OutputPath(shader.m_OutputPath), m_Stage(shader.m_Stage),
      m_IncludedPaths(shader.m_IncludedPaths), m_UpdateTime(shader.m_UpdateTime), m_Code(shader.m_Code),
      m_Module(shader.m_Module)
{
    shader.m_IsMoved = true;
}

Shader::~Shader()
{
    if (m_IsMoved)
        return;

    if (!std::filesystem::is_regular_file(m_OutputPath) ||
        m_UpdateTime > std::filesystem::last_write_time(m_OutputPath))
    {
        logger::info("Saving {} to disk", m_OutputPath.string());
        std::ofstream file(m_OutputPath, std::ios::binary);
        file.write(m_Code.data(), m_Code.size());
    }

    DeviceContext::GetLogical().destroyShaderModule(m_Module);
}

std::filesystem::file_time_type Shader::ComputeUpdateTime() const
{
    if (!std::filesystem::is_regular_file(m_Path))
        return std::filesystem::file_time_type::max();

    std::filesystem::file_time_type maxUpdateTime = std::filesystem::last_write_time(m_Path);

    for (const std::filesystem::path &path : m_IncludedPaths)
    {
        if (std::filesystem::is_regular_file(path))
            maxUpdateTime = std::max(maxUpdateTime, std::filesystem::last_write_time(path));
        else
            return std::filesystem::file_time_type::max();
    }

    return maxUpdateTime;
}

void Shader::UpdateModule(std::span<const uint32_t> code, std::filesystem::file_time_type updateTime)
{
    vk::ShaderModuleCreateInfo createInfo(vk::ShaderModuleCreateFlags(), code);
    DeviceContext::GetLogical().destroyShaderModule(m_Module);
    m_Module = DeviceContext::GetLogical().createShaderModule(createInfo);
    auto codeText = SpanCast<const uint32_t, const char>(code);
    m_Code = std::vector(codeText.begin(), codeText.end());
    m_UpdateTime = updateTime;
}

vk::ShaderModule Shader::GetModule(
    const shaderc::Compiler &compiler, const shaderc::CompileOptions &options, Includer *includer
)
{
    auto updateTime = ComputeUpdateTime();
    if (updateTime <= m_UpdateTime)
    {
        logger::debug("{} is up to date", m_Path.string());
        return m_Module;
    }

    logger::trace("Recomputing dependencies of {}", m_Path.string());

    if (!std::filesystem::is_regular_file(m_Path))
        throw error(std::format("Failed to compile shader {} - file doesn't exist", m_Path.string()));
    FileInfo info = ReadFile(m_Path);

    includer->IncludedFiles.clear();
    auto preprocessResult = compiler.PreprocessGlsl(
        info.Buffer.data(), info.Buffer.size(), ToShaderKind(m_Stage), info.Path.c_str(), options
    );

    if (preprocessResult.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        logger::error("Failed to preprocess shader {}", m_Path.string());
        logger::error(preprocessResult.GetErrorMessage());
        if (m_Module != nullptr)
            logger::warn("Using old version of the shader: {}", m_Path.string());
        return m_Module;
    }

    includer->IncludedFiles.swap(m_IncludedPaths);
    for (const auto &include : m_IncludedPaths)
        logger::trace("{} depends on {}", m_Path.string(), include.string());
    updateTime = ComputeUpdateTime();

    if (std::filesystem::is_regular_file(m_OutputPath))
    {
        auto outputTime = std::filesystem::last_write_time(m_OutputPath);
        if (outputTime > updateTime)
        {
            logger::info("Loading {} from disk", m_OutputPath.string());
            FileInfo info = ReadFile(m_OutputPath);
            UpdateModule(SpanCast<const char, const uint32_t>(info.Buffer), outputTime);
            return m_Module;
        }
    }

    auto preprocessedCode = std::span(preprocessResult);
    auto compileResult = compiler.CompileGlslToSpv(
        preprocessedCode.data(), preprocessedCode.size(), ToShaderKind(m_Stage), info.Path.c_str(), options
    );

    if (compileResult.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        logger::error("Failed to compile shader {}", m_Path.string());
        logger::error(compileResult.GetErrorMessage());
        if (m_Module != nullptr)
            logger::warn("Using old version of the shader: {}", m_Path.string());
        return m_Module;
    }

    logger::info("Shader {} compiled successfully!", m_Path.string());
    
    UpdateModule(std::span(compileResult), updateTime);
    return m_Module;
}

ShaderLibrary::ShaderLibrary()
{
    assert(m_Compiler.IsValid() == true);

    auto includer = std::make_unique<Includer>();
    m_Includer = includer.get();
    m_Options.SetIncluder(std::move(includer));

    m_Options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
#ifndef NDEBUG
    m_Options.SetGenerateDebugInfo();
#endif
}

ShaderLibrary::~ShaderLibrary() = default;

ShaderId ShaderLibrary::AddShader(std::filesystem::path path, vk::ShaderStageFlagBits stage)
{
    m_Shaders.emplace_back(path, stage);
    m_Stages.emplace_back(vk::PipelineShaderStageCreateFlags(), stage, nullptr, "main");
    return m_Shaders.size() - 1;
}

void ShaderLibrary::ResizeGroups(uint32_t size)
{
    if (m_Groups.size() < size)
        m_Groups.resize(size);
}

void ShaderLibrary::AddGeneralGroup(uint32_t groupIndex, ShaderId shaderId)
{
    ResizeGroups(groupIndex + 1);
    m_Groups[groupIndex] = { vk::RayTracingShaderGroupTypeKHR::eGeneral, static_cast<uint32_t>(shaderId) };
}

void ShaderLibrary::AddHitGroup(uint32_t groupIndex, ShaderId closestHitId, ShaderId anyHitId)
{
    ResizeGroups(groupIndex + 1);
    m_Groups[groupIndex] = { vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR,
                             static_cast<uint32_t>(closestHitId), static_cast<uint32_t>(anyHitId) };
}

vk::Pipeline ShaderLibrary::CreatePipeline(vk::PipelineLayout layout)
{
    for (int i = 0; i < m_Shaders.size(); i++)
    {
        auto module = m_Shaders[i].GetModule(m_Compiler, m_Options, m_Includer);
        if (module == nullptr)
            throw error("Pipeline creation failed!");
        m_Stages[i].setModule(module);
    }

    vk::RayTracingPipelineCreateInfoKHR createInfo(vk::PipelineCreateFlags(), m_Stages, m_Groups, 1);
    createInfo.setLayout(layout);

    vk::ResultValue<vk::Pipeline> result = DeviceContext::GetLogical().createRayTracingPipelineKHR(
        nullptr, nullptr, createInfo, nullptr, Application::GetDispatchLoader()
    );

    assert(result.result == vk::Result::eSuccess);

    logger::info("Pipeline creation successfull!");
    return result.value;
}

shaderc_include_result *Includer::GetInclude(
    const char *requested_source, shaderc_include_type type, const char *requesting_source,
    size_t include_depth
)
{
    std::filesystem::path path;
    FileInfo *fileInfo = nullptr;

    try
    {
        path = GetFilePath(requested_source, type, requesting_source);
    }
    catch (const error &err)
    {
        fileInfo = new FileInfo();
        fileInfo->Path = err.what();
        return new shaderc_include_result {
            "", 0, fileInfo->Path.c_str(), fileInfo->Path.size(), fileInfo,
        };
    }

    if (IncludedFiles.contains(path))
    {
        // File was already included - return empty file as to not include it twice (#pragma once)
        const auto &strpath = m_Cache[path].Path;
        return new shaderc_include_result { strpath.c_str(), strpath.size(), nullptr, 0, nullptr };
    }

    auto it = m_Cache.find(path);
    if (it == m_Cache.end() || it->second.Time < std::filesystem::last_write_time(path))
        m_Cache[path] = ReadFile(path);

    IncludedFiles.insert(path);
    fileInfo = &m_Cache[path];

    return new shaderc_include_result { fileInfo->Path.c_str(), fileInfo->Path.size(),
                                        fileInfo->Buffer.data(), fileInfo->Buffer.size(), nullptr };
}

void Includer::ReleaseInclude(shaderc_include_result *data)
{
    delete data->user_data;
    delete data;
}

std::filesystem::path Includer::GetFilePath(
    const char *requested_source, shaderc_include_type type, const char *requesting_source
)
{
    std::filesystem::path requested(requested_source);
    std::filesystem::path requesting(requesting_source);

    switch (type)
    {
    case shaderc_include_type_relative:
    {
        std::filesystem::path relativePath = requesting.remove_filename() / requested;
        if (std::filesystem::is_regular_file(relativePath))
            return relativePath;
        break;
    }
    case shaderc_include_type_standard:
    {
        for (const std::filesystem::path &path : SystemIncludePaths)
        {
            std::filesystem::path absolutePath = path / requested;
            if (std::filesystem::is_regular_file(absolutePath))
                return absolutePath;
        }
        break;
    }
    default:
        throw error("Invalid include type");
    }

    std::string include = type == shaderc_include_type_standard ? std::format("<{}>", requested_source)
                                                                : std::format("\"{}\"", requested_source);
    throw error(std::format("File not found when including {}", include));
}

}
