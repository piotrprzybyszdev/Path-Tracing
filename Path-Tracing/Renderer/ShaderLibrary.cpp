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
    case vk::ShaderStageFlagBits::eCompute:
        return shaderc_compute_shader;
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

FileInfo ReadFile(const std::filesystem::path &path)
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
    const auto &config = Application::GetConfig();
    std::filesystem::path outputPath = config.ShaderCachePath / path.filename();
    outputPath.replace_extension(config.ShaderSpvExtension);
    return outputPath;
}

Shader::Shader(std::filesystem::path path, vk::ShaderStageFlagBits stage)
    : m_Path(std::move(path)), m_OutputPath(ToOutputPath(m_Path)), m_Stage(stage)
{
}

Shader::Shader(Shader &&shader) noexcept
    : m_Path(std::move(shader.m_Path)), m_OutputPath(std::move(shader.m_OutputPath)), m_Stage(shader.m_Stage),
      m_IncludedPaths(std::move(shader.m_IncludedPaths)), m_UpdateTime(shader.m_UpdateTime),
      m_Code(std::move(shader.m_Code)), m_Module(shader.m_Module)
{
    shader.m_IsMoved = true;
}

vk::ShaderStageFlagBits Shader::GetStage() const
{
    return m_Stage;
}

const std::filesystem::path &Shader::GetPath() const
{
    return m_Path;
}

bool Shader::HasChanged() const
{
    return ComputeUpdateTime() > m_UpdateTime;
}

Shader::~Shader() noexcept
{
    if (m_IsMoved)
        return;

    if ((!std::filesystem::is_regular_file(m_OutputPath) ||
         m_UpdateTime > std::filesystem::last_write_time(m_OutputPath)) &&
        m_UpdateTime >= ComputeUpdateTime())
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
    // Shaders are reflected only once
    if (m_Module == nullptr)
        Reflect(code);

    vk::ShaderModuleCreateInfo createInfo(vk::ShaderModuleCreateFlags(), code);
    DeviceContext::GetLogical().destroyShaderModule(m_Module);
    m_Module = DeviceContext::GetLogical().createShaderModule(createInfo);
    auto codeText = SpanCast<const uint32_t, const char>(code);
    m_Code = std::vector(codeText.begin(), codeText.end());
    m_UpdateTime = updateTime;
}

void Shader::Reflect(
    const spirv_cross::Compiler &compiler, std::span<const spirv_cross::Resource> resources,
    vk::DescriptorType descriptorType
)
{
    for (const spirv_cross::Resource &resource : resources)
    {
        const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);

        assert(set == 0);  // TODO: Support multiple sets

        m_SetLayoutBindings.emplace_back(binding, descriptorType, 1, m_Stage);
        logger::debug(
            "{} ({}): {}, set = {}, binding = {}", m_Path.string(), vk::to_string(m_Stage),
            vk::to_string(descriptorType), set, binding
        );
    }
}

void Shader::Reflect(std::span<const uint32_t> code)
{
    spirv_cross::Compiler compiler(code.data(), code.size());

    spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    Reflect(compiler, resources.acceleration_structures, vk::DescriptorType::eAccelerationStructureKHR);
    Reflect(compiler, resources.sampled_images, vk::DescriptorType::eCombinedImageSampler);
    Reflect(compiler, resources.storage_buffers, vk::DescriptorType::eStorageBuffer);
    Reflect(compiler, resources.storage_images, vk::DescriptorType::eStorageImage);
    Reflect(compiler, resources.uniform_buffers, vk::DescriptorType::eUniformBuffer);
    
    if (!resources.push_constant_buffers.empty())
    {
        auto &pushConstants = resources.push_constant_buffers.front();

        const auto ranges = compiler.get_active_buffer_ranges(pushConstants.id);
        assert(ranges.size() == 1);

        const auto &range = ranges.front();

        m_PushConstants = { m_Stage, static_cast<uint32_t>(range.offset),
                            static_cast<uint32_t>(range.range) };
        logger::debug(
            "{} ({}): PC offset = {}, size = {}", m_Path.string(), vk::to_string(m_Stage), range.offset,
            range.range
        );
    }

    auto specializationConstants = compiler.get_specialization_constants();
    for (const auto &specialization : specializationConstants)
    {
        const uint32_t id = specialization.constant_id;
        m_SpecializationConstantIds.push_back(id);
        logger::debug("{} ({}): SC id = {}", m_Path.string(), vk::to_string(m_Stage), id);
    }
}

bool Shader::RecompileIfChanged(
    const shaderc::Compiler &compiler, const shaderc::CompileOptions &options, Includer *includer
)
{
    auto updateTime = ComputeUpdateTime();
    if (updateTime <= m_UpdateTime)
    {
        logger::debug("{} is up to date", m_Path.string());
        return false;
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
        return false;
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
            return true;
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
        return false;
    }

    logger::info("Shader {} compiled successfully!", m_Path.string());

    UpdateModule(std::span(compileResult), updateTime);
    return true;
}

