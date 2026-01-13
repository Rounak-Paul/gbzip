#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#ifndef _WIN32
    #include <sys/ioctl.h>
    #include <unistd.h>
    #include <termios.h>
    #include <sys/resource.h>
    #ifdef __APPLE__
        #include <mach/mach.h>
        #include <sys/sysctl.h>
    #else
        #include <sys/sysinfo.h>
    #endif
#else
    #include <windows.h>
    #include <psapi.h>
#endif

// ============================================================================
// Global TUI State
// ============================================================================

tui_state_t g_tui = {0};

// Static buffers for formatting
static char format_buffer[64];
static char speed_buffer[64];
static char duration_buffer[64];

// Spinner frames
static const char* spinner_frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
static const char* spinner_frames_ascii[] = {"|", "/", "-", "\\"};
static const int spinner_frame_count = 10;
static const int spinner_frame_count_ascii = 4;

// Progress bar characters
static const char* bar_filled_unicode = "█";
static const char* bar_empty_unicode = "░";
static const char bar_filled_ascii = '#';
static const char bar_empty_ascii = '-';

// Box drawing characters
static const char* box_tl = "╭";
static const char* box_tr = "╮";
static const char* box_bl = "╰";
static const char* box_br = "╯";
static const char* box_h = "─";
static const char* box_v = "│";

static const char box_tl_ascii = '+';
static const char box_tr_ascii = '+';
static const char box_bl_ascii = '+';
static const char box_br_ascii = '+';
static const char box_h_ascii = '-';
static const char box_v_ascii = '|';

// ASCII Art Logo
static const char* gbzip_logo[] = {
    "   ██████╗ ██████╗ ███████╗██╗██████╗ ",
    "  ██╔════╝ ██╔══██╗╚══███╔╝██║██╔══██╗",
    "  ██║  ███╗██████╔╝  ███╔╝ ██║██████╔╝",
    "  ██║   ██║██╔══██╗ ███╔╝  ██║██╔═══╝ ",
    "  ╚██████╔╝██████╔╝███████╗██║██║     ",
    "   ╚═════╝ ╚═════╝ ╚══════╝╚═╝╚═╝     "
};

static const char* gbzip_logo_ascii[] = {
    "   ____ ____ _____ ___ ____  ",
    "  / ___| __ |__  /|_ _|  _ \\ ",
    " | |  _|  _ \\ / /  | || |_) |",
    " | |_| | |_) / /_  | ||  __/ ",
    "  \\____|____/____|___|_|     "
};

static const int logo_height = 6;
static const int logo_height_ascii = 5;

// ============================================================================
// Platform-Specific Implementations
// ============================================================================

#ifndef _WIN32

void tui_get_terminal_size(int* width, int* height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width = ws.ws_col;
        *height = ws.ws_row;
    } else {
        *width = 80;
        *height = 24;
    }
}

bool tui_supports_colors(void) {
    const char* term = getenv("TERM");
    if (!term) return false;
    
    // Check for common color-supporting terminals
    if (strstr(term, "color") || strstr(term, "256") ||
        strstr(term, "xterm") || strstr(term, "screen") ||
        strstr(term, "vt100") || strstr(term, "linux") ||
        strstr(term, "ansi") || strstr(term, "rxvt") ||
        strstr(term, "kitty") || strstr(term, "alacritty")) {
        return true;
    }
    
    // Check for COLORTERM environment variable
    const char* colorterm = getenv("COLORTERM");
    if (colorterm && strlen(colorterm) > 0) {
        return true;
    }
    
    return isatty(STDOUT_FILENO);
}

bool tui_supports_unicode(void) {
    const char* lang = getenv("LANG");
    const char* lc_all = getenv("LC_ALL");
    const char* lc_ctype = getenv("LC_CTYPE");
    
    if ((lang && (strstr(lang, "UTF-8") || strstr(lang, "utf8"))) ||
        (lc_all && (strstr(lc_all, "UTF-8") || strstr(lc_all, "utf8"))) ||
        (lc_ctype && (strstr(lc_ctype, "UTF-8") || strstr(lc_ctype, "utf8")))) {
        return true;
    }
    
    // macOS and modern Linux usually support Unicode
    #ifdef __APPLE__
        return true;
    #endif
    
    return false;
}

