/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS platform abstraction layer.
// The core only depends on the interfaces declared here; the backends
// (win32 / bare) implement them separately.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "memcfg.h"

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
void* kalloc(size_t sz);          // kernel arena (kernel + NVFS + network)
void  kfree(void* p);
void* krealloc(void* p, size_t sz);
void* balloc(size_t sz);          // browser arena (up to BROWSER_MEM_BUDGET_MB)
void  bfree(void* p);
void* brealloc(void* p, size_t sz);

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

// ---- timezone ----
// time() itself is always true UTC. Two facts decide how the hardware clock is
// turned into it: what the RTC stores (0 = local wall clock, the Windows
// dual-boot convention; 1 = UTC, the Linux one), and which zone we are in
// (minutes EAST of UTC: CST-8 => +480). localtime() and the desktop clock
// apply the offset. Host build: Windows owns the zone, the setter is a no-op.
int  platform_tz_offset_minutes();
void platform_set_tz_offset_minutes(int minutes);
bool platform_rtc_is_utc();
void platform_set_rtc_is_utc(bool on);

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
// The backend reports a new framebuffer size (host window resized, or the
// firmware handed us a different mode). The desktop re-lays out to match.
void nefuos_screen_resized(int w, int h);
void nefuos_mark_firstboot_done(); // mark first-boot wizard complete (test mode)
void nefuos_unlock(); // unlock the lock screen (test mode)
void nefuos_shutdown(); // cleanup on exit (saves VFS etc.)

// memory stats (for the info display)
void platform_mem_stats(uint32_t* used, uint32_t* total);
const char* platform_name();

// ===================== persistence (host file / bare ATA disk) =====================
bool platform_fs_load(uint8_t** out, uint32_t* out_size);
void platform_fs_save(const uint8_t* data, uint32_t size);

// Incremental variant used by the NVFS write-back: persist only the 512-byte
// blocks [first_block, first_block+count) of an image whose full size is
// total_size. The backend falls back to a full write whenever the stored copy
// is missing or has a different size, so a partial write can never leave a
// short (unmountable) image behind. Returns false when nothing was written.
bool platform_fs_save_range(const uint8_t* data, uint32_t total_size,
                            uint32_t first_block, uint32_t count);

// How often the NVFS write-back should flush: the bare-metal kernel keeps four
// commits per second (250 ms), the host only once every 4 s - it already holds
// the image in RAM, so rewriting the persistence file four times a second
// would buy nothing and cost a lot of disk churn.
uint32_t platform_fs_flush_interval_ms();

// ===================== LLM model blob =====================
// Load the neural-model blob (TinyStories int8). Host may read a model file;
// bare has no model disk and returns false, so the app falls back to its
// rule table.
bool platform_llm_load(uint8_t** out, uint32_t* out_size);

// Opt-in for the neural path (host CLI `--llm`). Off by default: the int8
// engine is still experimental (intermittent fault on long generations), so
// the shipped chatbot answers from its rule table unless asked otherwise.
extern bool g_llm_opt_in;

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

// ===================== local file access (host) =====================
// Read a whole file into a kalloc'd buffer (caller kfree's). Used by the
// browser for file:// URLs (local HTML test pages, saved snapshots).
// bare: only VFS-backed paths could succeed; returns false otherwise.
bool platform_read_file(const char* path, uint8_t** out, uint32_t* out_size);

// ===================== threading =====================
void* platform_thread_create(void (*func)(void*), void* arg);
// like platform_thread_create but with an explicit stack size (bytes); use for
// short-lived worker threads (download/decode) that need only a small stack
void* platform_thread_create_stk(void (*func)(void*), void* arg, uint32_t stack_bytes);

// chunked HTTP GET: cb is called once per received block; return false from cb
// to abort the download early. Returns true when the response was fully read
// and delivered through cb. Lets the caller stream bytes straight into a parser
// instead of buffering the whole page.
bool platform_http_stream(const char* url, void* ctx,
                          bool (*cb)(void* ctx, const uint8_t* data, uint32_t len));

