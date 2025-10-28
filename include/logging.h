#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <time.h>
#include "gbzip.h"

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_PROGRESS,
    LOG_WARNING,
    LOG_ERROR,
    LOG_SUCCESS
} log_level_t;

typedef enum {
    EVENT_INIT,
    EVENT_FILE_ADD,
    EVENT_FILE_IGNORE,
    EVENT_PROGRESS,
    EVENT_COMPRESSION,
    EVENT_FINALIZE,
    EVENT_COMPLETE,
    EVENT_ERROR,
    EVENT_WARNING
} event_type_t;

// Logging configuration
typedef struct {
    int verbose;
    int quiet;
    int structured;
    FILE* output_stream;
} log_config_t;

void init_logging(log_config_t* config);
void log_event(event_type_t event, log_level_t level, const char* format, ...);
void log_progress(const progress_t* progress, const char* phase, double percent, double speed, const char* speed_units);
void log_file_operation(const char* operation, const char* file_path, size_t file_size);
void log_archive_info(const char* archive_path, size_t total_files, size_t total_bytes, double elapsed_time);
void log_error(const char* context, const char* error_message);

const char* get_event_name(event_type_t event);
const char* get_level_name(log_level_t level);
const char* format_timestamp(void);

extern log_config_t g_log_config;

#endif // LOGGING_H