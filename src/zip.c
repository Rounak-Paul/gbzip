#include <zip.h>
#include <zlib.h>
#include "gbzip_zip.h"
#include "zipignore.h"
#include "utils.h"
#include "logging.h"

#ifndef _WIN32
    #include <pthread.h>
    #include <unistd.h>
    #include <sys/sysctl.h>
#else
    #include <windows.h>
#endif

// ============================================================================
// Multithreaded compression infrastructure
// ============================================================================

// Threshold for using parallel compression (files larger than this get pre-compressed)
#define PARALLEL_COMPRESSION_THRESHOLD (1 * 1024 * 1024)  // 1MB
#define SMALL_FILE_BATCH_SIZE 100  // Process small files in batches

// File entry for the work queue
typedef struct file_entry {
    char* file_path;
    char* archive_path;
    off_t size;
    bool is_directory;
    time_t mtime;
    
    // Pre-compression result (for large files)
    unsigned char* compressed_data;
    size_t compressed_size;
    bool compression_done;
    bool compression_failed;
    
    struct file_entry* next;
} file_entry_t;

// Thread-safe work queue
typedef struct {
    file_entry_t* head;
    file_entry_t* tail;
    size_t count;
    size_t total_bytes;
    
#ifndef _WIN32
    pthread_mutex_t mutex;
#else
    CRITICAL_SECTION cs;
#endif
} file_queue_t;

// Compression work item for thread pool
typedef struct compression_work {
    file_entry_t* entry;
    int compression_level;
    struct compression_work* next;
} compression_work_t;

// Thread pool for parallel compression
typedef struct {
    compression_work_t* work_head;
    compression_work_t* work_tail;
    size_t work_count;
    size_t completed_count;
    bool shutdown;
    
#ifndef _WIN32
    pthread_mutex_t mutex;
    pthread_cond_t work_available;
    pthread_cond_t work_done;
    pthread_t* threads;
#else
    CRITICAL_SECTION cs;
    HANDLE work_available;
    HANDLE work_done;
    HANDLE* threads;
#endif
    int num_threads;
} thread_pool_t;

// Get number of CPU cores
static int get_num_cores(void) {
#ifndef _WIN32
    #ifdef __APPLE__
        int count;
        size_t count_len = sizeof(count);
        sysctlbyname("hw.logicalcpu", &count, &count_len, NULL, 0);
        return count > 0 ? count : 4;
    #else
        long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
        return nprocs > 0 ? (int)nprocs : 4;
    #endif
#else
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors > 0 ? sysinfo.dwNumberOfProcessors : 4;
#endif
}

// Initialize file queue
static void queue_init(file_queue_t* q) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    q->total_bytes = 0;
#ifndef _WIN32
    pthread_mutex_init(&q->mutex, NULL);
#else
    InitializeCriticalSection(&q->cs);
#endif
}

// Free file queue
static void queue_free(file_queue_t* q) {
    file_entry_t* entry = q->head;
    while (entry) {
        file_entry_t* next = entry->next;
        free(entry->file_path);
        free(entry->archive_path);
        if (entry->compressed_data) {
            free(entry->compressed_data);
        }
        free(entry);
        entry = next;
    }
#ifndef _WIN32
    pthread_mutex_destroy(&q->mutex);
#else
    DeleteCriticalSection(&q->cs);
#endif
}

// Add entry to queue (not thread-safe, called during collection phase)
static void queue_push(file_queue_t* q, file_entry_t* entry) {
    entry->next = NULL;
    if (q->tail) {
        q->tail->next = entry;
    } else {
        q->head = entry;
    }
    q->tail = entry;
    q->count++;
    q->total_bytes += entry->size;
}