vk::PipelineShaderStageCreateInfo Shader::GetStageCreateInfo() const
{
    return vk::PipelineShaderStageCreateInfo(vk::PipelineShaderStageCreateFlags(), m_Stage, m_Module, "main");
}

std::span<const vk::DescriptorSetLayoutBinding> Shader::GetSetLayoutBindings() const
{
    return m_SetLayoutBindings;
}

vk::PushConstantRange Shader::GetPushConstants() const
{
    return m_PushConstants;
}

std::span<const uint32_t> Shader::GetSpecializationConstantIds() const
{
    return m_SpecializationConstantIds;
}

ShaderLibrary::ShaderLibrary()
{
    assert(m_Compiler.IsValid() == true);

    m_Options.SetTargetEnvironment(shaderc_target_env_vulkan, Application::GetVulkanApiVersion());

    auto includer = std::make_unique<Includer>();
    m_Includer = includer.get();
    m_Options.SetIncluder(std::move(includer));

    const std::filesystem::path &shaderCachePath = Application::GetConfig().ShaderCachePath;
    if (!std::filesystem::is_directory(shaderCachePath))
    {
        std::filesystem::remove(shaderCachePath);
        std::filesystem::create_directory(shaderCachePath);
    }

    if (Application::GetConfig().ShaderDebugInfo)
        m_Options.SetGenerateDebugInfo();

    if (Application::GetConfig().OptimizeShaders)
        m_Options.SetOptimizationLevel(shaderc_optimization_level_performance);
    else
        m_Options.SetOptimizationLevel(shaderc_optimization_level_zero);
}

ShaderLibrary::~ShaderLibrary() = default;

ShaderId ShaderLibrary::AddShader(std::filesystem::path path, vk::ShaderStageFlagBits stage)
{
    m_Shaders.emplace_back(std::move(path), stage);
    return m_Shaders.size() - 1;
}

const Shader &ShaderLibrary::GetShader(ShaderId id) const
{
    return m_Shaders[id];
}

void ShaderLibrary::CompileShaders()
{
    for (Shader &shader : m_Shaders)
        shader.RecompileIfChanged(m_Compiler, m_Options, m_Includer);
}

std::vector<bool> ShaderLibrary::RecompileChanged(std::span<const ShaderId> shaderIds)
{
    std::vector<bool> upToDate;
   
    std::ranges::transform(shaderIds, std::back_inserter(upToDate), [this](ShaderId id) {
        return !m_Shaders[id].RecompileIfChanged(m_Compiler, m_Options, m_Includer);
    });

    return upToDate;
}

Includer::Includer()
    : m_MaxIncludeDepth(Application::GetConfig().MaxShaderIncludeDepth),
      m_Cache(Application::GetConfig().MaxShaderIncludeCacheSize)
{
}

shaderc_include_result *Includer::GetInclude(
    const char *requested_source, shaderc_include_type type, const char *requesting_source,
    size_t include_depth
)
{
    if (include_depth > m_MaxIncludeDepth)
    {
        FileInfo *fileInfo = new FileInfo();
        fileInfo->Path = std::format("MaxIncludeDepth exceeded {}/{}", include_depth, m_MaxIncludeDepth);
        logger::warn(fileInfo->Path);
        return new shaderc_include_result {
            "", 0, fileInfo->Path.c_str(), fileInfo->Path.size(), fileInfo,
        };
    }

    std::filesystem::path path;

    try
    {
        path = GetFilePath(requested_source, type, requesting_source);
    }
    catch (const error &err)
    {
        FileInfo *fileInfo = new FileInfo();
        fileInfo->Path = err.what();
        return new shaderc_include_result {
            "", 0, fileInfo->Path.c_str(), fileInfo->Path.size(), fileInfo,
        };
    }

    if (IncludedFiles.contains(path))
    {
        // File was already included - return empty file as to not include it twice (#pragma once)
        FileInfo *fileInfo = new FileInfo();
        fileInfo->Path = path.string();
        return new shaderc_include_result { fileInfo->Path.c_str(), fileInfo->Path.size(), nullptr, 0,
                                            fileInfo };
    }

    if (!m_Cache.Contains(path) || m_Cache.Get(path).Time < std::filesystem::last_write_time(path))
        auto _ = m_Cache.Insert(path, ReadFile(path));

    IncludedFiles.insert(path);
    const FileInfo *fileInfo = &m_Cache.Get(path);

    return new shaderc_include_result { fileInfo->Path.c_str(), fileInfo->Path.size(),
                                        fileInfo->Buffer.data(), fileInfo->Buffer.size(), nullptr };
}

void Includer::ReleaseInclude(shaderc_include_result *data)
{
    delete static_cast<FileInfo *>(data->user_data);
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
