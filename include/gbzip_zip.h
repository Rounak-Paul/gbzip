#ifndef GBZIP_ZIP_H
#define GBZIP_ZIP_H

#include <zip.h>
#include "gbzip.h"
#include "zipignore.h"

// ZIP context structure
typedef struct {
    zip_t* archive;
    char* filename;
    zipignore_t zipignore;
    progress_t progress;
    bool verbose;
} zip_context_t;

// Function prototypes
int create_zip(const options_t* opts);
int extract_zip(const options_t* opts);
int list_zip(const options_t* opts);

// Internal helper functions
int add_file_to_zip(zip_context_t* ctx, const char* file_path, const char* archive_path);
int add_directory_to_zip(zip_context_t* ctx, const char* dir_path, const char* base_path);
int extract_file_from_zip(zip_context_t* ctx, zip_uint64_t index, const char* output_dir);

// ZIP file information
typedef struct {
    char name[PATH_MAX];
    time_t mtime;
    off_t size;
    bool is_directory;
} zip_entry_t;

int get_zip_entries(const char* zip_file, zip_entry_t** entries, size_t* count);
void free_zip_entries(zip_entry_t* entries, size_t count);

#endif // GBZIP_ZIP_H