#pragma once

#include <stdio.h>

typedef enum LogLevel { TRACE, DEBUG, INFO, WARN, ERROR } LogLevel;

void log_init(FILE*);
void log_fmt(LogLevel, const char*, ...);
void log_msg(LogLevel, const char*);
void log_error(int error, const char* msg);

#define ERR_FMT(fmt, ...)                                                      \
    do {                                                                       \
        log_fmt(ERROR, "%s (%d): " fmt "\n", __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define ERR(msg)                                                               \
    do {                                                                       \
        ERR_FMT("%s", (msg));                                                  \
    } while (0)
