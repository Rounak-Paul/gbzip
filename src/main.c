#include "gbzip.h"
#include "gbzip_zip.h"
#include "zipignore.h"
#include "diff.h"
#include "utils.h"

void print_usage(const char* program_name) {
    printf("gbzip - Advanced ZIP utility with ignore files and diff support\n\n");
    printf("Usage: %s [-options] zipfile [file...] [-xi list]\n\n", program_name);
    printf("  The default action is to add or replace zipfile entries from list.\n");
    printf("  If zipfile and list are omitted, gbzip compresses stdin to stdout.\n\n");
    printf("Options:\n");
    printf("  -r   recurse into directories (default)     -j   junk (don't record) directory names\n");
    printf("  -0   store only (no compression)            -9   compress better\n");
    printf("  -q   quiet operation                         -v   verbose operation\n");
    printf("  -f   force overwrite existing files         -u   update: only changed or new files\n");
    printf("  -x   extract files from zipfile             -l   list files in zipfile\n");
    printf("  -t   test zipfile integrity                 -T   timestamp archive to latest\n");
    printf("  -d <dir>  extract files into directory     -m   move into zipfile (delete OS files)\n");
    printf("  -i   include only files matching patterns   -@   read names from stdin\n");
    printf("  -I <file>  use custom zipignore file        -Z   create default .zipignore file\n");
    printf("  -D   differential update (timestamp based)  -h   show this help message\n");
    printf("      --version  show version information\n\n");
    printf("Examples:\n");
    printf("  %s archive.zip *.c src/         Create archive from C files and src directory\n", program_name);
    printf("  %s -r archive.zip project/      Create archive recursively from project directory\n", program_name);
    printf("  %s -x archive.zip               Extract archive to current directory\n", program_name);
    printf("  %s -x -d mydir archive.zip      Extract archive to mydir directory\n", program_name);
    printf("  %s -x archive.zip mydir         Alternative: extract to mydir directory\n", program_name);
    printf("  %s -l archive.zip               List contents of archive\n", program_name);
    printf("  %s -D archive.zip project/      Update archive with changes in project\n", program_name);
    printf("  %s -Z                           Create default .zipignore file\n", program_name);
}

void print_version(void) {
    printf("gbzip version %s\n", GBZIP_VERSION);
    printf("Advanced ZIP utility with ignore files and differential archiving\n");
    printf("Built with libzip support for cross-platform compatibility\n");
}

