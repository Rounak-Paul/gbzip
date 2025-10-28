#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <time.h>
#include "gbzip.h"

// Log levels for different types of output
typedef enum {
    LOG_DEBUG = 0,      // Detailed debug information
    LOG_INFO = 1,       // General information
    LOG_PROGRESS = 2,   // Progress updates
    LOG_WARNING = 3,    // Warnings
    LOG_ERROR = 4,      // Errors
    LOG_SUCCESS = 5     // Success messages
} log_level_t;

// Event types for structured logging
typedef enum {
    EVENT_INIT = 0,         // Archive initialization
    EVENT_FILE_ADD,         // Individual file added
    EVENT_FILE_IGNORE,      // File ignored
    EVENT_PROGRESS,         // Progress update
    EVENT_COMPRESSION,      // Compression phase
    EVENT_FINALIZE,         // Finalization phase
    EVENT_COMPLETE,         // Operation complete
    EVENT_ERROR,            // Error occurred
    EVENT_WARNING           // Warning issued
} event_type_t;

// Logging configuration
typedef struct {
    int verbose;            // Show verbose output
    int quiet;              // Suppress non-essential output
    int structured;         // Use structured JSON-like output for UI parsing
    FILE* output_stream;    // Where to write logs (stdout/stderr)
} log_config_t;

// Initialize logging system
void init_logging(log_config_t* config);

// Structured logging functions for UI parsing
void log_event(event_type_t event, log_level_t level, const char* format, ...);
void log_progress_structured(const progress_t* progress, const char* phase, double percent, double speed, const char* speed_units);
void log_file_operation(const char* operation, const char* file_path, size_t file_size);
void log_archive_info(const char* archive_path, size_t total_files, size_t total_bytes, double elapsed_time);
void log_error_structured(const char* context, const char* error_message);

// Traditional logging functions (for backward compatibility)
void log_traditional(log_level_t level, const char* format, ...);

// Helper functions
const char* get_event_name(event_type_t event);
const char* get_level_name(log_level_t level);
const char* format_timestamp(void);

// Global logging configuration
extern log_config_t g_log_config;

#endif // LOGGING_H