static void get_memory_info(size_t* used, size_t* total) {
    #ifdef __APPLE__
        // macOS memory info
        mach_port_t host_port = mach_host_self();
        vm_size_t page_size;
        host_page_size(host_port, &page_size);
        
        vm_statistics64_data_t vm_stat;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        
        if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
            *used = ((uint64_t)vm_stat.active_count + 
                     (uint64_t)vm_stat.wire_count) * page_size;
        }
        
        int mib[2] = {CTL_HW, HW_MEMSIZE};
        uint64_t memsize;
        size_t len = sizeof(memsize);
        sysctl(mib, 2, &memsize, &len, NULL, 0);
        *total = memsize;
    #else
        // Linux memory info
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            *total = si.totalram * si.mem_unit;
            *used = (si.totalram - si.freeram - si.bufferram) * si.mem_unit;
        }
    #endif
}

static double get_cpu_usage(void) {
    static clock_t last_cpu = 0;
    static clock_t last_time = 0;
    
    clock_t current_cpu = clock();
    clock_t current_time = time(NULL);
    
    double cpu_usage = 0.0;
    if (last_time > 0 && current_time > last_time) {
        double cpu_diff = (double)(current_cpu - last_cpu) / CLOCKS_PER_SEC;
        double time_diff = (double)(current_time - last_time);
        cpu_usage = (cpu_diff / time_diff) * 100.0;
        if (cpu_usage > 100.0) cpu_usage = 100.0;
    }
    
    last_cpu = current_cpu;
    last_time = current_time;
    
    return cpu_usage;
}

#else // Windows

void tui_get_terminal_size(int* width, int* height) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        *width = 80;
        *height = 24;
    }
}

bool tui_supports_colors(void) {
    // Windows 10+ supports ANSI codes
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        return SetConsoleMode(hOut, dwMode);
    }
    return false;
}

bool tui_supports_unicode(void) {
    // Windows console can support Unicode with proper setup
    return GetConsoleOutputCP() == CP_UTF8;
}

static void get_memory_info(size_t* used, size_t* total) {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    *total = memInfo.ullTotalPhys;
    *used = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
}

static double get_cpu_usage(void) {
    // Simplified CPU usage for Windows
    return 0.0; // TODO: Implement proper Windows CPU usage
}

#endif

// ============================================================================
// Core TUI Functions
// ============================================================================

void tui_init(void) {
    memset(&g_tui, 0, sizeof(g_tui));
    
    tui_get_terminal_size(&g_tui.terminal_width, &g_tui.terminal_height);
    g_tui.colors_enabled = tui_supports_colors();
    g_tui.unicode_enabled = tui_supports_unicode();
    g_tui.progress_style = PROGRESS_STYLE_BLOCK;
    g_tui.start_time = time(NULL);
    g_tui.is_active = true;
    g_tui.show_system_stats = true;
    g_tui.total_phases = 4;
    g_tui.current_phase = 1;
    g_tui.phase_name = "Initializing";
    
    // Enable Windows ANSI support
    #ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    #endif
    
    // Hide cursor during TUI operation
    if (g_tui.colors_enabled) {
        printf(TUI_CURSOR_HIDE);
        fflush(stdout);
    }
}

void tui_cleanup(void) {
    if (g_tui.colors_enabled) {
        printf(TUI_CURSOR_SHOW);
        printf(TUI_RESET);
        fflush(stdout);
    }
    g_tui.is_active = false;
}

// ============================================================================
// Formatting Utilities
// ============================================================================

const char* tui_format_bytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }
    
    if (unit == 0) {
        snprintf(format_buffer, sizeof(format_buffer), "%zu %s", bytes, units[unit]);
    } else {
        snprintf(format_buffer, sizeof(format_buffer), "%.1f %s", size, units[unit]);
    }
    
    return format_buffer;
}

