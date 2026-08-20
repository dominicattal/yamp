#include "log.h"
#include <cstdio>
#include <ctime>
#include <cstdarg>

static void get_timestamp(int* Y, int* M, int* D, int* h, int* m, int* s)
{
    std::time_t cur_time;
    struct tm local_time;
    cur_time = std::time(nullptr);
    local_time = *localtime(&cur_time);
    *Y = local_time.tm_year + 1900;
    *M = local_time.tm_mon + 1;
    *D = local_time.tm_mday;
    *h = local_time.tm_hour;
    *m = local_time.tm_min;
    *s = local_time.tm_sec;
}

void _log_info(const char* msg, const char* file, int line, ...)
{
    int Y, M, D, h, m, s;
    va_list args;
    get_timestamp(&Y, &M, &D, &h, &m, &s);
    fprintf(stderr, "\033[35;2;70;140;70m%02d:%02d:%02d \033[96m%s:%d\033[0m ", h, m, s, file, line);
    va_start(args, line);
    vfprintf(stderr, msg, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}
