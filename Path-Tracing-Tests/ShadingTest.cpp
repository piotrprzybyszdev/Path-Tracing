#include <gtest/gtest.h>

#include "Shaders/TestShaderTypes.incl"

#include "TestRenderer.h"

using namespace PathTracingTests;

using ShadingTestPipelineConfig = PathTracing::PipelineConfig<0>;

TEST(ShadingTest, FresnelSchlick)
{
    using Input = Shaders::FresnelSchlickInput;
    using Output = Shaders::FresnelSchlickOutput;

    std::array<Input, 2> input = {
        Input { .theta = 0.5f, .f0 = glm::vec3(1.0f, 2.0f, 3.0f) / 6.0f },
        Input { .theta = 0.5f, .f0 = glm::vec3(4.0f, 5.0f, 6.0f) / 6.0f },
    };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", ShadingTestPipelineConfig(), input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        EXPECT_FALSE(glm::any(glm::equal(outputElement.result, glm::vec3(0.0f))));
        EXPECT_FALSE(glm::any(glm::isnan(outputElement.result)));
    }
}