const char* tui_format_speed(double bytes_per_sec) {
    if (bytes_per_sec < 1024) {
        snprintf(speed_buffer, sizeof(speed_buffer), "%.0f B/s", bytes_per_sec);
    } else if (bytes_per_sec < 1024 * 1024) {
        snprintf(speed_buffer, sizeof(speed_buffer), "%.1f KB/s", bytes_per_sec / 1024.0);
    } else if (bytes_per_sec < 1024 * 1024 * 1024) {
        snprintf(speed_buffer, sizeof(speed_buffer), "%.1f MB/s", bytes_per_sec / (1024.0 * 1024.0));
    } else {
        snprintf(speed_buffer, sizeof(speed_buffer), "%.1f GB/s", bytes_per_sec / (1024.0 * 1024.0 * 1024.0));
    }
    return speed_buffer;
}

const char* tui_format_duration(time_t seconds) {
    if (seconds < 60) {
        snprintf(duration_buffer, sizeof(duration_buffer), "%lds", (long)seconds);
    } else if (seconds < 3600) {
        snprintf(duration_buffer, sizeof(duration_buffer), "%ldm %lds", 
                 (long)(seconds / 60), (long)(seconds % 60));
    } else {
        snprintf(duration_buffer, sizeof(duration_buffer), "%ldh %ldm %lds",
                 (long)(seconds / 3600), (long)((seconds % 3600) / 60), (long)(seconds % 60));
    }
    return duration_buffer;
}

// ============================================================================
// Display Functions
// ============================================================================

void tui_move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

void tui_print_color(const char* color, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (g_tui.colors_enabled && color) {
        printf("%s", color);
    }
    
    vprintf(format, args);
    
    if (g_tui.colors_enabled && color) {
        printf(TUI_RESET);
    }
    
    va_end(args);
}

void tui_clear_lines(int count) {
    for (int i = 0; i < count; i++) {
        printf(TUI_CLEAR_LINE "\n");
    }
    // Move cursor back up
    printf("\033[%dA", count);
}

void tui_show_header(void) {
    int width = g_tui.terminal_width;
    
    printf("\n");
    
    if (g_tui.unicode_enabled && g_tui.colors_enabled) {
        // Fancy Unicode header
        tui_print_color(TUI_BRIGHT_CYAN, "");
        for (int i = 0; i < logo_height; i++) {
            int padding = (width - 40) / 2;
            if (padding < 0) padding = 0;
            printf("%*s%s\n", padding, "", gbzip_logo[i]);
        }
        printf(TUI_RESET);
    } else {
        // ASCII fallback
        for (int i = 0; i < logo_height_ascii; i++) {
            int padding = (width - 30) / 2;
            if (padding < 0) padding = 0;
            printf("%*s%s\n", padding, "", gbzip_logo_ascii[i]);
        }
    }
    
    // Subtitle
    int subtitle_len = 42;
    int padding = (width - subtitle_len) / 2;
    if (padding < 0) padding = 0;
    
    tui_print_color(TUI_DIM, "%*s", padding, "");
    tui_print_color(TUI_BRIGHT_WHITE, "Fast Multithreaded ZIP with Smart Ignore\n");
    
    // Separator line
    printf("\n");
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  ");
        for (int i = 0; i < width - 4; i++) printf("─");
        printf("\n");
    } else {
        printf("  ");
        for (int i = 0; i < width - 4; i++) printf("-");
        printf("\n");
    }
    printf("\n");
}

