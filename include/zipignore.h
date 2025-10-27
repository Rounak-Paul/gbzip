#ifndef ZIPIGNORE_H
#define ZIPIGNORE_H

#include "gbzip.h"

// Maximum number of ignore patterns
#define MAX_IGNORE_PATTERNS 1000
#define MAX_PATTERN_LENGTH 256
#define MAX_RECURSION_DEPTH 100

// Ignore pattern structure
typedef struct {
    char pattern[MAX_PATTERN_LENGTH];
    bool is_directory;
    bool is_negation;
} ignore_pattern_t;

// Zipignore context
typedef struct {
    ignore_pattern_t patterns[MAX_IGNORE_PATTERNS];
    int pattern_count;
    char base_dir[PATH_MAX];
} zipignore_t;

// Function prototypes
int load_zipignore(zipignore_t* zi, const char* base_dir, const char* zipignore_file);
bool should_ignore(const zipignore_t* zi, const char* path);
int create_default_zipignore(void);
void free_zipignore(zipignore_t* zi);

// Pattern matching functions
bool pattern_match(const char* pattern, const char* text);
char* normalize_path(const char* path);

#endif // ZIPIGNORE_H