#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "../include/gbzip.h"
#include "../include/utils.h"
#include "../include/zipignore.h"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("  ✓ %s\n", message); \
    } else { \
        printf("  ✗ %s (FAILED)\n", message); \
    } \
} while(0)

// Helper to allocate and initialize a zipignore_t on the heap
static zipignore_t* create_test_zipignore(const char* base_dir) {
    zipignore_t* zi = malloc(sizeof(zipignore_t));
    if (!zi) return NULL;
    memset(zi, 0, sizeof(zipignore_t));
    if (base_dir) {
        strncpy(zi->base_dir, base_dir, PATH_MAX - 1);
    }
    return zi;
}

// Helper to create directories recursively
static int mkdir_p(const char* path) {
    char tmp[PATH_MAX];
    char* p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

// Helper to create a file with content
static int create_test_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    if (content) fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

// Helper to remove directory tree
static void remove_tree(const char* path) {
    char cmd[PATH_MAX + 10];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    system(cmd);
}

// Helper to add a pattern directly for testing
static void add_test_pattern(zipignore_t* zi, const char* scope_dir, const char* pattern_str,
                             bool is_directory, bool is_negation, bool is_anchored) {
    if (zi->pattern_count >= MAX_IGNORE_PATTERNS) return;
    
    ignore_pattern_t* p = &zi->patterns[zi->pattern_count];
    memset(p, 0, sizeof(ignore_pattern_t));
    strncpy(p->scope_dir, scope_dir, PATH_MAX - 1);
    strncpy(p->pattern, pattern_str, MAX_PATTERN_LENGTH - 1);
    p->is_directory = is_directory;
    p->is_negation = is_negation;
    p->is_anchored = is_anchored;
    zi->pattern_count++;
}

int test_file_utils(void) {
    printf("\n=== Testing file utilities ===\n");
    
    TEST_ASSERT(file_exists(".") == true, "Current directory exists");
    TEST_ASSERT(file_exists("nonexistent_file_xyz123") == false, "Nonexistent file doesn't exist");
    TEST_ASSERT(is_directory(".") == true, "Current directory is a directory");
    TEST_ASSERT(is_directory("../README.md") == false, "README.md is not a directory");
    
    return EXIT_SUCCESS;
}

int test_pattern_matching_basic(void) {
    printf("\n=== Testing basic pattern matching ===\n");
    
    // Basic wildcards
    TEST_ASSERT(pattern_match("*.txt", "file.txt") == true, "*.txt matches file.txt");
    TEST_ASSERT(pattern_match("*.txt", "file.log") == false, "*.txt doesn't match file.log");
    TEST_ASSERT(pattern_match("*.txt", "nested/file.txt") == false, "*.txt doesn't match nested/file.txt (no path crossing)");
    
    // Question mark
    TEST_ASSERT(pattern_match("file?.txt", "file1.txt") == true, "file?.txt matches file1.txt");
    TEST_ASSERT(pattern_match("file?.txt", "file12.txt") == false, "file?.txt doesn't match file12.txt");
    TEST_ASSERT(pattern_match("???", "abc") == true, "??? matches abc");
    TEST_ASSERT(pattern_match("???", "ab") == false, "??? doesn't match ab");
    
    // Exact match
    TEST_ASSERT(pattern_match("file.txt", "file.txt") == true, "Exact match works");
    TEST_ASSERT(pattern_match("file.txt", "other.txt") == false, "Exact match fails for different file");
    
    // Multiple wildcards
    TEST_ASSERT(pattern_match("*.min.*", "jquery.min.js") == true, "*.min.* matches jquery.min.js");
    TEST_ASSERT(pattern_match("test*spec*", "test_my_spec_file") == true, "Multiple * in pattern");
    
    return EXIT_SUCCESS;
}

int test_pattern_matching_doublestar(void) {
    printf("\n=== Testing ** pattern matching ===\n");
    
    // ** matches any path
    TEST_ASSERT(pattern_match("**/*.txt", "file.txt") == true, "**/*.txt matches file.txt");
    TEST_ASSERT(pattern_match("**/*.txt", "a/file.txt") == true, "**/*.txt matches a/file.txt");
    TEST_ASSERT(pattern_match("**/*.txt", "a/b/c/file.txt") == true, "**/*.txt matches a/b/c/file.txt");
    
    // Leading **
    TEST_ASSERT(pattern_match("**/test", "test") == true, "**/test matches test");
    TEST_ASSERT(pattern_match("**/test", "a/test") == true, "**/test matches a/test");
    TEST_ASSERT(pattern_match("**/test", "a/b/test") == true, "**/test matches a/b/test");
    
    // Trailing **
    TEST_ASSERT(pattern_match("build/**", "build/output") == true, "build/** matches build/output");
    TEST_ASSERT(pattern_match("build/**", "build/a/b/c") == true, "build/** matches build/a/b/c");
    
    // ** in middle
    TEST_ASSERT(pattern_match("a/**/z", "a/z") == true, "a/**/z matches a/z");
    TEST_ASSERT(pattern_match("a/**/z", "a/b/z") == true, "a/**/z matches a/b/z");
    TEST_ASSERT(pattern_match("a/**/z", "a/b/c/d/z") == true, "a/**/z matches a/b/c/d/z");
    
    return EXIT_SUCCESS;
}

int test_pattern_matching_character_class(void) {
    printf("\n=== Testing character class pattern matching ===\n");
    
    TEST_ASSERT(pattern_match("[abc]", "a") == true, "[abc] matches a");
    TEST_ASSERT(pattern_match("[abc]", "b") == true, "[abc] matches b");
    TEST_ASSERT(pattern_match("[abc]", "d") == false, "[abc] doesn't match d");
    
    TEST_ASSERT(pattern_match("[a-z]", "m") == true, "[a-z] matches m");
    TEST_ASSERT(pattern_match("[a-z]", "A") == false, "[a-z] doesn't match A");
    TEST_ASSERT(pattern_match("[0-9]", "5") == true, "[0-9] matches 5");
    
    TEST_ASSERT(pattern_match("file[0-9].txt", "file5.txt") == true, "file[0-9].txt matches file5.txt");
    TEST_ASSERT(pattern_match("file[0-9].txt", "fileX.txt") == false, "file[0-9].txt doesn't match fileX.txt");
    
    return EXIT_SUCCESS;
}

int test_zipignore_empty(void) {
    printf("\n=== Testing empty zipignore ===\n");
    
    zipignore_t* zi = create_test_zipignore("/test");
    if (!zi) return EXIT_FAILURE;
    
    // With no patterns, nothing should be ignored
    TEST_ASSERT(should_ignore(zi, "/test/file.txt") == false, "Empty zipignore ignores nothing");
    TEST_ASSERT(should_ignore(zi, "/test/a/b/c/file.log") == false, "Empty zipignore ignores nothing (nested)");
    TEST_ASSERT(should_ignore(zi, "/test/.git/config") == false, "Empty zipignore ignores nothing (.git)");
    
    free(zi);
    return EXIT_SUCCESS;
}

int test_zipignore_simple_patterns(void) {
    printf("\n=== Testing simple zipignore patterns ===\n");
    
    zipignore_t* zi = create_test_zipignore("/project");
    if (!zi) return EXIT_FAILURE;
    
    // Add simple patterns (non-anchored, matches anywhere)
    add_test_pattern(zi, "/project", "*.log", false, false, false);
    add_test_pattern(zi, "/project", "*.tmp", false, false, false);
    add_test_pattern(zi, "/project", ".DS_Store", false, false, false);
    
    // Test matching
    TEST_ASSERT(should_ignore(zi, "/project/debug.log") == true, "*.log matches debug.log");
    TEST_ASSERT(should_ignore(zi, "/project/a/b/error.log") == true, "*.log matches nested error.log");
    TEST_ASSERT(should_ignore(zi, "/project/temp.tmp") == true, "*.tmp matches temp.tmp");
    TEST_ASSERT(should_ignore(zi, "/project/.DS_Store") == true, ".DS_Store matches");
    TEST_ASSERT(should_ignore(zi, "/project/subdir/.DS_Store") == true, ".DS_Store matches in subdir");
    
    // Test non-matching
    TEST_ASSERT(should_ignore(zi, "/project/file.txt") == false, "*.log doesn't match file.txt");
    TEST_ASSERT(should_ignore(zi, "/project/src/main.c") == false, "Source files not ignored");
    
    free(zi);
    return EXIT_SUCCESS;
}

int test_zipignore_directory_patterns(void) {
    printf("\n=== Testing directory patterns ===\n");
    
    zipignore_t* zi = create_test_zipignore("/project");
    if (!zi) return EXIT_FAILURE;
    
    // Directory patterns (is_directory = true)
    add_test_pattern(zi, "/project", "build", true, false, false);
    add_test_pattern(zi, "/project", "node_modules", true, false, false);
    add_test_pattern(zi, "/project", ".git", true, false, false);
    
    // Test directory matching
    TEST_ASSERT(should_ignore(zi, "/project/build/output.exe") == true, "build/ matches files inside build");
    TEST_ASSERT(should_ignore(zi, "/project/build/a/b/c.txt") == true, "build/ matches deeply nested files");
    TEST_ASSERT(should_ignore(zi, "/project/node_modules/lodash/index.js") == true, "node_modules/ matches");
    TEST_ASSERT(should_ignore(zi, "/project/.git/config") == true, ".git/ matches");
    TEST_ASSERT(should_ignore(zi, "/project/.git/objects/ab/1234") == true, ".git/ matches nested objects");
    
    // Test non-matching
    TEST_ASSERT(should_ignore(zi, "/project/src/build.c") == false, "build/ doesn't match build.c file");
    TEST_ASSERT(should_ignore(zi, "/project/builder/main.c") == false, "build/ doesn't match builder/");
    
    free(zi);
    return EXIT_SUCCESS;
}

int test_zipignore_anchored_patterns(void) {
    printf("\n=== Testing anchored patterns ===\n");
    
    zipignore_t* zi = create_test_zipignore("/project");
    if (!zi) return EXIT_FAILURE;
    
    // Anchored pattern (starts with / or contains /)
    add_test_pattern(zi, "/project", "TODO", false, false, true);  // /TODO - only at root
    add_test_pattern(zi, "/project", "docs/internal", false, false, true);  // docs/internal path
    
    // Test anchored matching
    TEST_ASSERT(should_ignore(zi, "/project/TODO") == true, "/TODO matches at root");
    TEST_ASSERT(should_ignore(zi, "/project/src/TODO") == false, "/TODO doesn't match in subdirectory");
    TEST_ASSERT(should_ignore(zi, "/project/docs/internal") == true, "docs/internal matches");
    TEST_ASSERT(should_ignore(zi, "/project/other/docs/internal") == false, "docs/internal doesn't match in other path");
    
    free(zi);
    return EXIT_SUCCESS;
}

int test_zipignore_negation(void) {
    printf("\n=== Testing negation patterns ===\n");
    
    zipignore_t* zi = create_test_zipignore("/project");
    if (!zi) return EXIT_FAILURE;
    
    // First ignore all .log files, then un-ignore important.log
    add_test_pattern(zi, "/project", "*.log", false, false, false);
    add_test_pattern(zi, "/project", "important.log", false, true, false);  // negation
    
    // Test negation
    TEST_ASSERT(should_ignore(zi, "/project/debug.log") == true, "*.log matches debug.log");
    TEST_ASSERT(should_ignore(zi, "/project/error.log") == true, "*.log matches error.log");
    TEST_ASSERT(should_ignore(zi, "/project/important.log") == false, "!important.log negates ignore");
    TEST_ASSERT(should_ignore(zi, "/project/subdir/important.log") == false, "!important.log negates in subdir");
    
    free(zi);
    return EXIT_SUCCESS;
}

int test_zipignore_nested_files(void) {
    printf("\n=== Testing nested .zipignore files ===\n");
    
    // Create a test directory structure
    const char* test_dir = "/tmp/gbzip_test_nested";
    remove_tree(test_dir);
    
    mkdir_p(test_dir);
    mkdir_p("/tmp/gbzip_test_nested/src");
    mkdir_p("/tmp/gbzip_test_nested/src/lib");
    mkdir_p("/tmp/gbzip_test_nested/docs");
    
    // Create root .zipignore
    create_test_file("/tmp/gbzip_test_nested/.zipignore", 
        "# Root zipignore\n"
        "*.log\n"
        "build/\n"
    );
    
    // Create nested .zipignore in src/
    create_test_file("/tmp/gbzip_test_nested/src/.zipignore",
        "# Src zipignore\n"
        "*.bak\n"
        "temp/\n"
    );
    
    // Create nested .zipignore in docs/
    create_test_file("/tmp/gbzip_test_nested/docs/.zipignore",
        "# Docs zipignore\n"
        "draft*\n"
    );
    
    // Create test files
    create_test_file("/tmp/gbzip_test_nested/app.log", "");
    create_test_file("/tmp/gbzip_test_nested/src/main.c", "");
    create_test_file("/tmp/gbzip_test_nested/src/backup.bak", "");
    create_test_file("/tmp/gbzip_test_nested/src/lib/util.c", "");
    create_test_file("/tmp/gbzip_test_nested/docs/readme.md", "");
    create_test_file("/tmp/gbzip_test_nested/docs/draft-v1.md", "");
    
    // Load zipignore - allocate on heap
    zipignore_t* zi = malloc(sizeof(zipignore_t));
    if (!zi) return EXIT_FAILURE;
    load_zipignore(zi, test_dir, NULL);
    load_nested_zipignore(zi, "/tmp/gbzip_test_nested/src");
    load_nested_zipignore(zi, "/tmp/gbzip_test_nested/docs");
    
    // Test root patterns apply everywhere
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/app.log") == true, "Root *.log matches app.log");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/src/debug.log") == true, "Root *.log matches in src/");
    
    // Test src/ patterns only apply in src/
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/src/backup.bak") == true, "src/*.bak matches in src/");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/backup.bak") == false, "src/*.bak doesn't match at root");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/docs/backup.bak") == false, "src/*.bak doesn't match in docs/");
    
    // Test docs/ patterns only apply in docs/
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/docs/draft-v1.md") == true, "docs/draft* matches in docs/");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/draft-v1.md") == false, "docs/draft* doesn't match at root");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/src/draft-v1.md") == false, "docs/draft* doesn't match in src/");
    
    // Test normal files not ignored
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/src/main.c") == false, "main.c not ignored");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_nested/docs/readme.md") == false, "readme.md not ignored");
    
    free_zipignore(zi);
    free(zi);
    remove_tree(test_dir);
    
    return EXIT_SUCCESS;
}

int test_zipignore_deeply_nested(void) {
    printf("\n=== Testing deeply nested .zipignore files ===\n");
    
    const char* test_dir = "/tmp/gbzip_test_deep";
    remove_tree(test_dir);
    
    // Create deep directory structure
    mkdir_p("/tmp/gbzip_test_deep/a/b/c/d");
    
    // Create .zipignore at various levels
    create_test_file("/tmp/gbzip_test_deep/.zipignore", "*.root\n");
    create_test_file("/tmp/gbzip_test_deep/a/.zipignore", "*.level1\n");
    create_test_file("/tmp/gbzip_test_deep/a/b/.zipignore", "*.level2\n");
    create_test_file("/tmp/gbzip_test_deep/a/b/c/.zipignore", "*.level3\n");
    create_test_file("/tmp/gbzip_test_deep/a/b/c/d/.zipignore", "*.level4\n");
    
    zipignore_t* zi = malloc(sizeof(zipignore_t));
    if (!zi) return EXIT_FAILURE;
    load_zipignore(zi, test_dir, NULL);
    load_nested_zipignore(zi, "/tmp/gbzip_test_deep/a");
    load_nested_zipignore(zi, "/tmp/gbzip_test_deep/a/b");
    load_nested_zipignore(zi, "/tmp/gbzip_test_deep/a/b/c");
    load_nested_zipignore(zi, "/tmp/gbzip_test_deep/a/b/c/d");
    
    // Root pattern applies everywhere
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/file.root") == true, "*.root at root");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/a/b/c/d/file.root") == true, "*.root at deepest level");
    
    // Each level's pattern only applies at or below that level
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/file.level1") == false, "*.level1 doesn't match at root");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/a/file.level1") == true, "*.level1 matches in a/");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/a/b/c/d/file.level1") == true, "*.level1 matches in a/b/c/d/");
    
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/a/file.level3") == false, "*.level3 doesn't match in a/");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/a/b/c/file.level3") == true, "*.level3 matches in a/b/c/");
    
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/a/b/c/file.level4") == false, "*.level4 doesn't match in a/b/c/");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_deep/a/b/c/d/file.level4") == true, "*.level4 matches in a/b/c/d/");
    
    free_zipignore(zi);
    free(zi);
    remove_tree(test_dir);
    
    return EXIT_SUCCESS;
}

int test_zipignore_edge_cases(void) {
    printf("\n=== Testing edge cases ===\n");
    
    zipignore_t* zi = create_test_zipignore("/project");
    if (!zi) return EXIT_FAILURE;
    
    // NULL handling
    TEST_ASSERT(should_ignore(NULL, "/project/file.txt") == false, "NULL zipignore returns false");
    TEST_ASSERT(should_ignore(zi, NULL) == false, "NULL path returns false");
    
    // Empty base dir handling
    zipignore_t* zi2 = create_test_zipignore("");
    if (!zi2) { free(zi); return EXIT_FAILURE; }
    add_test_pattern(zi2, "", "*.log", false, false, false);
    TEST_ASSERT(should_ignore(zi2, "debug.log") == true, "Empty base dir with matching pattern");
    
    // Pattern with only special characters
    add_test_pattern(zi, "/project", "*", false, false, false);
    TEST_ASSERT(should_ignore(zi, "/project/anything") == true, "* matches anything");
    
    free(zi2);
    free(zi);
    return EXIT_SUCCESS;
}

int test_zipignore_gitignore_compatibility(void) {
    printf("\n=== Testing gitignore compatibility ===\n");
    
    const char* test_dir = "/tmp/gbzip_test_gitcompat";
    remove_tree(test_dir);
    mkdir_p(test_dir);
    
    // Create a .zipignore with various gitignore-style patterns
    create_test_file("/tmp/gbzip_test_gitcompat/.zipignore",
        "# Comment line\n"
        "\n"
        "# Simple patterns\n"
        "*.log\n"
        "*.tmp\n"
        "\n"
        "# Directory pattern\n"
        "build/\n"
        "__pycache__/\n"
        "\n"
        "# Anchored pattern (leading /)\n"
        "/TODO\n"
        "/config.local\n"
        "\n"
        "# Pattern with path\n"
        "docs/internal/\n"
        "\n"
        "# Double-star patterns\n"
        "**/secret.key\n"
        "logs/**\n"
        "\n"
        "# Negation\n"
        "!important.log\n"
        "\n"
        "# Trailing space should be trimmed\n"
        "trailing.txt   \n"
    );
    
    zipignore_t* zi = malloc(sizeof(zipignore_t));
    if (!zi) return EXIT_FAILURE;
    load_zipignore(zi, test_dir, NULL);
    
    // Test simple patterns
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/app.log") == true, "*.log matches");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/sub/app.log") == true, "*.log matches in subdir");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/temp.tmp") == true, "*.tmp matches");
    
    // Test directory patterns
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/build/output") == true, "build/ matches");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/__pycache__/cache.pyc") == true, "__pycache__/ matches");
    
    // Test anchored patterns
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/TODO") == true, "/TODO matches at root");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/sub/TODO") == false, "/TODO doesn't match in subdir");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/config.local") == true, "/config.local matches at root");
    
    // Test path patterns
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/docs/internal/secret.md") == true, "docs/internal/ matches");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/other/docs/internal/file") == false, "docs/internal/ is anchored");
    
    // Test double-star patterns
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/secret.key") == true, "**/secret.key at root");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/a/b/c/secret.key") == true, "**/secret.key nested");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/logs/app.log") == true, "logs/** matches");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/logs/2024/01/app.log") == true, "logs/** matches nested");
    
    // Test negation
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/debug.log") == true, "*.log matches debug.log");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/important.log") == false, "!important.log negates");
    
    // Test trailing whitespace trimming
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_gitcompat/trailing.txt") == true, "Trailing whitespace trimmed");
    
    free_zipignore(zi);
    free(zi);
    remove_tree(test_dir);
    
    return EXIT_SUCCESS;
}

int test_zipignore_load_unload(void) {
    printf("\n=== Testing load/unload cycle ===\n");
    
    const char* test_dir = "/tmp/gbzip_test_loadunload";
    remove_tree(test_dir);
    mkdir_p(test_dir);
    
    create_test_file("/tmp/gbzip_test_loadunload/.zipignore", "*.testlog\n");
    
    // Load
    zipignore_t* zi = malloc(sizeof(zipignore_t));
    int result = load_zipignore(zi, test_dir, NULL);
    TEST_ASSERT(result == EXIT_SUCCESS, "load_zipignore succeeds");
    // Pattern count may include home ~/.zipignore patterns, so just check >= 1
    TEST_ASSERT(zi->pattern_count >= 1, "At least 1 pattern loaded");
    TEST_ASSERT(should_ignore(zi, "/tmp/gbzip_test_loadunload/test.testlog") == true, "Pattern works after load");
    
    // Free
    free_zipignore(zi);
    TEST_ASSERT(zi->pattern_count == 0, "Pattern count is 0 after free");
    
    // Load with no .zipignore file (may still load home ~/.zipignore)
    remove_tree(test_dir);
    mkdir_p(test_dir);
    result = load_zipignore(zi, test_dir, NULL);
    TEST_ASSERT(result == EXIT_SUCCESS, "load_zipignore succeeds without local .zipignore file");
    // Home ~/.zipignore might exist, so pattern_count could be >= 0
    TEST_ASSERT(result == EXIT_SUCCESS, "No error when no local .zipignore");
    
    free_zipignore(zi);
    free(zi);
    remove_tree(test_dir);
    
    return EXIT_SUCCESS;
}

int test_zipignore_duplicate_load_prevention(void) {
    printf("\n=== Testing duplicate load prevention ===\n");
    
    const char* test_dir = "/tmp/gbzip_test_dupload";
    remove_tree(test_dir);
    mkdir_p(test_dir);
    mkdir_p("/tmp/gbzip_test_dupload/sub");
    
    create_test_file("/tmp/gbzip_test_dupload/.zipignore", "*.testdup\n");
    create_test_file("/tmp/gbzip_test_dupload/sub/.zipignore", "*.testbak\n");
    
    zipignore_t* zi = malloc(sizeof(zipignore_t));
    load_zipignore(zi, test_dir, NULL);
    // Home ~/.zipignore may add patterns, so check >= 1
    int initial_count = zi->pattern_count;
    int initial_files = zi->loaded_files_count;
    TEST_ASSERT(zi->pattern_count >= 1, "At least 1 pattern loaded initially");
    TEST_ASSERT(zi->loaded_files_count >= 1, "At least 1 file loaded initially");
    
    // Load nested
    load_nested_zipignore(zi, "/tmp/gbzip_test_dupload/sub");
    TEST_ASSERT(zi->pattern_count == initial_count + 1, "Pattern count increased by 1 after nested load");
    TEST_ASSERT(zi->loaded_files_count == initial_files + 1, "Loaded files count increased by 1");
    
    int after_nested_count = zi->pattern_count;
    int after_nested_files = zi->loaded_files_count;
    
    // Try to load same files again - should not duplicate
    load_nested_zipignore(zi, "/tmp/gbzip_test_dupload");
    load_nested_zipignore(zi, "/tmp/gbzip_test_dupload/sub");
    TEST_ASSERT(zi->pattern_count == after_nested_count, "Pattern count unchanged after duplicate load attempt");
    TEST_ASSERT(zi->loaded_files_count == after_nested_files, "Loaded files count unchanged");
    
    // Test is_zipignore_loaded
    TEST_ASSERT(is_zipignore_loaded(zi, "/tmp/gbzip_test_dupload/.zipignore") == true, "Root .zipignore is marked as loaded");
    TEST_ASSERT(is_zipignore_loaded(zi, "/tmp/gbzip_test_dupload/sub/.zipignore") == true, "Sub .zipignore is marked as loaded");
    TEST_ASSERT(is_zipignore_loaded(zi, "/tmp/gbzip_test_dupload/nonexistent/.zipignore") == false, "Nonexistent .zipignore not marked as loaded");
    
    free_zipignore(zi);
    free(zi);
    remove_tree(test_dir);
    
    return EXIT_SUCCESS;
}

int test_path_utilities(void) {
    printf("\n=== Testing path utilities ===\n");
    
    const char* filename = get_filename("/path/to/file.txt");
    TEST_ASSERT(strcmp(filename, "file.txt") == 0, "get_filename extracts filename");
    
    filename = get_filename("file.txt");
    TEST_ASSERT(strcmp(filename, "file.txt") == 0, "get_filename handles no path");
    
    const char* ext = get_file_extension("file.txt");
    TEST_ASSERT(strcmp(ext, "txt") == 0, "get_file_extension extracts extension");
    
    ext = get_file_extension("archive.tar.gz");
    TEST_ASSERT(strcmp(ext, "gz") == 0, "get_file_extension handles double extension");
    
    ext = get_file_extension("noextension");
    TEST_ASSERT(strcmp(ext, "") == 0, "get_file_extension handles no extension");
    
    return EXIT_SUCCESS;
}

int test_normalize_path(void) {
    printf("\n=== Testing path normalization ===\n");
    
    char* norm;
    
    norm = normalize_path("/path/to/file");
    TEST_ASSERT(norm != NULL, "normalize_path returns non-null");
    TEST_ASSERT(strstr(norm, "\\") == NULL || strstr(norm, "/") == NULL, "Path separators normalized");
    free(norm);
    
    norm = normalize_path("path/to/file/");
    TEST_ASSERT(norm != NULL, "normalize_path handles trailing slash");
    size_t len = strlen(norm);
    TEST_ASSERT(norm[len-1] != '/' && norm[len-1] != '\\', "Trailing slash removed");
    free(norm);
    
    norm = normalize_path(NULL);
    TEST_ASSERT(norm == NULL, "normalize_path handles NULL");
    
    return EXIT_SUCCESS;
}

int main(void) {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║     GBZIP Comprehensive Test Suite       ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    
    test_file_utils();
    test_pattern_matching_basic();
    test_pattern_matching_doublestar();
    test_pattern_matching_character_class();
    test_zipignore_empty();
    test_zipignore_simple_patterns();
    test_zipignore_directory_patterns();
    test_zipignore_anchored_patterns();
    test_zipignore_negation();
    test_zipignore_nested_files();
    test_zipignore_deeply_nested();
    test_zipignore_edge_cases();
    test_zipignore_gitignore_compatibility();
    test_zipignore_load_unload();
    test_zipignore_duplicate_load_prevention();
    test_path_utilities();
    test_normalize_path();
    
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║              TEST RESULTS                ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    printf("  Tests run:    %d\n", tests_run);
    printf("  Tests passed: %d\n", tests_passed);
    printf("  Tests failed: %d\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf("\n  ✓ All tests passed!\n\n");
        return EXIT_SUCCESS;
    } else {
        printf("\n  ✗ Some tests failed!\n\n");
        return EXIT_FAILURE;
    }
}