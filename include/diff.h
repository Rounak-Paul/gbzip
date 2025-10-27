#ifndef DIFF_H
#define DIFF_H

#include "gbzip.h"
#include "gbzip_zip.h"

// File change types
typedef enum {
    CHANGE_NONE,
    CHANGE_ADDED,
    CHANGE_MODIFIED,
    CHANGE_DELETED
} change_type_t;

// File change information
typedef struct {
    char path[PATH_MAX];
    change_type_t change_type;
    time_t old_mtime;
    time_t new_mtime;
    off_t old_size;
    off_t new_size;
} file_change_t;

// Diff context
typedef struct {
    file_change_t* changes;
    size_t change_count;
    size_t change_capacity;
    char* base_dir;
    char* zip_file;
} diff_context_t;

// Function prototypes
int diff_zip(const options_t* opts);
int compare_with_existing_zip(const char* zip_file, const char* directory, diff_context_t* diff_ctx);
int apply_changes_to_zip(const char* zip_file, const diff_context_t* diff_ctx, bool verbose);

// Internal helper functions
int add_change(diff_context_t* diff_ctx, const char* path, change_type_t type, 
               time_t old_mtime, time_t new_mtime, off_t old_size, off_t new_size);
void free_diff_context(diff_context_t* diff_ctx);
void print_diff_summary(const diff_context_t* diff_ctx);

#endif // DIFF_H