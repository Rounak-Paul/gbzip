#ifndef GBZIP_H
#define GBZIP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlwapi.h>
    #include <io.h>
    #include <direct.h>
    #define PATH_SEPARATOR '\\'
    #define PATH_MAX MAX_PATH
    #ifndef S_ISREG
        #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
    #endif
    #ifndef S_ISDIR
        #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
    #endif
    typedef __int64 off_t;
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <fnmatch.h>
    #define PATH_SEPARATOR '/'
    #ifndef PATH_MAX
        #define PATH_MAX 4096
    #endif
#endif

// Version information
#define GBZIP_VERSION_MAJOR 1
#define GBZIP_VERSION_MINOR 0
#define GBZIP_VERSION_PATCH 0
#define GBZIP_VERSION "1.0.0"

// Default zipignore filename
#define ZIPIGNORE_FILENAME ".zipignore"
#define DEFAULT_ZIPIGNORE_PATH "~/.zipignore"

// Exit codes
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EXIT_INVALID_ARGS 2
#define EXIT_FILE_ERROR 3
#define EXIT_ZIP_ERROR 4

// Operation modes
typedef enum {
    OP_CREATE,
    OP_EXTRACT,
    OP_LIST,
    OP_DIFF,
    OP_HELP,
    OP_VERSION
} operation_t;

// Program options
typedef struct {
    operation_t operation;
    char* zip_file;
    char* target_dir;
    char* zipignore_file;
    char** input_files;
    int input_file_count;
    bool verbose;
    bool quiet;
    bool recursive;
    bool force;
    bool junk_paths;
    bool store_only;
    bool compress_better;
    bool update_mode;
    bool test_mode;
    bool timestamp_mode;
    bool delete_mode;
    bool move_mode;
    bool read_stdin;
    bool diff_mode;
    bool create_default_zipignore;
    int compression_level;
} options_t;

// Progress reporting
typedef struct {
    size_t total_files;
    size_t processed_files;
    size_t total_bytes;
    size_t processed_bytes;
    time_t start_time;
} progress_t;

// Function prototypes
void print_usage(const char* program_name);
void print_version(void);
int parse_arguments(int argc, char* argv[], options_t* opts);

#endif // GBZIP_H