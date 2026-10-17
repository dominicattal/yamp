#include "ui.h"
#include "mp.h"
#include "config.h"
#include <iostream>
#include <filesystem>
#include <spdlog/spdlog.h>

int main(int argc, char** argv)
{
    spdlog::set_pattern("[%^%l%$] [%s:%#] %v");
    if (argc == 1)
    {
        SPDLOG_ERROR("Missing config file path");
        return 1;
    }
    if (!config_load(argv[1]))
    {
        SPDLOG_ERROR("Could not load config file {}", argv[1]);
        return 1;
    }
    ui_init();
    mp_init();
    ui_loop();
    mp_cleanup();
    ui_cleanup();
    return 0;
}