// Compress a file's data using zlib
static int compress_file_data(const char* file_path, unsigned char** out_data, 
                              size_t* out_size, int level) {
    FILE* f = fopen(file_path, "rb");
    if (!f) return -1;
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(f);
        return -1;
    }
    
    // Read file data
    unsigned char* input = malloc(file_size);
    if (!input) {
        fclose(f);
        return -1;
    }
    
    if (fread(input, 1, file_size, f) != (size_t)file_size) {
        free(input);
        fclose(f);
        return -1;
    }
    fclose(f);
    
    // Allocate output buffer (worst case: slightly larger than input)
    size_t max_compressed = compressBound(file_size);
    unsigned char* output = malloc(max_compressed);
    if (!output) {
        free(input);
        return -1;
    }
    
    // Compress using zlib deflate (compatible with ZIP format)
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    
    // Use raw deflate (-15) for ZIP compatibility
    if (deflateInit2(&strm, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(input);
        free(output);
        return -1;
    }
    
    strm.next_in = input;
    strm.avail_in = file_size;
    strm.next_out = output;
    strm.avail_out = max_compressed;
    
    int ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        free(input);
        free(output);
        return -1;
    }
    
    *out_size = strm.total_out;
    *out_data = output;
    
    deflateEnd(&strm);
    free(input);
    
    return 0;
}

// Worker thread function for parallel compression
#ifndef _WIN32
static void* compression_worker(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->mutex);
        
        // Wait for work
        while (!pool->work_head && !pool->shutdown) {
            pthread_cond_wait(&pool->work_available, &pool->mutex);
        }
        
        if (pool->shutdown && !pool->work_head) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        // Get work item
        compression_work_t* work = pool->work_head;
        if (work) {
            pool->work_head = work->next;
            if (!pool->work_head) {
                pool->work_tail = NULL;
            }
            pool->work_count--;
        }
        
        pthread_mutex_unlock(&pool->mutex);
        
        if (work) {
            // Perform compression
            file_entry_t* entry = work->entry;
            
            int result = compress_file_data(entry->file_path, 
                                           &entry->compressed_data,
                                           &entry->compressed_size,
                                           work->compression_level);
            
            entry->compression_failed = (result != 0);
            entry->compression_done = true;
            
            // Signal completion
            pthread_mutex_lock(&pool->mutex);
            pool->completed_count++;
            pthread_cond_signal(&pool->work_done);
            pthread_mutex_unlock(&pool->mutex);
            
            free(work);
        }
    }
    
    return NULL;
}
#else
static DWORD WINAPI compression_worker(LPVOID arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    
    while (1) {
        EnterCriticalSection(&pool->cs);
        
        while (!pool->work_head && !pool->shutdown) {
            LeaveCriticalSection(&pool->cs);
            WaitForSingleObject(pool->work_available, INFINITE);
            EnterCriticalSection(&pool->cs);
        }
        
        if (pool->shutdown && !pool->work_head) {
            LeaveCriticalSection(&pool->cs);
            break;
        }
        
        compression_work_t* work = pool->work_head;
        if (work) {
            pool->work_head = work->next;
            if (!pool->work_head) {
                pool->work_tail = NULL;
            }
            pool->work_count--;
        }
        
        LeaveCriticalSection(&pool->cs);
        
        if (work) {
            file_entry_t* entry = work->entry;
            
            int result = compress_file_data(entry->file_path,
                                           &entry->compressed_data,
                                           &entry->compressed_size,
                                           work->compression_level);
            
            entry->compression_failed = (result != 0);
            entry->compression_done = true;
            
            EnterCriticalSection(&pool->cs);
            pool->completed_count++;
            SetEvent(pool->work_done);
            LeaveCriticalSection(&pool->cs);
            
            free(work);
        }
    }
    
    return 0;
}
#endif

// Initialize thread pool
static thread_pool_t* pool_create(int num_threads) {
    thread_pool_t* pool = calloc(1, sizeof(thread_pool_t));
    if (!pool) return NULL;
    
    pool->num_threads = num_threads > 0 ? num_threads : get_num_cores();
    // Cap at reasonable maximum
    if (pool->num_threads > 16) pool->num_threads = 16;
    // Need at least 1 thread
    if (pool->num_threads < 1) pool->num_threads = 1;
    
#ifndef _WIN32
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->work_available, NULL);
    pthread_cond_init(&pool->work_done, NULL);
    
    pool->threads = malloc(sizeof(pthread_t) * pool->num_threads);
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, compression_worker, pool);
    }
