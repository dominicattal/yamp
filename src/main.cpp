#include "ui.h"
#include "mp.h"
#include <iostream>
#include <filesystem>
#include <spdlog/spdlog.h>

int main(int, char**)
{
    spdlog::set_pattern("[%^%l%$] [%s:%#] %v");
    ui_init();
    mp_init();
    ui_loop();
    mp_cleanup();
    ui_cleanup();
    return 0;
}
