#include "logging.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>

// Global logging configuration
log_config_t g_log_config = {
    .verbose = 0,
    .quiet = 0,
    .structured = 0,
    .output_stream = NULL
};

void init_logging(log_config_t* config) {
    if (config) {
        memcpy(&g_log_config, config, sizeof(log_config_t));
    }
    if (!g_log_config.output_stream) {
        g_log_config.output_stream = stdout;
    }
}

const char* get_event_name(event_type_t event) {
    switch (event) {
        case EVENT_INIT: return "INIT";
        case EVENT_FILE_ADD: return "FILE_ADD";
        case EVENT_FILE_IGNORE: return "FILE_IGNORE";
        case EVENT_PROGRESS: return "PROGRESS";
        case EVENT_COMPRESSION: return "COMPRESSION";
        case EVENT_FINALIZE: return "FINALIZE";
        case EVENT_COMPLETE: return "COMPLETE";
        case EVENT_ERROR: return "ERROR";
        case EVENT_WARNING: return "WARNING";
        default: return "UNKNOWN";
    }
}

const char* get_level_name(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO: return "INFO";
        case LOG_PROGRESS: return "PROGRESS";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR: return "ERROR";
        case LOG_SUCCESS: return "SUCCESS";
        default: return "UNKNOWN";
    }
}

const char* format_timestamp(void) {
    static char timestamp[32];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    return timestamp;
}

void log_event(event_type_t event, log_level_t level, const char* format, ...) {
    if (g_log_config.quiet && level < LOG_WARNING) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    if (g_log_config.structured) {
        // JSON-like structured output for UI parsing
        fprintf(g_log_config.output_stream, "{\"timestamp\":\"%s\",\"event\":\"%s\",\"level\":\"%s\",\"message\":\"",
                format_timestamp(), get_event_name(event), get_level_name(level));
        vfprintf(g_log_config.output_stream, format, args);
        fprintf(g_log_config.output_stream, "\"}\n");
    } else {
        // Traditional human-readable output
        if (g_log_config.verbose) {
            fprintf(g_log_config.output_stream, "[%s] %s: ", get_level_name(level), get_event_name(event));
        }
        vfprintf(g_log_config.output_stream, format, args);
        if (!g_log_config.structured) {
            fprintf(g_log_config.output_stream, "\n");
        }
    }
    
    va_end(args);
    fflush(g_log_config.output_stream);
}

void log_progress_structured(const progress_t* progress, const char* phase, double percent, double speed, const char* speed_units) {
    if (g_log_config.quiet) {
        return;
    }
    
    time_t elapsed = time(NULL) - progress->start_time;
    
    if (g_log_config.structured) {
        fprintf(g_log_config.output_stream, 
                "{\"timestamp\":\"%s\",\"event\":\"PROGRESS\",\"level\":\"INFO\","
                "\"phase\":\"%s\",\"percent\":%.1f,\"files_processed\":%zu,\"total_files\":%zu,"
                "\"bytes_processed\":%zu,\"speed\":%.1f,\"speed_units\":\"%s\",\"elapsed\":%ld}\n",
                format_timestamp(), phase, percent, progress->processed_files, progress->total_files,
                progress->processed_bytes, speed, speed_units, elapsed);
    } else {
        // Traditional progress output
        if (strcmp(phase, "compression") == 0) {
            // Compression phase with animation
            const char* spinner = "|/-\\";
            static int step = 0;
            char animation = spinner[step % 4];
            step++;
            
            fprintf(g_log_config.output_stream, "\rCompressing and writing archive %c (%.1f%%) - %.1f %s - %lds elapsed", 
                   animation, percent, speed, speed_units, elapsed);
        } else {
            // File adding phase
            fprintf(g_log_config.output_stream, "\r%s: %zu/%zu files (%.1f%%) - %.1f %s", 
                   phase, progress->processed_files, progress->total_files, percent, speed, speed_units);
        }
    }
    
    fflush(g_log_config.output_stream);
}

void log_file_operation(const char* operation, const char* file_path, size_t file_size) {
    if (g_log_config.quiet && !g_log_config.verbose) {
        return;
    }
    
    if (g_log_config.structured) {
        fprintf(g_log_config.output_stream,
                "{\"timestamp\":\"%s\",\"event\":\"FILE_OPERATION\",\"level\":\"DEBUG\","
                "\"operation\":\"%s\",\"file_path\":\"%s\",\"file_size\":%zu}\n",
                format_timestamp(), operation, file_path, file_size);
    } else if (g_log_config.verbose) {
        if (file_size > 10 * 1024 * 1024) { // Show size for files > 10MB
            double size_mb = file_size / (1024.0 * 1024.0);
            fprintf(g_log_config.output_stream, "%s: %s (%.1f MB)\n", operation, file_path, size_mb);
        } else {
            fprintf(g_log_config.output_stream, "%s: %s\n", operation, file_path);
        }
    }
    
    fflush(g_log_config.output_stream);
}

void log_archive_info(const char* archive_path, size_t total_files, size_t total_bytes, double elapsed_time) {
    if (g_log_config.quiet) {
        return;
    }
    
    double speed = elapsed_time > 0 ? (double)total_bytes / elapsed_time : 0;
    const char* speed_units = "B/s";
    
    if (speed > 1024) {
        speed /= 1024;
        speed_units = "KB/s";
        if (speed > 1024) {
            speed /= 1024;
            speed_units = "MB/s";
        }
    }
    
    if (g_log_config.structured) {
        fprintf(g_log_config.output_stream,
                "{\"timestamp\":\"%s\",\"event\":\"COMPLETE\",\"level\":\"SUCCESS\","
                "\"archive_path\":\"%s\",\"total_files\":%zu,\"total_bytes\":%zu,"
                "\"elapsed_time\":%.1f,\"average_speed\":%.1f,\"speed_units\":\"%s\"}\n",
                format_timestamp(), archive_path, total_files, total_bytes,
                elapsed_time, speed, speed_units);
    } else {
        fprintf(g_log_config.output_stream, "ZIP archive created successfully\n");
        fprintf(g_log_config.output_stream, "Files processed: %zu\n", total_files);
        fprintf(g_log_config.output_stream, "Total size: %zu bytes\n", total_bytes);
        fprintf(g_log_config.output_stream, "Average speed: %.1f %s\n", speed, speed_units);
        fprintf(g_log_config.output_stream, "Total time: %.0f seconds\n", elapsed_time);
    }
    
    fflush(g_log_config.output_stream);
}

void log_error_structured(const char* context, const char* error_message) {
    if (g_log_config.structured) {
        fprintf(stderr,
                "{\"timestamp\":\"%s\",\"event\":\"ERROR\",\"level\":\"ERROR\","
                "\"context\":\"%s\",\"message\":\"%s\"}\n",
                format_timestamp(), context, error_message);
    } else {
        fprintf(stderr, "Error: %s - %s\n", context, error_message);
    }
    
    fflush(stderr);
}

void log_traditional(log_level_t level, const char* format, ...) {
    if (g_log_config.quiet && level < LOG_WARNING) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    FILE* stream = (level >= LOG_WARNING) ? stderr : g_log_config.output_stream;
    
    if (g_log_config.verbose && !g_log_config.structured) {
        fprintf(stream, "[%s] ", get_level_name(level));
    }
    
    vfprintf(stream, format, args);
    fprintf(stream, "\n");
    
    va_end(args);
    fflush(stream);
}