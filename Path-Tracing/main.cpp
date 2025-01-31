#include <cassert>
#include <iostream>

#include "Core/Core.h"

using namespace PathTracing;

namespace
{
bool check()
{
    return false;
}
}

using TestAssert = Assert<bool, check, true>;

int main()
{
    try
    {
        for (int i = 0; i < 1000; i++)
        {
            Timer timer("test");

            std::cout << "Hello World!" << std::endl;
        }

        Stats::FlushTimers();

        for (const auto &[name, value] : Stats::GetStats())
        {
            std::cout << value << std::endl;
        }

        TestAssert();
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