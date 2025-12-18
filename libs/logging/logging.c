#include "logging.h"

static logging_t logging = {
    .file      = NULL,
    .level     = DEBUG,
    .owns_file = false,
};

void init_logging (const char * const filename, const logging_level level)
{
    FILE* file = fopen(filename, "a");
    if (file)
    {
        logging.file      = file;
        logging.owns_file = true;
    } else
    {
        logging.file      = stderr;
        logging.owns_file = false;
    }

    logging.level = level;
}

static void format_log (const logging_level level, const char * const str, char* res_str);

void log_printf (const logging_level level, const char* fmt, ...)
{
    if (!logging.file || level < logging.level) return;

    char str[MAX_LOG_STR_SIZE]                             = {  };
    char res_str[MAX_LOG_STR_SIZE + MAX_ADDED_FORMAT_SIZE] = {  };
    
    va_list args = {  };
    
    va_start(args, fmt);
    vsnprintf(str, MAX_LOG_STR_SIZE, fmt, args);
    va_end(args);

    format_log(level, str, res_str);
    fprintf(logging.file, "%s", res_str);
    
    fflush(logging.file);
}

static void get_timestamp (char * const timestamp)
{
    time_t    current_time;
    struct tm *local_time_info;
    
    current_time    = time(NULL);
    local_time_info = localtime(&current_time);

    strftime(timestamp, STR_TIMESTAMP_SIZE, "%d-%m-%Y %H:%M:%S", local_time_info);
}

static void format_log (const logging_level level, const char * const str, char* res_str)
{
    char timestamp[STR_TIMESTAMP_SIZE];
    get_timestamp(timestamp);
    sprintf(res_str, "[%s %s] %s\n", timestamp, logging_levels[level], str);
}

void close_log_file ()
{
    if (logging.file && logging.owns_file)
    {
        fclose(logging.file);
    }
    logging.file      = NULL;
    logging.owns_file = false;
}
