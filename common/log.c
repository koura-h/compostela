#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include "sclog.h"

int
_sc_log(const char* file, int line, int lv, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    fprintf(stderr, "%s(%d): ", file, line);
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
    va_end(va);

    return 0;
}
