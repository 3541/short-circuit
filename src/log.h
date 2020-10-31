#pragma once

#include <stdio.h>

enum LogLevel { TRACE, DEBUG, INFO, WARN, ERROR };

void log_init(FILE*);
void log_fmt(enum LogLevel, const char*, ...);
void log_msg(enum LogLevel, const char*);
void log_error(int error, const char* msg);

#define ERR_FMT(fmt, ...)                                                      \
    do {                                                                       \
        log_fmt(ERROR, "%s (%d): " fmt "\n", __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define ERR(msg)                                                               \
    do {                                                                       \
        ERR_FMT("%s", (msg));                                                  \
    } while (0)
