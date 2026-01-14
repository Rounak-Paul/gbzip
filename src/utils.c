#include "utils.h"
#include "logging.h"
#include "tui.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

bool file_exists(const char* path) {
    if (!path) return false;
    
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    return (stat(path, &st) == 0);
#endif
}

bool is_directory(const char* path) {
    if (!path) return false;
    
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

bool is_regular_file(const char* path) {
    if (!path) return false;
    
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
#endif
}

int create_directory_recursive(const char* path) {
    if (!path) return EXIT_FAILURE;
    
    char* path_copy = malloc(strlen(path) + 1);
    if (!path_copy) return EXIT_FAILURE;
    
    strcpy(path_copy, path);
    
    char* p = path_copy;
    
    // Skip root slash on Unix or drive letter on Windows
#ifdef _WIN32
    if (strlen(p) >= 2 && p[1] == ':') {
        p += 2;
    }
#endif
    if (*p == PATH_SEPARATOR) {
        p++;
    }
    
    for (char* slash = strchr(p, PATH_SEPARATOR); slash; slash = strchr(p, PATH_SEPARATOR)) {
        *slash = '\0';
        
        if (!file_exists(path_copy)) {
#ifdef _WIN32
            if (!CreateDirectoryA(path_copy, NULL)) {
                free(path_copy);
                return EXIT_FAILURE;
            }
#else
            if (mkdir(path_copy, 0755) != 0) {
                free(path_copy);
                return EXIT_FAILURE;
            }
#endif
        }
        
        *slash = PATH_SEPARATOR;
        p = slash + 1;
    }
    
    // Create the final directory
    if (!file_exists(path_copy)) {
#ifdef _WIN32
        if (!CreateDirectoryA(path_copy, NULL)) {
            free(path_copy);
            return EXIT_FAILURE;
        }
#else
        if (mkdir(path_copy, 0755) != 0) {
            free(path_copy);
            return EXIT_FAILURE;
        }
#endif
    }
    
    free(path_copy);
    return EXIT_SUCCESS;
}

char* get_home_directory(void) {
#ifdef _WIN32
    char* home = getenv("USERPROFILE");
    if (!home) {
        home = getenv("HOMEDRIVE");
        char* homepath = getenv("HOMEPATH");
        if (home && homepath) {
            char* full_home = malloc(strlen(home) + strlen(homepath) + 1);
            if (full_home) {
                strcpy(full_home, home);
                strcat(full_home, homepath);
                return full_home;
            }
        }
        return NULL;
    }
    char* result = malloc(strlen(home) + 1);
    if (result) {
        strcpy(result, home);
    }
    return result;
#else
    char* home = getenv("HOME");
    if (!home) return NULL;
    
    char* result = malloc(strlen(home) + 1);
    if (result) {
        strcpy(result, home);
    }
    return result;
#endif
}

char* get_absolute_path(const char* path) {
    if (!path) return NULL;
    
#ifdef _WIN32
    char* abs_path = malloc(PATH_MAX);
    if (!abs_path) return NULL;
    
    if (!GetFullPathNameA(path, PATH_MAX, abs_path, NULL)) {
        free(abs_path);
        return NULL;
    }
    return abs_path;
#else
    return realpath(path, NULL);
#endif
}

time_t get_file_mtime(const char* path) {
    if (!path) return 0;
    
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attrs)) {
        return 0;
    }
    
    FILETIME ft = attrs.ftLastWriteTime;
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    
    return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return st.st_mtime;
#endif
}

off_t get_file_size(const char* path) {
    if (!path) return -1;
    
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attrs)) {
        return -1;
    }
    
    LARGE_INTEGER size;
    size.LowPart = attrs.nFileSizeLow;
    size.HighPart = attrs.nFileSizeHigh;
    return size.QuadPart;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return st.st_size;
#endif
}

char* trim_whitespace(char* str) {
    if (!str) return NULL;
    
    // Trim leading whitespace
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
        str++;
    }
    
    if (*str == '\0') return str;
    
    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    *(end + 1) = '\0';
    
    return str;
}

char* join_path(const char* dir, const char* file) {
    if (!dir || !file) return NULL;
    
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    size_t total_len = dir_len + file_len + 2; // +1 for separator, +1 for null terminator
    
    char* result = malloc(total_len);
    if (!result) return NULL;
    
    strcpy(result, dir);
    
    // Add separator if needed
    if (dir_len > 0 && dir[dir_len - 1] != PATH_SEPARATOR) {
        result[dir_len] = PATH_SEPARATOR;
        result[dir_len + 1] = '\0';
    }
    
    strcat(result, file);
    return result;
}

