#ifndef ZIPIGNORE_H
#define ZIPIGNORE_H

#include "gbzip.h"

// Maximum number of ignore patterns
#define MAX_IGNORE_PATTERNS 1000
#define MAX_PATTERN_LENGTH 256
#define MAX_RECURSION_DEPTH 100
#define MAX_ZIPIGNORE_FILES 100  // Maximum number of nested .zipignore files

// Ignore pattern structure
typedef struct {
    char pattern[MAX_PATTERN_LENGTH];
    char scope_dir[PATH_MAX];  // Directory where this pattern applies (from which .zipignore)
    bool is_directory;         // Pattern ends with / (directory only)
    bool is_negation;          // Pattern starts with ! (negate previous match)
    bool is_anchored;          // Pattern starts with / or contains / (anchored to scope_dir)
} ignore_pattern_t;

// Zipignore context
typedef struct {
    ignore_pattern_t patterns[MAX_IGNORE_PATTERNS];
    int pattern_count;
    char base_dir[PATH_MAX];
    char loaded_files[MAX_ZIPIGNORE_FILES][PATH_MAX];  // Track loaded .zipignore files
    int loaded_files_count;
} zipignore_t;

// Function prototypes
int load_zipignore(zipignore_t* zi, const char* base_dir, const char* zipignore_file);
int load_nested_zipignore(zipignore_t* zi, const char* dir_path);
bool should_ignore(const zipignore_t* zi, const char* path);
int create_default_zipignore(void);
void free_zipignore(zipignore_t* zi);

// Helper to check if a .zipignore file was already loaded
bool is_zipignore_loaded(const zipignore_t* zi, const char* file_path);

// Pattern matching functions
bool pattern_match(const char* pattern, const char* text);
char* normalize_path(const char* path);

#endif // ZIPIGNORE_H