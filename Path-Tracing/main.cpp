#include <cassert>
#include <iostream>

#include "Core/Core.h"
#include "Core/Input.h"
#include "Renderer/Renderer.h"

#include "Window.h"

using namespace PathTracing;

int main()
{
    logger::set_level(logger::level::info);

    try
    {
        Window window(1280, 720, "Path Tracing", true);
        Input::SetWindow(window.GetHandle());

        Camera camera(45.0f, 100.0f, 0.1f);

        Renderer renderer(window, camera);

        float lastFrameTime = 0.0f;
        while (!window.ShouldClose())
        {
            Timer timer("Frame total");

            float time = glfwGetTime();

            float timeStep = time - lastFrameTime;
            lastFrameTime = time;

            {
                Timer timer("Update");
                window.OnUpdate(timeStep);
                camera.OnUpdate(timeStep);
                renderer.OnUpdate(timeStep);
            }

            {
                Timer timer("Render");
                window.OnRender();
                renderer.OnRender();
            }
        }
    }
    catch (const error &error)
    {
        return EXIT_FAILURE;
    }
    catch (const std::exception &exception)
    {
        logger::critical(exception.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
