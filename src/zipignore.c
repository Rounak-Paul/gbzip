#include "zipignore.h"
#include "utils.h"



int load_zipignore(zipignore_t* zi, const char* base_dir, const char* zipignore_file) {
    if (!zi || !base_dir) {
        return EXIT_FAILURE;
    }
    
    // Initialize zipignore structure
    memset(zi, 0, sizeof(zipignore_t));
    strncpy(zi->base_dir, base_dir, PATH_MAX - 1);
    zi->base_dir[PATH_MAX - 1] = '\0';
    
    // Determine zipignore file path
    char zipignore_path[PATH_MAX];
    if (zipignore_file) {
        strncpy(zipignore_path, zipignore_file, PATH_MAX - 1);
    } else {
        // Look for .zipignore in current directory first
        snprintf(zipignore_path, PATH_MAX, "%s%c%s", base_dir, PATH_SEPARATOR, ZIPIGNORE_FILENAME);
        if (!file_exists(zipignore_path)) {
            // Try user's home directory
            char* home_dir = get_home_directory();
            if (home_dir) {
                snprintf(zipignore_path, PATH_MAX, "%s%c%s", home_dir, PATH_SEPARATOR, ZIPIGNORE_FILENAME);
                free(home_dir);
                if (!file_exists(zipignore_path)) {
                    // No zipignore file found, proceed with empty ignore list
                    return EXIT_SUCCESS;
                }
            } else {
                // No home directory available, proceed with empty ignore list
                return EXIT_SUCCESS;
            }
        }
    }
    
    FILE* file = fopen(zipignore_path, "r");
    if (!file) {
        // Cannot open zipignore file, proceed with empty ignore list
        return EXIT_SUCCESS;
    }
    
    char line[MAX_PATTERN_LENGTH];
    while (fgets(line, sizeof(line), file) && zi->pattern_count < MAX_IGNORE_PATTERNS) {
        // Remove trailing newline
        line[strcspn(line, "\r\n")] = 0;
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        // Trim whitespace
        char* trimmed = trim_whitespace(line);
        if (strlen(trimmed) == 0) {
            continue;
        }
        
        ignore_pattern_t* pattern = &zi->patterns[zi->pattern_count];
        
        // Check for negation pattern
        if (trimmed[0] == '!') {
            pattern->is_negation = true;
            trimmed++; // Skip the '!' character
        } else {
            pattern->is_negation = false;
        }
        
        // Check if pattern is for directories
        size_t len = strlen(trimmed);
        if (len > 0 && trimmed[len - 1] == '/') {
            pattern->is_directory = true;
            trimmed[len - 1] = '\0'; // Remove trailing slash
        } else {
            pattern->is_directory = false;
        }
        
        strncpy(pattern->pattern, trimmed, MAX_PATTERN_LENGTH - 1);
        pattern->pattern[MAX_PATTERN_LENGTH - 1] = '\0';
        zi->pattern_count++;
    }
    
    fclose(file);
    return EXIT_SUCCESS;
}

bool should_ignore(const zipignore_t* zi, const char* path) {
    if (!zi || !path) {
        return false;
    }
    
    // Safety check for path length
    if (strlen(path) > PATH_MAX - 1) {
        return false;
    }
    
    char* normalized_path = normalize_path(path);
    if (!normalized_path) {
        return false;
    }
    
    // Remove base directory from path for pattern matching
    char relative_path[PATH_MAX];
    if (strncmp(normalized_path, zi->base_dir, strlen(zi->base_dir)) == 0) {
        const char* rel_start = normalized_path + strlen(zi->base_dir);
        if (rel_start[0] == PATH_SEPARATOR) {
            rel_start++;
        }
        strncpy(relative_path, rel_start, PATH_MAX - 1);
    } else {
        strncpy(relative_path, normalized_path, PATH_MAX - 1);
    }
    relative_path[PATH_MAX - 1] = '\0';
    
    bool should_ignore_file = false;
    
    // Check all patterns with safety limits
    for (int i = 0; i < zi->pattern_count && i < MAX_IGNORE_PATTERNS; i++) {
        const ignore_pattern_t* pattern = &zi->patterns[i];
        
        // Safety check for pattern length
        if (strlen(pattern->pattern) == 0 || strlen(pattern->pattern) > MAX_PATTERN_LENGTH - 1) {
            continue;
        }
        
        // Check if pattern matches
        bool matches = false;
        
        if (pattern->is_directory) {
            // For directory patterns, check if path starts with pattern
            size_t pattern_len = strlen(pattern->pattern);
            if (strncmp(relative_path, pattern->pattern, pattern_len) == 0) {
                // Exact match or path continues with separator
                if (relative_path[pattern_len] == '\0' || 
                    relative_path[pattern_len] == PATH_SEPARATOR) {
                    matches = true;
                }
            }

        } else {
            // For file patterns, use glob matching
            matches = pattern_match(pattern->pattern, relative_path);
            
            // Also check just the filename
            if (!matches) {
                char* filename = strrchr(relative_path, PATH_SEPARATOR);
                if (filename) {
                    filename++; // Skip the separator
                    matches = pattern_match(pattern->pattern, filename);
                } else {
                    matches = pattern_match(pattern->pattern, relative_path);
                }
            }
        }
        
        if (matches) {
            if (pattern->is_negation) {
                should_ignore_file = false; // Negation overrides previous ignores
            } else {
                should_ignore_file = true;
            }
        }
    }
    
    free(normalized_path);
    return should_ignore_file;
}

