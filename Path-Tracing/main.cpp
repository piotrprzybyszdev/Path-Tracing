#include <cassert>
#include <iostream>

#include "Core/Core.h"

#include "Application.h"

using namespace PathTracing;

int main()
{
    logger::set_level(logger::level::info);

    try
    {
        Application::Init();
        Application::Run();
    }
    catch (const error &error)
    {
        Application::Shutdown();
        return EXIT_FAILURE;
    }
    catch (const std::exception &exception)
    {
        logger::critical(exception.what());
        Application::Shutdown();
        return EXIT_FAILURE;
    }

    Application::Shutdown();
    return EXIT_SUCCESS;
}