const char* get_filename(const char* path) {
    if (!path) return NULL;
    
    const char* filename = strrchr(path, PATH_SEPARATOR);
    if (filename) {
        return filename + 1;
    }
    
    // Also check for the other separator on Windows
#ifdef _WIN32
    filename = strrchr(path, '/');
    if (filename) {
        return filename + 1;
    }
#endif
    
    return path;
}

const char* get_file_extension(const char* path) {
    if (!path) return NULL;
    
    const char* filename = get_filename(path);
    const char* ext = strrchr(filename, '.');
    
    return ext ? ext + 1 : "";
}

bool is_safe_path(const char* path) {
    if (!path) return false;
    
    // Reject paths containing ".."
    if (strstr(path, "..") != NULL) return false;
    
    // Reject absolute paths on Unix
#ifndef _WIN32
    if (path[0] == '/') return false;
#else
    // Reject absolute paths on Windows (drive letters)
    if (strlen(path) > 1 && path[1] == ':') return false;
    // Reject UNC paths
    if (strlen(path) > 1 && path[0] == '\\' && path[1] == '\\') return false;
#endif
    
    // Reject paths that are too long
    if (strlen(path) >= PATH_MAX) return false;
    
    return true;
}

bool is_suspicious_file(const char* filename) {
    if (!filename) return false;
    
    const char* ext = get_file_extension(filename);
    if (!ext) return false;
    
    // List of potentially dangerous file extensions
    const char* dangerous_exts[] = {
        "exe", "com", "bat", "cmd", "pif", "scr", "vbs", "js", "jar",
        "app", "deb", "pkg", "dmg", "run", "msi", "dll", "so", "dylib",
        NULL
    };
    
    for (int i = 0; dangerous_exts[i]; i++) {
        if (strcasecmp(ext, dangerous_exts[i]) == 0) {
            return true;
        }
    }
    
    return false;
}

int traverse_directory(const char* dir_path, bool recursive, file_callback_t callback, void* user_data) {
    if (!dir_path || !callback) {
        return EXIT_FAILURE;
    }
    
#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    
    char search_path[PATH_MAX];
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);
    
    hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return EXIT_FAILURE;
    }
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char* full_path = join_path(dir_path, find_data.cFileName);
        if (!full_path) continue;
        
        file_info_t info;
        strncpy(info.path, full_path, PATH_MAX - 1);
        info.path[PATH_MAX - 1] = '\0';
        info.is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        
        // Convert FILETIME to time_t
        FILETIME ft = find_data.ftLastWriteTime;
        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        info.mtime = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
        
        if (info.is_directory) {
            info.size = 0;
        } else {
            LARGE_INTEGER size;
            size.LowPart = find_data.nFileSizeLow;
            size.HighPart = find_data.nFileSizeHigh;
            info.size = size.QuadPart;
        }
        
        int result = callback(&info, user_data);
        if (result != EXIT_SUCCESS) {
            free(full_path);
            FindClose(hFind);
            return result;
        }
        
        if (info.is_directory && recursive) {
            result = traverse_directory(full_path, recursive, callback, user_data);
            if (result != EXIT_SUCCESS) {
                free(full_path);
                FindClose(hFind);
                return result;
            }
        }
        
        free(full_path);
    } while (FindNextFileA(hFind, &find_data));
    
    FindClose(hFind);
#else
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return EXIT_FAILURE;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char* full_path = join_path(dir_path, entry->d_name);
        if (!full_path) continue;
        
        file_info_t info;
        strncpy(info.path, full_path, PATH_MAX - 1);
        info.path[PATH_MAX - 1] = '\0';
        info.is_directory = is_directory(full_path);
        info.mtime = get_file_mtime(full_path);
        info.size = info.is_directory ? 0 : get_file_size(full_path);
        
        int result = callback(&info, user_data);
        if (result != EXIT_SUCCESS) {
            free(full_path);
            closedir(dir);
            return result;
        }
        
        if (info.is_directory && recursive) {
            result = traverse_directory(full_path, recursive, callback, user_data);
            if (result != EXIT_SUCCESS) {
                free(full_path);
                closedir(dir);
                return result;
            }
        }
        
        free(full_path);
    }
    
    closedir(dir);
#endif
    
    return EXIT_SUCCESS;
}

void init_progress(progress_t* progress) {
    if (!progress) return;
    
    memset(progress, 0, sizeof(progress_t));
    progress->start_time = time(NULL);
    progress->phase = PHASE_ADDING_FILES;
    progress->phase_weight = 0.02;
}

