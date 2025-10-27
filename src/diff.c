#include "diff.h"
#include "utils.h"
#include "zipignore.h"

static int collect_files_callback(const file_info_t* info, void* user_data) {
    diff_context_t* diff_ctx = (diff_context_t*)user_data;
    
    // Skip directories for now, we'll handle them separately
    if (info->is_directory) {
        return EXIT_SUCCESS;
    }
    
    // Calculate relative path
    const char* relative_path = info->path;
    if (strncmp(info->path, diff_ctx->base_dir, strlen(diff_ctx->base_dir)) == 0) {
        relative_path = info->path + strlen(diff_ctx->base_dir);
        if (*relative_path == PATH_SEPARATOR) {
            relative_path++;
        }
    }
    
    // Convert to ZIP path format (forward slashes)
    char zip_path[PATH_MAX];
    strncpy(zip_path, relative_path, PATH_MAX - 1);
    zip_path[PATH_MAX - 1] = '\0';
    
    for (char* p = zip_path; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    
    // This will be processed in compare_with_existing_zip
    return add_change(diff_ctx, zip_path, CHANGE_ADDED, 0, info->mtime, 0, info->size);
}

int diff_zip(const options_t* opts) {
    if (!opts || !opts->zip_file || !opts->target_dir) {
        return EXIT_INVALID_ARGS;
    }
    
    // Check if target directory exists
    if (!is_directory(opts->target_dir)) {
        fprintf(stderr, "Error: Target directory '%s' does not exist\n", opts->target_dir);
        return EXIT_FILE_ERROR;
    }
    
    // Initialize diff context
    diff_context_t diff_ctx;
    memset(&diff_ctx, 0, sizeof(diff_ctx));
    diff_ctx.base_dir = get_absolute_path(opts->target_dir);
    diff_ctx.zip_file = opts->zip_file;
    diff_ctx.change_capacity = 1000;
    diff_ctx.changes = malloc(sizeof(file_change_t) * diff_ctx.change_capacity);
    
    if (!diff_ctx.base_dir || !diff_ctx.changes) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        free_diff_context(&diff_ctx);
        return EXIT_FAILURE;
    }
    
    if (opts->verbose) {
        printf("Comparing directory '%s' with ZIP archive '%s'\n", opts->target_dir, opts->zip_file);
    }
    
    int result;
    
    // If ZIP file doesn't exist, create it from scratch
    if (!file_exists(opts->zip_file)) {
        if (opts->verbose) {
            printf("ZIP file does not exist, creating new archive\n");
        }
        result = create_zip(opts);
    } else {
        // Compare with existing ZIP
        result = compare_with_existing_zip(opts->zip_file, opts->target_dir, &diff_ctx);
        
        if (result == EXIT_SUCCESS) {
            if (diff_ctx.change_count == 0) {
                if (opts->verbose) {
                    printf("No changes detected\n");
                }
            } else {
                if (opts->verbose) {
                    print_diff_summary(&diff_ctx);
                }
                
                // Apply changes to ZIP
                result = apply_changes_to_zip(opts->zip_file, &diff_ctx, opts->verbose);
                
                if (result == EXIT_SUCCESS && opts->verbose) {
                    printf("ZIP archive updated successfully\n");
                }
            }
        }
    }
    
    free_diff_context(&diff_ctx);
    return result;
}

