// nefuOS power management, lock screen, and autostart subsystem (LVGL based)
#pragma once
#include "../platform.h"
#include "../gui/lvgl_win.h"

namespace nefu {

// Global state
extern bool g_locked;          // True when system is on lock screen
extern UefiConfig g_uefi;      // Persistent UEFI/BIOS configuration
extern LvglWin* s_splash_win;  // Boot splash window (LVGL)
extern LvglWin* s_lock_win;    // Lock screen window (LVGL)

// Initialize power management after LVGL/WM is ready
void power_init();

// Show LVGL boot splash screen with progress bar (0-100)
void boot_splash_show(int progress);

// Show LVGL lock screen as fullscreen modal
void lock_screen();

// Dismiss LVGL lock screen and return to desktop
void unlock_screen();

// Perform full OS shutdown: save all state and power off hardware
void os_shutdown();

// Perform system reboot: save all state and reset CPU
void os_reboot();

// Enter low-power suspend state
void os_suspend();

// Launch all apps configured in autostart list
void autostart_run();

// Verify password input against stored SHA256 hash
bool verify_password(const char* input);

// Detailed BSOD-style panic screen (LVGL fullscreen). Shows the error code,
// fault address, register dump and recovery options (reboot / recovery).
// The optional message is capped at 40 characters to keep the layout clean.
void blue_screen(uint32_t code, const char* msg);

}