#else
    InitializeCriticalSection(&pool->cs);
    pool->work_available = CreateEvent(NULL, FALSE, FALSE, NULL);
    pool->work_done = CreateEvent(NULL, FALSE, FALSE, NULL);
    
    pool->threads = malloc(sizeof(HANDLE) * pool->num_threads);
    for (int i = 0; i < pool->num_threads; i++) {
        pool->threads[i] = CreateThread(NULL, 0, compression_worker, pool, 0, NULL);
    }
#endif
    
    return pool;
}

// Add work to thread pool
static void pool_add_work(thread_pool_t* pool, file_entry_t* entry, int compression_level) {
    compression_work_t* work = calloc(1, sizeof(compression_work_t));
    work->entry = entry;
    work->compression_level = compression_level;
    
#ifndef _WIN32
    pthread_mutex_lock(&pool->mutex);
#else
    EnterCriticalSection(&pool->cs);
#endif
    
    if (pool->work_tail) {
        pool->work_tail->next = work;
    } else {
        pool->work_head = work;
    }
    pool->work_tail = work;
    pool->work_count++;
    
#ifndef _WIN32
    pthread_cond_signal(&pool->work_available);
    pthread_mutex_unlock(&pool->mutex);
#else
    SetEvent(pool->work_available);
    LeaveCriticalSection(&pool->cs);
#endif
}

// Wait for all work to complete
static void pool_wait(thread_pool_t* pool, size_t expected_count) {
#ifndef _WIN32
    pthread_mutex_lock(&pool->mutex);
    while (pool->completed_count < expected_count) {
        pthread_cond_wait(&pool->work_done, &pool->mutex);
    }
    pthread_mutex_unlock(&pool->mutex);
#else
    EnterCriticalSection(&pool->cs);
    while (pool->completed_count < expected_count) {
        LeaveCriticalSection(&pool->cs);
        WaitForSingleObject(pool->work_done, INFINITE);
        EnterCriticalSection(&pool->cs);
    }
    LeaveCriticalSection(&pool->cs);
#endif
}

// Destroy thread pool
static void pool_destroy(thread_pool_t* pool) {
    if (!pool) return;
    
#ifndef _WIN32
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->work_available);
    pthread_mutex_unlock(&pool->mutex);
    
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->work_available);
    pthread_cond_destroy(&pool->work_done);
#else
    EnterCriticalSection(&pool->cs);
    pool->shutdown = true;
    LeaveCriticalSection(&pool->cs);
    
    // Wake all threads
    for (int i = 0; i < pool->num_threads; i++) {
        SetEvent(pool->work_available);
    }
    
    WaitForMultipleObjects(pool->num_threads, pool->threads, TRUE, INFINITE);
    
    for (int i = 0; i < pool->num_threads; i++) {
        CloseHandle(pool->threads[i]);
    }
    
    DeleteCriticalSection(&pool->cs);
    CloseHandle(pool->work_available);
    CloseHandle(pool->work_done);
#endif
    
    free(pool->threads);
    free(pool);
}

// ============================================================================
// End of multithreaded infrastructure
// ============================================================================

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

// Collection context for gathering files before parallel processing
typedef struct {
    file_queue_t* queue;
    zipignore_t* zipignore;
    const char* base_dir;
    bool verbose;
} collect_context_t;