int compare_with_existing_zip(const char* zip_file, const char* directory, diff_context_t* diff_ctx) {
    if (!zip_file || !directory || !diff_ctx) {
        return EXIT_INVALID_ARGS;
    }
    
    // Load zipignore patterns
    zipignore_t zipignore;
    load_zipignore(&zipignore, directory, NULL);
    
    // Get entries from ZIP file
    zip_entry_t* zip_entries = NULL;
    size_t zip_count = 0;
    
    int result = get_zip_entries(zip_file, &zip_entries, &zip_count);
    if (result != EXIT_SUCCESS) {
        free_zipignore(&zipignore);
        return result;
    }
    
    // Collect current files in directory
    diff_ctx->change_count = 0; // Reset change count
    result = traverse_directory(directory, true, collect_files_callback, diff_ctx);
    if (result != EXIT_SUCCESS) {
        free_zip_entries(zip_entries, zip_count);
        free_zipignore(&zipignore);
        return result;
    }
    
    // Create a temporary copy to track processed files
    file_change_t* current_files = malloc(sizeof(file_change_t) * diff_ctx->change_count);
    if (!current_files) {
        free_zip_entries(zip_entries, zip_count);
        free_zipignore(&zipignore);
        return EXIT_FAILURE;
    }
    
    memcpy(current_files, diff_ctx->changes, sizeof(file_change_t) * diff_ctx->change_count);
    size_t current_count = diff_ctx->change_count;
    diff_ctx->change_count = 0; // Reset for actual changes
    
    // Compare ZIP entries with current files
    for (size_t i = 0; i < zip_count; i++) {
        const zip_entry_t* zip_entry = &zip_entries[i];
        
        // Skip directories
        if (zip_entry->is_directory) {
            continue;
        }
        
        // Check if file should be ignored
        char full_path[PATH_MAX];
        snprintf(full_path, PATH_MAX, "%s%c%s", directory, PATH_SEPARATOR, zip_entry->name);
        
        // Convert ZIP path to system path
        for (char* p = full_path + strlen(directory) + 1; *p; p++) {
            if (*p == '/') {
                *p = PATH_SEPARATOR;
            }
        }
        
        if (should_ignore(&zipignore, full_path)) {
            continue;
        }
        
        // Look for this file in current files
        bool found = false;
        for (size_t j = 0; j < current_count; j++) {
            if (strcmp(current_files[j].path, zip_entry->name) == 0) {
                found = true;
                
                // Check if file was modified
                if (current_files[j].new_mtime > zip_entry->mtime || 
                    current_files[j].new_size != zip_entry->size) {
                    
                    add_change(diff_ctx, zip_entry->name, CHANGE_MODIFIED,
                              zip_entry->mtime, current_files[j].new_mtime,
                              zip_entry->size, current_files[j].new_size);
                }
                
                // Mark as processed
                current_files[j].change_type = CHANGE_NONE;
                break;
            }
        }
        
        if (!found) {
            // File was deleted
            add_change(diff_ctx, zip_entry->name, CHANGE_DELETED,
                      zip_entry->mtime, 0, zip_entry->size, 0);
        }
    }
    
    // Add new files (those not marked as CHANGE_NONE)
    for (size_t i = 0; i < current_count; i++) {
        if (current_files[i].change_type == CHANGE_ADDED) {
            // Check if file should be ignored
            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s%c%s", directory, PATH_SEPARATOR, current_files[i].path);
            
            // Convert ZIP path to system path
            for (char* p = full_path + strlen(directory) + 1; *p; p++) {
                if (*p == '/') {
                    *p = PATH_SEPARATOR;
                }
            }
            
            if (!should_ignore(&zipignore, full_path)) {
                add_change(diff_ctx, current_files[i].path, CHANGE_ADDED,
                          0, current_files[i].new_mtime, 0, current_files[i].new_size);
            }
        }
    }
    
    free(current_files);
    free_zip_entries(zip_entries, zip_count);
    free_zipignore(&zipignore);
    
    return EXIT_SUCCESS;
}