// Generic HTTP request with method + extra headers + optional body (host:
// WinINet). On success *out_body is a kalloc'd buffer (caller kfree) and
// *out_status holds the HTTP status code. Returns false on transport failure.
bool platform_http_request(const char* method, const char* url,
                           const char* headers,
                           const uint8_t* body, uint32_t body_len,
                           int* out_status, uint8_t** out_body, uint32_t* out_body_len);

// Cookies set by the most recent platform_http_request, one raw
// "name=value; attrs" line per '\n'. Empty when the response carried none, or
// when the backend has its own cookie store (WinINet keeps one per process, so
// the host backend simply returns ""). The pointer is only valid until the next
// request; the JS layer copies it immediately.
const char* platform_http_last_set_cookie();

// Response headers of the most recent platform_http_request, flattened to
// "Name: value" lines separated by '\n' (status line removed). Empty when the
// backend does not expose them. Same lifetime rule: valid until the next
// request, so the caller must copy what it keeps.
const char* platform_http_last_headers();

// Streaming variant: cb is called once per received block (return false to
// abort). *out_status receives the HTTP status code.
bool platform_http_stream_request(const char* method, const char* url,
                                  const char* headers, void* ctx,
                                  bool (*cb)(void* ctx, const uint8_t* data, uint32_t len),
                                  int* out_status);

// Streaming variant with a request body - what a form POST needs. Same
// callback contract as platform_http_stream_request(); `body` (may be null
// when body_len is 0) is sent with the request. Redirects are followed with
// the usual semantics: 303 (and any 3xx except 307/308) drops the body and
// re-issues a GET.
bool platform_http_stream_send(const char* method, const char* url,
                               const char* headers,
                               const uint8_t* body, uint32_t body_len,
                               void* ctx,
                               bool (*cb)(void* ctx, const uint8_t* data, uint32_t len),
                               int* out_status);

// ===================== WebSocket client (RFC 6455) =====================
// Minimal client for the browser's JS runtime (bloxd.io live channel, SPA
// push channels). Supports ws:// and wss://, the HTTP Upgrade handshake,
// text/binary frames and close.
//
// The host backend runs a reader thread per socket, so the frame loop only
// ever pops already-complete messages: polling never blocks, and a stalled
// server cannot freeze the UI.
//
//   int h = platform_ws_open("wss://host/path");  // < 0 on failure
//   while (platform_ws_state(h) == 1) { ... }
//   platform_ws_send(h, bytes, len, /*binary=*/1);
//   int len = platform_ws_peek(h, &is_binary);    // 0 = none, <0 = closed
//   platform_ws_read(h, buf, len);
//   platform_ws_close(h);
//
// Messages are delivered whole: peek reports the full length, read copies
// (and consumes) it. Fragmented frames are reassembled by the backend.
int  platform_ws_open(const char* url);
// Same handshake, but with an explicit extra-headers block ("Name: value\r\n"
// lines) sent on the Upgrade request. Colyseus authenticates the socket with
// the cookie minted by the social host, and WinHTTP sends no cookie by itself.
int  platform_ws_open_hdr(const char* url, const char* extra_headers);
int  platform_ws_state(int h);          // 0 connecting, 1 open, 2 closed
int  platform_ws_send(int h, const uint8_t* data, int len, int binary);
int  platform_ws_peek(int h, int* out_binary);   // >0 len, 0 none, -1 closed
int  platform_ws_read(int h, uint8_t* buf, int cap);
void platform_ws_close(int h);

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
    KEY_FAST = 34,   // Win / Super (modifier; powers bare quick-update)
};

} // namespace nefu

// C linkage HTTP request for the MicroPython nefu_http module. Buffers are
// malloc'd (caller free()s them).
#ifdef __cplusplus
extern "C" {
#endif
int nefu_http_request(const char* method, const char* url,
                      const char* body, const char* content_type,
                      const char* extra_headers,
                      int* out_status, char** out_headers, uint32_t* out_headers_len,
                      char** out_body, uint32_t* out_body_len);
#ifdef __cplusplus
}
#endif

