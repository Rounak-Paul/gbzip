#ifndef TUI_H
#define TUI_H

#include "gbzip.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// Terminal UI Module - Cross-platform TUI with colors, progress bars, and stats
// ============================================================================

// ANSI Color codes
#define TUI_RESET       "\033[0m"
#define TUI_BOLD        "\033[1m"
#define TUI_DIM         "\033[2m"
#define TUI_ITALIC      "\033[3m"
#define TUI_UNDERLINE   "\033[4m"
#define TUI_BLINK       "\033[5m"
#define TUI_REVERSE     "\033[7m"

// Foreground colors
#define TUI_BLACK       "\033[30m"
#define TUI_RED         "\033[31m"
#define TUI_GREEN       "\033[32m"
#define TUI_YELLOW      "\033[33m"
#define TUI_BLUE        "\033[34m"
#define TUI_MAGENTA     "\033[35m"
#define TUI_CYAN        "\033[36m"
#define TUI_WHITE       "\033[37m"

// Bright foreground colors
#define TUI_BRIGHT_BLACK    "\033[90m"
#define TUI_BRIGHT_RED      "\033[91m"
#define TUI_BRIGHT_GREEN    "\033[92m"
#define TUI_BRIGHT_YELLOW   "\033[93m"
#define TUI_BRIGHT_BLUE     "\033[94m"
#define TUI_BRIGHT_MAGENTA  "\033[95m"
#define TUI_BRIGHT_CYAN     "\033[96m"
#define TUI_BRIGHT_WHITE    "\033[97m"

// Background colors
#define TUI_BG_BLACK    "\033[40m"
#define TUI_BG_RED      "\033[41m"
#define TUI_BG_GREEN    "\033[42m"
#define TUI_BG_YELLOW   "\033[43m"
#define TUI_BG_BLUE     "\033[44m"
#define TUI_BG_MAGENTA  "\033[45m"
#define TUI_BG_CYAN     "\033[46m"
#define TUI_BG_WHITE    "\033[47m"

// Cursor control
#define TUI_CURSOR_UP(n)        "\033[" #n "A"
#define TUI_CURSOR_DOWN(n)      "\033[" #n "B"
#define TUI_CURSOR_RIGHT(n)     "\033[" #n "C"
#define TUI_CURSOR_LEFT(n)      "\033[" #n "D"
#define TUI_CURSOR_HOME         "\033[H"
#define TUI_CURSOR_HIDE         "\033[?25l"
#define TUI_CURSOR_SHOW         "\033[?25h"
#define TUI_CLEAR_LINE          "\033[2K"
#define TUI_CLEAR_SCREEN        "\033[2J"
#define TUI_SAVE_CURSOR         "\033[s"
#define TUI_RESTORE_CURSOR      "\033[u"

// Progress bar styles
typedef enum {
    PROGRESS_STYLE_BLOCK,       // █░░░░░░░░░
    PROGRESS_STYLE_ARROW,       // ====>-----
    PROGRESS_STYLE_DOT,         // ●●●●○○○○○○
    PROGRESS_STYLE_BRAILLE,     // ⣿⣿⣿⣿⣀⣀⣀⣀
    PROGRESS_STYLE_GRADIENT     // ▓▓▓▓▒▒░░░░
} progress_style_t;

// System stats structure
typedef struct {
    double cpu_usage;           // 0.0 - 100.0
    size_t memory_used;         // bytes
    size_t memory_total;        // bytes
    int num_threads;
    int active_threads;
} system_stats_t;

