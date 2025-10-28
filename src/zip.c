#include <zip.h>
#include "gbzip_zip.h"
#include "zipignore.h"
#include "utils.h"
#include "logging.h"

#ifndef _WIN32
    #include <pthread.h>
    #include <unistd.h>
#else
    #include <windows.h>
#endif

// Global variables for progress monitoring
static volatile bool g_compression_active = false;
static progress_t* g_progress_ptr = NULL;
static bool g_show_progress = false;
static volatile size_t g_total_compressed_bytes = 0;
static char g_output_filename[PATH_MAX] = {0};

#ifndef _WIN32
static pthread_t g_progress_thread;
#else
static HANDLE g_progress_handle = NULL;
#endif

static int count_files_callback(const file_info_t* info, void* user_data) {
    (void)info; // Suppress unused parameter warning
    size_t* count = (size_t*)user_data;
    (*count)++;
    return EXIT_SUCCESS;
}

static int add_file_callback(const file_info_t* info, void* user_data) {
    zip_context_t* ctx = (zip_context_t*)user_data;
    
    if (should_ignore(&ctx->zipignore, info->path)) {
        log_file_operation("Ignored", info->path, info->size);
        return EXIT_SUCCESS;
    }
    

    const char* base_dir = ctx->zipignore.base_dir;
    const char* relative_path = info->path;
    
    if (strncmp(info->path, base_dir, strlen(base_dir)) == 0) {
        relative_path = info->path + strlen(base_dir);
        if (*relative_path == PATH_SEPARATOR) {
            relative_path++;
        }
    }
    

    char archive_path[PATH_MAX];
    strncpy(archive_path, relative_path, PATH_MAX - 1);
    archive_path[PATH_MAX - 1] = '\0';
    
    for (char* p = archive_path; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    
    if (info->is_directory) {

        strcat(archive_path, "/");
        
        zip_int64_t idx = zip_dir_add(ctx->archive, archive_path, ZIP_FL_ENC_UTF_8);
        if (idx < 0) {
            fprintf(stderr, "Error adding directory %s to archive: %s\n", 
                    archive_path, zip_strerror(ctx->archive));
            return EXIT_ZIP_ERROR;
        }
        
        log_file_operation("Added directory", archive_path, 0);
    } else {

        int result = add_file_to_zip(ctx, info->path, archive_path);
        if (result != EXIT_SUCCESS) {
            return result;
        }
    }
    
    update_progress(&ctx->progress, info->size);
    
    // Track large files for better finalization estimates
    if (info->size > 10 * 1024 * 1024) {
        ctx->progress.large_files_count++;
        ctx->progress.large_files_bytes += info->size;
    }
    
    if (ctx->verbose) {
        print_progress(&ctx->progress, "Creating");
    }
    
    return EXIT_SUCCESS;
}

static int add_single_file(zip_context_t* ctx, const char* file_path) {
    if (!ctx || !file_path) {
        return EXIT_INVALID_ARGS;
    }
    
    // Check if file should be ignored
    if (should_ignore(&ctx->zipignore, file_path)) {
        return EXIT_SUCCESS;
    }
    
    // Check if file exists and is regular
    if (!file_exists(file_path)) {
        fprintf(stderr, "Error: File '%s' does not exist\n", file_path);
        return EXIT_FILE_ERROR;
    }
    
    if (is_directory(file_path)) {
        fprintf(stderr, "Error: '%s' is a directory, not a file\n", file_path);
        return EXIT_FILE_ERROR;
    }
    
    // Use just the filename for archive path (like standard zip)
    const char* archive_path = strrchr(file_path, PATH_SEPARATOR);
    if (archive_path) {
        archive_path++; // Skip the separator
    } else {
        archive_path = file_path; // No path separator found
    }
    
    int result = add_file_to_zip(ctx, file_path, archive_path);
    if (result == EXIT_SUCCESS) {
        off_t file_size = get_file_size(file_path);
        ctx->progress.processed_files++;
        ctx->progress.processed_bytes += file_size;
        
        // Track large files for better finalization estimates
        if (file_size > 10 * 1024 * 1024) { // Files larger than 10MB
            ctx->progress.large_files_count++;
            ctx->progress.large_files_bytes += file_size;
        }
        
        if (ctx->verbose) {
            print_progress(&ctx->progress, "Creating");
        }
    }
    
    return result;
}

int create_zip(const options_t* opts) {
    if (!opts || !opts->zip_file) {
        return EXIT_INVALID_ARGS;
    }
    
    // Initialize ZIP context
    zip_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.filename = opts->zip_file;
    ctx.verbose = opts->verbose && !opts->quiet;
    
    // Determine base directory for zipignore loading
    const char* base_dir = ".";
    if (opts->input_file_count > 0) {
        // Use directory of first input file as base
        base_dir = opts->input_files[0];
        if (!is_directory(base_dir)) {
            base_dir = "."; // Use current directory if first input is not a directory
        }
    } else if (opts->target_dir) {
        base_dir = opts->target_dir;
    }
    
    // Load zipignore patterns
    int result = load_zipignore(&ctx.zipignore, base_dir, opts->zipignore_file);
    if (result != EXIT_SUCCESS && opts->verbose) {
        fprintf(stderr, "Warning: Could not load zipignore patterns\n");
    }
    
    // Create ZIP archive
    int error;
    ctx.archive = zip_open(opts->zip_file, ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!ctx.archive) {
        zip_error_t zip_error;
        zip_error_init_with_code(&zip_error, error);
        fprintf(stderr, "Error creating ZIP file '%s': %s\n", 
                opts->zip_file, zip_error_strerror(&zip_error));
        zip_error_fini(&zip_error);
        return EXIT_ZIP_ERROR;
    }
    
    // Count total files for progress reporting
    init_progress(&ctx.progress);
    
    // Process input files
    if (opts->input_file_count > 0) {
        // Process specified files/directories
        for (int i = 0; i < opts->input_file_count; i++) {
            const char* input = opts->input_files[i];
            if (is_directory(input)) {
                if (traverse_directory(input, opts->recursive, count_files_callback, &ctx.progress.total_files) != EXIT_SUCCESS) {
                    if (opts->verbose) {
                        fprintf(stderr, "Warning: Could not count files in '%s'\n", input);
                    }
                }
            } else {
                ctx.progress.total_files++;
            }
        }
    } else if (opts->target_dir) {
        // Use target directory (backward compatibility)
        if (traverse_directory(opts->target_dir, opts->recursive, count_files_callback, &ctx.progress.total_files) != EXIT_SUCCESS) {
            if (opts->verbose) {
                fprintf(stderr, "Warning: Could not count files for progress reporting\n");
            }
        }
    } else {
        // Default to current directory
        if (traverse_directory(".", opts->recursive, count_files_callback, &ctx.progress.total_files) != EXIT_SUCCESS) {
            if (opts->verbose) {
                fprintf(stderr, "Warning: Could not count files for progress reporting\n");
            }
        }
    }
    
    log_event(EVENT_INIT, LOG_INFO, "Creating ZIP archive '%s'", opts->zip_file);
    log_event(EVENT_INIT, LOG_INFO, "Total files to process: %zu", ctx.progress.total_files);
    
    // Add files to ZIP
    if (opts->input_file_count > 0) {
        // Process specified files/directories
        for (int i = 0; i < opts->input_file_count && result == EXIT_SUCCESS; i++) {
            const char* input = opts->input_files[i];
            if (is_directory(input)) {
                result = traverse_directory(input, opts->recursive, add_file_callback, &ctx);
            } else {
                // Add single file
                result = add_single_file(&ctx, input);
            }
        }
    } else if (opts->target_dir) {
        // Use target directory (backward compatibility)
        result = traverse_directory(opts->target_dir, opts->recursive, add_file_callback, &ctx);
    } else {
        // Default to current directory
        result = traverse_directory(".", opts->recursive, add_file_callback, &ctx);
    }
    
    if (result == EXIT_SUCCESS) {
        // Switch to finalization phase
        set_progress_phase(&ctx.progress, PHASE_FINALIZING, 0.02);
        
        if (ctx.progress.large_files_count > 0) {
            double large_mb = ctx.progress.large_files_bytes / (1024.0 * 1024.0);
            log_event(EVENT_COMPRESSION, LOG_INFO, "Compressing %zu large files (%.1f MB) - this may take several minutes", 
                     ctx.progress.large_files_count, large_mb);
        } else if (ctx.progress.total_files > 100) {
            log_event(EVENT_COMPRESSION, LOG_INFO, "Finalizing archive (this may take a while for large archives)");
        }
        
        if (!g_log_config.structured && ctx.verbose) {
            printf("\n");
        }
        
        // Close ZIP archive with progress updates (this is where the actual compression and writing happens)
        int close_result = zip_close_with_progress(ctx.archive, &ctx.progress, ctx.verbose, opts->zip_file);
        if (close_result < 0) {
            fprintf(stderr, "\nError closing ZIP file: %s\n", zip_strerror(ctx.archive));
            result = EXIT_ZIP_ERROR;
        } else {
            time_t elapsed = time(NULL) - ctx.progress.start_time;
            log_archive_info(opts->zip_file, ctx.progress.processed_files, ctx.progress.processed_bytes, (double)elapsed);
            
            if (!g_log_config.structured) {
                if (ctx.verbose) {
                    printf(" done\n");
                } else if (ctx.progress.total_files > 100) {
                    printf(" done\n");
                }
                
                if (!ctx.verbose && !opts->quiet) {
                    printf("Created '%s' with %zu files\n", opts->zip_file, ctx.progress.processed_files);
                }
            }
        }
    } else {
        zip_discard(ctx.archive);
    }
    
    free_zipignore(&ctx.zipignore);
    return result;
}

#ifndef _WIN32
// Unix/Linux/macOS progress thread
void* progress_thread(void* arg) {
    (void)arg; // Suppress unused parameter warning
    int step = 0;
    size_t last_file_size = 0;
    
    while (g_compression_active) {
        if (g_show_progress && g_progress_ptr) {
            // Monitor output file size growth if we have a filename
            if (g_output_filename[0] != '\0' && file_exists(g_output_filename)) {
                size_t current_file_size = get_file_size(g_output_filename);
                if (current_file_size > last_file_size) {
                    g_total_compressed_bytes = current_file_size;
                    last_file_size = current_file_size;
                }
            }
            
            print_compression_progress(g_progress_ptr, step++);
        }
        sleep(1); // Update every 1 second
    }
    return NULL;
}
#else
// Windows progress thread
DWORD WINAPI progress_thread(LPVOID arg) {
    (void)arg; // Suppress unused parameter warning
    int step = 0;
    size_t last_file_size = 0;
    
    while (g_compression_active) {
        if (g_show_progress && g_progress_ptr) {
            // Monitor output file size growth if we have a filename
            if (g_output_filename[0] != '\0' && file_exists(g_output_filename)) {
                size_t current_file_size = get_file_size(g_output_filename);
                if (current_file_size > last_file_size) {
                    g_total_compressed_bytes = current_file_size;
                    last_file_size = current_file_size;
                }
            }
            
            print_compression_progress(g_progress_ptr, step++);
        }
        Sleep(1000); // Update every 1 second
    }
    return 0;
}
#endif


// Cross-platform function to close ZIP archive with progress updates
int zip_close_with_progress(zip_t* archive, progress_t* progress, bool verbose, const char* output_filename) {
    if (!archive) return -1;
    
    // For smaller archives, just close normally without extra output
    if (!progress || (progress->large_files_bytes < 5 * 1024 * 1024 && progress->total_files < 50)) {
        return zip_close(archive);
    }
    
    // Determine if we should show progress
    bool show_progress = verbose || progress->large_files_bytes > 20 * 1024 * 1024;
    time_t start_compression = time(NULL);
    
    if (show_progress) {
        // Store output filename for file size monitoring
        if (output_filename) {
            strncpy(g_output_filename, output_filename, PATH_MAX - 1);
            g_output_filename[PATH_MAX - 1] = '\0';
        }
        
        g_progress_ptr = progress;
        g_show_progress = true;
        g_compression_active = true;
        g_total_compressed_bytes = 0;
        
        // Start progress thread
        #ifndef _WIN32
            pthread_create(&g_progress_thread, NULL, progress_thread, NULL);
        #else
            g_progress_handle = CreateThread(NULL, 0, progress_thread, NULL, 0, NULL);
        #endif
        
        print_compression_progress(progress, 0);
        fflush(stdout);
    }
    
    // Perform the actual compression
    int result = zip_close(archive);
    
    // Stop progress monitoring
    if (show_progress) {
        g_compression_active = false;
        
        // Wait for progress thread to finish
        #ifndef _WIN32
            pthread_join(g_progress_thread, NULL);
        #else
            if (g_progress_handle) {
                WaitForSingleObject(g_progress_handle, 1000);
                CloseHandle(g_progress_handle);
                g_progress_handle = NULL;
            }
        #endif
        
        time_t end_compression = time(NULL);
        long compression_time = end_compression - start_compression;
        
        if (result == 0) {
            printf("\rCompressing and writing archive ✓ (100.0%%) - completed in %lds", compression_time);
            if (compression_time > 10) {
                printf("\n  Large file compression required extended time");
            }
            printf("\n");
        } else {
            printf("\rCompression failed after %lds\n", compression_time);
        }
        
        // Clear the filename
        g_output_filename[0] = '\0';
    }
    
    return result;
}

int extract_zip(const options_t* opts) {
    if (!opts || !opts->zip_file || !opts->target_dir) {
        return EXIT_INVALID_ARGS;
    }
    
    // Check if ZIP file exists
    if (!file_exists(opts->zip_file)) {
        fprintf(stderr, "Error: ZIP file '%s' does not exist\n", opts->zip_file);
        return EXIT_FILE_ERROR;
    }
    
    // Create target directory if it doesn't exist
    if (!is_directory(opts->target_dir)) {
        if (create_directory_recursive(opts->target_dir) != EXIT_SUCCESS) {
            fprintf(stderr, "Error: Could not create target directory '%s'\n", opts->target_dir);
            return EXIT_FILE_ERROR;
        }
    }
    
    // Open ZIP archive
    int error;
    zip_t* archive = zip_open(opts->zip_file, ZIP_RDONLY, &error);
    if (!archive) {
        zip_error_t zip_error;
        zip_error_init_with_code(&zip_error, error);
        fprintf(stderr, "Error opening ZIP file '%s': %s\n", 
                opts->zip_file, zip_error_strerror(&zip_error));
        zip_error_fini(&zip_error);
        return EXIT_ZIP_ERROR;
    }
    
    zip_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.archive = archive;
    ctx.filename = opts->zip_file;
    ctx.verbose = opts->verbose;
    
    init_progress(&ctx.progress);
    ctx.progress.total_files = zip_get_num_entries(archive, 0);
    
    if (ctx.verbose) {
        printf("Extracting ZIP archive '%s' to '%s'\n", opts->zip_file, opts->target_dir);
        printf("Total entries: %zu\n", ctx.progress.total_files);
    }
    
    // Extract all entries
    int result = EXIT_SUCCESS;
    for (zip_uint64_t i = 0; i < ctx.progress.total_files; i++) {
        result = extract_file_from_zip(&ctx, i, opts->target_dir);
        if (result != EXIT_SUCCESS) {
            break;
        }
        
        update_progress(&ctx.progress, 1);
        if (ctx.verbose) {
            print_progress(&ctx.progress, "Extracting");
        }
    }
    
    zip_close(archive);
    
    if (result == EXIT_SUCCESS && ctx.verbose) {
        printf("\nZIP archive extracted successfully\n");
        printf("Files extracted: %zu\n", ctx.progress.processed_files);
    }
    
    return result;
}

int list_zip(const options_t* opts) {
    if (!opts || !opts->zip_file) {
        return EXIT_INVALID_ARGS;
    }
    
    // Check if ZIP file exists
    if (!file_exists(opts->zip_file)) {
        fprintf(stderr, "Error: ZIP file '%s' does not exist\n", opts->zip_file);
        return EXIT_FILE_ERROR;
    }
    
    // Open ZIP archive
    int error;
    zip_t* archive = zip_open(opts->zip_file, ZIP_RDONLY, &error);
    if (!archive) {
        zip_error_t zip_error;
        zip_error_init_with_code(&zip_error, error);
        fprintf(stderr, "Error opening ZIP file '%s': %s\n", 
                opts->zip_file, zip_error_strerror(&zip_error));
        zip_error_fini(&zip_error);
        return EXIT_ZIP_ERROR;
    }
    
    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    
    printf("Archive: %s\n", opts->zip_file);
    printf("Entries: %lld\n\n", (long long)num_entries);
    
    if (opts->verbose) {
        printf("%-10s %-19s %s\n", "Size", "Modified", "Name");
        printf("%-10s %-19s %s\n", "----------", "-------------------", "----");
    }
    
    for (zip_uint64_t i = 0; i < (zip_uint64_t)num_entries; i++) {
        const char* name = zip_get_name(archive, i, 0);
        if (!name) {
            fprintf(stderr, "Error getting entry name at index %lld\n", (long long)i);
            continue;
        }
        
        zip_stat_t stat;
        if (zip_stat_index(archive, i, 0, &stat) == 0) {
            if (opts->verbose) {
                char time_str[20];
                struct tm* tm_info = localtime(&stat.mtime);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                
                printf("%-10lld %-19s %s\n", 
                       (long long)stat.size, time_str, name);
            } else {
                printf("%s\n", name);
            }
        } else {
            printf("%s\n", name);
        }
    }
    
    zip_close(archive);
    return EXIT_SUCCESS;
}

int add_file_to_zip(zip_context_t* ctx, const char* file_path, const char* archive_path) {
    if (!ctx || !file_path || !archive_path) {
        return EXIT_INVALID_ARGS;
    }
    
    zip_source_t* source = zip_source_file(ctx->archive, file_path, 0, -1);
    if (!source) {
        fprintf(stderr, "Error creating ZIP source for file '%s': %s\n", 
                file_path, zip_strerror(ctx->archive));
        return EXIT_ZIP_ERROR;
    }
    
    zip_int64_t idx = zip_file_add(ctx->archive, archive_path, source, ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        fprintf(stderr, "Error adding file '%s' to archive: %s\n", 
                archive_path, zip_strerror(ctx->archive));
        zip_source_free(source);
        return EXIT_ZIP_ERROR;
    }
    
    // Set file modification time
    time_t mtime = get_file_mtime(file_path);
    if (mtime > 0) {
        zip_file_set_mtime(ctx->archive, idx, mtime, 0);
    }
    
    off_t file_size = get_file_size(file_path);
    if (file_size > 10 * 1024 * 1024) { // Files larger than 10MB
        log_file_operation("Added large file", archive_path, file_size);
    } else {
        log_file_operation("Added file", archive_path, file_size);
    }
    
    return EXIT_SUCCESS;
}

int extract_file_from_zip(zip_context_t* ctx, zip_uint64_t index, const char* output_dir) {
    if (!ctx || !output_dir) {
        return EXIT_INVALID_ARGS;
    }
    
    zip_stat_t stat;
    if (zip_stat_index(ctx->archive, index, 0, &stat) < 0) {
        fprintf(stderr, "Error getting file stats at index %lld: %s\n", 
                (long long)index, zip_strerror(ctx->archive));
        return EXIT_ZIP_ERROR;
    }
    
    // Build output path
    char* output_path = join_path(output_dir, stat.name);
    if (!output_path) {
        return EXIT_FAILURE;
    }
    
    // Convert forward slashes to platform-specific separators
    for (char* p = output_path; *p; p++) {
        if (*p == '/') {
            *p = PATH_SEPARATOR;
        }
    }
    
    // Check if it's a directory
    size_t name_len = strlen(stat.name);
    bool is_dir = (name_len > 0 && stat.name[name_len - 1] == '/');
    
    if (is_dir) {
        // Create directory
        if (create_directory_recursive(output_path) != EXIT_SUCCESS) {
            fprintf(stderr, "Error creating directory '%s'\n", output_path);
            free(output_path);
            return EXIT_FILE_ERROR;
        }
        
        if (ctx->verbose) {
            printf("Created directory: %s\n", output_path);
        }
    } else {
        // Extract file
        
        // Create parent directory if needed
        char* dir_path = malloc(strlen(output_path) + 1);
        if (!dir_path) {
            free(output_path);
            return EXIT_FAILURE;
        }
        
        strcpy(dir_path, output_path);
        char* last_sep = strrchr(dir_path, PATH_SEPARATOR);
        if (last_sep) {
            *last_sep = '\0';
            create_directory_recursive(dir_path);
        }
        free(dir_path);
        
        // Open file in ZIP
        zip_file_t* file = zip_fopen_index(ctx->archive, index, 0);
        if (!file) {
            fprintf(stderr, "Error opening file in ZIP at index %lld: %s\n", 
                    (long long)index, zip_strerror(ctx->archive));
            free(output_path);
            return EXIT_ZIP_ERROR;
        }
        
        // Create output file
        FILE* output_file = fopen(output_path, "wb");
        if (!output_file) {
            fprintf(stderr, "Error creating output file '%s'\n", output_path);
            zip_fclose(file);
            free(output_path);
            return EXIT_FILE_ERROR;
        }
        
        // Copy data
        char buffer[8192];
        zip_int64_t bytes_read;
        while ((bytes_read = zip_fread(file, buffer, sizeof(buffer))) > 0) {
            if (fwrite(buffer, 1, bytes_read, output_file) != (size_t)bytes_read) {
                fprintf(stderr, "Error writing to output file '%s'\n", output_path);
                fclose(output_file);
                zip_fclose(file);
                free(output_path);
                return EXIT_FILE_ERROR;
            }
        }
        
        fclose(output_file);
        zip_fclose(file);
        
        if (ctx->verbose) {
            printf("Extracted file: %s\n", output_path);
        }
    }
    
    free(output_path);
    return EXIT_SUCCESS;
}

int get_zip_entries(const char* zip_file, zip_entry_t** entries, size_t* count) {
    if (!zip_file || !entries || !count) {
        return EXIT_INVALID_ARGS;
    }
    
    // Open ZIP archive
    int error;
    zip_t* archive = zip_open(zip_file, ZIP_RDONLY, &error);
    if (!archive) {
        return EXIT_ZIP_ERROR;
    }
    
    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    if (num_entries < 0) {
        zip_close(archive);
        return EXIT_ZIP_ERROR;
    }
    
    *entries = malloc(sizeof(zip_entry_t) * num_entries);
    if (!*entries) {
        zip_close(archive);
        return EXIT_FAILURE;
    }
    
    *count = 0;
    for (zip_uint64_t i = 0; i < (zip_uint64_t)num_entries; i++) {
        const char* name = zip_get_name(archive, i, 0);
        if (!name) continue;
        
        zip_stat_t stat;
        if (zip_stat_index(archive, i, 0, &stat) < 0) continue;
        
        zip_entry_t* entry = &(*entries)[*count];
        strncpy(entry->name, name, PATH_MAX - 1);
        entry->name[PATH_MAX - 1] = '\0';
        entry->mtime = stat.mtime;
        entry->size = stat.size;
        
        // Check if it's a directory
        size_t name_len = strlen(name);
        entry->is_directory = (name_len > 0 && name[name_len - 1] == '/');
        
        (*count)++;
    }
    
    zip_close(archive);
    return EXIT_SUCCESS;
}

void free_zip_entries(zip_entry_t* entries, size_t count) {
    (void)count; // Suppress unused parameter warning
    free(entries);
}