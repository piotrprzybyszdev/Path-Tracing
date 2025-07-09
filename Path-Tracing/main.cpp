#include "Core/Core.h"

#include "Application.h"

using namespace PathTracing;

int main()
{
#ifndef NDEBUG
    logger::set_level(logger::level::debug);
#else
    logger::set_level(logger::level::info);
#endif

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
