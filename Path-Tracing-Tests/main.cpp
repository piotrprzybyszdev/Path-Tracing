#include <gtest/gtest.h>

#include "Shaders/TestShaderTypes.incl"

#include "TestEnvironment.h"
#include "TestRenderer.h"

using namespace PathTracingTests;

static PathTracing::PipelineConfig<1> PaddingTestMaxPipelineConfig = { Shaders::PaddingTestModeMax };
static PathTracing::PipelineConfig<0> ShadingTestMaxPipelineConfig = {};

int main(int argc, char *argv[])
{
    TestRenderer::SetMaxConfig("testPadding.comp", PaddingTestMaxPipelineConfig);
    TestRenderer::SetMaxConfig("testShading.comp", ShadingTestMaxPipelineConfig);

    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new TestEnvironment());
    return RUN_ALL_TESTS();
}