// TUI state structure
typedef struct {
    // Display settings
    int terminal_width;
    int terminal_height;
    bool colors_enabled;
    bool unicode_enabled;
    progress_style_t progress_style;
    
    // Operation info
    char operation[32];         // "Creating", "Extracting", etc.
    char filename[256];         // Current file being processed
    char archive_name[256];     // Archive name
    
    // Progress tracking
    size_t total_files;
    size_t processed_files;
    size_t total_bytes;
    size_t processed_bytes;
    size_t compressed_bytes;    // Bytes after compression
    
    // Compression progress (for final write phase)
    double compression_percent;
    double compression_speed;
    bool show_compression_bar;
    
    // Large file compression progress
    size_t large_file_total;        // Total number of large files
    size_t large_file_current;      // Current large file being processed
    size_t large_file_size;         // Size of current large file
    double large_file_percent;      // Progress within current large file
    char large_file_name[256];      // Name of current large file
    bool show_large_file_bar;       // Whether to show large file progress
    
    // Per-thread progress tracking (for multi-threaded compression)
    #define MAX_THREAD_PROGRESS 16
    struct {
        char filename[64];          // File being compressed by this thread
        size_t file_size;           // Size of file
        double percent;             // Compression progress (0-100)
        bool active;                // Whether thread is actively working
    } thread_progress[MAX_THREAD_PROGRESS];
    int active_thread_count;        // Number of threads with active work
    size_t completed_large_files;   // Count of completed large files
    
    // Speed tracking
    time_t start_time;
    double current_speed;       // bytes/sec
    double avg_speed;           // bytes/sec
    
    // System stats
    system_stats_t sys_stats;
    
    // Phase info
    int current_phase;          // 1=collecting, 2=compressing, 3=writing, 4=finalizing
    int total_phases;
    const char* phase_name;
    
    // Animation
    int spinner_frame;
    int animation_tick;
    
    // Flags
    bool is_active;
    bool show_system_stats;
    bool compact_mode;
} tui_state_t;

// Global TUI state
extern tui_state_t g_tui;

// ============================================================================
// Core TUI Functions
// ============================================================================

// Initialize the TUI system
void tui_init(void);

// Cleanup and restore terminal
void tui_cleanup(void);

// Check if terminal supports features
bool tui_supports_colors(void);
bool tui_supports_unicode(void);

// Get terminal dimensions
void tui_get_terminal_size(int* width, int* height);

// ============================================================================
// Display Functions
// ============================================================================

// Show the main TUI header with ASCII art
void tui_show_header(void);

// Show a progress bar
void tui_progress_bar(double percent, int width, const char* label);

// Show file progress
void tui_show_file_progress(const char* filename, size_t current, size_t total);

// Show overall progress with all stats
void tui_show_progress(void);

// Show system stats (CPU, memory, threads)
void tui_show_system_stats(void);

// Show compression stats
void tui_show_compression_stats(size_t original, size_t compressed);

// Show a spinner animation
void tui_spinner(const char* message);

// Show completion summary
void tui_show_summary(void);

// ============================================================================
// Update Functions
// ============================================================================

// Update current file being processed
void tui_set_current_file(const char* filename);

// Update progress
void tui_update_progress(size_t bytes_processed);

// Update compression progress (0-100%)
void tui_update_compression(double percent, double speed);

// Update large file progress (current file being compressed)
void tui_update_large_file_progress(size_t current, size_t total, const char* filename, 
                                     size_t file_size, double percent);

// Update per-thread compression progress
void tui_update_thread_progress(int thread_id, const char* filename, size_t file_size, 
                                 double percent, bool active);

// Set completed large file count
void tui_set_large_file_counts(size_t completed, size_t total);

// Update phase
void tui_set_phase(int phase, const char* phase_name);

// Update system stats
void tui_update_system_stats(void);

// Refresh the entire display
void tui_refresh(void);

// ============================================================================
// Utility Functions
// ============================================================================

// Format bytes to human readable
const char* tui_format_bytes(size_t bytes);

// Format time duration
const char* tui_format_duration(time_t seconds);

// Format speed
const char* tui_format_speed(double bytes_per_sec);

// Move cursor to position
void tui_move_cursor(int row, int col);

// Print colored text
void tui_print_color(const char* color, const char* format, ...);

// Draw a box
void tui_draw_box(int x, int y, int width, int height, const char* title);

// Clear specified number of lines from current position
void tui_clear_lines(int count);

#endif // TUI_H
