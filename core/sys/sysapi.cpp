// nefuOS System API Library Implementation
#include "sysapi.h"
#include "../platform.h"
#include "../klib/klib.h"
#include "../apps/apps.h"
#include "settings.h"
#include "power.h"

namespace nefu {
namespace sysapi {

// ============================================
// File System API - simplified stubs
// ============================================

int file_open(const char* path, int flags) { return -1; }
void file_close(int fd) {}
int file_read(int fd, void* buf, int count) { return 0; }
int file_write(int fd, const void* buf, int count) { return 0; }
int file_seek(int fd, int offset, int whence) { return 0; }
uint32_t file_size(int fd) { return 0; }
int dir_list(const char* path, FileInfo* entries, int max_entries) { return 0; }
int dir_mkdir(const char* path) { return -1; }
int file_remove(const char* path) { return -1; }
int file_rename(const char* oldpath, const char* newpath) { return -1; }

// ============================================
// Process API
// ============================================

int process_pid() {
    return 1;
}

int process_list(ProcessInfo* procs, int max_procs) {
    if (!procs || max_procs <= 0) return 0;
    int count = 0;
    if (count < max_procs) {
        procs[count].pid = 0;
        ksprintf(procs[count].name, 64, "nefuOS Kernel");
        procs[count].type = 0;
        procs[count].mem_usage = 1024;
        procs[count].status = 0;
        count++;
    }
    return count;
}

void process_exit(int code) {
    while (1) {}
}

// ============================================
// Memory API
// ============================================

void* mem_alloc(size_t size) {
    return kalloc(size);
}

void mem_free(void* ptr) {
    kfree(ptr);
}

uint32_t mem_total() {
    HwInfo hw;
    platform_hw_info(&hw);
    return hw.mem_total_mb;
}

uint32_t mem_free() {
    return mem_total() / 2;
}

// ============================================
// Network API
// ============================================

bool http_get(const char* url, uint8_t** response, uint32_t* response_len) {
    if (!url || !response || !response_len) return false;
    return platform_http_get(url, response, response_len);
}

bool network_available() {
    return true;
}

// ============================================
// Display API
// ============================================

int screen_width() { return 800; }
int screen_height() { return 600; }
int screen_bpp() { return 32; }

// ============================================
// Power API
// ============================================

void power_shutdown() {
    nefuos_shutdown();
    platform_poweroff();
}

void power_reboot() {
    nefuos_shutdown();
    platform_reboot();
}

void power_reboot_to_bios() {
    power_reboot();
}

void power_suspend() {
    lock_screen();
}

// ============================================
// Settings API
// ============================================

const char* settings_get(const char* key) {
    return "";
}

void settings_set(const char* key, const char* value) {}

void settings_save() {}

// ============================================
// Time API
// ============================================

uint64_t time_now() {
    return platform_tick_ms() / 1000;
}

uint64_t time_uptime() {
    return platform_tick_ms() / 1000;
}

// ============================================
// Notification API
// ============================================

void notify(const char* title, const char* message) {}

// ============================================
// Application API
// ============================================

bool app_launch(int app_id) {
    app_launch(app_id);
    return true;
}

int app_list(int* app_ids, int max_apps) {
    if (!app_ids || max_apps <= 0) return 0;
    return app_installed_list(app_ids, max_apps);
}

bool app_installed(int app_id) {
    return app_id >= 0 && app_id < APP_COUNT;
}

} // namespace sysapi
} // namespace nefu
