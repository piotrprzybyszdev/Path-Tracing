#include "Application.h"
#include "TestEnvironment.h"
#include "TestRenderer.h"

#include "Core/Core.h"

namespace PathTracingTests
{

void TestEnvironment::SetUp()
{
    const vk::DeviceSize bufferSize = PathTracing::FromKiB(8);

    PathTracing::Application::Init(0, nullptr);
    TestRenderer::Init();
    TestRenderer::AllocateResources(bufferSize, bufferSize);
}

void TestEnvironment::TearDown()
{
    TestRenderer::Shutdown();
    PathTracing::Application::Shutdown();
}

}