// nefuOS power management, lock screen, and autostart subsystem (LVGL based)
#pragma once
#include "../platform.h"
#include "../gui/lvgl_win.h"
#include "../gui/gfx.h"   // Surface (bsod_paint)

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

// ---- BSOD-style panic screen (fullscreen framebuffer overlay) ----
// Shown automatically on a real kernel/app fault (host: SEH crash handler).
// It fills the whole nefuOS screen, swallows all keys and CANNOT be
// dismissed; self-recovery runs automatically on entry and only "Reboot"
// restarts the system. `regs` optionally carries the real register dump
// from the faulting context (0 = synthetic values).
struct CrashRegs {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, rip;
    uint64_t eflags;
};
extern bool g_crash_active;          // true while the blue screen is showing
void blue_screen(uint32_t code, const char* msg, const CrashRegs* regs = 0);
void bsod_paint(Surface& fb);        // fullscreen overlay renderer (called from nefuos_frame)
void bsod_handle_mouse(int x, int y, uint8_t buttons);   // Reboot / Recovery hit-test
void system_recovery();              // rebuild standard dirs + core files (idempotent)

}
