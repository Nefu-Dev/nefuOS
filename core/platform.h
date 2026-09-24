// nefuOS platform abstraction layer.
// The core only depends on the interfaces declared here; the backends
// (win32 / bare) implement them separately.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {

// image decode: host backend uses the OS decoder (jpg/png/gif/bmp);
// bare backend returns false and the caller falls back to built-in bmp/ppm
struct Surface;
bool platform_decode_image(const uint8_t* data, uint32_t size, struct Surface& out);

// ===================== TTF text =====================
// Renders a UTF-8 string with a real TrueType font into a freshly allocated
// RGBA buffer (alpha = glyph coverage). Host backend implements with GDI+;
// bare backend returns false and the caller falls back to the bitmap font.
// Caller frees the buffer with platform_ttf_free().
bool  platform_ttf_text(const char* utf8, int px, int& out_w, int& out_h, uint8_t*& out_rgba);
void  platform_ttf_free(uint8_t* p);

// ===================== memory =====================
void* kalloc(size_t sz);
void  kfree(void* p);
void* krealloc(void* p, size_t sz);

// ===================== screen =====================
// Software pixel buffer, 32bpp, little-endian BGRA (uint32 = 0x00RRGGBB)
struct Screen {
    uint8_t* addr;
    int width;
    int height;
    int pitch;      // bytes per row (>= width*4)
    int bpp = 32;   // 24 for QEMU stdvga VBE modes (3-byte pixels, 4-byte stride)
};
Screen* platform_screen();
// Host backend blits the buffer to the real screen each frame (bare: no-op)
void platform_present();

// ===================== time =====================
uint32_t platform_tick_ms();        // ms since boot
uint32_t platform_seconds_of_day();

struct DateInfo {
    int year, month, day;   // year full (e.g. 2026), month 1..12, day 1..31
    int hour, min, sec;     // 0..23 / 0..59
    int dow;                // 0 = Sunday .. 6 = Saturday
};
bool platform_rtc_date(DateInfo* out);   // CMOS RTC on bare, GetLocalTime on host

uint32_t nefuos_uptime_ms();        // ms since nefuOS started (bare: uptime)

// ===================== debug output =====================
void platform_dbg(const char* s);

// ===================== core callbacks (called by backends) =====================
void nefuos_handle_key(int keycode, char ascii, bool down, const char* utf8 = 0);
void nefuos_handle_mouse(int x, int y, uint8_t buttons);
void nefuos_handle_scroll(int delta);
void nefuos_tick();     // periodic heartbeat (clock/animation)
void nefuos_frame();    // redraw every frame
void nefuos_init();     // system init
void nefuos_mark_firstboot_done(); // mark first-boot wizard complete (test mode)
void nefuos_unlock(); // unlock the lock screen (test mode)
void nefuos_shutdown(); // cleanup on exit (saves VFS etc.)

// memory stats (for the info display)
void platform_mem_stats(uint32_t* used, uint32_t* total);
const char* platform_name();

// ===================== persistence (host only, bare: no-op) =====================
bool platform_fs_load(uint8_t** out, uint32_t* out_size);
void platform_fs_save(const uint8_t* data, uint32_t size);

// ===================== power off / exit =====================
void platform_poweroff();
void platform_reboot();    // really reboot
void platform_suspend();  // standby / suspend

// ===================== full hardware identification info =====================
struct HwInfo {
    char cpu_model[64];      // CPU model string
    uint32_t cpu_mhz;        // CPU clock (MHz)
    uint64_t mem_total_mb;  // total physical memory (MB)
    char bios_version[32];   // BIOS/UEFI version
    char bios_vendor[32];    // BIOS vendor
    uint8_t  cpu_cores;      // core count
};
// get full hardware info (CPUID / system probing)
bool platform_hw_info(HwInfo* out);

// ===================== UEFI/BIOS config =====================
// custom boot entries: for dual/multi-boot (name + target device + type)
#define NEFU_MAX_BOOT_ENTRIES 8
struct UefiBootEntry {
    char name[32];       // boot entry display name (e.g. "nefuOS", "Windows")
    char device[16];     // target device / system id (e.g. "C:", "D:", "nefuOS")
    char kind[16];       // type tag: "OS" / "Disk" / "ISO"
};
struct UefiConfig {
    char username[32];       // username set in UEFI settings
    char password_hash[65];  // login password hash (64 hex chars + NUL)
    uint8_t  boot_timeout;   // boot wait time (seconds)
    bool     boot_splash;    // whether to show the boot splash page
    char wallpaper_boot[32]; // boot wallpaper
    char wallpaper_lock[32]; // lock-screen wallpaper
    // ---- boot entries (multi-boot) ----
    int  boot_first;         // index of the first boot device (uses the disk list when there are no custom entries)
    int  boot_entry_count;   // number of custom boot entries
    UefiBootEntry boot_entries[NEFU_MAX_BOOT_ENTRIES];
};
// read/write UEFI config (stored in CMOS/RTC battery-backed storage or VFS)
bool platform_uefi_load(UefiConfig* out);
bool platform_uefi_save(const UefiConfig* cfg);

