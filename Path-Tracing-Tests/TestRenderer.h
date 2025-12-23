#pragma once

#include <memory>
#include <unordered_map>

#include "Renderer/CommandBuffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/ShaderLibrary.h"

namespace PathTracingTests
{

class TestRenderer
{
public:
    static void Init();
    static void Shutdown();

    static void SetMaxConfig(std::string shaderName, const PathTracing::PipelineConfigView maxConfig);

    static void AllocateResources(uint32_t inputBufferSize, uint32_t outputBufferSize);
    
    template<size_t N>
    static void ExecutePipeline(
        const std::string &path, const PathTracing::PipelineConfig<N> &config, uint32_t size
    )
    {
        s_Pipelines.at(path)->Update(config);
        ExecutePipeline(path, size);
    }

    template<typename T> static void WriteInput(std::span<T> input)
    {
        s_InputBuffer->Upload(PathTracing::SpanCast<T, std::byte>(input));
    }

    template<typename T> static std::span<T> ReadOutput()
    {
        s_OutputBuffer->Readback(s_Output);
        return PathTracing::SpanCast<std::byte, T>(std::span(s_Output));
    }

private:
    static std::unordered_map<std::string, std::unique_ptr<PathTracing::ComputePipeline>> s_Pipelines;
    static std::unordered_map<std::string, PathTracing::PipelineConfigView> s_PipelineMaxConfigs;

    static std::unique_ptr<PathTracing::ShaderLibrary> s_ShaderLibrary;
    static std::unique_ptr<PathTracing::CommandBuffer> s_CommandBuffer;
    static std::unique_ptr<PathTracing::BufferBuilder> s_BufferBuilder;

    static std::unique_ptr<PathTracing::Buffer> s_InputBuffer;
    static std::unique_ptr<PathTracing::Buffer> s_OutputBuffer;

    static std::vector<std::byte> s_Output;

private:
    static void ExecutePipeline(const std::string &path, uint32_t size);
};

}