int apply_changes_to_zip(const char* zip_file, const diff_context_t* diff_ctx, bool verbose) {
    if (!zip_file || !diff_ctx) {
        return EXIT_INVALID_ARGS;
    }
    
    // Open existing ZIP for modification
    int error;
    zip_t* archive = zip_open(zip_file, 0, &error);
    if (!archive) {
        zip_error_t zip_error;
        zip_error_init_with_code(&zip_error, error);
        fprintf(stderr, "Error opening ZIP file '%s': %s\n", 
                zip_file, zip_error_strerror(&zip_error));
        zip_error_fini(&zip_error);
        return EXIT_ZIP_ERROR;
    }
    
    // Apply changes
    for (size_t i = 0; i < diff_ctx->change_count; i++) {
        const file_change_t* change = &diff_ctx->changes[i];
        
        switch (change->change_type) {
            case CHANGE_ADDED:
            case CHANGE_MODIFIED: {
                // Build full file path
                char file_path[PATH_MAX];
                snprintf(file_path, PATH_MAX, "%s%c%s", diff_ctx->base_dir, PATH_SEPARATOR, change->path);
                
                // Convert ZIP path to system path
                for (char* p = file_path + strlen(diff_ctx->base_dir) + 1; *p; p++) {
                    if (*p == '/') {
                        *p = PATH_SEPARATOR;
                    }
                }
                
                // Remove old entry if it exists (for modifications)
                if (change->change_type == CHANGE_MODIFIED) {
                    zip_int64_t index = zip_name_locate(archive, change->path, 0);
                    if (index >= 0) {
                        zip_delete(archive, index);
                    }
                }
                
                // Add new/updated file
                zip_source_t* source = zip_source_file(archive, file_path, 0, -1);
                if (!source) {
                    fprintf(stderr, "Error creating ZIP source for file '%s': %s\n", 
                            file_path, zip_strerror(archive));
                    zip_close(archive);
                    return EXIT_ZIP_ERROR;
                }
                
                zip_int64_t idx = zip_file_add(archive, change->path, source, ZIP_FL_ENC_UTF_8);
                if (idx < 0) {
                    fprintf(stderr, "Error adding file '%s' to archive: %s\n", 
                            change->path, zip_strerror(archive));
                    zip_source_free(source);
                    zip_close(archive);
                    return EXIT_ZIP_ERROR;
                }
                
                // Set file modification time
                zip_file_set_mtime(archive, idx, change->new_mtime, 0);
                
                if (verbose) {
                    printf("%s: %s\n", 
                           change->change_type == CHANGE_ADDED ? "Added" : "Modified", 
                           change->path);
                }
                break;
            }
            
            case CHANGE_DELETED: {
                zip_int64_t index = zip_name_locate(archive, change->path, 0);
                if (index >= 0) {
                    zip_delete(archive, index);
                    
                    if (verbose) {
                        printf("Deleted: %s\n", change->path);
                    }
                } else {
                    if (verbose) {
                        printf("Warning: Could not find file '%s' to delete\n", change->path);
                    }
                }
                break;
            }
            
            default:
                break;
        }
    }
    
    // Close and save changes
    if (zip_close(archive) < 0) {
        fprintf(stderr, "Error closing ZIP file: %s\n", zip_strerror(archive));
        return EXIT_ZIP_ERROR;
    }
    
    return EXIT_SUCCESS;
}

int add_change(diff_context_t* diff_ctx, const char* path, change_type_t type, 
               time_t old_mtime, time_t new_mtime, off_t old_size, off_t new_size) {
    if (!diff_ctx || !path) {
        return EXIT_INVALID_ARGS;
    }
    
    // Expand array if needed
    if (diff_ctx->change_count >= diff_ctx->change_capacity) {
        diff_ctx->change_capacity *= 2;
        file_change_t* new_changes = realloc(diff_ctx->changes, 
                                            sizeof(file_change_t) * diff_ctx->change_capacity);
        if (!new_changes) {
            return EXIT_FAILURE;
        }
        diff_ctx->changes = new_changes;
    }
    
    file_change_t* change = &diff_ctx->changes[diff_ctx->change_count];
    strncpy(change->path, path, PATH_MAX - 1);
    change->path[PATH_MAX - 1] = '\0';
    change->change_type = type;
    change->old_mtime = old_mtime;
    change->new_mtime = new_mtime;
    change->old_size = old_size;
    change->new_size = new_size;
    
    diff_ctx->change_count++;
    return EXIT_SUCCESS;
}

void free_diff_context(diff_context_t* diff_ctx) {
    if (!diff_ctx) return;
    
    free(diff_ctx->changes);
    free(diff_ctx->base_dir);
    memset(diff_ctx, 0, sizeof(diff_context_t));
}

void print_diff_summary(const diff_context_t* diff_ctx) {
    if (!diff_ctx) return;
    
    size_t added = 0, modified = 0, deleted = 0;
    
    for (size_t i = 0; i < diff_ctx->change_count; i++) {
        switch (diff_ctx->changes[i].change_type) {
            case CHANGE_ADDED: added++; break;
            case CHANGE_MODIFIED: modified++; break;
            case CHANGE_DELETED: deleted++; break;
            default: break;
        }
    }
    
    printf("Changes detected:\n");
    printf("  Added: %zu files\n", added);
    printf("  Modified: %zu files\n", modified);
    printf("  Deleted: %zu files\n", deleted);
    printf("  Total changes: %zu\n\n", diff_ctx->change_count);
}