int parse_arguments(int argc, char* argv[], options_t* opts) {
    // Initialize options with defaults
    memset(opts, 0, sizeof(options_t));
    opts->operation = OP_CREATE; // Default operation is create
    opts->recursive = true;
    opts->verbose = false;
    opts->quiet = false;
    opts->force = false;
    opts->junk_paths = false;
    opts->store_only = false;
    opts->compress_better = false;
    opts->update_mode = false;
    opts->test_mode = false;
    opts->timestamp_mode = false;
    opts->delete_mode = false;
    opts->move_mode = false;
    opts->read_stdin = false;
    opts->diff_mode = false;
    opts->create_default_zipignore = false;
    opts->compression_level = 6; // Default compression level
    
    if (argc < 2) {
        opts->operation = OP_HELP;
        return EXIT_SUCCESS;
    }
    
    int arg_index = 1;
    
    // Parse options (zip-style)
    while (arg_index < argc && argv[arg_index][0] == '-') {
        const char* arg = argv[arg_index];
        
        // Handle long options
        if (strcmp(arg, "--version") == 0) {
            opts->operation = OP_VERSION;
            return EXIT_SUCCESS;
        } else if (strcmp(arg, "--help") == 0) {
            opts->operation = OP_HELP;
            return EXIT_SUCCESS;
        }
        
        // Handle combined short options (like -rv)
        for (int i = 1; arg[i] != '\0'; i++) {
            switch (arg[i]) {
                case 'r':
                    opts->recursive = true;
                    break;
                case 'v':
                    opts->verbose = true;
                    break;
                case 'q':
                    opts->quiet = true;
                    opts->verbose = false;
                    break;
                case 'f':
                    opts->force = true;
                    break;
                case 'j':
                    opts->junk_paths = true;
                    break;
                case '0':
                    opts->store_only = true;
                    opts->compression_level = 0;
                    break;
                case '9':
                    opts->compress_better = true;
                    opts->compression_level = 9;
                    break;
                case 'x':
                    opts->operation = OP_EXTRACT;
                    break;
                case 'l':
                    opts->operation = OP_LIST;
                    break;
                case 't':
                    opts->test_mode = true;
                    break;
                case 'T':
                    opts->timestamp_mode = true;
                    break;
                case 'd':
                    // Extract directory (like unzip -d)
                    if (++arg_index >= argc) {
                        fprintf(stderr, "Error: -d requires a directory path\n");
                        return EXIT_INVALID_ARGS;
                    }
                    opts->target_dir = argv[arg_index];
                    goto next_arg; // Break out of both loops to move to next argument
                case 'm':
                    opts->move_mode = true;
                    break;
                case 'u':
                    opts->update_mode = true;
                    break;
                case '@':
                    opts->read_stdin = true;
                    break;
                case 'D':
                    opts->diff_mode = true;
                    break;
                case 'Z':
                    opts->create_default_zipignore = true;
                    opts->operation = OP_HELP; // Will create zipignore and exit
                    break;
                case 'I':
                    // Custom zipignore file (next argument)
                    if (++arg_index >= argc) {
                        fprintf(stderr, "Error: -I requires a filename\n");
                        return EXIT_INVALID_ARGS;
                    }
                    opts->zipignore_file = argv[arg_index];
                    goto next_arg; // Break out of both loops to move to next argument
                case 'h':
                    opts->operation = OP_HELP;
                    return EXIT_SUCCESS;
                default:
                    fprintf(stderr, "Error: Unknown option -%c\n", arg[i]);
                    return EXIT_INVALID_ARGS;
            }
        }
        next_arg:
        arg_index++;
    }
    
    // Handle special case for creating default zipignore
    if (opts->create_default_zipignore) {
        return EXIT_SUCCESS; // Will be handled in main()
    }
    
    // Parse zipfile and input files (zip-style)
    if (arg_index >= argc) {
        if (opts->operation == OP_HELP) {
            return EXIT_SUCCESS;
        }
        fprintf(stderr, "Error: No zipfile specified\n");
        return EXIT_INVALID_ARGS;
    }
    
    // First non-option argument is the zipfile
    opts->zip_file = argv[arg_index++];
    
    // Remaining arguments are input files/directories
    if (arg_index < argc) {
        opts->input_files = &argv[arg_index];
        opts->input_file_count = argc - arg_index;
    } else {
        // No input files specified
        if (opts->operation == OP_CREATE && !opts->diff_mode) {
            opts->target_dir = "."; // Default to current directory
        }
    }
    
    // Set target directory for extraction if not specified
    if (opts->operation == OP_EXTRACT && !opts->target_dir) {
        // Check if there's a directory specified after the zip file
        if (opts->input_file_count > 0) {
            opts->target_dir = opts->input_files[0];
            opts->input_files = NULL;
            opts->input_file_count = 0;
        } else {
            opts->target_dir = ".";
        }
    }
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    options_t opts;
    int result = parse_arguments(argc, argv, &opts);
    
    if (result != EXIT_SUCCESS) {
        if (result == EXIT_INVALID_ARGS) {
            print_usage(argv[0]);
        }
        return result;
    }
    
    // Handle special case for creating default zipignore
    if (opts.create_default_zipignore) {
        return create_default_zipignore();
    }
    
    switch (opts.operation) {
        case OP_HELP:
            print_usage(argv[0]);
            return EXIT_SUCCESS;
            
        case OP_VERSION:
            print_version();
            return EXIT_SUCCESS;
            
        case OP_CREATE:
            if (opts.diff_mode) {
                return diff_zip(&opts);
            }
            return create_zip(&opts);
            
        case OP_EXTRACT:
            return extract_zip(&opts);
            
        case OP_LIST:
            return list_zip(&opts);
            
        default:
            fprintf(stderr, "Error: Invalid operation\n");
            return EXIT_FAILURE;
    }
}