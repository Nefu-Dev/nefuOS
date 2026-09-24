# nefuOS System API Library (sysapi) Usage Guide

## Overview

nefuOS provides a comprehensive system API library (`sysapi.h`) that allows applications to interact with the system at a low level. This includes file operations, process management, memory management, network access, display control, power management, and more.

---

## Quick Start

### Including the Library

```cpp
#include <sys/sysapi.h>

using namespace nefu::sys;
```

### Namespaces

| Namespace | Description |
|-----------|-------------|
| `nefu::sys::file` | File system operations |
| `nefu::sys::process` | Process management |
| `nefu::sys::memory` | Memory allocation |
| `nefu::sys::net` | Network operations |
| `nefu::sys::display` | Display control |
| `nefu::sys::power` | Power management |
| `nefu::sys::settings` | System settings |
| `nefu::sys::time` | Time functions |
| `nefu::sys::notification` | Notifications |
| `nefu::sys::app` | Application management |

---

## File Operations

### Check if a file exists

```cpp
bool exists = file::exists("/home/user/document.txt");
```

### Get file size

```cpp
uint64_t size = file::get_size("/home/user/document.txt");
if (size > 0) {
    klogf("File size: %d bytes\n", size);
}
```

### Read a file

```cpp
uint8_t* buffer = 0;
uint32_t size = 0;
if (file::read("/home/user/document.txt", &buffer, &size)) {
    // Use buffer...
    kfree(buffer);  // Don't forget to free!
}
```

### Write a file

```cpp
const char* content = "Hello, nefuOS!";
bool success = file::write("/home/user/hello.txt", 
                          (const uint8_t*)content, 
                          strlen(content));
```

### Delete a file

```cpp
bool success = file::remove("/home/user/old_file.txt");
```

### List directory contents

```cpp
char* files[100];
int count = file::list_dir("/home/user", files, 100);
for (int i = 0; i < count; i++) {
    klogf("  %s\n", files[i]);
}
```

---

## Process Management

### Get current process ID

```cpp
int pid = process::get_pid();
klogf("Current PID: %d\n", pid);
```

### Get parent process ID

```cpp
int ppid = process::get_ppid();
```

### Get process name

```cpp
const char* name = process::get_name();
klogf("Process name: %s\n", name);
```

### Sleep

```cpp
process::sleep(1000);  // Sleep for 1 second (1000 ms)
```

### Exit

```cpp
process::exit(0);  // Exit with code 0
```

---

## Memory Management

### Allocate memory

```cpp
void* ptr = memory::alloc(4096);  // Allocate 4 KB
if (ptr) {
    // Use memory...
    memory::free(ptr);
}
```

### Allocate aligned memory

```cpp
void* ptr = memory::alloc_aligned(4096, 16);  // 16-byte aligned
```

### Get total memory

```cpp
uint64_t total = memory::get_total();
klogf("Total memory: %d MB\n", total / (1024 * 1024));
```

### Get free memory

```cpp
uint64_t free_mem = memory::get_free();
klogf("Free memory: %d MB\n", free_mem / (1024 * 1024));
```

---

## Network Operations

### Get local IP address

```cpp
uint32_t ip = net::get_ip();
klogf("Local IP: %d.%d.%d.%d\n", 
      (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
      (ip >> 8) & 0xFF, ip & 0xFF);
```

### Get MAC address

```cpp
uint8_t mac[6];
net::get_mac(mac);
klogf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
```

### Ping a host

```cpp
bool reachable = net::ping("google.com", 4, 1000);
if (reachable) {
    klogf("Host is reachable!\n");
}
```

### HTTP GET request

```cpp
uint8_t* response = 0;
uint32_t response_size = 0;
if (net::http_get("https://example.com", &response, &response_size)) {
    // Use response...
    kfree(response);
}
```

---

## Display Control

### Get screen width/height

```cpp
int width = display::get_width();
int height = display::get_height();
klogf("Screen: %dx%d\n", width, height);
```

### Set brightness

```cpp
display::set_brightness(80);  // 80% brightness
```

### Turn screen on/off

```cpp
display::power_off();
// ... later ...
display::power_on();
```

---

## Power Management

### Shutdown

```cpp
power::shutdown();
```

