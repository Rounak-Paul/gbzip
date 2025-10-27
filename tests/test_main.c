#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../include/gbzip.h"
#include "../include/utils.h"
#include "../include/zipignore.h"

int test_file_utils() {
    printf("Testing file utilities...\n");
    
    // Test file_exists
    assert(file_exists(".") == true);
    assert(file_exists("nonexistent_file") == false);
    
    // Test is_directory
    assert(is_directory(".") == true);
    assert(is_directory("../README.md") == false);
    
    printf("File utilities tests passed\n");
    return EXIT_SUCCESS;
}

int test_zipignore() {
    printf("Testing zipignore functionality...\n");
    
    zipignore_t zi;
    // Initialize empty zipignore structure for testing
    memset(&zi, 0, sizeof(zipignore_t));
    strcpy(zi.base_dir, ".");
    
    printf("Testing with empty zipignore (no patterns)\n");
    
    // Test that nothing is ignored with empty zipignore
    bool ignores_git = should_ignore(&zi, "./.git");
    bool ignores_gitconfig = should_ignore(&zi, "./.git/config");
    bool ignores_build = should_ignore(&zi, "./build/output.exe");
    bool ignores_log = should_ignore(&zi, "./test.log");
    bool allows_source = should_ignore(&zi, "./src/main.c");
    
    printf("Git directory (.git): %s\n", ignores_git ? "ignored" : "allowed");
    printf("Git config (.git/config): %s\n", ignores_gitconfig ? "ignored" : "allowed");
    printf("Build file (build/output.exe): %s\n", ignores_build ? "ignored" : "allowed");
    printf("Log file (test.log): %s\n", ignores_log ? "ignored" : "allowed");
    printf("Source file (src/main.c): %s\n", allows_source ? "ignored" : "allowed");
    
    // With no patterns, nothing should be ignored
    assert(zi.pattern_count == 0);
    assert(ignores_git == false);      // Git should not be ignored
    assert(ignores_gitconfig == false); // Git config should not be ignored
    assert(ignores_build == false);    // Build files should not be ignored
    assert(ignores_log == false);      // Log files should not be ignored
    assert(allows_source == false);    // Source files should not be ignored
    
    free_zipignore(&zi);
    
    printf("Zipignore tests passed\n");
    return EXIT_SUCCESS;
}

int test_path_utilities() {
    printf("Testing path utilities...\n");
    
    // Test get_filename
    const char* filename = get_filename("/path/to/file.txt");
    assert(strcmp(filename, "file.txt") == 0);
    
    filename = get_filename("file.txt");
    assert(strcmp(filename, "file.txt") == 0);
    
    // Test get_file_extension
    const char* ext = get_file_extension("file.txt");
    assert(strcmp(ext, "txt") == 0);
    
    ext = get_file_extension("archive.tar.gz");
    assert(strcmp(ext, "gz") == 0);
    
    printf("Path utilities tests passed\n");
    return EXIT_SUCCESS;
}

int main() {
    printf("Running gbzip tests...\n\n");
    
    int result = EXIT_SUCCESS;
    
    result |= test_file_utils();
    result |= test_zipignore();
    result |= test_path_utilities();
    
    if (result == EXIT_SUCCESS) {
        printf("\nAll tests passed successfully!\n");
    } else {
        printf("\nSome tests failed!\n");
    }
    
    return result;
}