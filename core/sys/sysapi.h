// nefuOS System API Library
// Provides system-level functionality for applications
// Version: 1.0
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace sysapi {

// ============================================
// File System API
// ============================================

// File types
enum FileType {
    FILE_REGULAR = 0,
    FILE_DIRECTORY = 1,
    FILE_SYMLINK = 2,
};

// File info structure
struct FileInfo {
    char name[256];
    FileType type;
    uint32_t size;
    uint32_t created;
    uint32_t modified;
};

// Open a file
// Returns file descriptor, -1 on error
int file_open(const char* path, int flags);

// Close a file
void file_close(int fd);

// Read from file
int file_read(int fd, void* buf, int count);

// Write to file
int file_write(int fd, const void* buf, int count);

// Seek in file
int file_seek(int fd, int offset, int whence);

// Get file size
uint32_t file_size(int fd);

// List directory entries
int dir_list(const char* path, FileInfo* entries, int max_entries);

// Create directory
int dir_mkdir(const char* path);

// Remove file/directory
int file_remove(const char* path);

// Rename file/directory
int file_rename(const char* oldpath, const char* newpath);

// ============================================
// Process API
// ============================================

// Process info
struct ProcessInfo {
    int pid;
    char name[64];
    int type;
    uint32_t mem_usage;
    int status;
};

// Get current process ID
int process_pid();

// Get process list
int process_list(ProcessInfo* procs, int max_procs);

// Exit current process
void process_exit(int code);

// ============================================
// Memory API
// ============================================

// Allocate memory
void* mem_alloc(size_t size);

// Free memory
void mem_free(void* ptr);

// Get total memory
uint32_t mem_total();

// Get free memory
uint32_t mem_free();

// ============================================
// Network API
// ============================================

// HTTP GET request
// Returns response data, caller must free with mem_free
bool http_get(const char* url, uint8_t** response, uint32_t* response_len);

// Check if network is available
bool network_available();

// ============================================
// Display API
// ============================================

// Get screen width
int screen_width();

// Get screen height
int screen_height();

// Get color depth
int screen_bpp();

// ============================================
// Power API
// ============================================

// Shutdown system
void power_shutdown();

// Reboot system
void power_reboot();

// Reboot to BIOS
void power_reboot_to_bios();

// Suspend system
void power_suspend();

// ============================================
// Settings API
// ============================================

// Get setting value
const char* settings_get(const char* key);

// Set setting value
void settings_set(const char* key, const char* value);

// Save settings to disk
void settings_save();

// ============================================
// Time API
// ============================================

// Get current time (Unix timestamp)
uint64_t time_now();

// Get uptime in seconds
uint64_t time_uptime();

// ============================================
// Notification API
// ============================================

// Show notification
void notify(const char* title, const char* message);

// ============================================
// Application API
// ============================================

// Launch an application
bool app_launch(int app_id);

// Get installed application list
int app_list(int* app_ids, int max_apps);

// Check if app is installed
bool app_installed(int app_id);

} // namespace sysapi
} // namespace nefu