void tui_progress_bar(double percent, int width, const char* label) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    int bar_width = width - 10; // Leave room for percentage
    if (bar_width < 10) bar_width = 10;
    
    int filled = (int)(bar_width * percent / 100.0);
    int empty = bar_width - filled;
    
    // Choose color based on progress
    const char* color = TUI_GREEN;
    if (percent < 33) color = TUI_YELLOW;
    else if (percent < 66) color = TUI_CYAN;
    
    if (label && strlen(label) > 0) {
        tui_print_color(TUI_BRIGHT_WHITE, "  %s ", label);
    } else {
        printf("  ");
    }
    
    // Opening bracket
    tui_print_color(TUI_DIM, "[");
    
    if (g_tui.unicode_enabled) {
        // Unicode progress bar
        tui_print_color(color, "");
        for (int i = 0; i < filled; i++) {
            printf("%s", bar_filled_unicode);
        }
        tui_print_color(TUI_DIM, "");
        for (int i = 0; i < empty; i++) {
            printf("%s", bar_empty_unicode);
        }
    } else {
        // ASCII progress bar
        tui_print_color(color, "");
        for (int i = 0; i < filled; i++) {
            printf("%c", bar_filled_ascii);
        }
        tui_print_color(TUI_DIM, "");
        for (int i = 0; i < empty; i++) {
            printf("%c", bar_empty_ascii);
        }
    }
    
    // Closing bracket and percentage
    tui_print_color(TUI_DIM, "]");
    tui_print_color(TUI_BRIGHT_WHITE, " %5.1f%%", percent);
    printf(TUI_RESET);
}

void tui_spinner(const char* message) {
    g_tui.spinner_frame = (g_tui.spinner_frame + 1) % 
        (g_tui.unicode_enabled ? spinner_frame_count : spinner_frame_count_ascii);
    
    const char* frame;
    if (g_tui.unicode_enabled) {
        frame = spinner_frames[g_tui.spinner_frame];
    } else {
        frame = spinner_frames_ascii[g_tui.spinner_frame];
    }
    
    tui_print_color(TUI_CYAN, "  %s ", frame);
    tui_print_color(TUI_WHITE, "%s", message);
}

void tui_draw_box(int x, int y, int width, int height, const char* title) {
    tui_move_cursor(y, x);
    
    if (g_tui.unicode_enabled) {
        // Top border
        printf("%s", box_tl);
        if (title) {
            int title_len = strlen(title);
            int padding = (width - 2 - title_len) / 2;
            for (int i = 0; i < padding; i++) printf("%s", box_h);
            tui_print_color(TUI_BRIGHT_WHITE, " %s ", title);
            for (int i = 0; i < width - 2 - padding - title_len - 2; i++) printf("%s", box_h);
        } else {
            for (int i = 0; i < width - 2; i++) printf("%s", box_h);
        }
        printf("%s\n", box_tr);
        
        // Sides
        for (int i = 0; i < height - 2; i++) {
            tui_move_cursor(y + 1 + i, x);
            printf("%s", box_v);
            tui_move_cursor(y + 1 + i, x + width - 1);
            printf("%s\n", box_v);
        }
        
        // Bottom border
        tui_move_cursor(y + height - 1, x);
        printf("%s", box_bl);
        for (int i = 0; i < width - 2; i++) printf("%s", box_h);
        printf("%s\n", box_br);
    } else {
        // ASCII box
        printf("%c", box_tl_ascii);
        for (int i = 0; i < width - 2; i++) printf("%c", box_h_ascii);
        printf("%c\n", box_tr_ascii);
        
        for (int i = 0; i < height - 2; i++) {
            tui_move_cursor(y + 1 + i, x);
            printf("%c", box_v_ascii);
            tui_move_cursor(y + 1 + i, x + width - 1);
            printf("%c\n", box_v_ascii);
        }
        
        tui_move_cursor(y + height - 1, x);
        printf("%c", box_bl_ascii);
        for (int i = 0; i < width - 2; i++) printf("%c", box_h_ascii);
        printf("%c\n", box_br_ascii);
    }
}

void tui_show_file_progress(const char* filename, size_t current, size_t total) {
    int max_filename_len = g_tui.terminal_width - 30;
    if (max_filename_len < 20) max_filename_len = 20;
    
    char display_name[256];
    if (strlen(filename) > (size_t)max_filename_len) {
        snprintf(display_name, sizeof(display_name), "...%s", 
                 filename + strlen(filename) - max_filename_len + 3);
    } else {
        strncpy(display_name, filename, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';
    }
    
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  ");
    
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_GREEN, "● ");
    } else {
        tui_print_color(TUI_GREEN, "* ");
    }
    
    tui_print_color(TUI_WHITE, "%s", display_name);
    tui_print_color(TUI_DIM, " (%zu/%zu)", current, total);
    printf("\n");
}

