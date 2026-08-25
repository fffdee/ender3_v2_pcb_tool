/** @file banux_debug.c @brief Injectable Banux core logging implementation. */
#include <stdarg.h>
#include <stdio.h>
#include "debug.h"

static BanuxDebugWriter_t s_writer;

void BanuxDebug_SetWriter(BanuxDebugWriter_t writer)
{
    s_writer = writer;
}

void BanuxDebug_Printf(const char *format, ...)
{
    va_list args;
    char buffer[128];

    if (!s_writer || !format) return;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    s_writer(buffer);
}
