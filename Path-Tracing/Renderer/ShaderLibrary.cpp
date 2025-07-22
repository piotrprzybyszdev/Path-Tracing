#include <fstream>

#include "Core/Core.h"

#include "Application.h"
#include "DeviceContext.h"
#include "ShaderLibrary.h"
#include "Utils.h"

namespace PathTracing
{

ShaderLibrary::ShaderLibrary()
{
}

ShaderLibrary::~ShaderLibrary()
{
    for (vk::ShaderModule module : m_Modules)
        DeviceContext::GetLogical().destroyShaderModule(module);
}

void ShaderLibrary::AddRaygenShader(std::filesystem::path path, std::string_view entry)
{
    AddShader(
        path, entry, vk::ShaderStageFlagBits::eRaygenKHR, vk::RayTracingShaderGroupTypeKHR::eGeneral,
        RaygenShaderIndex
    );
}

void ShaderLibrary::AddMissShader(std::filesystem::path path, std::string_view entry)
{
    AddShader(
        path, entry, vk::ShaderStageFlagBits::eMissKHR, vk::RayTracingShaderGroupTypeKHR::eGeneral,
        MissShaderIndex
    );
}

void ShaderLibrary::AddClosestHitShader(std::filesystem::path path, std::string_view entry)
{
    AddShader(
        path, entry, vk::ShaderStageFlagBits::eClosestHitKHR,
        vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, ClosestHitShaderIndex
    );
}

vk::Pipeline ShaderLibrary::CreatePipeline(vk::PipelineLayout layout)
{
    vk::RayTracingPipelineCreateInfoKHR createInfo(vk::PipelineCreateFlags(), m_Stages, m_Groups, 1);
    createInfo.setLayout(layout);

    vk::ResultValue<vk::Pipeline> result = DeviceContext::GetLogical().createRayTracingPipelineKHR(
        nullptr, nullptr, createInfo, nullptr, Application::GetDispatchLoader()
    );

    assert(result.result == vk::Result::eSuccess);
    return result.value;
}

void ShaderLibrary::AddShader(
    std::filesystem::path path, std::string_view entry, vk::ShaderStageFlagBits stage,
    vk::RayTracingShaderGroupTypeKHR type, uint32_t index
)
{
    vk::ShaderModule module = LoadShader(path);
    m_Stages.push_back({ vk::PipelineShaderStageCreateFlags(), stage, module, entry.data() });

    if (stage == vk::ShaderStageFlagBits::eClosestHitKHR)
    {
        vk::RayTracingShaderGroupCreateInfoKHR createInfo(type);
        createInfo.setClosestHitShader(index);
        m_Groups.push_back(createInfo);
    }
    else
        m_Groups.push_back({ type, index });

    m_Modules.push_back(module);

    Utils::SetDebugName(module, std::filesystem::absolute(path).string());
}

vk::ShaderModule ShaderLibrary::LoadShader(std::filesystem::path path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
        throw error(std::format("Shader file {} cannot be opened", path.string()));

    size_t size = file.tellg();
    std::vector<char> buffer(size);

    file.seekg(0);
    file.read(buffer.data(), size);

    file.close();

    vk::ShaderModuleCreateInfo createInfo(
        vk::ShaderModuleCreateFlags(), size, reinterpret_cast<uint32_t *>(buffer.data())
    );
    return DeviceContext::GetLogical().createShaderModule(createInfo);
}

}
