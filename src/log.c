#include "log.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

static FILE* log_out = NULL;

void log_init(FILE* out) { log_out = out; }

void log_fmt(LogLevel level, const char* fmt, ...) {
    if (level < LOG_LEVEL)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(log_out, fmt, args);
    fputc('\n', log_out);
    va_end(args);
}

void log_msg(LogLevel level, const char* msg) { log_fmt(level, "%s", msg); }

void log_error(int error, const char* msg) {
    log_fmt(ERROR, "Error: %s (%s).", strerror(error), msg);
}
