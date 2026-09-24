// nefuOS Extended System API
// Additional system functions: power management, display, input, storage, etc.
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace sys {

// ============================================
// Display API
// ============================================

namespace display {

// Get screen width in pixels
int get_width();

// Get screen height in pixels
int get_height();

// Get color depth (bits per pixel)
int get_bpp();

// Set screen resolution
bool set_resolution(int width, int height, int bpp);

// Get current brightness (0-100)
int get_brightness();

// Set screen brightness (0-100)
void set_brightness(int level);

// Turn screen on/off
void power_on();
void power_off();

// Get DPI
int get_dpi();

// Get refresh rate (Hz)
int get_refresh_rate();

// Set VSync
void set_vsync(bool enabled);

// Get display name
const char* get_name();

// Get display vendor
const char* get_vendor();

// Get display serial number
const char* get_serial();

// Get display manufacturer week
int get_manufacture_week();

// Get display manufacturer year
int get_manufacture_year();

// Get physical width in mm
int get_physical_width();

// Get physical height in mm
int get_physical_height();

} // namespace display

// ============================================
// Input API
// ============================================

namespace input {

// Mouse buttons
enum MouseButton {
    MOUSE_LEFT = 1,
    MOUSE_RIGHT = 2,
    MOUSE_MIDDLE = 4
};

// Get mouse X position
int get_mouse_x();

// Get mouse Y position
int get_mouse_y();

// Get mouse button state
int get_mouse_buttons();

// Get mouse wheel delta
int get_mouse_wheel();

// Get keyboard modifier state
int get_modifiers();

// Check if key is pressed
bool is_key_pressed(int keycode);

// Get keyboard layout
const char* get_keyboard_layout();

// Set keyboard layout
void set_keyboard_layout(const char* layout);

// Get mouse acceleration
float get_mouse_acceleration();

// Set mouse acceleration
void set_mouse_acceleration(float accel);

// Get mouse speed
int get_mouse_speed();

// Set mouse speed
void set_mouse_speed(int speed);

// Get double-click time (ms)
int get_double_click_time();

// Set double-click time
void set_double_click_time(int ms);

// Get key repeat delay (ms)
int get_key_repeat_delay();

// Set key repeat delay
void set_key_repeat_delay(int ms);

// Get key repeat rate (per second)
int get_key_repeat_rate();

// Set key repeat rate
void set_key_repeat_rate(int rate);

} // namespace input

// ============================================
// Storage API
// ============================================

namespace storage {

// Disk info
struct DiskInfo {
    char name[64];
    char mount_point[128];
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    int type;  // 0=unknown, 1=hdd, 2=ssd, 3=usb, 4=optical
    bool removable;
    bool read_only;
};

// Get number of disks
int get_disk_count();

// Get disk info by index
bool get_disk_info(int index, DiskInfo* info);

// Get disk info by mount point
bool get_disk_info_by_path(const char* path, DiskInfo* info);

// Mount a disk
bool mount(const char* device, const char* mount_point, const char* filesystem);

// Unmount a disk
bool unmount(const char* mount_point);

// Format a disk
bool format(const char* device, const char* filesystem);

// Get total disk space
uint64_t get_total_space(const char* path);

// Get used disk space
uint64_t get_used_space(const char* path);

// Get free disk space
uint64_t get_free_space(const char* path);

// Get filesystem type
const char* get_filesystem_type(const char* path);

// Check if path is on removable media
bool is_removable(const char* path);

// Check if path is read-only
bool is_read_only(const char* path);

// Eject removable media
bool eject(const char* device);

// Scan for new devices
void rescan();

} // namespace storage

// ============================================
// Power API
// ============================================

namespace power {

// Power state
enum PowerState {
    POWER_ON,
    POWER_SLEEP,
    POWER_HIBERNATE,
    POWER_OFF
};

// Get current power state
PowerState get_state();

// Shutdown the system
void shutdown();

// Reboot the system
void reboot();

// Reboot to BIOS/UEFI
void reboot_to_bios();

// Suspend to RAM
void sleep();

// Hibernate to disk
void hibernate();

// Lock the screen
void lock_screen();

// Get battery percentage (0-100, -1 = no battery)
int get_battery_percent();

// Is battery charging?
bool is_charging();

// Get battery time remaining (minutes, -1 = unknown)
int get_battery_time_remaining();

// Get AC adapter status
bool is_ac_connected();

// Set power saving mode
void set_power_saving(bool enabled);

// Get power saving mode
bool is_power_saving();

// Set screen off timeout (seconds)
void set_screen_off_timeout(int seconds);

// Get screen off timeout
int get_screen_off_timeout();

// Set sleep timeout (seconds)
void set_sleep_timeout(int seconds);

// Get sleep timeout
int get_sleep_timeout();

// Set hibernate timeout (seconds)
void set_hibernate_timeout(int seconds);

// Get hibernate timeout
int get_hibernate_timeout();

} // namespace power

// ============================================
// Network API
// ============================================

namespace network {

// Network interface info
struct InterfaceInfo {
    char name[64];
    char ip_address[32];
    char netmask[32];
    char gateway[32];
    char mac_address[32];
    bool up;
    bool wireless;
    bool connected;
    int signal_strength;  // 0-100
};

// Get number of interfaces
int get_interface_count();

// Get interface info by index
bool get_interface_info(int index, InterfaceInfo* info);

// Get interface info by name
bool get_interface_info_by_name(const char* name, InterfaceInfo* info);

// Bring interface up
bool ifup(const char* name);

// Bring interface down
bool ifdown(const char* name);

// Ping a host
bool ping(const char* host, int count = 4, int timeout_ms = 1000);

// Get host by name
const char* resolve_host(const char* hostname);

// Get hostname
const char* get_hostname();

// Set hostname
void set_hostname(const char* hostname);

// Get DNS servers
int get_dns_servers(char* servers[], int max_servers);

// Set DNS servers
void set_dns_servers(const char* servers[], int count);

// Get proxy settings
bool get_proxy_enabled();
const char* get_proxy_host();
int get_proxy_port();

// Set proxy settings
void set_proxy(bool enabled, const char* host, int port);

// Get download speed (bytes/sec)
uint64_t get_download_speed();

// Get upload speed (bytes/sec)
uint64_t get_upload_speed();

// Get total bytes received
uint64_t get_total_rx();

// Get total bytes sent
uint64_t get_total_tx();

} // namespace network

// ============================================
// Audio API
// ============================================

namespace audio {

// Audio device info
struct DeviceInfo {
    char name[64];
    int sample_rate;
    int channels;
    int bits_per_sample;
    bool input;
    bool output;
    bool default_device;
};

// Get number of audio devices
int get_device_count();

// Get audio device info
bool get_device_info(int index, DeviceInfo* info);

// Get default output device
int get_default_output();

// Set default output device
void set_default_output(int index);

// Get default input device
int get_default_input();

// Set default input device
void set_default_input(int index);

// Get master volume (0-100)
int get_master_volume();

// Set master volume
void set_master_volume(int volume);

// Get mute state
bool is_muted();

// Set mute state
void set_muted(bool muted);

// Play a WAV file
bool play_wav(const char* path);

// Stop playback
void stop_playback();

// Pause playback
void pause_playback();

// Resume playback
void resume_playback();

// Get playback position (seconds)
float get_playback_position();

// Seek to position
void seek(float seconds);

// Get playback duration (seconds)
float get_playback_duration();

// Record audio
bool start_recording(const char* path);

// Stop recording
void stop_recording();

} // namespace audio

// ============================================
// Notification API
// ============================================

namespace notification {

// Show a notification
void show(const char* title, const char* message, int timeout_ms = 5000);

// Show an error notification
void show_error(const char* title, const char* message);

// Show a warning notification
void show_warning(const char* title, const char* message);

// Show an info notification
void show_info(const char* title, const char* message);

// Show a success notification
void show_success(const char* title, const char* message);

// Close all notifications
void close_all();

// Get number of active notifications
int get_active_count();

// Set notification position
void set_position(int x, int y);

// Set notification sound
void set_sound(const char* sound_path);

// Enable/disable notifications
void set_enabled(bool enabled);

// Are notifications enabled?
bool is_enabled();

} // namespace notification

// ============================================
// Settings API
// ============================================

namespace settings {

// Get a setting value
const char* get(const char* key);

// Set a setting value
void set(const char* key, const char* value);

// Delete a setting
void remove(const char* key);

// Check if setting exists
bool exists(const char* key);

// Get all setting keys
int get_keys(char* keys[], int max_keys);

// Save settings to disk
void save();

// Load settings from disk
void load();

// Reset all settings to defaults
void reset_to_defaults();

// Import settings from file
bool import(const char* path);

// Export settings to file
bool export_to(const char* path);

// Get setting type
enum Type {
    TYPE_STRING,
    TYPE_INT,
    TYPE_BOOL,
    TYPE_FLOAT
};

Type get_type(const char* key);

// Get integer setting
int get_int(const char* key, int default_value = 0);

// Set integer setting
void set_int(const char* key, int value);

// Get boolean setting
bool get_bool(const char* key, bool default_value = false);

// Set boolean setting
void set_bool(const char* key, bool value);

// Get float setting
float get_float(const char* key, float default_value = 0.0f);

// Set float setting
void set_float(const char* key, float value);

} // namespace settings

// ============================================
// Time API
// ============================================

namespace time {

// Get current time (Unix timestamp)
uint64_t now();

// Get uptime (seconds)
uint64_t uptime();

// Get wall clock time
struct DateTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int day_of_week;
    int day_of_year;
    bool is_dst;
};

DateTime get_datetime();

// Set system time
void set_datetime(const DateTime& dt);

// Get timezone offset (minutes)
int get_timezone_offset();

// Set timezone offset
void set_timezone_offset(int minutes);

// Get timezone name
const char* get_timezone_name();

// Set timezone
void set_timezone(const char* name);

// Format time as string
void format(const char* format, char* out, int out_len);

// Parse time from string
uint64_t parse(const char* str, const char* format);

// Sleep for milliseconds
void sleep_ms(uint64_t ms);

// Sleep for microseconds
void sleep_us(uint64_t us);

// Get performance counter (ticks)
uint64_t get_ticks();

// Get ticks per second
uint64_t get_ticks_per_second();

// Measure elapsed time
uint64_t elapsed_ms(uint64_t start_ticks);

} // namespace time

} // namespace sys
} // namespace nefu