void tui_update_system_stats(void) {
    get_memory_info(&g_tui.sys_stats.memory_used, &g_tui.sys_stats.memory_total);
    g_tui.sys_stats.cpu_usage = get_cpu_usage();
}

void tui_show_system_stats(void) {
    tui_update_system_stats();
    
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  ┌─ System ───────────────────────────────────┐\n");
    
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  │ ");
    
    // Memory
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_YELLOW, "󰍛 ");
    } else {
        tui_print_color(TUI_YELLOW, "MEM ");
    }
    double mem_percent = 0;
    if (g_tui.sys_stats.memory_total > 0) {
        mem_percent = (double)g_tui.sys_stats.memory_used / g_tui.sys_stats.memory_total * 100;
    }
    tui_print_color(TUI_WHITE, "%s / %s ", 
                    tui_format_bytes(g_tui.sys_stats.memory_used),
                    tui_format_bytes(g_tui.sys_stats.memory_total));
    tui_print_color(TUI_DIM, "(%.0f%%)", mem_percent);
    
    // Threads
    tui_print_color(TUI_DIM, "  │  ");
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_CYAN, "󰓅 ");
    } else {
        tui_print_color(TUI_CYAN, "THR ");
    }
    tui_print_color(TUI_WHITE, "%d threads", g_tui.sys_stats.num_threads);
    
    tui_print_color(TUI_DIM, "  │\n");
    
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  └──────────────────────────────────────────────┘\n");
}

void tui_show_compression_stats(size_t original, size_t compressed) {
    double ratio = 0;
    if (original > 0) {
        ratio = (1.0 - (double)compressed / original) * 100;
    }
    
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  ");
    
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_MAGENTA, "󰛡 ");
    } else {
        tui_print_color(TUI_MAGENTA, "COMP ");
    }
    
    tui_print_color(TUI_WHITE, "%s ", tui_format_bytes(original));
    tui_print_color(TUI_DIM, "→ ");
    tui_print_color(TUI_GREEN, "%s ", tui_format_bytes(compressed));
    tui_print_color(TUI_DIM, "(");
    tui_print_color(TUI_BRIGHT_GREEN, "%.1f%% saved", ratio);
    tui_print_color(TUI_DIM, ")\n");
}

void tui_show_progress(void) {
    time_t elapsed = time(NULL) - g_tui.start_time;
    if (elapsed < 1) elapsed = 1;
    
    // Calculate speed
    g_tui.current_speed = (double)g_tui.processed_bytes / elapsed;
    g_tui.avg_speed = g_tui.current_speed;
    
    double percent = 0;
    if (g_tui.total_bytes > 0) {
        percent = (double)g_tui.processed_bytes / g_tui.total_bytes * 100;
    }
    
    // Phase indicator
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  Phase %d/%d: ", g_tui.current_phase, g_tui.total_phases);
    tui_print_color(TUI_BRIGHT_CYAN, "%s\n", g_tui.phase_name);
    
    // Files processed line
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  ");
    
    // Files processed
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_BLUE, "󰈙 ");
    } else {
        tui_print_color(TUI_BLUE, "FILES ");
    }
    tui_print_color(TUI_WHITE, "%zu/%zu", g_tui.processed_files, g_tui.total_files);
    
    // Speed
    tui_print_color(TUI_DIM, "  │  ");
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_GREEN, "󰓅 ");
    } else {
        tui_print_color(TUI_GREEN, "SPEED ");
    }
    tui_print_color(TUI_WHITE, "%s", tui_format_speed(g_tui.current_speed));
    
    // Elapsed time
    tui_print_color(TUI_DIM, "  │  ");
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_YELLOW, "󰔛 ");
    } else {
        tui_print_color(TUI_YELLOW, "TIME ");
    }
    tui_print_color(TUI_WHITE, "%s", tui_format_duration(elapsed));
    
    printf("\n");
    
    // Bytes processed
    printf(TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  ");
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_MAGENTA, "󰋊 ");
    } else {
        tui_print_color(TUI_MAGENTA, "DATA ");
    }
    tui_print_color(TUI_WHITE, "%s / %s\n", 
                    tui_format_bytes(g_tui.processed_bytes),
                    tui_format_bytes(g_tui.total_bytes));
    
    // Compression progress bar (always at bottom, most important)
    printf(TUI_CLEAR_LINE);
    if (g_tui.show_compression_bar) {
        tui_print_color(TUI_DIM, "  ");
        if (g_tui.unicode_enabled) {
            tui_print_color(TUI_CYAN, "󰛡 ");
        } else {
            tui_print_color(TUI_CYAN, "ZIP ");
        }
        tui_print_color(TUI_WHITE, "Compressing ");
        tui_progress_bar(g_tui.compression_percent, g_tui.terminal_width - 30, NULL);
        tui_print_color(TUI_DIM, " %s\n", tui_format_speed(g_tui.compression_speed));
    } else {
        // File progress bar when not in compression phase
        tui_progress_bar(percent, g_tui.terminal_width - 4, "Progress");
        printf("\n");
    }
}

