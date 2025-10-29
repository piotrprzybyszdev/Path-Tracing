#include "Application.h"
#include "TestEnvironment.h"
#include "TestRenderer.h"

namespace PathTracingTests
{

void TestEnvironment::SetUp()
{
    PathTracing::Application::Init(0, nullptr);
    TestRenderer::Init();
    TestRenderer::AllocateResources(128, 128);
}

void TestEnvironment::TearDown()
{
    TestRenderer::Shutdown();
    PathTracing::Application::Shutdown();
}

}