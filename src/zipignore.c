#include "zipignore.h"
#include "utils.h"


// Forward declarations
static bool pattern_match_gitignore_recursive(const char* pattern, const char* text, int depth);
static bool pattern_match_gitignore(const char* pattern, const char* text);

// Helper function to load patterns from a specific .zipignore file into the context
static int load_patterns_from_file(zipignore_t* zi, const char* zipignore_path, const char* scope_dir) {
    if (!zi || !zipignore_path || !scope_dir) {
        return EXIT_FAILURE;
    }
    
    FILE* file = fopen(zipignore_path, "r");
    if (!file) {
        return EXIT_SUCCESS; // Not an error, file just doesn't exist
    }
    
    char line[MAX_PATTERN_LENGTH];
    while (fgets(line, sizeof(line), file) && zi->pattern_count < MAX_IGNORE_PATTERNS) {
        // Remove trailing newline and carriage return
        line[strcspn(line, "\r\n")] = 0;
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        // Trim trailing whitespace (gitignore behavior)
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t')) {
            // Check for escaped trailing space
            if (len > 1 && line[len-2] == '\\') {
                line[len-2] = line[len-1]; // Replace backslash with space
                line[len-1] = '\0';
                len--;
                break;
            }
            line[--len] = '\0';
        }
        
        // Skip if line is now empty
        if (len == 0) {
            continue;
        }
        
        char* trimmed = line;
        
        // Skip leading whitespace
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }
        
        if (strlen(trimmed) == 0) {
            continue;
        }
        
        ignore_pattern_t* pattern = &zi->patterns[zi->pattern_count];
        memset(pattern, 0, sizeof(ignore_pattern_t));
        
        // Set the scope directory for this pattern
        strncpy(pattern->scope_dir, scope_dir, PATH_MAX - 1);
        pattern->scope_dir[PATH_MAX - 1] = '\0';
        
        // Check for negation pattern (starts with !)
        if (trimmed[0] == '!') {
            pattern->is_negation = true;
            trimmed++; // Skip the '!' character
        }
        
        // Check if pattern is for directories only (ends with /)
        len = strlen(trimmed);
        if (len > 0 && trimmed[len - 1] == '/') {
            pattern->is_directory = true;
            trimmed[len - 1] = '\0'; // Remove trailing slash
            len--;
        }
        
        // Check if pattern is anchored (starts with / or contains /)
        // A pattern with a slash (except trailing) is anchored to the .zipignore location
        if (trimmed[0] == '/') {
            pattern->is_anchored = true;
            trimmed++; // Skip leading slash
            len--;
        } else {
            // Check if pattern contains a slash (other than the removed trailing one)
            pattern->is_anchored = (strchr(trimmed, '/') != NULL);
        }
        
        if (len == 0) {
            continue; // Skip empty patterns
        }
        
        strncpy(pattern->pattern, trimmed, MAX_PATTERN_LENGTH - 1);
        pattern->pattern[MAX_PATTERN_LENGTH - 1] = '\0';
        zi->pattern_count++;
    }
    
    fclose(file);
    
    // Track that we loaded this file
    if (zi->loaded_files_count < MAX_ZIPIGNORE_FILES) {
        strncpy(zi->loaded_files[zi->loaded_files_count], zipignore_path, PATH_MAX - 1);
        zi->loaded_files[zi->loaded_files_count][PATH_MAX - 1] = '\0';
        zi->loaded_files_count++;
    }
    
    return EXIT_SUCCESS;
}

bool is_zipignore_loaded(const zipignore_t* zi, const char* file_path) {
    if (!zi || !file_path) {
        return false;
    }
    
    for (int i = 0; i < zi->loaded_files_count; i++) {
        if (strcmp(zi->loaded_files[i], file_path) == 0) {
            return true;
        }
    }
    return false;
}

int load_zipignore(zipignore_t* zi, const char* base_dir, const char* zipignore_file) {
    if (!zi || !base_dir) {
        return EXIT_FAILURE;
    }
    
    // Initialize zipignore structure
    memset(zi, 0, sizeof(zipignore_t));
    strncpy(zi->base_dir, base_dir, PATH_MAX - 1);
    zi->base_dir[PATH_MAX - 1] = '\0';
    
    char zipignore_path[PATH_MAX];
    
    if (zipignore_file) {
        // If a specific zipignore file is provided, use only that
        if (file_exists(zipignore_file)) {
            load_patterns_from_file(zi, zipignore_file, base_dir);
        }
    } else {
        // Hierarchical loading: home -> local (later patterns can override earlier ones)
        
        // 1. First, load from user's home directory (global defaults)
        char* home_dir = get_home_directory();
        if (home_dir) {
            snprintf(zipignore_path, PATH_MAX, "%s%c%s", home_dir, PATH_SEPARATOR, ZIPIGNORE_FILENAME);
            if (file_exists(zipignore_path)) {
                // Home patterns apply globally (scope is base_dir)
                load_patterns_from_file(zi, zipignore_path, base_dir);
            }
            free(home_dir);
        }
        
        // 2. Then, load from local base directory (can add to or override home patterns)
        snprintf(zipignore_path, PATH_MAX, "%s%c%s", base_dir, PATH_SEPARATOR, ZIPIGNORE_FILENAME);
        if (file_exists(zipignore_path)) {
            load_patterns_from_file(zi, zipignore_path, base_dir);
        }
    }
    
    return EXIT_SUCCESS;
}