void tui_show_summary(void) {
    time_t elapsed = time(NULL) - g_tui.start_time;
    if (elapsed < 1) elapsed = 1;
    
    double avg_speed = (double)g_tui.processed_bytes / elapsed;
    double compression_ratio = 0;
    if (g_tui.processed_bytes > 0 && g_tui.compressed_bytes > 0) {
        compression_ratio = (1.0 - (double)g_tui.compressed_bytes / g_tui.processed_bytes) * 100;
    }
    
    printf("\n");
    
    // Summary box
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  ╭─────────────────────────────────────────╮\n");
        tui_print_color(TUI_DIM, "  │");
        tui_print_color(TUI_BRIGHT_WHITE, "            Completed                   ");
        tui_print_color(TUI_DIM, "│\n");
        tui_print_color(TUI_DIM, "  ├─────────────────────────────────────────┤\n");
    } else {
        printf("  +------------------------------------------+\n");
        printf("  |              Summary                     |\n");
        printf("  +------------------------------------------+\n");
    }
    
    // Archive
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  │ ");
        tui_print_color(TUI_CYAN, "󰀼 Archive:  ");
    } else {
        printf("  | Archive:   ");
    }
    tui_print_color(TUI_WHITE, "%-28s", g_tui.archive_name);
    tui_print_color(TUI_DIM, "│\n");
    
    // Files
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  │ ");
        tui_print_color(TUI_BLUE, "󰈙 Files:    ");
    } else {
        printf("  | Files:     ");
    }
    tui_print_color(TUI_WHITE, "%-28zu", g_tui.processed_files);
    tui_print_color(TUI_DIM, "│\n");
    
    // Size
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  │ ");
        tui_print_color(TUI_MAGENTA, "󰋊 Size:     ");
    } else {
        printf("  | Size:      ");
    }
    tui_print_color(TUI_WHITE, "%-28s", tui_format_bytes(g_tui.processed_bytes));
    tui_print_color(TUI_DIM, "│\n");
    
    // Time
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  │ ");
        tui_print_color(TUI_YELLOW, "󰔛 Time:     ");
    } else {
        printf("  | Time:      ");
    }
    tui_print_color(TUI_WHITE, "%-28s", tui_format_duration(elapsed));
    tui_print_color(TUI_DIM, "│\n");
    
    // Speed
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  │ ");
        tui_print_color(TUI_GREEN, "󰓅 Speed:    ");
    } else {
        printf("  | Speed:     ");
    }
    tui_print_color(TUI_WHITE, "%-28s", tui_format_speed(avg_speed));
    tui_print_color(TUI_DIM, "│\n");
    
    // Compression (if applicable)
    if (compression_ratio > 0) {
        char ratio_str[32];
        snprintf(ratio_str, sizeof(ratio_str), "%.1f%% saved", compression_ratio);
        if (g_tui.unicode_enabled) {
            tui_print_color(TUI_DIM, "  │ ");
            tui_print_color(TUI_BRIGHT_GREEN, "󰛡 Compression:");
        } else {
            printf("  | Compression:");
        }
        tui_print_color(TUI_WHITE, " %-25s", ratio_str);
        tui_print_color(TUI_DIM, "│\n");
    }
    
    // Bottom border
    if (g_tui.unicode_enabled) {
        tui_print_color(TUI_DIM, "  ╰─────────────────────────────────────────╯\n");
    } else {
        printf("  +------------------------------------------+\n");
    }
    
    printf("\n");
}

