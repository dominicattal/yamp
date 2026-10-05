#include "ui.h"
#include "mp.h"
#include <iostream>
#include <filesystem>
#include <spdlog/spdlog.h>

int main(int, char**)
{
    spdlog::set_pattern("[%^%l%$] [%s:%#] %v");
    mp_init();
    ui_init();
    ui_loop();
    ui_cleanup();
    mp_cleanup();
    return 0;
}
