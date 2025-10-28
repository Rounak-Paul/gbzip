#ifndef UTILS_H
#define UTILS_H

#include "gbzip.h"

// File system utilities
bool file_exists(const char* path);
bool is_directory(const char* path);
bool is_regular_file(const char* path);
int create_directory_recursive(const char* path);
char* get_home_directory(void);
char* get_absolute_path(const char* path);
time_t get_file_mtime(const char* path);
off_t get_file_size(const char* path);

// String utilities
char* trim_whitespace(char* str);
char* join_path(const char* dir, const char* file);
const char* get_filename(const char* path);
const char* get_file_extension(const char* path);

// Security utilities
bool is_safe_path(const char* path);
bool is_suspicious_file(const char* filename);

// Directory traversal
typedef struct {
    char path[PATH_MAX];
    bool is_directory;
    time_t mtime;
    off_t size;
} file_info_t;

typedef int (*file_callback_t)(const file_info_t* info, void* user_data);
int traverse_directory(const char* dir_path, bool recursive, file_callback_t callback, void* user_data);

// Progress reporting functions

void init_progress(progress_t* progress);
void update_progress(progress_t* progress, size_t bytes_processed);
void set_progress_phase(progress_t* progress, progress_phase_t phase, double weight);
void print_progress(const progress_t* progress, const char* operation);
void print_finalization_progress(const progress_t* progress, const char* message);
void print_compression_progress(const progress_t* progress, int step);

#endif // UTILS_H