int load_nested_zipignore(zipignore_t* zi, const char* dir_path) {
    if (!zi || !dir_path) {
        return EXIT_FAILURE;
    }
    
    // Check if there's a .zipignore file in this directory
    char zipignore_path[PATH_MAX];
    snprintf(zipignore_path, PATH_MAX, "%s%c%s", dir_path, PATH_SEPARATOR, ZIPIGNORE_FILENAME);
    
    // Skip if already loaded or doesn't exist
    if (!file_exists(zipignore_path) || is_zipignore_loaded(zi, zipignore_path)) {
        return EXIT_SUCCESS;
    }
    
    // Load patterns from this nested .zipignore
    // The scope_dir is the directory containing the .zipignore file
    return load_patterns_from_file(zi, zipignore_path, dir_path);
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
    
    // Convert to forward slashes for consistent matching (gitignore uses /)
    char match_path[PATH_MAX];
    strncpy(match_path, normalized_path, PATH_MAX - 1);
    match_path[PATH_MAX - 1] = '\0';
    for (char* p = match_path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    
    // Get relative path from base directory
    char base_dir_normalized[PATH_MAX];
    strncpy(base_dir_normalized, zi->base_dir, PATH_MAX - 1);
    base_dir_normalized[PATH_MAX - 1] = '\0';
    for (char* p = base_dir_normalized; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    
    bool should_ignore_file = false;
    
    // Check all patterns (later patterns override earlier ones, like gitignore)
    for (int i = 0; i < zi->pattern_count && i < MAX_IGNORE_PATTERNS; i++) {
        const ignore_pattern_t* pattern = &zi->patterns[i];
        
        // Safety check for pattern length
        if (strlen(pattern->pattern) == 0 || strlen(pattern->pattern) > MAX_PATTERN_LENGTH - 1) {
            continue;
        }
        
        // Get scope directory normalized
        char scope_normalized[PATH_MAX];
        strncpy(scope_normalized, pattern->scope_dir, PATH_MAX - 1);
        scope_normalized[PATH_MAX - 1] = '\0';
        for (char* p = scope_normalized; *p; p++) {
            if (*p == '\\') *p = '/';
        }
        
        // Check if the file is within the scope of this pattern's .zipignore
        char pattern_relative_path[PATH_MAX];
        bool in_scope = false;
        
        size_t scope_len = strlen(scope_normalized);
        if (scope_len == 0) {
            // No scope_dir means pattern applies globally
            in_scope = true;
            strncpy(pattern_relative_path, match_path, PATH_MAX - 1);
            pattern_relative_path[PATH_MAX - 1] = '\0';
        } else if (strncmp(match_path, scope_normalized, scope_len) == 0) {
            // File is under this pattern's scope directory
            const char* scoped_start = match_path + scope_len;
            if (scoped_start[0] == '\0') {
                // Exact match (the directory itself)
                in_scope = true;
                pattern_relative_path[0] = '\0';
            } else if (scoped_start[0] == '/') {
                in_scope = true;
                strncpy(pattern_relative_path, scoped_start + 1, PATH_MAX - 1);
                pattern_relative_path[PATH_MAX - 1] = '\0';
            }
        }
        
        if (!in_scope) {
            continue; // This pattern doesn't apply to this file
        }
        
        // Skip empty relative path
        if (strlen(pattern_relative_path) == 0) {
            continue;
        }
        
        // Check if pattern matches using gitignore-style matching
        bool matches = false;
        
        if (pattern->is_anchored) {
            // Anchored patterns must match from the start of the relative path
            matches = pattern_match_gitignore(pattern->pattern, pattern_relative_path);
        } else {
            // Non-anchored patterns can match anywhere in the path
            // First try matching the full path
            matches = pattern_match_gitignore(pattern->pattern, pattern_relative_path);
            
            // If not matched and pattern doesn't contain /, try matching just the basename
            if (!matches && strchr(pattern->pattern, '/') == NULL) {
                // Get the basename
                const char* basename = strrchr(pattern_relative_path, '/');
                if (basename) {
                    basename++; // Skip the slash
                } else {
                    basename = pattern_relative_path;
                }
                matches = pattern_match_gitignore(pattern->pattern, basename);
            }
        }
        
        // For directory patterns, also check if the path is inside the matched directory
        if (!matches && pattern->is_directory) {
            // Try to match as a prefix (the path might be a file inside the directory)
            size_t pattern_len = strlen(pattern->pattern);
            if (strncmp(pattern_relative_path, pattern->pattern, pattern_len) == 0) {
                if (pattern_relative_path[pattern_len] == '/') {
                    matches = true;
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

// Gitignore-style pattern matching with support for *, **, and ?
static bool pattern_match_gitignore_recursive(const char* pattern, const char* text, int depth) {
    if (!pattern || !text) {
        return false;
    }
    
    // Prevent infinite recursion
    if (depth > MAX_RECURSION_DEPTH) {
        return false;
    }
    
    const char* p = pattern;
    const char* t = text;
    const char* p_star = NULL;  // Position after last * in pattern
    const char* t_star = NULL;  // Position in text when we hit last *
    
    while (*t) {
        // Handle **
        if (p[0] == '*' && p[1] == '*') {
            // ** matches everything including /
            p += 2;
            
            // Skip any following slashes in pattern (e.g., **/ )
            while (*p == '/') p++;
            
            // If pattern ends with **, match everything
            if (*p == '\0') return true;
            
            // Try to match rest of pattern at every position
            while (*t) {
                if (pattern_match_gitignore_recursive(p, t, depth + 1)) return true;
                t++;
            }
            return pattern_match_gitignore_recursive(p, t, depth + 1);
        }
        
        // Handle single *
        if (*p == '*') {
            p_star = ++p;  // Remember position after *
            t_star = t;    // Remember current text position
            continue;
        }
        
        // Handle ?
        if (*p == '?') {
            // ? matches any single character except /
            if (*t == '/') {
                if (p_star) {
                    p = p_star;
                    t = ++t_star;
                    continue;
                }
                return false;
            }
            p++;
            t++;
            continue;
        }
        
        // Handle character class [...]
        if (*p == '[') {
            const char* bracket_start = p;
            p++;
            bool negated = false;
            bool matched = false;
            
            if (*p == '!' || *p == '^') {
                negated = true;
                p++;
            }
            
            while (*p && *p != ']') {
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    // Range like [a-z]
                    if (*t >= p[0] && *t <= p[2]) {
                        matched = true;
                    }
                    p += 3;
                } else {
                    if (*p == *t) {
                        matched = true;
                    }
                    p++;
                }
            }
            
            if (*p == ']') {
                p++;
            } else {
                // Malformed pattern, treat [ as literal
                p = bracket_start;
                if (*p == *t) {
                    p++;
                    t++;
                    continue;
                }
                if (p_star) {
                    p = p_star;
                    t = ++t_star;
                    continue;
                }
                return false;
            }
            
            if (negated) matched = !matched;
            
            if (matched) {
                t++;
                continue;
            }
            
            if (p_star) {
                p = p_star;
                t = ++t_star;
                continue;
            }
            return false;
        }
        
        // Regular character match
        if (*p == *t) {
            p++;
            t++;
            continue;
        }
        
        // No match - try backtracking with *
        if (p_star) {
            // * cannot match across /
            if (*t_star == '/') {
                return false;
            }
            p = p_star;
            t = ++t_star;
            continue;
        }
        
        return false;
    }
    
    // Skip trailing * and **
    while (*p == '*') p++;
    
    return *p == '\0';
}

// Wrapper function for gitignore-style matching
static bool pattern_match_gitignore(const char* pattern, const char* text) {
    return pattern_match_gitignore_recursive(pattern, text, 0);
}

static bool pattern_match_recursive(const char* pattern, const char* text, int depth) {
    if (!pattern || !text || depth > MAX_RECURSION_DEPTH) {
        return false;
    }
    
#ifdef _WIN32
    // Use our gitignore-style matcher on Windows
    return pattern_match_gitignore(pattern, text);
#else
    // First try fnmatch for basic patterns
    if (fnmatch(pattern, text, FNM_PATHNAME) == 0) {
        return true;
    }
    // Fall back to gitignore-style matching for ** patterns
    if (strstr(pattern, "**") != NULL) {
        return pattern_match_gitignore(pattern, text);
    }
    return false;
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
    fprintf(file, "# Patterns in this file apply to the current directory and all subdirectories.\n");
    fprintf(file, "# You can place .zipignore files in subdirectories for directory-specific rules.\n");
    fprintf(file, "#\n");
    fprintf(file, "# Example patterns:\n");
    fprintf(file, "# *.tmp          - Ignore all .tmp files\n");
    fprintf(file, "# build/         - Ignore the build directory\n");
    fprintf(file, "# .git/          - Ignore the .git directory\n");
    fprintf(file, "# !important.tmp - Negation: don't ignore this file\n");
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