#ifndef LOG_H
#define LOG_H

void _log_info(const char* msg, const char* file, int line, ...);
#define log_info(msg, ...) \
{ \
    const char* fmt = msg; \
    _log_info(fmt, __FILE__, __LINE__, __VA_ARGS__); \
}

#endif