// ============================================================================
// Update Functions
// ============================================================================

void tui_set_current_file(const char* filename) {
    if (filename) {
        strncpy(g_tui.filename, filename, sizeof(g_tui.filename) - 1);
        g_tui.filename[sizeof(g_tui.filename) - 1] = '\0';
    }
}

void tui_update_progress(size_t bytes_processed) {
    g_tui.processed_bytes += bytes_processed;
    g_tui.processed_files++;
    
    // Calculate current speed
    time_t elapsed = time(NULL) - g_tui.start_time;
    if (elapsed < 1) elapsed = 1;
    g_tui.current_speed = (double)g_tui.processed_bytes / elapsed;
    
    // Throttle refreshes based on file count and byte progress
    static size_t last_refresh_files = 0;
    static size_t last_refresh_bytes = 0;
    
    if (g_tui.is_active) {
        // Update every 100 files or 1% progress, whichever comes first
        size_t file_threshold = 100;
        size_t byte_threshold = g_tui.total_bytes / 100; // 1% of total
        if (byte_threshold < 1024 * 1024) byte_threshold = 1024 * 1024; // Minimum 1MB
        
        bool should_refresh = 
            (g_tui.processed_files - last_refresh_files >= file_threshold) ||
            (g_tui.processed_bytes - last_refresh_bytes >= byte_threshold) ||
            (g_tui.processed_files == g_tui.total_files); // Always update on last file
        
        if (should_refresh) {
            tui_refresh();
            last_refresh_files = g_tui.processed_files;
            last_refresh_bytes = g_tui.processed_bytes;
        }
    }
}

void tui_update_compression(double percent, double speed) {
    g_tui.compression_percent = percent;
    g_tui.compression_speed = speed;
    g_tui.show_compression_bar = true;
}

void tui_set_phase(int phase, const char* phase_name) {
    // If transitioning from phase 1 (scanning), show final count and move to new line
    if (g_tui.current_phase == 1 && phase != 1) {
        printf("\r" TUI_CLEAR_LINE);
        tui_print_color(TUI_CYAN, "  • ");
        tui_print_color(TUI_WHITE, "Found %zu files", g_tui.total_files);
        tui_print_color(TUI_DIM, " (");
        tui_print_color(TUI_CYAN, "%s", tui_format_bytes(g_tui.total_bytes));
        tui_print_color(TUI_DIM, ")");
        printf("\n");
        fflush(stdout);
    }
    // If transitioning from phase 3, show final count and move to new line
    else if (g_tui.current_phase == 3 && phase != 3) {
        printf("\r" TUI_CLEAR_LINE);
        tui_print_color(TUI_CYAN, "  • ");
        tui_print_color(TUI_WHITE, "Added %zu files", g_tui.processed_files);
        tui_print_color(TUI_DIM, " (");
        tui_print_color(TUI_CYAN, "%s", tui_format_bytes(g_tui.processed_bytes));
        tui_print_color(TUI_DIM, " @ ");
        tui_print_color(TUI_CYAN, "%s", tui_format_speed(g_tui.current_speed));
        tui_print_color(TUI_DIM, ")");
        printf("\n");
        fflush(stdout);
    }
    g_tui.current_phase = phase;
    g_tui.phase_name = phase_name;
}

