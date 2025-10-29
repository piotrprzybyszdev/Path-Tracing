#include <gtest/gtest.h>

#include "Shaders/TestShaderTypes.incl"

#include "TestRenderer.h"

using namespace PathTracingTests;

using PaddingTestPipelineConfig = PathTracing::PipelineConfig<1>;

TEST(PaddingTest, SolidColorMaterial)
{
    using Input = PathTracing::Shaders::SolidColorMaterial;
    using Output = Input;

    std::array<Input, 2> input = {
        Input { .Color = glm::vec3(1.0f, 2.0f, 3.0f) },
        Input { .Color = glm::vec3(4.0f, 5.0f, 6.0f) },
    };

    PaddingTestPipelineConfig config = { Shaders::PaddingTestModeSolidColorMaterial };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testPadding.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &inputElement = input[i];
        auto &outputElement = output[i];
        EXPECT_EQ(inputElement.Color, outputElement.Color);
    }
}

TEST(PaddingTest, TexturedMaterial)
{
    using Input = PathTracing::Shaders::TexturedMaterial;
    using Output = Input;

    std::array<Input, 2> input = {
        Input { .ColorIdx = 1, .NormalIdx = 2, .RoughnessIdx = 3, .MetalicIdx = 4 },
        Input { .ColorIdx = 5, .NormalIdx = 6, .RoughnessIdx = 7, .MetalicIdx = 8 },
    };

    PaddingTestPipelineConfig config = { Shaders::PaddingTestModeTexturedMaterial };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testPadding.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &inputElement = input[i];
        auto &outputElement = output[i];
        EXPECT_EQ(inputElement.ColorIdx, outputElement.ColorIdx);
        EXPECT_EQ(inputElement.NormalIdx, outputElement.NormalIdx);
        EXPECT_EQ(inputElement.RoughnessIdx, outputElement.RoughnessIdx);
        EXPECT_EQ(inputElement.MetalicIdx, outputElement.MetalicIdx);
    }
}

TEST(PaddingTest, DirectionalLight)
{
    using Input = PathTracing::Shaders::DirectionalLight;
    using Output = Input;

    std::array<Input, 2> input = {
        Input { .Color = glm::vec3(1.0f, 2.0f, 3.0f), .Direction = glm::vec3(4.0f, 5.0f, 6.0f) },
        Input { .Color = glm::vec3(0.1f, 0.2f, 0.3f), .Direction = glm::vec3(0.4f, 0.5f, 0.6f) },
    };

    PaddingTestPipelineConfig config = { Shaders::PaddingTestModeDirectionalLight };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testPadding.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &inputElement = input[i];
        auto &outputElement = output[i];
        EXPECT_EQ(inputElement.Color, outputElement.Color);
        EXPECT_EQ(inputElement.Direction, outputElement.Direction);
    }
}

TEST(PaddingTest, PointLight)
{
    using Input = PathTracing::Shaders::PointLight;
    using Output = Input;

    std::array<Input, 2> input = {
        Input {
            .Color = glm::vec3(1.0f, 2.0f, 3.0f),
            .Position = glm::vec3(4.0f, 5.0f, 6.0f),
            .AttenuationConstant = 1.1f,
            .AttenuationLinear = 2.2f,
            .AttenuationQuadratic = 3.3f,
        },
        Input {
            .Color = glm::vec3(0.1f, 0.2f, 0.3f),
            .Position = glm::vec3(0.4f, 0.5f, 0.6f),
            .AttenuationConstant = 4.4f,
            .AttenuationLinear = 5.5f,
            .AttenuationQuadratic = 6.6f,
        },
    };

    PaddingTestPipelineConfig config = { Shaders::PaddingTestModePointLight };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testPadding.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &inputElement = input[i];
        auto &outputElement = output[i];
        EXPECT_EQ(inputElement.Color, outputElement.Color);
        EXPECT_EQ(inputElement.Position, outputElement.Position);
        EXPECT_EQ(inputElement.AttenuationConstant, outputElement.AttenuationConstant);
        EXPECT_EQ(inputElement.AttenuationLinear, outputElement.AttenuationLinear);
        EXPECT_EQ(inputElement.AttenuationQuadratic, outputElement.AttenuationQuadratic);
    }
}