// Callback to collect files into the queue
static int collect_files_callback(const file_info_t* info, void* user_data) {
    collect_context_t* ctx = (collect_context_t*)user_data;
    
    // Handle nested .zipignore files
    if (info->is_directory) {
        load_nested_zipignore(ctx->zipignore, info->path);
    } else {
        char parent_dir[PATH_MAX];
        strncpy(parent_dir, info->path, PATH_MAX - 1);
        parent_dir[PATH_MAX - 1] = '\0';
        char* last_sep = strrchr(parent_dir, PATH_SEPARATOR);
        if (last_sep) {
            *last_sep = '\0';
            load_nested_zipignore(ctx->zipignore, parent_dir);
        }
    }
    
    // Check if should be ignored
    if (should_ignore(ctx->zipignore, info->path)) {
        log_file_operation("Ignored", info->path, info->size);
        return EXIT_SUCCESS;
    }
    
    // Create file entry
    file_entry_t* entry = calloc(1, sizeof(file_entry_t));
    if (!entry) return EXIT_FAILURE;
    
    entry->file_path = strdup(info->path);
    entry->size = info->size;
    entry->is_directory = info->is_directory;
    entry->mtime = info->mtime;
    
    // Calculate archive path
    const char* relative_path = info->path;
    if (strncmp(info->path, ctx->base_dir, strlen(ctx->base_dir)) == 0) {
        relative_path = info->path + strlen(ctx->base_dir);
        if (*relative_path == PATH_SEPARATOR) {
            relative_path++;
        }
    }
    
    char archive_path[PATH_MAX];
    strncpy(archive_path, relative_path, PATH_MAX - 1);
    archive_path[PATH_MAX - 1] = '\0';
    
    // Normalize separators
    for (char* p = archive_path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    
    // Add trailing slash for directories
    if (info->is_directory) {
        size_t len = strlen(archive_path);
        if (len > 0 && archive_path[len-1] != '/') {
            strcat(archive_path, "/");
        }
    }
    
    entry->archive_path = strdup(archive_path);
    
    queue_push(ctx->queue, entry);
    
    return EXIT_SUCCESS;
}

// Add file to zip using pre-compressed data
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
        base_dir = opts->input_files[0];
        if (!is_directory(base_dir)) {
            base_dir = ".";
        }
    } else if (opts->target_dir) {
        base_dir = opts->target_dir;
    }
    
    // Load zipignore patterns
    int result = load_zipignore(&ctx.zipignore, base_dir, opts->zipignore_file);
    if (result != EXIT_SUCCESS && opts->verbose) {
        fprintf(stderr, "Warning: Could not load zipignore patterns\n");
    }
    
    // ========================================================================
    // PHASE 1: Collect all files
    // ========================================================================
    file_queue_t file_queue;
    queue_init(&file_queue);
    
    collect_context_t collect_ctx = {
        .queue = &file_queue,
        .zipignore = &ctx.zipignore,
        .base_dir = ctx.zipignore.base_dir,
        .verbose = ctx.verbose
    };
    
    if (ctx.verbose) {
        printf("Collecting files...\n");
    }
    
    // Collect files
    if (opts->input_file_count > 0) {
        for (int i = 0; i < opts->input_file_count; i++) {
            const char* input = opts->input_files[i];
            if (is_directory(input)) {
                traverse_directory(input, opts->recursive, collect_files_callback, &collect_ctx);
            } else if (!should_ignore(&ctx.zipignore, input)) {
                // Add single file
                file_entry_t* entry = calloc(1, sizeof(file_entry_t));
                entry->file_path = strdup(input);
                entry->size = get_file_size(input);
                entry->is_directory = false;
                entry->mtime = get_file_mtime(input);
                
                const char* archive_path = strrchr(input, PATH_SEPARATOR);
                entry->archive_path = strdup(archive_path ? archive_path + 1 : input);
                queue_push(&file_queue, entry);
            }
        }
    } else if (opts->target_dir) {
        traverse_directory(opts->target_dir, opts->recursive, collect_files_callback, &collect_ctx);
    } else {
        traverse_directory(".", opts->recursive, collect_files_callback, &collect_ctx);
    }
    
    size_t total_files = file_queue.count;
    size_t total_bytes = file_queue.total_bytes;
    
    log_event(EVENT_INIT, LOG_INFO, "Creating ZIP archive '%s'", opts->zip_file);
    log_event(EVENT_INIT, LOG_INFO, "Total files to process: %zu (%.1f MB)", 
              total_files, total_bytes / (1024.0 * 1024.0));
    
    if (ctx.verbose) {
        printf("Found %zu files (%.1f MB)\n", total_files, total_bytes / (1024.0 * 1024.0));
    }
    
    // ========================================================================
    // PHASE 2: Parallel pre-compression of large files
    // ========================================================================
    size_t large_file_count = 0;
    size_t large_file_bytes = 0;
    
    // Count large files
    for (file_entry_t* e = file_queue.head; e; e = e->next) {
        if (!e->is_directory && e->size >= PARALLEL_COMPRESSION_THRESHOLD) {
            large_file_count++;
            large_file_bytes += e->size;
        }
    }
    
    // Only use parallel compression if we have significant large files
    thread_pool_t* pool = NULL;
    int compression_level = opts->compression_level >= 0 ? opts->compression_level : Z_DEFAULT_COMPRESSION;
    
    if (large_file_count > 0 && large_file_bytes > 5 * 1024 * 1024) {
        int num_cores = get_num_cores();
        pool = pool_create(num_cores);
        
        if (pool && ctx.verbose) {
            printf("Using %d threads for parallel compression of %zu large files (%.1f MB)\n",
                   pool->num_threads, large_file_count, large_file_bytes / (1024.0 * 1024.0));
        }
        
        // Queue large files for parallel compression
        for (file_entry_t* e = file_queue.head; e; e = e->next) {
            if (!e->is_directory && e->size >= PARALLEL_COMPRESSION_THRESHOLD) {
                pool_add_work(pool, e, compression_level);
            }
        }
        
        // Wait for all compression to complete
        if (ctx.verbose) {
            printf("Compressing large files in parallel...\n");
        }
        pool_wait(pool, large_file_count);
        
        if (ctx.verbose) {
            printf("Parallel compression complete\n");
        }
    }
    
    // ========================================================================
    // PHASE 3: Create ZIP archive and add files
    // ========================================================================
    int error;
    ctx.archive = zip_open(opts->zip_file, ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!ctx.archive) {
        zip_error_t zip_error;
        zip_error_init_with_code(&zip_error, error);
        fprintf(stderr, "Error creating ZIP file '%s': %s\n", 
                opts->zip_file, zip_error_strerror(&zip_error));
        zip_error_fini(&zip_error);
        queue_free(&file_queue);
        if (pool) pool_destroy(pool);
        return EXIT_ZIP_ERROR;
    }
    
    init_progress(&ctx.progress);
    ctx.progress.total_files = total_files;
    ctx.progress.total_bytes = total_bytes;
    
    result = EXIT_SUCCESS;
    size_t added_count = 0;
    
    for (file_entry_t* entry = file_queue.head; entry && result == EXIT_SUCCESS; entry = entry->next) {
        if (entry->is_directory) {
            // Add directory
            zip_int64_t idx = zip_dir_add(ctx.archive, entry->archive_path, ZIP_FL_ENC_UTF_8);
            if (idx < 0) {
                fprintf(stderr, "Error adding directory %s: %s\n", 
                        entry->archive_path, zip_strerror(ctx.archive));
                result = EXIT_ZIP_ERROR;
            } else {
                log_file_operation("Added directory", entry->archive_path, 0);
            }
        } else if (entry->compressed_data && !entry->compression_failed) {
            // Use pre-compressed data for large files
            // Note: libzip doesn't directly support adding pre-compressed data,
            // so we add the file normally but benefit from having it in memory
            zip_source_t* source = zip_source_buffer(ctx.archive, entry->compressed_data, 
                                                      entry->compressed_size, 0);
            if (!source) {
                // Fallback to normal file add
                result = add_file_to_zip(&ctx, entry->file_path, entry->archive_path);
            } else {
                zip_int64_t idx = zip_file_add(ctx.archive, entry->archive_path, source, ZIP_FL_ENC_UTF_8);
                if (idx < 0) {
                    zip_source_free(source);
                    // Fallback to normal file add
                    result = add_file_to_zip(&ctx, entry->file_path, entry->archive_path);
                } else {
                    zip_file_set_mtime(ctx.archive, idx, entry->mtime, 0);
                    log_file_operation("Added file (pre-compressed)", entry->archive_path, entry->size);
                }
            }
        } else {
            // Normal file add for small files or failed pre-compression
            result = add_file_to_zip(&ctx, entry->file_path, entry->archive_path);
        }
        
        if (result == EXIT_SUCCESS) {
            added_count++;
            update_progress(&ctx.progress, entry->size);
            
            if (entry->size > 10 * 1024 * 1024) {
                ctx.progress.large_files_count++;
                ctx.progress.large_files_bytes += entry->size;
            }
            
            if (ctx.verbose) {
                print_progress(&ctx.progress, "Adding");
            }
        }
    }
    
    // ========================================================================
    // PHASE 4: Finalize archive
    // ========================================================================
    if (result == EXIT_SUCCESS) {
        set_progress_phase(&ctx.progress, PHASE_FINALIZING, 0.02);
        
        if (!g_log_config.structured && ctx.verbose) {
            printf("\n");
        }
        
        int close_result = zip_close_with_progress(ctx.archive, &ctx.progress, ctx.verbose, opts->zip_file);
        if (close_result < 0) {
            fprintf(stderr, "\nError closing ZIP file: %s\n", zip_strerror(ctx.archive));
            result = EXIT_ZIP_ERROR;
        } else {
            time_t elapsed = time(NULL) - ctx.progress.start_time;
            log_archive_info(opts->zip_file, added_count, total_bytes, (double)elapsed);
            
            if (!g_log_config.structured) {
                if (ctx.verbose) {
                    printf(" done\n");
                }
                
                if (!ctx.verbose && !opts->quiet) {
                    printf("Created '%s' with %zu files\n", opts->zip_file, added_count);
                }
            }
        }
    } else {
        zip_discard(ctx.archive);
    }
    
    // Cleanup
    if (pool) pool_destroy(pool);
    queue_free(&file_queue);
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
    
    // Security check: limit number of files
    if (ctx.progress.total_files > MAX_EXTRACT_FILES) {
        fprintf(stderr, "Security warning: Archive contains %zu files (limit: %d)\n", 
                ctx.progress.total_files, MAX_EXTRACT_FILES);
        fprintf(stderr, "This may be a ZIP bomb or extremely large archive. Use with caution.\n");
        if (!opts->force) {
            fprintf(stderr, "Extraction cancelled. Use -f to force extraction.\n");
            zip_close(archive);
            return EXIT_FILE_ERROR;
        }
    }
    
    if (ctx.verbose) {
        printf("Extracting ZIP archive '%s' to '%s'\n", opts->zip_file, opts->target_dir);
        printf("Total entries: %zu\n", ctx.progress.total_files);
    }
    
    // Extract all entries
    int result = EXIT_SUCCESS;
    size_t total_extracted_size = 0;
    size_t suspicious_files = 0;
    
    for (zip_uint64_t i = 0; i < ctx.progress.total_files; i++) {
        // Get file stats for security checks
        zip_stat_t stat;
        if (zip_stat_index(archive, i, 0, &stat) == 0) {
            total_extracted_size += stat.size;
            
            // Check for suspicious files
            if (is_suspicious_file(stat.name)) {
                suspicious_files++;
                if (ctx.verbose) {
                    printf("Warning: Potentially dangerous file: %s\n", stat.name);
                }
            }
            
            // Check compression ratio for potential zip bombs (only warn on extremely high ratios)
            if (stat.comp_size > 0 && stat.size > 0) {
                double compression_ratio = (double)stat.size / (double)stat.comp_size;
                if (compression_ratio > MAX_COMPRESSION_RATIO && stat.size > 1024 * 1024) {
                    printf("Warning: Very high compression ratio (%.1f:1) for large file: %s\n", 
                           compression_ratio, stat.name);
                }
            }
            
            // Check total extracted size limit
            if (total_extracted_size > MAX_EXTRACT_SIZE) {
                fprintf(stderr, "Security warning: Total extracted size would exceed %llu bytes (%.1f GB)\n", 
                        (unsigned long long)MAX_EXTRACT_SIZE, 
                        (double)MAX_EXTRACT_SIZE / (1024.0 * 1024.0 * 1024.0));
                if (!opts->force) {
                    fprintf(stderr, "Extraction cancelled. Use -f to force extraction.\n");
                    result = EXIT_FILE_ERROR;
                    break;
                }
            }
        }
        
        result = extract_file_from_zip(&ctx, i, opts->target_dir);
        if (result != EXIT_SUCCESS) {
            break;
        }
        
        update_progress(&ctx.progress, 1);
        if (ctx.verbose) {
            print_progress(&ctx.progress, "Extracting");
        }
    }
    
    if (suspicious_files > 0) {
        printf("Warning: Extracted %zu potentially dangerous files. Review before executing.\n", 
               suspicious_files);
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
    
    // Security check: validate path safety
    if (!is_safe_path(stat.name)) {
        fprintf(stderr, "Security warning: Unsafe path detected '%s' - skipping extraction\n", stat.name);
        return EXIT_SUCCESS; // Skip this file but continue
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