// ===================== networking (real adapter info) =====================
// bare: e1000 static config (10.0.2.15/24, gw 10.0.2.2) once net_init ran;
// host: real Windows adapter info via GetAdaptersInfo.
struct NetAdapterInfo {
    bool up;                  // link up?
    uint8_t mac[6];
    uint32_t ip;              // host order
    uint32_t gw;
    uint32_t mask;
    char name[48];            // human readable adapter name
};
// Fills out with the active adapter. Returns false when no adapter is usable.
bool platform_net_get(NetAdapterInfo* out);
int platform_net_get_all(NetAdapterInfo* list, int max);  // returns count

// host: real Wi-Fi scan via WlanGetAvailableNetworkList; bare: returns 0
// (e1000 is wired). ssid is NUL-terminated, rssi in 0..100, open=true when
// the network has no encryption.
struct WifiNetInfo {
    char ssid[33];
    int  rssi;
    bool open;
};
int platform_wifi_scan(WifiNetInfo* list, int max);   // returns count

// Real ICMP ping. bare: net_ping on the e1000 link; host: IcmpSendEcho.
// Returns true when a reply arrives within timeout_ms.
bool platform_ping(uint32_t ip, int timeout_ms);

// Real HTTP(S) GET. host: WinINet (DNS + TLS included); bare: returns false
// and the browser falls back to the in-house TCP stack (IP literals only).
// On success *out is a malloc'd buffer (caller frees), *out_size its length.
bool platform_http_get(const char* url, uint8_t** out, uint32_t* out_size);

// ===================== threading =====================
void* platform_thread_create(void (*func)(void*), void* arg);
void  platform_thread_sleep(uint32_t ms);

// ===================== audio (host) =====================
// Real playback. play_wav_mem accepts an in-memory RIFF/WAVE (PCM).
// play_wav resolves a VFS path (implemented in core/nefuos.cpp).
// audio_available reports whether a sound card was probed successfully.
bool platform_audio_available();
bool platform_play_wav(const char* path);
bool platform_play_wav_mem(const uint8_t* data, uint32_t size);
bool platform_play_wav_path(const char* path);   // core helper: VFS load + play_wav_mem
void platform_stop_sound();

// ===================== media (host: Media Foundation) =====================
// Real decode + playback of common audio/video formats (whatever the OS
// codecs provide: WAV/MP3/FLAC/AAC/M4A/WMA audio, MP4/MOV/AVI/WMV/MPG/3GP
// video). The host backend streams decoded PCM through waveOut and hands
// decoded RGB32 video frames to the caller. The bare backend stubs return
// false / -1 and apps fall back to their demo content.
bool  platform_media_available();
// Open a REAL host-side media file. audio_only=true plays just the audio
// track (music player); false decodes video frames too (video player).
bool  platform_media_open(const char* host_path, bool audio_only);
void  platform_media_close();
bool  platform_media_play();
bool  platform_media_pause();
void  platform_media_stop();
bool  platform_media_seek_sec(int sec);
int   platform_media_position_sec();     // -1 = unknown
int   platform_media_duration_sec();     // -1 = unknown
void  platform_media_set_volume(int percent);  // 0..100
bool  platform_media_has_video();
// Native decoded video size (false when no video stream / no frame yet).
bool  platform_media_frame_info(int* w, int* h);
// Copy the newest decoded frame into out_rgba (RGB32, w*h*4 bytes, BGRA
// memory order = nefuOS 0x00RRGGBB). Returns false when no frame available.
bool  platform_media_grab_frame(uint8_t* out_rgba);
// Real "open file" dialog on the host. Returns true and fills out_path with
// the picked file. filter_desc/filter_pattern e.g. ("Media files","*.mp4;*.avi").
bool  platform_host_file_dialog(char* out_path, int max, const char* filter_desc, const char* filter_pattern);

// ===================== disk / block devices =====================
// Real block-device enumeration. bare: ATA IDENTIFY probe on the legacy
// primary/secondary controllers. host: Windows logical drives (model is
// the drive letter / label). Returns the number of entries written.
struct DiskInfo {
    char     name[16];    // kernel name, e.g. "sda" / "C:"
    char     model[44];   // model string or label
    uint64_t sectors;     // total 512-byte sectors (0 = unknown)
    bool     removable;   // removable media?
};
int platform_disk_scan(DiskInfo* list, int max);

// ===================== keycodes =====================
enum {
    KEY_NONE = 0,
    KEY_ENTER = 1,
    KEY_BACKSPACE = 2,
    KEY_ESC = 3,
    KEY_LEFT = 4, KEY_UP = 5, KEY_RIGHT = 6, KEY_DOWN = 7,
    KEY_TAB = 8,
    KEY_DEL = 9,
    KEY_HOME = 10, KEY_END = 11, KEY_PGUP = 12, KEY_PGDN = 13,
    KEY_SPACE = 14,
    KEY_F1 = 20, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_SHIFT = 30, KEY_CTRL = 31, KEY_ALT = 32, KEY_CAPS = 33,
};

} // namespace nefu