static bool pattern_match_recursive(const char* pattern, const char* text, int depth) {
    if (!pattern || !text || depth > MAX_RECURSION_DEPTH) {
        return false;
    }
    
#ifdef _WIN32
    // Windows doesn't have fnmatch, so we implement basic wildcard matching
    const char* p = pattern;
    const char* t = text;
    
    while (*p && *t) {
        if (*p == '*') {
            // Skip consecutive asterisks
            while (*p == '*') p++;
            
            // If pattern ends with *, match everything
            if (*p == '\0') return true;
            
            // Try to match the rest of the pattern
            while (*t) {
                if (pattern_match_recursive(p, t, depth + 1)) return true;
                t++;
            }
            return false;
        } else if (*p == '?' || *p == *t) {
            p++;
            t++;
        } else {
            return false;
        }
    }
    
    // Skip any trailing asterisks in pattern
    while (*p == '*') p++;
    
    return (*p == '\0' && *t == '\0');
#else
    return fnmatch(pattern, text, FNM_PATHNAME) == 0;
#endif
}

bool pattern_match(const char* pattern, const char* text) {
    return pattern_match_recursive(pattern, text, 0);
}

char* normalize_path(const char* path) {
    if (!path) {
        return NULL;
    }
    
    char* normalized = malloc(PATH_MAX);
    if (!normalized) {
        return NULL;
    }
    
    strncpy(normalized, path, PATH_MAX - 1);
    normalized[PATH_MAX - 1] = '\0';
    
    // Convert all separators to the platform-specific separator
    for (char* p = normalized; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = PATH_SEPARATOR;
        }
    }
    
    // Remove trailing separator (except for root)
    size_t len = strlen(normalized);
    if (len > 1 && normalized[len - 1] == PATH_SEPARATOR) {
        normalized[len - 1] = '\0';
    }
    
    return normalized;
}

int create_default_zipignore(void) {
    char zipignore_path[PATH_MAX];
    snprintf(zipignore_path, PATH_MAX, "%s", ZIPIGNORE_FILENAME);
    
    if (file_exists(zipignore_path)) {
        printf("Warning: %s already exists. Use -f to overwrite.\n", zipignore_path);
        return EXIT_SUCCESS;
    }
    
    FILE* file = fopen(zipignore_path, "w");
    if (!file) {
        fprintf(stderr, "Error: Cannot create %s\n", zipignore_path);
        return EXIT_FILE_ERROR;
    }
    
    // Create an empty zipignore file with just a comment
    fprintf(file, "# Add patterns to ignore files/directories in ZIP archives\n");
    fprintf(file, "# Example patterns:\n");
    fprintf(file, "# *.tmp\n");
    fprintf(file, "# build/\n");
    fprintf(file, "# .git/\n");
    fprintf(file, "\n");
    
    fclose(file);
    printf("Created empty %s file\n", zipignore_path);
    return EXIT_SUCCESS;
}

void free_zipignore(zipignore_t* zi) {
    if (zi) {
        memset(zi, 0, sizeof(zipignore_t));
    }
}