void update_progress(progress_t* progress, size_t bytes_processed) {
    if (!progress) return;
    
    progress->processed_files++;
    progress->processed_bytes += bytes_processed;
}

void set_progress_phase(progress_t* progress, progress_phase_t phase, double weight) {
    if (!progress) return;
    
    progress->phase = phase;
    progress->phase_weight = weight;
}

void print_progress(const progress_t* progress, const char* operation) {
    if (!progress || !operation) return;
    
    time_t elapsed = time(NULL) - progress->start_time;
    if (elapsed == 0) elapsed = 1;
    
    double percent = 0.0;
    if (progress->total_files > 0) {
        if (progress->phase == PHASE_ADDING_FILES) {
            double file_percent = (double)progress->processed_files / progress->total_files;
            percent = file_percent * progress->phase_weight * 100.0;
        } else {
            percent = progress->phase_weight * 100.0;
        }
    }
    
    double speed = (double)progress->processed_bytes / elapsed;
    const char* units = "B/s";
    
    if (speed > 1024) {
        speed /= 1024;
        units = "KB/s";
        if (speed > 1024) {
            speed /= 1024;
            units = "MB/s";
        }
    }
    
    const char* phase_name = (progress->phase == PHASE_ADDING_FILES) ? "adding_files" : "finalizing";
    log_progress(progress, phase_name, percent, speed, units);
    fflush(stdout);
}

void print_finalization_progress(const progress_t* progress, const char* message) {
    if (!progress || !message) return;
    
    time_t elapsed = time(NULL) - progress->start_time;
    if (elapsed == 0) elapsed = 1; // Avoid division by zero
    
    double speed = (double)progress->processed_bytes / elapsed;
    const char* units = "B/s";
    
    if (speed > 1024) {
        speed /= 1024;
        units = "KB/s";
        if (speed > 1024) {
            speed /= 1024;
            units = "MB/s";
        }
    }
    
    printf("\r%s (%.1f %s)...", message, speed, units);
    fflush(stdout);
}

void print_compression_progress(const progress_t* progress, int step) {
    if (!progress) return;
    
    time_t current_time = time(NULL);
    time_t elapsed = current_time - progress->start_time;
    if (elapsed == 0) elapsed = 1;
    
    double speed = (double)progress->processed_bytes / elapsed;
    const char* units = "B/s";
    double speed_display = speed;
    
    if (speed_display > 1024) {
        speed_display /= 1024;
        units = "KB/s";
        if (speed_display > 1024) {
            speed_display /= 1024;
            units = "MB/s";
        }
    }
    
    // Simple animation to show progress
    const char* spinner = "|/-\\";
    char animation = spinner[step % 4];
    
    // More realistic progress estimation during compression
    double estimated_progress = 90.0; // Start at 90% (end of file adding phase)
    
    if (progress->large_files_bytes > 0) {
        // Estimate based on compression time for large files
        // Assume roughly 30-50 MB/s compression speed for large files
        double estimated_compression_time = (double)progress->large_files_bytes / (40.0 * 1024.0 * 1024.0); // 40 MB/s average
        if (estimated_compression_time < 5.0) estimated_compression_time = 5.0; // Minimum 5 seconds
        
        double compression_progress = (double)elapsed / estimated_compression_time;
        if (compression_progress > 1.0) compression_progress = 1.0;
        
        // Progress from 2% to 99.5% based on estimated compression time
        estimated_progress = 2.0 + (compression_progress * 97.5);
    } else {
        // For smaller files, more linear progress over time
        double time_factor = (double)elapsed / 30.0; // Assume 30 seconds max for small files
        if (time_factor > 1.0) time_factor = 1.0;
        estimated_progress = 2.0 + (time_factor * 97.5);
    }
    
    if (estimated_progress > 99.5) estimated_progress = 99.5; // Don't show 100% until done
    
    // Update TUI if active
    if (g_tui.is_active) {
        tui_update_compression(estimated_progress, speed);
        tui_refresh();
        return;
    }
    
    // Use structured logging for compression progress, but maintain animation for traditional output
    if (g_log_config.structured) {
        static progress_t temp_progress = {0};
        temp_progress.start_time = progress->start_time;
        temp_progress.processed_bytes = progress->processed_bytes;
        temp_progress.total_files = progress->total_files;
        temp_progress.processed_files = progress->processed_files;
        log_progress(&temp_progress, "compression", estimated_progress, speed_display, units);
    } else {
        printf("\rCompressing and writing archive %c (%.1f%%) - %.1f %s - %lds elapsed", 
               animation, estimated_progress, speed_display, units, elapsed);
        fflush(stdout);
    }
}