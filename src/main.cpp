#include "ui.h"
#include "mp.h"
#include <iostream>
#include <filesystem>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

int main(int, char**)
{
    spdlog::set_pattern("[%^%l%$] [%s:%#] %v");
    SPDLOG_INFO("test");
    mp_init();
    ui_init();
    ui_loop();
    ui_cleanup();
    mp_cleanup();
    return 0;
}
