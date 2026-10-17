#include "config.h"
#include <ini.h>
#include <spdlog/spdlog.h>

Config config;

static int handler(void* user, const char* section, const char* name, const char* value)
{
    (void)user;

    auto match = [section, name](const char* s, const char* n) -> bool
    {
        return strcmp(section, s) == 0 && strcmp(name, n) == 0;
    };

    if (match("yamp", "db_path"))
    {
        config.db_path = value;
    }

    return 1;
}

bool config_load(const char* path)
{
    bool parsed = ini_parse(path, handler, nullptr) == 0;

    if (!parsed)
    {
        SPDLOG_ERROR("Could not load config file {}", path);
        return false;
    }

    if (config.db_path.size() == 0)
    {
        SPDLOG_ERROR("Missing db_path in config file {}", path);
        return false;
    }

    return true;
}
