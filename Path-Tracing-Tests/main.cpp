#include <gtest/gtest.h>

#include "Shaders/ShadingTestShaderTypes.incl"
#include "Shaders/BsdfTestShaderTypes.incl"

#include "TestEnvironment.h"
#include "TestRenderer.h"

using namespace PathTracingTests;

static PathTracing::PipelineConfig<1> PaddingTestMaxPipelineConfig = { Shaders::PaddingTestModeMax };
static PathTracing::PipelineConfig<1> ShadingTestMaxPipelineConfig = { Shaders::ShadingTestModeMax };
static PathTracing::PipelineConfig<1> BsdfTestMaxPipelineConfig = { Shaders::BsdfTestModeMax };

int main(int argc, char *argv[])
{
    TestRenderer::SetMaxConfig("testPadding.comp", PaddingTestMaxPipelineConfig);
    TestRenderer::SetMaxConfig("testShading.comp", ShadingTestMaxPipelineConfig);
    TestRenderer::SetMaxConfig("testBsdf.comp", BsdfTestMaxPipelineConfig);

    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new TestEnvironment());
    return RUN_ALL_TESTS();
}
