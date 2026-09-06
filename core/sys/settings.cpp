// nefuOS
#include "settings.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

SysSettings g_settings;

uint32_t accent_color(int idx) {
    switch (idx) {
    case 1: return 0x002E8B57;  // green
    case 2: return 0x006A5ACD;  // purple
    case 3: return 0x00D2691E;  // orange
    default: return 0x003E87B5; // blue
    }
}

void wallpaper_colors(int idx, uint32_t* top, uint32_t* bottom, uint32_t* base) {
    switch (idx) {
    case 1: // sunset
        *top = 0x00C0563C; *bottom = 0x00F5C86A; *base = 0x00FAEBD7;
        break;
    case 2: // dark
        *top = 0x00141A24; *bottom = 0x0030475E; *base = 0x00223035;
        break;
    default: // ：blue →
        *top = 0x00345C86; *bottom = 0x0088B7D8; *base = 0x00F2EFE8;
        break;
    }
}

static bool parse_int(const char* key, const char* line, int* out) {
    int k = 0;
    while (key[k] && line[k] && key[k] == line[k]) k++;
    if (key[k] != 0) return false;
    while (line[k] == ' ' || line[k] == '=' || line[k] == '\t') k++;
    if (!line[k]) return false;
    int v = 0;
    while (line[k] >= '0' && line[k] <= '9') { v = v * 10 + (line[k] - '0'); k++; }
    *out = v;
    return true;
}

void settings_load() {
    FSNode* f = g_vfs->resolve("/etc/settings.conf");
    if (!f || f->is_dir || f->size == 0) return;
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) return;
    memcpy(buf, f->data, f->size);
    buf[f->size] = 0;
    char* line = buf;
    for (uint32_t i = 0; i < f->size; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            int v = 0;
            if (parse_int("accent", line, &v)) g_settings.accent = v;
            else if (parse_int("wallpaper", line, &v)) g_settings.wallpaper = v;
            else if (parse_int("clock", line, &v)) g_settings.show_clock = (v != 0);
            else if (parse_int("taskbar", line, &v)) g_settings.show_taskbar = (v != 0);
            line = buf + i + 1;
        }
    }
    if (g_settings.accent < 0 || g_settings.accent > 3) g_settings.accent = 0;
    if (g_settings.wallpaper < 0 || g_settings.wallpaper > 2) g_settings.wallpaper = 0;
    kfree(buf);
}

void settings_save() {
    char buf[160];
    int n = ksprintf(buf, sizeof(buf),
        "accent=%d\nwallpaper=%d\nclock=%d\ntaskbar=%d\n",
        g_settings.accent, g_settings.wallpaper,
        g_settings.show_clock ? 1 : 0, g_settings.show_taskbar ? 1 : 0);
    FSNode* f = g_vfs->resolve("/etc/settings.conf");
    if (!f) {
        g_vfs->mkdir("/etc");
        f = g_vfs->create_file("/etc/settings.conf");
    }
    if (f) g_vfs->write_file(f, (const uint8_t*)buf, (uint32_t)n);
}

} // namespace nefu
