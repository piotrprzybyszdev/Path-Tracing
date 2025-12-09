#include <gtest/gtest.h>

#include "Shaders/BsdfTestShaderTypes.incl"

#include "TestData.h"
#include "TestRenderer.h"

using namespace PathTracingTests;

using BsdfTestPipelineConfig = PathTracing::PipelineConfig<1>;

TEST(BsdfTest, SampleLobePdfs)
{
    using Input = Shaders::SampleLobePdfsInput;
    using Output = Shaders::SampleLobePdfsOutput;

    const std::array<float, 5> floats = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    std::array<Input, floats.size() * floats.size() * floats.size()> input;

    for (int k = 0; k < floats.size(); k++)
        for (int i = 0; i < floats.size(); i++)
            for (int j = 0; j < floats.size(); j++)
                input[k * floats.size() * floats.size() + i * floats.size() + j] = {
                    .Metalness = floats[i], .Transmission = floats[j], .F = floats[k]
                };

    BsdfTestPipelineConfig config = { Shaders::BsdfTestModeSampleLobePdfs };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testBsdf.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        const float sum = outputElement.Diffuse + outputElement.Glossy + outputElement.Metallic +
                          outputElement.Transmissive;
        ASSERT_FLOAT_EQ(sum, 1.0f);
    }
}
