#include "App.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <stdexcept>

int main()
{
    spdlog::set_level(spdlog::level::trace);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%-5l%$] %v");

    try
    {
        App app;
        app.run();
    }
    catch (const std::exception& e)
    {
        spdlog::critical("Fatal: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
