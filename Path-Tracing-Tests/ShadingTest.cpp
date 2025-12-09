#include <gtest/gtest.h>

#include "Shaders/ShadingTestShaderTypes.incl"

#include "TestCommon.h"
#include "TestData.h"
#include "TestRenderer.h"

using namespace PathTracingTests;

using ShadingTestPipelineConfig = PathTracing::PipelineConfig<1>;

TEST(ShadingTest, GGXDistribution)
{
    using Input = Shaders::GGXDistributionInput;
    using Output = Shaders::GGXDistributionOutput;
    using Generator = Data::Vec3FloatGenerator;

    std::array<Input, Generator::GetSize()> input;

    Generator generator;
    for (int i = 0; i < input.size(); i++)
        std::tie(input[i].H, input[i].alpha) = generator.Next();

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeGGXDistribution };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertFloat(outputElement.result);
    }
}

TEST(ShadingTest, Lambda)
{
    using Input = Shaders::LambdaInput;
    using Output = Shaders::LambdaOutput;

    using Generator = Data::Vec3FloatGenerator;

    std::array<Input, Generator::GetSize()> input;

    Generator generator;
    for (int i = 0; i < input.size(); i++)
        std::tie(input[i].V, input[i].alpha) = generator.Next();

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeLambda };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertFloat(outputElement.result);
    }
}

TEST(ShadingTest, GGXSmith)
{
    using Input = Shaders::GGXSmithInput;
    using Output = Shaders::GGXSmithOutput;

    using Generator = Data::Vec3FloatGenerator;

    std::array<Input, Generator::GetSize()> input;

    Generator generator;
    for (int i = 0; i < input.size(); i++)
        std::tie(input[i].V, input[i].alpha) = generator.Next();

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeGGXSmith };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertFloat(outputElement.result);
    }
}

TEST(ShadingTest, DielectricFresnel)
{
    using Input = Shaders::DielectricFresnelInput;
    using Output = Shaders::DielectricFresnelOutput;

    using Generator = Data::FloatFloatGenerator;

    std::array<Input, Generator::GetSize()> input;

    Generator generator;
    for (int i = 0; i < input.size(); i++)
        std::tie(input[i].VdotH, input[i].eta) = generator.Next();

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeDielectricFresnel };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertFloat(outputElement.result);
    }
}

TEST(ShadingTest, SchlickFresnel)
{
    using Input = Shaders::SchlickFresnelInput;
    using Output = Shaders::SchlickFresnelOutput;

    std::array<Input, Data::EdgeCaseFloats.size()> input;

    for (int i = 0; i < input.size(); i++)
        input[i].VdotH = Data::EdgeCaseFloats[i];

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeSchlickFresnel };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertFloat(outputElement.result);
    }
}

TEST(ShadingTest, EvaluateReflection)
{
    using Input = Shaders::EvaluateReflectionInput;
    using Output = Shaders::EvaluateReflectionOutput;

    using Generator1 = Data::Vec3Vec3Generator;
    using Generator2 = Data::Vec3FloatGenerator;

    std::array<Input, Generator1::GetSize() * Generator2::GetSize()> input;

    Generator1 generator1;
    for (int i = 0; i < Generator1::GetSize(); i++)
    {
        Generator2 generator2;
        auto val1 = generator1.Next();
        for (int j = 0; j < Generator2::GetSize(); j++)
        {
            size_t index = i * Generator2::GetSize() + j;
            std::tie(input[index].V, input[index].L) = val1;
            std::tie(input[index].F, input[index].alpha) = generator2.Next();
        }
    }

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeEvaluateReflection };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertVec3(outputElement.result);
        AssertFloat(outputElement.pdf);
    }
}

TEST(ShadingTest, EvaluateRefraction)
{
    using Input = Shaders::EvaluateRefractionInput;
    using Output = Shaders::EvaluateRefractionOutput;

    using Generator1 = Data::Vec3Vec3Generator;
    using Generator2 = Data::Vec3FloatGenerator;

    std::array<Input, Data::EdgeCaseFloats.size() * Generator1::GetSize() * Generator2::GetSize()> input;

    for (int k = 0; k < Data::EdgeCaseFloats.size(); k++)
    {
        Generator1 generator1;
        for (int i = 0; i < Generator1::GetSize(); i++)
        {
            Generator2 generator2;
            auto val1 = generator1.Next();
            for (int j = 0; j < Generator2::GetSize(); j++)
            {
                const size_t index =
                    k * Generator1::GetSize() * Generator2::GetSize() + i * Generator2::GetSize() + j;
                input[index].eta = Data::EdgeCaseFloats[k];
                std::tie(input[index].V, input[index].L) = val1;
                std::tie(input[index].F, input[index].alpha) = generator2.Next();
            }
        }
    }

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeEvaluateRefraction };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertVec3(outputElement.result);
        AssertFloat(outputElement.pdf);
    }
}

TEST(ShadingTest, SampleGGX)
{
    using Input = Shaders::SampleGGXInput;
    using Output = Shaders::SampleGGXOutput;

    using Generator1 = Data::FloatFloatGenerator;
    using Generator2 = Data::Vec3FloatGenerator;

    std::array<Input, Generator1::GetSize() * Generator2::GetSize()> input;
    Generator1 generator1;
    for (int i = 0; i < Generator1::GetSize(); i++)
    {
        Generator2 generator2;
        auto val1 = generator1.Next();
        for (int j = 0; j < Generator2::GetSize(); j++)
        {
            size_t index = i * Generator2::GetSize() + j;
            std::tie(input[index].u.x, input[index].u.y) = val1;
            std::tie(input[index].V, input[index].alpha) = generator2.Next();
        }
    }

    ShadingTestPipelineConfig config = { Shaders::ShadingTestModeSampleGGX };

    TestRenderer::WriteInput<Input>(input);
    TestRenderer::ExecutePipeline("testShading.comp", config, input.size());
    auto output = TestRenderer::ReadOutput<Output>();

    for (int i = 0; i < input.size(); i++)
    {
        auto &outputElement = output[i];
        AssertVec3(outputElement.result);
    }
}
