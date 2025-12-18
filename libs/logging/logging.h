#ifndef LOGGING_H
#define LOGGING_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_F_ARGS 32

#define STR_TIMESTAMP_SIZE    32
#define MAX_ADDED_FORMAT_SIZE 128
#define MAX_LOG_STR_SIZE      2048

typedef enum
{
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4,
} logging_level;

static const char *const logging_levels[5] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

#define log_printe(level, format, ...)                                       \
    {                                                                        \
        log_printf(level, "[File %s at line %d at %s] " format,              \
                   __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__); \
    }

typedef struct
{
    FILE*         file;
    logging_level level;
    bool          owns_file;
} logging_t;

/*
    Init logging module
    Parameters:
        filename - filename to write into
        level    - logging level
*/
void init_logging (const char * const filename, const logging_level level);

/*
    Print to log
    Parameters:
        level    - level of log output
        fmt, ... - string with formatted args
*/
void log_printf  (const logging_level level, const char* fmt, ...);

/*
    Close log file
*/
void close_log_file ();

/*
    Checks that work in debug and exits and in release not. Also logs errors
*/

#define CHECK(level, condition, format, ...)                                              \
    ( (condition) ? 1                                                                     \
        : (                                                                               \
            log_printf(level, "[File %s at line %d at %s] %s",                            \
                       __FILE__, __LINE__, __func__, (format), ##__VA_ARGS__), \
            0 ) )

#endif
