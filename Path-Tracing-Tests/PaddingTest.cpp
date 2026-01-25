#include <gtest/gtest.h>

#include "Shaders/ShadingTestShaderTypes.incl"

#include "TestRenderer.h"

using namespace PathTracingTests;

using PaddingTestPipelineConfig = PathTracing::PipelineConfig<1>;

TEST(PaddingTest, MetallicRoughnessMaterial)
{
    using Input = PathTracing::Shaders::MetallicRoughnessMaterial;
    using Output = Input;

    std::array<Input, 2> input = {
        Input { .EmissiveColor = glm::vec3(1.1f, 2.2f, 3.3f), .EmissiveIntensity = 4.4f, 
                .Color = glm::vec4(1.0f, 2.0f, 3.0f, 12.0f), .Roughness = 4.0f, .Metalness = 5.0f,
                .Ior = 1.5f, .Transmission = 2.5f, .AttenuationColor = glm::vec3(3.5f, 4.5f, 5.5f),
                .AttenuationDistance = 6.5f, .EmissiveIdx = 10, .ColorIdx = 1, .NormalIdx = 2,
                .RoughnessIdx = 3, .MetallicIdx = 4},
        Input { .EmissiveColor = glm::vec3(5.5f, 6.6f, 7.7f), .EmissiveIntensity = 8.8f, 
                .Color = glm::vec4(5.0f, 6.0f, 7.0f, 11.0f), .Roughness = 8.0f, .Metalness = 9.0f,
                .Ior = 1.9f, .Transmission = 2.9f, .AttenuationColor = glm::vec3(3.9f, 4.9f, 5.9f),
                .AttenuationDistance = 6.9f, .EmissiveIdx = 9, .ColorIdx = 5, .NormalIdx = 6,
                .RoughnessIdx = 7, .MetallicIdx = 8 },
    };

    PaddingTestPipelineConfig config = { Shaders::PaddingTestModeMetallicRoughnessMaterial };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testPadding.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &inputElement = input[i];
        auto &outputElement = output[i];
        EXPECT_EQ(inputElement.EmissiveColor, outputElement.EmissiveColor);
        EXPECT_EQ(inputElement.EmissiveIntensity, outputElement.EmissiveIntensity);
        EXPECT_EQ(inputElement.Color, outputElement.Color);
        EXPECT_EQ(inputElement.Roughness, outputElement.Roughness);
        EXPECT_EQ(inputElement.Metalness, outputElement.Metalness);
        EXPECT_EQ(inputElement.Ior, outputElement.Ior);
        EXPECT_EQ(inputElement.Transmission, outputElement.Transmission);
        EXPECT_EQ(inputElement.AttenuationColor, outputElement.AttenuationColor);
        EXPECT_EQ(inputElement.AttenuationDistance, outputElement.AttenuationDistance);
        EXPECT_EQ(inputElement.EmissiveIdx, outputElement.EmissiveIdx);
        EXPECT_EQ(inputElement.ColorIdx, outputElement.ColorIdx);
        EXPECT_EQ(inputElement.NormalIdx, outputElement.NormalIdx);
        EXPECT_EQ(inputElement.RoughnessIdx, outputElement.RoughnessIdx);
        EXPECT_EQ(inputElement.MetallicIdx, outputElement.MetallicIdx);
    }
}