void tui_refresh(void) {
    if (!g_tui.is_active) return;
    
    g_tui.animation_tick++;
    
    // Update terminal size periodically
    if (g_tui.animation_tick % 10 == 0) {
        tui_get_terminal_size(&g_tui.terminal_width, &g_tui.terminal_height);
    }
    
    // Use simple single-line updates to avoid excessive terminal output
    // Phase 1: Scanning directories
    if (g_tui.current_phase == 1) {
        static int scan_spinner = 0;
        const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        printf("\r" TUI_CLEAR_LINE);
        tui_print_color(TUI_CYAN, "  %s ", frames[scan_spinner++ % 10]);
        tui_print_color(TUI_WHITE, "Scanning");
        if (g_tui.total_files > 0) {
            tui_print_color(TUI_DIM, " [");
            tui_print_color(TUI_WHITE, "%zu files", g_tui.total_files);
            tui_print_color(TUI_DIM, "]");
        }
        fflush(stdout);
        return;
    }
    
    // Phase 2: Parallel pre-compression
    if (g_tui.current_phase == 2) {
        static int compress_spinner = 0;
        const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        printf("\r" TUI_CLEAR_LINE);
        tui_print_color(TUI_CYAN, "  %s ", frames[compress_spinner++ % 10]);
        tui_print_color(TUI_WHITE, "Pre-compressing large files");
        if (g_tui.sys_stats.num_threads > 0) {
            tui_print_color(TUI_DIM, " (");
            tui_print_color(TUI_CYAN, "%d threads", g_tui.sys_stats.num_threads);
            tui_print_color(TUI_DIM, ")");
        }
        fflush(stdout);
        return;
    }
    
    // Phase 3: Adding files to archive
    if (g_tui.current_phase == 3 && !g_tui.show_compression_bar) {
        double percent = 0;
        if (g_tui.total_bytes > 0) {
            percent = (double)g_tui.processed_bytes / g_tui.total_bytes * 100.0;
        }
        
        printf("\r" TUI_CLEAR_LINE);
        tui_print_color(TUI_CYAN, "  • ");
        tui_print_color(TUI_WHITE, "Adding ");
        
        // Progress bar
        int bar_width = 25;
        int filled = (int)(bar_width * percent / 100.0);
        int empty = bar_width - filled;
        
        tui_print_color(TUI_DIM, "[");
        tui_print_color(TUI_GREEN, "");
        for (int i = 0; i < filled; i++) printf("█");
        tui_print_color(TUI_DIM, "");
        for (int i = 0; i < empty; i++) printf("░");
        tui_print_color(TUI_DIM, "] ");
        
        tui_print_color(TUI_BRIGHT_WHITE, "%zu/%zu", g_tui.processed_files, g_tui.total_files);
        tui_print_color(TUI_DIM, " ");
        tui_print_color(TUI_GREEN, "%s", tui_format_bytes(g_tui.processed_bytes));
        tui_print_color(TUI_DIM, " @ ");
        tui_print_color(TUI_CYAN, "%s", tui_format_speed(g_tui.current_speed));
        fflush(stdout);
        return;
    }
    
    // Phase 4: Compressing archive - single line with compression bar
    if (g_tui.current_phase == 4) {
        printf("\r" TUI_CLEAR_LINE);
        tui_print_color(TUI_CYAN, "  • ");
        tui_print_color(TUI_WHITE, "Compressing ");
        if (g_tui.show_compression_bar) {
            // Show compression progress bar inline
            int bar_width = 30;
            int filled = (int)(bar_width * g_tui.compression_percent / 100.0);
            int empty = bar_width - filled;
            
            tui_print_color(TUI_DIM, "[");
            tui_print_color(TUI_GREEN, "");
            for (int i = 0; i < filled; i++) printf("█");
            tui_print_color(TUI_DIM, "");
            for (int i = 0; i < empty; i++) printf("░");
            tui_print_color(TUI_DIM, "] ");
            tui_print_color(TUI_WHITE, "%.1f%%", g_tui.compression_percent);
            tui_print_color(TUI_DIM, " @ ");
            tui_print_color(TUI_CYAN, "%s", tui_format_speed(g_tui.compression_speed));
        }
        fflush(stdout);
        return;
    }
    
    // Fallback for other phases - just print phase info
    printf("\r" TUI_CLEAR_LINE);
    tui_print_color(TUI_DIM, "  Phase %d/%d: ", g_tui.current_phase, g_tui.total_phases);
    tui_print_color(TUI_BRIGHT_CYAN, "%s", g_tui.phase_name);
    fflush(stdout);
}
