#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct Config {
    std::string db_path;
};

extern Config config;

bool config_load(const char* path);

#endif
