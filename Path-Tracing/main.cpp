#include "Core/Core.h"

#include "Application.h"

using namespace PathTracing;

int main(int argc, const char *argv[])
{
    try
    {
        Application::Init(argc, argv);
        Application::Run();
    }
    catch (const PrintHelpException &exception)
    {
        return EXIT_SUCCESS;
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
