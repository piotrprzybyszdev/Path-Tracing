#include "Renderer/DeviceContext.h"

#include "Application.h"
#include "TestRenderer.h"

namespace PathTracingTests
{

std::unordered_map<std::string, std::unique_ptr<PathTracing::ComputePipeline>> TestRenderer::s_Pipelines = {};
std::unordered_map<std::string, PathTracing::PipelineConfigView> TestRenderer::s_PipelineMaxConfigs = {};

std::unique_ptr<PathTracing::ShaderLibrary> TestRenderer::s_ShaderLibrary = nullptr;
std::unique_ptr<PathTracing::CommandBuffer> TestRenderer::s_CommandBuffer = nullptr;
std::unique_ptr<PathTracing::BufferBuilder> TestRenderer::s_BufferBuilder = nullptr;

std::unique_ptr<PathTracing::Buffer> TestRenderer::s_InputBuffer = nullptr;
std::unique_ptr<PathTracing::Buffer> TestRenderer::s_OutputBuffer = nullptr;

std::vector<std::byte> TestRenderer::s_Output = {};

void TestRenderer::Init()
{
    s_ShaderLibrary = std::make_unique<PathTracing::ShaderLibrary>();

    std::unordered_map<std::string, PathTracing::ShaderId> shaderIds;
    for (const std::filesystem::path &path :
         std::filesystem::directory_iterator(PathTracing::Application::GetConfig().ShaderDirectoryPath))
        if (path.extension() == ".comp")
            shaderIds[path.filename().string()] =
                s_ShaderLibrary->AddShader(path, vk::ShaderStageFlagBits::eCompute);

    s_ShaderLibrary->CompileShaders();

    for (auto &[name, id] : shaderIds)
    {
        PathTracing::ComputePipelineBuilder builder(*s_ShaderLibrary, id);
        auto pipeline = builder.CreatePipelineUnique(s_PipelineMaxConfigs.at(name));
        pipeline->CreateDescriptorSet(1);
        s_Pipelines[name] = std::move(pipeline);
    }

    s_CommandBuffer =
        std::make_unique<PathTracing::CommandBuffer>(PathTracing::DeviceContext::GetGraphicsQueue());

    s_BufferBuilder = std::make_unique<PathTracing::BufferBuilder>();

    s_BufferBuilder->SetUsageFlags(
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
    );
}

void TestRenderer::Shutdown()
{
    s_Output.clear();

    s_Pipelines.clear();

    s_OutputBuffer.reset();
    s_InputBuffer.reset();

    s_BufferBuilder.reset();
    s_CommandBuffer.reset();
    s_ShaderLibrary.reset();
}

void TestRenderer::SetMaxConfig(std::string shaderName, const PathTracing::PipelineConfigView maxConfig)
{
    s_PipelineMaxConfigs[shaderName] = maxConfig;
}

void TestRenderer::AllocateResources(uint32_t inputBufferSize, uint32_t outputBufferSize)
{
    s_InputBuffer = s_BufferBuilder->CreateHostBufferUnique(inputBufferSize);
    s_OutputBuffer = s_BufferBuilder->CreateHostBufferUnique(outputBufferSize);
}

void TestRenderer::ExecutePipeline(const std::string &path, uint32_t size)
{
    auto pipeline = s_Pipelines.at(path).get();

    PathTracing::DescriptorSet *descriptorSet = pipeline->GetDescriptorSet();
    descriptorSet->FlushUpdate(0);

    s_CommandBuffer->Begin();

    vk::CommandBuffer commandBuffer = s_CommandBuffer->Buffer;

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetHandle());

    std::array<uint64_t, 2> pc = { s_InputBuffer->GetDeviceAddress(), s_OutputBuffer->GetDeviceAddress() };

    commandBuffer.pushConstants(
        pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0u, pc.size() * sizeof(uint64_t), pc.data()
    );

    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0,
        { pipeline->GetDescriptorSet()->GetSet(0) }, {}
    );

    commandBuffer.dispatch(size, 1, 1);

    s_CommandBuffer->SubmitBlocking();
}

}