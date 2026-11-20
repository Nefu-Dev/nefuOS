/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS system main entry:init,input dispatch,frame loop
#include "platform.h"
#include "klib/klib.h"
#include "gui/desktop.h"
#include "gui/gfx.h"
#include "gui/wm.h"
#include "vfs/vfs.h"
#include "vfs/nvfs.h"
#include "apps/apps.h"
#include "sys/settings.h"
#include "sys/power.h"
#include "net/net.h"
#include "audio/audio_engine.h"
#include "sys/sha256.h"
#include "ime/pinyin.h"
#include "apps/update.h"
#include "lib/llm.h"

#ifdef NEFU_BARE
extern "C" void nefu_elf_selfcheck(void);   // VFS-backed Linux-ELF run (runelf path)
extern "C" void nefu_deb_selftest(void);    // VFS-backed .deb install + run selftest
extern "C" void nefu_x11_selftest(void);    // embedded X11 server + guest X client
extern "C" void nefu_time_selftest(void);   // RTC decode + timezone contract (COM1)
extern "C" void nefu_net_selftest(void);    // bare HTTP(S): npkg registry + update manifest
extern "C" void nefu_net_selftest_reset(void);
extern "C" bool nefu_net_selftest_step(void);
#endif

namespace nefu {

// -1 = use the persisted /etc/settings.conf value, 0/1 = force English/Chinese
// (host CLI `--lang N`; also handy for kiosk deployments).
int g_lang_override = -1;

// Off unless the host passes `--llm` (see core/platform.h): the int8 inference
// engine is experimental, so the shipped chatbot stays on its rule table.
bool g_llm_opt_in = false;

// nefuLLM boot self-test (bare COM1): load the real model disk, build the
// tokenizer workspace, run one short generation, log the result, then free
// everything so the chatbot re-loads it lazily on first English message.
#ifdef NEFU_BARE
static void llm_boot_selftest(void) {
    klogf("llm: selftest enter\n");
    uint8_t* blob = 0; uint32_t blob_sz = 0;
    if (!platform_llm_load(&blob, &blob_sz)) { klogf("llm: no model disk\n"); return; }
    size_t ws = llm::llm_workspace_needed(blob, blob_sz);
    void* wp = kalloc(ws ? ws : 1);
    if (!wp) { klogf("llm: ws alloc fail (%u B)\n", (unsigned)ws); kfree(blob); return; }
    llm::Model* m = 0;
    int rc = llm::llm_load(blob, blob_sz, wp, ws, &m);
    klogf("llm: blob %u B ws %u B load rc=%d\n", blob_sz, (unsigned)ws, rc);
    if (rc == 0) {
        size_t sc = llm::llm_scratch_needed(m);
        void* sp = kalloc(sc ? sc : 1);
        if (sp) {
            char out[160];
            int n = llm::llm_generate(m, "Once upon a time", out, (int)sizeof(out),
                                      sp, sc, 24);
            out[sizeof(out) - 1] = 0;
            klogf("llm: gen n=%d [%s]\n", n, out);
            kfree(sp);
        } else {
            klogf("llm: scratch alloc fail (%u B)\n", (unsigned)sc);
        }
    }
    kfree(wp);
    kfree(blob);
    klogf("llm: boot self-test done\n");
}
#endif


static bool s_inited = false;
static bool s_booted = false;
static uint32_t s_boot_start = 0;
static bool s_net_selftest_done = false;
static bool s_net_selftest_started = false;
static int s_mx = -8, s_my = -8;
// modifier key state: bit0=Shift bit1=Ctrl bit2=Alt (fed into KeyEvent.mods)
static uint8_t s_mods = 0;
// ---- lock screen (UEFI password gate) state ----
static bool s_locked = true;        // desktop starts behind the lock screen
static char s_lock_pwd[65] = "";
static int s_lock_len = 0;
static uint32_t s_lock_fail_ms = 0;
static uint32_t s_last_activity = 0;
// ---- first-boot setup wizard state ----
static uint32_t s_super_press_ms = 0;   // tick when the Win key was pressed (0 = up)
static bool s_super_alone = false;      // Win pressed & released alone (no combo key)
static bool s_setup = false;
static char s_setup_user[33] = "user";
static char s_setup_pwd1[65] = "";
static char s_setup_pwd2[65] = "";
static int  s_setup_field = 0;    // 0=user 1=password 2=confirm 3=engine
static int  s_setup_engine = 0;   // 0=minijs 1=noscript
static int  s_setup_len[3] = {0, 0, 0};
static uint32_t s_setup_fail_ms = 0;

bool nefuos_setup_active() { return s_setup; }
int  nefuos_setup_field() { return s_setup_field; }
int  nefuos_setup_engine() { return s_setup_engine; }
const char* nefuos_setup_user() { return s_setup_user; }
int  nefuos_setup_pwd_len(int field) {
    if (field < 0 || field > 2) return 0;
    return s_setup_len[field];
}
uint32_t nefuos_setup_fail_ms() { return s_setup_fail_ms; }

void nefuos_mark_firstboot_done() {
    if (!g_vfs) return;
    g_vfs->mkdir("/var/lib/nefuos");
    FSNode* f = g_vfs->create_file("/var/lib/nefuos/firstboot");
    if (f) g_vfs->write_file(f, (const uint8_t*)"1", 1);
    s_setup = false;  // wizard state already latched in nefuos_frame
}

static bool first_boot_pending() {
    return (g_vfs->resolve("/var/lib/nefuos/firstboot") == 0);
}

static void finish_setup() {
    if (s_setup_len[1] == 0 || strcmp(s_setup_pwd1, s_setup_pwd2) != 0) {
        s_setup_fail_ms = platform_tick_ms();
        s_setup_pwd1[0] = 0; s_setup_pwd2[0] = 0;
        s_setup_len[1] = 0; s_setup_len[2] = 0;
        s_setup_field = 1;
        return;
    }
    if (s_setup_len[0] == 0) strcpy(s_setup_user, "user");
    strncpy(g_uefi.username, s_setup_user, 31);
    g_uefi.username[31] = 0;
    uint8_t h[32];
    nefu_sha256((const uint8_t*)s_setup_pwd1, (uint32_t)s_setup_len[1], h);
    char hex[65];
    nefu_sha256_hex(h, hex);
    strncpy(g_uefi.password_hash, hex, 64);
    g_uefi.password_hash[64] = 0;
    platform_uefi_save(&g_uefi);
    // persist browser engine choice into /etc/nefu.conf
    FSNode* conf = g_vfs->resolve("/etc/nefu.conf");
    if (conf) {
        char buf[1024];
        uint32_t n = conf->size < 1023 ? conf->size : 1023;
        memcpy(buf, conf->data, n);
        buf[n] = 0;
        char* p = strstr(buf, "browser_engine=");
        char line[48];
        ksprintf(line, sizeof(line), "browser_engine=%s\n",
                 s_setup_engine == 1 ? "noscript" : "minijs");
        if (p) {
            char* nl = strchr(p, '\n');
            size_t old_len = nl ? (size_t)(nl + 1 - p) : strlen(p);
            size_t rest = strlen(p + old_len);
            memmove(p + strlen(line), p + old_len, rest + 1);
            memcpy(p, line, strlen(line));
        } else {
            size_t cl = strlen(buf);
            if (cl + strlen(line) < sizeof(buf) - 1) {
                memcpy(buf + cl, line, strlen(line) + 1);
            }
        }
        g_vfs->write_file(conf, (const uint8_t*)buf, (uint32_t)strlen(buf));
    }
    // firstboot marker so the setup wizard does not run again
    g_vfs->mkdir("/var/lib/nefuos");
    FSNode* m = g_vfs->create_file("/var/lib/nefuos/firstboot");
    if (m) g_vfs->write_file(m, (const uint8_t*)"done", 4);
    // The account itself, for platforms whose firmware store is not writable
    // (bare metal): nefuos_init() restores user + hash from here.
    {
        char abuf[160];
        ksprintf(abuf, sizeof(abuf), "user=%s\nhash=%s\n", g_uefi.username,
                 g_uefi.password_hash);
        FSNode* af = g_vfs->create_file("/var/lib/nefuos/account");
        if (af) g_vfs->write_file(af, (const uint8_t*)abuf, (uint32_t)strlen(abuf));
    }
    s_setup = false;
    s_locked = true;
    s_lock_len = 0;
    s_lock_pwd[0] = 0;
}

bool nefuos_is_locked() { return s_locked; }
int nefuos_lock_len() { return s_lock_len; }
const char* nefuos_lock_pwd() { return s_lock_pwd; }
uint32_t nefuos_lock_fail_ms() { return s_lock_fail_ms; }

void nefuos_lock_screen() {
    s_locked = true;
    s_lock_len = 0;
    s_lock_pwd[0] = 0;
}

void nefuos_unlock() {
    s_locked = false;
    s_lock_len = 0;
    s_lock_pwd[0] = 0;
    s_lock_fail_ms = 0;
    s_last_activity = platform_tick_ms();
}

static void lock_verify() {
    bool ok = false;
    if (g_uefi.password_hash[0] == 0) {
        ok = true;   // no UEFI password configured -> unlock freely
    } else {
        uint8_t h[32];
        nefu_sha256((const uint8_t*)s_lock_pwd, (uint32_t)s_lock_len, h);
        char hex[65];
        nefu_sha256_hex(h, hex);
        ok = (strcmp(hex, g_uefi.password_hash) == 0);
    }
    s_lock_len = 0;
    s_lock_pwd[0] = 0;
    if (ok) {
        s_locked = false;
        s_lock_fail_ms = 0;
        // The lock screen paints directly into the framebuffer, bypassing
        // LVGL. Without this, LVGL would only redraw its dirty regions on the
        // next desktop frame and the lock-screen pixels (panel, clock) would
        // stay on screen after unlocking.
        desktop_mark_dirty();
    } else {
        s_lock_fail_ms = platform_tick_ms();
    }
}

// (8x13,MSB=left)
static const unsigned char CURSOR[13] = { 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xF0, 0xF0,
                                          0xF0, 0xE8, 0xCC, 0x8C, 0x0C, 0x08 };

static void draw_cursor(Surface& fb, int mx, int my) {
    for (int y = 0; y < 13; y++) {
        for (int x = 0; x < 8; x++) {
            if (CURSOR[y] & (0x80 >> x)) {
                gfx::pixel(fb, mx + x, my + y, color::BLACK);
                if (y < 12 && !(CURSOR[y + 1] & (0x80 >> x))) {
                    gfx::pixel(fb, mx + x, my + y + 1, color::WHITE);
                }
            }
        }
    }
    gfx::pixel(fb, mx + 7, my + 12, color::WHITE);
}

void nefuos_init() {
    klogf("nefuOS init\n");
    g_vfs = new VFS();
    uint8_t* data = 0;
    uint32_t sz = 0;
    bool storage = platform_fs_load(&data, &sz) && data;
    bool fresh_nvfs = false;            // image built this boot (no disk copy yet)
    if (storage && nvfs::load_image(data, sz) && nvfs::mount(0)) {
        // Real NVFS image: journal replay already ran inside mount().
        nvfs::nvfs_to_vfs();
        klogf("NVFS mounted: %u bytes image, %u blocks free (%u inodes)\n",
              sz, (unsigned)(nvfs::free_space() / 512), nvfs::used_inode_count());
    } else if (storage && g_vfs->load(data, sz)) {
        // Legacy NFS1 snapshot: keep the tree, convert it to NVFS on save.
        klogf("legacy VFS snapshot loaded (%u bytes); converting to NVFS on save\n", sz);
        nvfs::init();
        fresh_nvfs = true;
    } else {
        if (storage) nvfs::unmount();   // release the unreadable buffer
        nvfs::init();
        g_vfs->create_default_tree();
        fresh_nvfs = true;
        klogf("no NVFS image found: fresh filesystem + default tree\n");
    }
    // even after loading a snapshot the standard hierarchy must exist
    g_vfs->ensure_standard_dirs();
    g_vfs->ensure_default_files();
    // like a fresh Ubuntu install: every directory carries at least one real
    // file, so no folder in the image is blank
    g_vfs->seed_empty_dirs();
    // remove stray nodes left by an older browser_save_page that flattened
    // the whole path into a single name (e.g. "_usr_downloads_page_...")
    g_vfs->cleanup_stray_nodes();
    // Account: bare metal has no writable firmware store (platform_uefi_save is
    // a stub there), so the wizard keeps the account in the filesystem too.
    // Apply it only when the platform itself has no password configured, which
    // keeps a host-side uefi config or a manual setup authoritative.
    if (g_uefi.password_hash[0] == 0) {
        FSNode* acc = g_vfs->resolve("/var/lib/nefuos/account");
        if (acc && acc->data && acc->size > 0 && acc->size < 512) {
            char abuf[512];
            memcpy(abuf, acc->data, acc->size);
            abuf[acc->size] = 0;
            char* u = strstr(abuf, "user=");
            char* hh = strstr(abuf, "hash=");
            if (u) {
                u += 5;
                char* e = u; while (*e && *e != '\n' && *e != '\r') e++;
                *e = 0;
                if (*u) strncpy(g_uefi.username, u, 31), g_uefi.username[31] = 0;
            }
            if (hh) {
                hh += 5;
                char* e = hh; while (*e && *e != '\n' && *e != '\r') e++;
                *e = 0;
                if (*hh) strncpy(g_uefi.password_hash, hh, 64), g_uefi.password_hash[64] = 0;
            }
            klogf("account restored from NVFS: user=%s\n", g_uefi.username);
        }
    }
    klogf("nvfs: boot tree dirty: all=%d paths=%d\n", (int)g_vfs->dirty_all(), g_vfs->dirty_count());
    // The steps above can change the tree on any boot, not just the first one:
    // a later boot still fills directories that were empty in the image and adds
    // default files that were missing. Persist those edits now — dirty_reset()
    // below throws the dirty table away, so anything left unflushed here would
    // silently vanish on the next boot.
    if (nvfs::is_mounted() &&
        (fresh_nvfs || g_vfs->dirty_all() || g_vfs->dirty_count() > 0)) {
        klogf("nvfs: boot changes: all=%d paths=%d\n", (int)g_vfs->dirty_all(), g_vfs->dirty_count());
        for (int i = 0; i < 4 && i < g_vfs->dirty_count(); i++)
            klogf("  dirty: %s\n", g_vfs->dirty_path(i));
        nvfs::vfs_to_nvfs();
        int nw = nvfs::flush();
        klogf("nvfs: boot tree written (%d blocks, %u free)\n",
              nw, (unsigned)(nvfs::free_space() / 512));
    }
    // whatever the boot itself wrote is already mirrored
    g_vfs->dirty_reset();
    settings_load();
    // deployment / testing override: `nefuOS.exe --lang 1` forces Chinese even
    // when /etc/settings.conf was written with another value. Applied after
    // settings_load() (which resets the struct) and before the desktop builds
    // its labels, so every T() picks the forced language.
    if (g_lang_override >= 0) g_settings.lang = g_lang_override;
    // Hand the persisted timezone to the time layer before anything draws a
    // clock: time() is true UTC, localtime() and the desktop clock add this
    // offset. /etc/settings.conf keeps tz=<minutes east> and rtc_utc=<0|1>.
    platform_set_tz_offset_minutes(g_settings.tz_offset_min);
    platform_set_rtc_is_utc(g_settings.rtc_is_utc);
    klogf("time: tz=%d min, rtc=%s\n", g_settings.tz_offset_min,
          g_settings.rtc_is_utc ? "utc" : "local");
    ime::init();
    apps_preinstall_defaults();   // ship store apps pre-installed
    store_load();
    g_wm = new WM();
    desktop_init();
    if (platform_name()[0] == 'b') __asm__ volatile("sti");   // enable IRQs before blocking net ops
    // bare-metal NIC (e1000/RTL8139/virtio in QEMU); win32 host has none -> skipped by name
    if (platform_name()[0] == 'b') {
        if (net_init()) {
            klogf("net: NIC up, ip=10.0.2.15\n");
        } else {
            klogf("net: no supported NIC found\n");
        }
        // bare-metal audio: probe real PCI sound cards (AC'97 / HD Audio);
        // the ISA SB16 fallback is probed later by the backend.
        if (audio::engine_init()) {
            klogf("audio: %s ready\n", audio::engine_device_name());
        } else {
            klogf("audio: no PCI sound card (falling back to SB16 probe)\n");
        }
    }
    s_boot_start = platform_tick_ms();
    // real block-device enumeration (ATA probe on bare metal, Windows
    // logical drives on the host) - logged so it is visible in headless runs
    {
        DiskInfo di[8];
        int dn = platform_disk_scan(di, 8);
        klogf("disk: %d block device(s)\n", dn);
        for (int i = 0; i < dn; i++)
            klogf("  %s %u MB %s\n", di[i].name,
                  (unsigned)(di[i].sectors / 2048),
                  di[i].removable ? "removable" : "fixed");
    }
#ifdef NEFU_BARE
    nefu_elf_selfcheck();   // VFS-backed Linux-ELF run (runelf path), headless COM1
    nefu_deb_selftest();    // VFS-backed .deb install + run (headless COM1)
    nefu_x11_selftest();    // embedded X11 server + guest X client (headless COM1)
    nefu_time_selftest();   // RTC decode + timezone contract (headless COM1)
    llm_boot_selftest();    // nefuLLM: load the real model disk + run one reply (COM1)
#endif
    s_inited = true;
    if (platform_name()[0] == 'b') __asm__ volatile("sti");   // enable IRQs for tick/input
    klogf("nefuOS ready on %s\n", platform_name());
}

void nefuos_shutdown() {
    if (!s_inited) return;
    klogf("nefuOS shutdown\n");
    if (nvfs::is_mounted()) {
        // Serialize the VFS tree into the NVFS image and flush the journal,
        // then hand the whole image to the platform for persistence.
        nvfs::vfs_to_nvfs();
        uint8_t* img = 0;
        uint32_t isz = 0;
        if (nvfs::image(&img, &isz) && img) {
            platform_fs_save(img, isz);
            klogf("NVFS saved: %u bytes (%u blocks free, %u inodes)\n",
                  isz, (unsigned)(nvfs::free_space() / 512), nvfs::used_inode_count());
        }
        nvfs::unmount();
    } else {
        // Defensive fallback: legacy flat snapshot (NVFS should always be up).
        uint8_t* data = 0;
        uint32_t sz = 0;
        if (g_vfs->save(&data, &sz)) {
            platform_fs_save(data, sz);
            kfree(data);
        }
    }
    s_inited = false;
}

uint32_t nefuos_uptime_ms() {
    if (!s_inited) return 0;
    return platform_tick_ms() - s_boot_start;
}

void nefuos_handle_mouse(int x, int y, uint8_t buttons) {
    s_mx = x;
    s_my = y;
    if (!s_inited || !s_booted) return;
    s_last_activity = platform_tick_ms();
    // Blue screen: only its Reboot / Recovery buttons are clickable.
    if (g_crash_active) { bsod_handle_mouse(x, y, buttons); return; }
    static int s_nhdbg = 0;
    if (s_nhdbg < 20) { s_nhdbg++; klogf("nh m=%d,%d b=%u locked=%d\n", x, y, (unsigned)buttons, (int)s_locked); }
    if (s_locked) return;   // lock screen consumes nothing from the desktop
    if (!desktop_handle_mouse(x, y, buttons)) {
        g_wm->handle_mouse(x, y, buttons);
    }
}

void nefuos_handle_key(int keycode, char ascii, bool down, const char* utf8) {
    if (!s_inited || !s_booted) return;
    // Blue screen swallows every key: it cannot be dismissed by keyboard.
    if (g_crash_active) return;
    // Track modifier keys (both down and up); they are consumed here and never
    // become normal key events. This is what powers Shift+/Ctrl+ shortcuts.
    if (keycode == KEY_SHIFT) { if (down) s_mods |= 0x01; else s_mods &= ~0x01; return; }
    // Ctrl was never tracked, so every "mods & 0x02" test in the system was
    // dead code (Ctrl+Shift+U, the browser's Ctrl+L/T/W shortcuts). Both
    // backends already deliver KEY_CTRL, so tracking it is all that is needed.
    if (keycode == KEY_CTRL)  { if (down) s_mods |= 0x02; else s_mods &= ~0x02; return; }
    if (keycode == KEY_FAST) {
        if (down) {
            s_mods |= 0x08;
            s_super_press_ms = platform_tick_ms();
            s_super_alone = true;
        } else {
            s_mods &= ~0x08;
            // Bare Win press+release within 500 ms (no combo key) -> fast update
            if (s_super_alone && s_super_press_ms &&
                (platform_tick_ms() - s_super_press_ms) < 500 && !s_locked) {
                update_global_shortcut();
            }
            s_super_press_ms = 0;
            s_super_alone = false;
        }
        return;
    }
    if (keycode == KEY_ALT)   { if (down) s_mods |= 0x04; else s_mods &= ~0x04; return; }
    if (!down) return;
    s_last_activity = platform_tick_ms();
    if (s_super_press_ms) s_super_alone = false;   // any key while Win held = combo, not a bare Win
    // Super/Win (⊞/⌘) shortcuts — global hotkeys, work anywhere on the desktop
    if (s_mods & 0x08) {
        char c = ascii;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        switch (c) {
        case 'd': case 'm':                     // Win+D / Win+M: show desktop
            g_wm->minimize_all();
            return;
        case 'f':                               // Win+F: fast system update (incremental)
        case 'u':                               // Win+U: same as Ctrl+Shift+U
            update_global_shortcut();
            return;
        case 'e': app_launch(APP_FILEMGR); return;      // Win+E: file manager
        case 'r': app_launch(APP_TERMINAL); return;     // Win+R: terminal
        case 'b': app_launch(APP_BROWSER); return;      // Win+B: browser
        case 'l': nefuos_lock_screen(); return;         // Win+L: lock screen
        case 'p': app_launch(APP_SETTINGS); return;     // Win+P: settings
        case 't': app_launch(APP_TASKMGR); return;      // Win+T: task manager
        case 'c': app_launch(APP_CALC); return;         // Win+C: calculator
        case 'g': app_launch(APP_GAMECENTER); return;   // Win+G: game center
        case 's': app_launch(APP_SEARCH); return;       // Win+S: search
        case 'w': app_launch(APP_WALLPAPER); return;    // Win+W: wallpaper
        case 'i': app_launch(APP_INPUTMETHOD); return;  // Win+I: input method
        default: break;
        }
    }
    // Ctrl+Shift+U: quick system update (progress window, non-blocking)
    if ((s_mods & 0x02) && (s_mods & 0x01) && (ascii == 'u' || ascii == 'U')) {
        update_global_shortcut();
        return;
    }
    if (s_setup) {
        char* dst = (s_setup_field == 0) ? s_setup_user
                  : (s_setup_field == 1) ? s_setup_pwd1 : s_setup_pwd2;
        if (ascii >= 32 && ascii < 127 && s_setup_len[s_setup_field] < 63 && s_setup_field < 3) {
            dst[s_setup_len[s_setup_field]++] = ascii;
            dst[s_setup_len[s_setup_field]] = 0;
        } else if (keycode == KEY_BACKSPACE && s_setup_field < 3 && s_setup_len[s_setup_field] > 0) {
            s_setup_len[s_setup_field]--;
            dst[s_setup_len[s_setup_field]] = 0;
        } else if (keycode == KEY_TAB) {
            s_setup_field = (s_setup_field + 1) % 4;
        } else if (keycode == KEY_ENTER) {
            if (s_setup_field == 3) finish_setup();
            else s_setup_field++;
        } else if ((keycode == KEY_LEFT || keycode == KEY_RIGHT) && s_setup_field == 3) {
            s_setup_engine = 1 - s_setup_engine;
        } else if (keycode == KEY_ESC) {
            s_setup_field = 3;   // jump straight to Finish
        }
        return;
    }
    if (s_locked) {
        if (ascii >= 32 && ascii < 127 && s_lock_len < 63) {
            s_lock_pwd[s_lock_len++] = ascii;
            s_lock_pwd[s_lock_len] = 0;
        } else if (keycode == KEY_BACKSPACE && s_lock_len > 0) {
            s_lock_len--;
            s_lock_pwd[s_lock_len] = 0;
        } else if (keycode == KEY_ENTER) {
            lock_verify();
        }
        return;
    }
    // ---- built-in Pinyin IME (Chinese input): intercept keys while enabled ----
    {
        char ime_out[16];
        if (ime::handle_key(keycode, ascii, s_mods, ime_out, sizeof(ime_out))) {
            if (ime_out[0]) {
                // Commit: deliver as a UTF-8 key event (LVGL textarea first,
                // then the focused classic window such as the editor/terminal).
                if (!desktop_handle_key(KEY_NONE, 0, s_mods, ime_out)) {
                    KeyEvent e;
                    e.keycode = KEY_NONE;
                    e.ascii = 0;
                    e.down = true;
                    e.mods = s_mods;
                    for (int i = 0; i < 8; i++) e.utf8[i] = ime_out[i] ? ime_out[i] : 0;
                    g_wm->handle_key(&e);
                }
            }
            return;
        }
    }
    if (desktop_handle_key(keycode, ascii, s_mods, utf8)) return;
    KeyEvent e;
    e.keycode = keycode;
    e.ascii = ascii;
    e.down = true;
    e.mods = s_mods;
    for (int i = 0; i < 8; i++) e.utf8[i] = (utf8 && utf8[i]) ? utf8[i] : 0;
    g_wm->handle_key(&e);
}

void nefuos_handle_scroll(int delta) {
    if (!s_inited || !s_booted) return;
    if (s_locked) return;   // lock screen consumes nothing
    // LVGL objects under the cursor (store list, start menu) scroll first;
    // classic WM windows keep their own scroll_y handling.
    if (desktop_handle_scroll(delta)) return;
    g_wm->handle_scroll(delta);
}

// ---- NVFS write-back (autosave) ----
// The bare-metal kernel commits the filesystem four times a second; the host
// only every four seconds (platform_fs_flush_interval_ms decides). Only the
// paths the VFS reported as changed are mirrored into the image, and flush()
// writes just the blocks those paths dirtied: a full 4 MiB rebuild at this
// cadence would be unaffordable on a PIO disk. A delete or a move cannot be
// mirrored path by path, so the VFS raises dirty_all() and we fall back to the
// original full rebuild for that interval.
static uint32_t s_fs_flush_ms = 0;
static bool     s_fs_flush_started = false;

static void nvfs_autosave() {
    if (!nvfs::is_mounted() || !g_vfs) return;
    uint32_t now = platform_tick_ms();
    if (!s_fs_flush_started) {           // first tick: just take the baseline
        s_fs_flush_started = true;
        s_fs_flush_ms = now;
        return;
    }
    if (now - s_fs_flush_ms < platform_fs_flush_interval_ms()) return;
    s_fs_flush_ms = now;

    if (g_vfs->dirty_all()) {
        nvfs::vfs_to_nvfs();
        g_vfs->dirty_reset();
        int n = nvfs::flush();
        klogf("nvfs: rebuild, %d blocks written\n", n);
        return;
    }
    int count = g_vfs->dirty_count();
    if (count == 0) {
        if (nvfs::dirty_blocks() > 0) nvfs::flush();   // e.g. journal cleared on mount
        return;
    }
    for (int i = 0; i < count; i++) {
        if (!nvfs::sync_path(g_vfs->dirty_path(i))) {
            klogf("nvfs: cannot mirror %s, rebuilding image\n", g_vfs->dirty_path(i));
            nvfs::vfs_to_nvfs();
            g_vfs->dirty_reset();
            nvfs::flush();
            return;
        }
    }
    g_vfs->dirty_drop(count);
    if (nvfs::flush() < 0)
        klogf("nvfs: flush failed, %u blocks pending\n", nvfs::dirty_blocks());
}

void nefuos_screen_resized(int w, int h) {
    if (w < 320 || h < 200) return;
    klogf("screen: resized to %dx%d\n", w, h);
    desktop_on_screen_resize(w, h);
    // windows that now sit outside the smaller work area are pulled back in
    // on the next paint (wm.cpp clamps against the live screen size)
    desktop_mark_dirty();
}

void nefuos_tick() {
    // audio heartbeat: keep the real sound card fed (mix + submit each tick)
#ifdef NEFU_BARE
    audio::engine_pump();
#endif
    // idle auto-lock (configured in Settings -> idle_lock_sec)
    if (s_booted && !s_locked && g_settings.idle_lock_sec > 0 &&
        platform_tick_ms() - s_last_activity > (uint32_t)g_settings.idle_lock_sec * 1000u) {
        nefuos_lock_screen();
    }
    if (g_net.up) {
        net_poll();   // drain NIC
        // Deferred link self-test: run once a few seconds after boot so the
        // cold-start path stays fast (ping/tcp waits must not block init).
        //
        // It must also wait for the first-boot wizard to be finished: the
        // self-test is a long blocking chain (ping + TLS + a 3 MB bundle
        // download + matchmaking + a WebSocket round trip) that takes tens of
        // seconds. Running it while the wizard is up freezes the UI, and the
        // keystrokes typed into the wizard are lost, so the setup never
        // completes and the desktop never appears. Only start it once the
        // user is actually in.
        if (!s_net_selftest_done && s_booted && !s_setup && !s_locked &&
            platform_tick_ms() - s_boot_start > 3000) {
            s_net_selftest_done = true;
            uint32_t t0 = platform_tick_ms();
            bool pong = net_ping(g_net.gw, 2000);
            klogf("net: ping gateway %s (%u ms)\n", pong ? "OK" : "FAIL",
                  (unsigned)(platform_tick_ms() - t0));
            klogf("net: rx=%u tx=%u arp=%u icmp=%u tcp=%u\n",
                  g_net.rx_count, g_net.tx_count, g_net.arp_reqs, g_net.icmp_reqs, g_net.tcp_conns);
            int sock = tcp_connect(g_net.gw, 8000, 3000);
            if (sock >= 0) {
                tcp_send(sock, "GET / HTTP/1.0\r\n\r\n", 18);
                char tbuf[512];
                int got = tcp_recv(sock, tbuf, sizeof(tbuf) - 1, 3000);
                if (got > 0) {
                    tbuf[got] = 0;
                    if (got > 60) tbuf[60] = 0;
                    klogf("net: http %d bytes: %s\n", got, tbuf);
                } else {
                    klogf("net: http recv empty\n");
                }
                tcp_close(sock);
            } else {
                klogf("net: tcp connect FAIL (no listener on 10.0.2.2:8000)\n");
            }
#ifdef NEFU_BARE
            // bare HTTP(S) proof: npkg registry (http) + update manifest (https).
            // Runs here (a few seconds after boot) because platform_tick_ms is
            // live in the main loop - at cold init the tick source is not yet
            // running and timeout loops would spin forever.
            //
            // The self-test is stepped, not run in one go: a monolithic run
            // blocks the single bare thread for tens of seconds and the UI
            // freezes with it. The link check below stays in this branch; the
            // HTTP(S) self-test advances one step per frame further down.
            nefu_net_selftest_reset();
            s_net_selftest_started = true;
#endif
        }
#ifdef NEFU_BARE
        // Advance the bare HTTP(S) self-test by one step per frame. Each step
        // is a separate network round trip, so the UI keeps running between
        // them instead of freezing for the whole chain.
        if (s_net_selftest_started && !s_net_selftest_done) {
            if (nefu_net_selftest_step()) s_net_selftest_done = true;
        }
#endif
    }
    // per-window animation / game update heartbeat
    if (g_wm) g_wm->tick();
    // filesystem write-back (bare: 4x per second, host: every 4 s)
    if (s_inited && !s_setup) nvfs_autosave();
}

void nefuos_frame() {
    if (!s_inited) return;
    Surface fb = screen_surface();
    // Bare metal paints its own pointer. LVGL only redraws dirty regions, so
    // the pixel under the previous arrow has to be invalidated before the
    // desktop pass, otherwise every move leaves a trail of cursors behind.
    static int s_cur_drawn_x = -1, s_cur_drawn_y = -1;
    if (s_mx >= 0 && s_my >= 0 && platform_name()[0] == 'b' &&
        s_cur_drawn_x >= 0 && (s_cur_drawn_x != s_mx || s_cur_drawn_y != s_my) &&
        s_booted && !s_locked && !s_setup) {
        desktop_dirty_rect(s_cur_drawn_x - 1, s_cur_drawn_y - 1, 10, 15);
    }
    // Blue screen: paint it INSTEAD of the desktop + windows so it fills the
    // whole screen and nothing can overlap it.
    if (g_crash_active) {
        bsod_paint(fb);
        if (s_mx >= 0 && s_my >= 0 && platform_name()[0] == 'b') draw_cursor(fb, s_mx, s_my);
        platform_present();
        return;
    }
    uint32_t now = platform_tick_ms();
    if (!s_booted) {
        desktop_paint_boot(fb);
        if (now - s_boot_start > 1600) {
            s_booted = true;
            s_last_activity = now;
            if (first_boot_pending()) {
                // first boot: run the setup wizard (user/password/browser)
                s_setup = true;
                s_locked = false;
                s_setup_field = 0;
                s_setup_engine = 0;
                s_setup_user[0] = 0; s_setup_pwd1[0] = 0; s_setup_pwd2[0] = 0;
                s_setup_len[0] = 0; s_setup_len[1] = 0; s_setup_len[2] = 0;
            } else {
                s_locked = true;  // boot splash -> lock screen
            }
        }
    } else if (s_setup) {
        desktop_paint_setup(fb);
    } else if (s_locked) {
        desktop_paint_lock(fb);
    } else {
        desktop_paint(fb);
        g_wm->paint_all(fb);
        g_wm->cleanup();
        ime::paint(fb);
    }
    // software cursor only on bare metal (no OS cursor there). On the Win32
    // host the real system cursor is used, so painting one here would leave
    // trails on the LVGL desktop (LVGL redraws only invalid regions).
    if (s_mx >= 0 && s_my >= 0 && platform_name()[0] == 'b') draw_cursor(fb, s_mx, s_my);
    if (platform_name()[0] == 'b') { s_cur_drawn_x = s_mx; s_cur_drawn_y = s_my; }
    platform_present();
}

} // namespace nefu

