// nefuOS SMP - Multi-threading & Drive Detection
// - CPU detection
// - Drive enumeration (USB/HDD/CD)
// - Thread management

#pragma once

#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace smp {

// CPU info
struct CpuInfo {
    uint32_t vendor[3];        // CPU vendor (GenuineIntel, AuthenticAMD)
    uint32_t version;          // Version/feature bits
    uint32_t features;         // Feature flags
    char brand[49];            // CPU brand string
    int cores;                  // Number of cores
    uint32_t tsc_freq;         // TSC frequency (Hz)
};

// Drive type
enum DriveType {
    DRIVE_UNKNOWN = 0,
    DRIVE_HDD,                  // Hard disk
    DRIVE_SSD,                  // Solid state
    DRIVE_USB,                  // USB flash
    DRIVE_CD,                   // CD/DVD
    DRIVE_FLOPPY,               // Floppy
};

// Drive info
struct DriveInfo {
    int index;                  // Drive index
    DriveType type;             // Drive type
    char name[32];              // Drive name
    uint64_t size_bytes;        // Total size
    uint64_t free_bytes;        // Free space
    bool removable;             // Removable?
    bool mounted;               // Mounted?
    char mount_point[64];       // Mount path
    char fs_type[16];           // Filesystem type (FAT32, NTFS, etc.)
};

// Global drive list
const int MAX_DRIVES = 8;
extern DriveInfo g_drives[MAX_DRIVES];
extern int g_drive_count;

// Detect CPU
void detect_cpu(CpuInfo& info);

// Detect drives
void detect_drives();

// Get drive by index
DriveInfo* get_drive(int index);

// Get drive by mount point
DriveInfo* get_drive_by_mount(const char* path);

// Mount drive
bool mount_drive(int index, const char* mount_point);

// Unmount drive
bool unmount_drive(int index);

// Thread functions
typedef void (*thread_func_t)(void* arg);

// Create thread
int thread_create(thread_func_t func, void* arg);

// Join thread
void thread_join(int tid);

// Sleep
void sleep_ms(uint32_t ms);

// Mutex
struct Mutex {
    volatile int locked;
    Mutex() : locked(0) {}
    void lock() { while (__sync_lock_test_and_set(&locked, 1)) {} }
    void unlock() { __sync_lock_release(&locked); }
};

// Initialize SMP subsystem
void init();

} // namespace smp
} // namespace nefu