### Reboot

```cpp
power::reboot();
```

### Reboot to BIOS

```cpp
power::reboot_to_bios();
```

### Lock screen

```cpp
power::lock_screen();
```

### Get battery percentage

```cpp
int battery = power::get_battery_percent();
if (battery >= 0) {
    klogf("Battery: %d%%\n", battery);
}
```

---

## System Settings

### Get a setting

```cpp
const char* value = settings::get("wallpaper");
klogf("Wallpaper: %s\n", value ? value : "(not set)");
```

### Set a setting

```cpp
settings::set("wallpaper", "/usr/share/wallpapers/default.png");
```

### Get integer setting

```cpp
int volume = settings::get_int("volume", 80);
klogf("Volume: %d\n", volume);
```

### Set boolean setting

```cpp
settings::set_bool("dark_mode", true);
```

---

## Time Functions

### Get current time

```cpp
uint64_t now = time::now();  // Unix timestamp
klogf("Now: %d\n", now);
```

### Get uptime

```cpp
uint64_t uptime = time::uptime();  // Seconds since boot
klogf("Uptime: %d seconds\n", uptime);
```

### Sleep

```cpp
time::sleep_ms(500);  // Sleep for 500 ms
```

---

## Notifications

### Show an info notification

```cpp
notification::show_info("System", "Welcome to nefuOS!");
```

### Show an error notification

```cpp
notification::show_error("Error", "Failed to open file");
```

---

## Application Management

### Launch an application

```cpp
int pid = app::launch("browser", "https://example.com");
if (pid > 0) {
    klogf("Launched browser, PID: %d\n", pid);
}
```

### List running applications

```cpp
int pids[100];
int count = app::list_running(pids, 100);
for (int i = 0; i < count; i++) {
    klogf("  PID %d\n", pids[i]);
}
```

### Terminate an application

```cpp
app::terminate(pid);
```

---

## Complete Example

Here's a complete example that uses multiple system APIs:

```cpp
#include <sys/sysapi.h>
#include <klib/klib.h>

using namespace nefu::sys;

int main() {
    // Print system info
    klogf("=== nefuOS System Info ===\n");
    
    int width = display::get_width();
    int height = display::get_height();
    klogf("Screen: %dx%d\n", width, height);
    
    uint64_t total_mem = memory::get_total();
    klogf("Total memory: %d MB\n", total_mem / (1024 * 1024));
    
    uint64_t uptime = time::uptime();
    klogf("Uptime: %d seconds\n", uptime);
    
    // Show a welcome notification
    notification::show_info("System", "nefuOS started successfully!");
    
    return 0;
}
```

---

## Best Practices

1. **Always check return values** - Most functions return `bool` or `int` to indicate success/failure.

2. **Free allocated memory** - When using `file::read()` or `net::http_get()`, always free the returned buffer with `kfree()`.

3. **Use namespaces** - Don't pollute the global namespace; use `nefu::sys::` prefix or `using namespace nefu::sys;`.

4. **Error handling** - Wrap system calls in error handling code:
   ```cpp
   if (!file::exists(path)) {
       notification::show_error("Error", "File not found");
       return;
   }
   ```

5. **Resource cleanup** - Use RAII patterns where possible to ensure resources are freed.

---

## API Reference Summary

| Category | Key Functions |
|----------|---------------|
| File | `exists()`, `read()`, `write()`, `remove()`, `list_dir()` |
| Process | `get_pid()`, `get_name()`, `sleep()`, `exit()` |
| Memory | `alloc()`, `free()`, `get_total()`, `get_free()` |
| Network | `get_ip()`, `get_mac()`, `ping()`, `http_get()` |
| Display | `get_width()`, `get_height()`, `set_brightness()` |
| Power | `shutdown()`, `reboot()`, `lock_screen()`, `get_battery_percent()` |
| Settings | `get()`, `set()`, `get_int()`, `set_bool()` |
| Time | `now()`, `uptime()`, `sleep_ms()` |
| Notification | `show_info()`, `show_error()`, `show_warning()` |
| App | `launch()`, `list_running()`, `terminate()` |

---

## License

This API library is part of nefuOS and is licensed under the MIT License.

For more information, see the nefuOS documentation or source code.
