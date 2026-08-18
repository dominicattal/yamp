#include "ui.h"
#include "mp.h"
#include <iostream>
#include <filesystem>

int main(int, char**)
{
    mp_init();
    ui_init();
    ui_loop();
    ui_cleanup();
    mp_cleanup();
    return 0;
}