TEST(PaddingTest, SpecularGlossinessMaterial)
{
    using Input = PathTracing::Shaders::SpecularGlossinessMaterial;
    using Output = Input;

    std::array<Input, 2> input = {
        Input { .EmissiveColor = glm::vec3(1.1f, 2.2f, 3.3f), .EmissiveIntensity = 4.4f,
                .Color = glm::vec4(1.0f, 2.0f, 3.0f, 12.0f), .Specular = glm::vec3(4.0f, 5.0f, 6.0f),
                .Glossiness = 7.0f, .AttenuationColor = glm::vec3(1.1f, 1.2f, 1.3f), .AttenuationDistance = 1.4f,
                .Ior = 1.5f, .Transmission = 1.6f, .EmissiveIdx = 10, .ColorIdx = 1, .NormalIdx = 2,
                .SpecularIdx = 3, .GlossinessIdx = 4 },
        Input { .EmissiveColor = glm::vec3(5.5f, 6.6f, 7.7f), .EmissiveIntensity = 8.8f,
                .Color = glm::vec4(8.0f, 9.0f, 10.0f, 11.0f), .Specular = glm::vec3(12.0f, 13.0f, 14.0f),
                .Glossiness = 15.0f, .AttenuationColor = glm::vec3(1.7f, 1.8f, 1.9f), .AttenuationDistance = 1.11f,
                .Ior = 1.12f, .Transmission = 1.13f, .EmissiveIdx = 9, .ColorIdx = 5, .NormalIdx = 6,
                .SpecularIdx = 7, .GlossinessIdx = 8 },
    };

    PaddingTestPipelineConfig config = { Shaders::PaddingTestModeSpecularGlossinessMaterial };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testPadding.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &inputElement = input[i];
        auto &outputElement = output[i];
        EXPECT_EQ(inputElement.EmissiveColor, outputElement.EmissiveColor);
        EXPECT_EQ(inputElement.EmissiveIntensity, outputElement.EmissiveIntensity);
        EXPECT_EQ(inputElement.Color, outputElement.Color);
        EXPECT_EQ(inputElement.Specular, outputElement.Specular);
        EXPECT_EQ(inputElement.Glossiness, outputElement.Glossiness);
        EXPECT_EQ(inputElement.Ior, outputElement.Ior);
        EXPECT_EQ(inputElement.Transmission, outputElement.Transmission);
        EXPECT_EQ(inputElement.AttenuationColor, outputElement.AttenuationColor);
        EXPECT_EQ(inputElement.AttenuationDistance, outputElement.AttenuationDistance);
        EXPECT_EQ(inputElement.EmissiveIdx, outputElement.EmissiveIdx);
        EXPECT_EQ(inputElement.ColorIdx, outputElement.ColorIdx);
        EXPECT_EQ(inputElement.NormalIdx, outputElement.NormalIdx);
        EXPECT_EQ(inputElement.SpecularIdx, outputElement.SpecularIdx);
        EXPECT_EQ(inputElement.GlossinessIdx, outputElement.GlossinessIdx);
    }
}

TEST(PaddingTest, PhongMaterial)
{
    using Input = PathTracing::Shaders::PhongMaterial;
    using Output = Input;

    std::array<Input, 2> input = {
        Input { .EmissiveColor = glm::vec3(1.1f, 2.2f, 3.3f), .EmissiveIntensity = 4.4f,
                .Color = glm::vec4(1.0f, 2.0f, 3.0f, 12.0f), .Specular = glm::vec3(4.0f, 5.0f, 6.0f),
                .Shininess = 7.0f, .AttenuationColor = glm::vec3(1.1f, 1.2f, 1.3f), .AttenuationDistance = 1.4f,
                .Ior = 1.5f, .Transmission = 1.6f, .EmissiveIdx = 10, .ColorIdx = 1, .NormalIdx = 2,
                .SpecularIdx = 3, .ShininessIdx = 4 },
        Input { .EmissiveColor = glm::vec3(5.5f, 6.6f, 7.7f), .EmissiveIntensity = 8.8f,
                .Color = glm::vec4(8.0f, 9.0f, 10.0f, 11.0f), .Specular = glm::vec3(12.0f, 13.0f, 14.0f),
                .Shininess = 15.0f, .AttenuationColor = glm::vec3(1.7f, 1.8f, 1.9f), .AttenuationDistance = 1.11f,
                .Ior = 1.12f, .Transmission = 1.13f, .EmissiveIdx = 9, .ColorIdx = 5, .NormalIdx = 6,
                .SpecularIdx = 7, .ShininessIdx = 8 },
    };

    PaddingTestPipelineConfig config = { Shaders::PaddingTestModeSpecularGlossinessMaterial };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testPadding.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &inputElement = input[i];
        auto &outputElement = output[i];
        EXPECT_EQ(inputElement.EmissiveColor, outputElement.EmissiveColor);
        EXPECT_EQ(inputElement.EmissiveIntensity, outputElement.EmissiveIntensity);
        EXPECT_EQ(inputElement.Color, outputElement.Color);
        EXPECT_EQ(inputElement.Specular, outputElement.Specular);
        EXPECT_EQ(inputElement.Shininess, outputElement.Shininess);
        EXPECT_EQ(inputElement.Ior, outputElement.Ior);
        EXPECT_EQ(inputElement.Transmission, outputElement.Transmission);
        EXPECT_EQ(inputElement.AttenuationColor, outputElement.AttenuationColor);
        EXPECT_EQ(inputElement.AttenuationDistance, outputElement.AttenuationDistance);
        EXPECT_EQ(inputElement.EmissiveIdx, outputElement.EmissiveIdx);
        EXPECT_EQ(inputElement.ColorIdx, outputElement.ColorIdx);
        EXPECT_EQ(inputElement.NormalIdx, outputElement.NormalIdx);
        EXPECT_EQ(inputElement.SpecularIdx, outputElement.SpecularIdx);
        EXPECT_EQ(inputElement.ShininessIdx, outputElement.ShininessIdx);
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
