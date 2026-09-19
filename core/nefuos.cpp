// nefuOS system main entry：init、input dispatch、frame loop
#include "platform.h"
#include "klib/klib.h"
#include "gui/desktop.h"
#include "gui/gfx.h"
#include "gui/wm.h"
#include "vfs/vfs.h"
#include "apps/apps.h"
#include "sys/settings.h"
#include "sys/power.h"
#include "net/net.h"
#include "sys/sha256.h"

namespace nefu {

static bool s_inited = false;
static bool s_booted = false;
static uint32_t s_boot_start = 0;
static bool s_net_selftest_done = false;
static int s_mx = -8, s_my = -8;
// ---- lock screen (UEFI password gate) state ----
static bool s_locked = true;        // desktop starts behind the lock screen
static char s_lock_pwd[65] = "";
static int s_lock_len = 0;
static uint32_t s_lock_fail_ms = 0;
static uint32_t s_last_activity = 0;
// ---- first-boot setup wizard state ----
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
    FSNode* d0 = g_vfs->mkdir("/var/lib/nefuos");
    FSNode* m = g_vfs->create_file("/var/lib/nefuos/firstboot");
    if (m) g_vfs->write_file(m, (const uint8_t*)"done", 4);
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
    } else {
        s_lock_fail_ms = platform_tick_ms();
    }
}

// （8x13，MSB=left）
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
    if (platform_fs_load(&data, &sz) && data && g_vfs->load(data, sz)) {
        klogf("VFS loaded from storage (%u bytes)\n", sz);
    } else {
        g_vfs->create_default_tree();
        klogf("VFS default tree created\n");
    }
    // even after loading an old snapshot the standard hierarchy must exist
    g_vfs->ensure_standard_dirs();
    g_vfs->ensure_default_files();
    // remove stray nodes left by an older browser_save_page that flattened
    // the whole path into a single name (e.g. "_usr_downloads_page_...")
    g_vfs->cleanup_stray_nodes();
    if (data) kfree(data);
    settings_load();
    apps_preinstall_defaults();   // ship store apps pre-installed
    store_load();
    g_wm = new WM();
    desktop_init();
    if (platform_name()[0] == 'b') __asm__ volatile("sti");   // enable IRQs before blocking net ops
    // bare-metal NIC (e1000 in QEMU); win32 host has none -> skipped by name
    if (platform_name()[0] == 'b') {
        if (net_init()) {
            klogf("net: e1000 up, ip=10.0.2.15\n");
        } else {
            klogf("net: no e1000 found\n");
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
    s_inited = true;
    if (platform_name()[0] == 'b') __asm__ volatile("sti");   // enable IRQs for tick/input
    klogf("nefuOS ready on %s\n", platform_name());
}

void nefuos_shutdown() {
    if (!s_inited) return;
    klogf("nefuOS shutdown\n");
    uint8_t* data = 0;
    uint32_t sz = 0;
    if (g_vfs->save(&data, &sz)) {
        platform_fs_save(data, sz);
        kfree(data);
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
    static int s_nhdbg = 0;
    if (s_nhdbg < 20) { s_nhdbg++; klogf("nh m=%d,%d b=%u locked=%d\n", x, y, (unsigned)buttons, (int)s_locked); }
    if (s_locked) return;   // lock screen consumes nothing from the desktop
    if (!desktop_handle_mouse(x, y, buttons)) {
        g_wm->handle_mouse(x, y, buttons);
    }
}

void nefuos_handle_key(int keycode, char ascii, bool down, const char* utf8) {
    if (!s_inited || !s_booted) return;
    if (!down) return;
    s_last_activity = platform_tick_ms();
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
    if (desktop_handle_key(keycode, ascii)) return;
    KeyEvent e;
    e.keycode = keycode;
    e.ascii = ascii;
    e.down = true;
    for (int i = 0; i < 8; i++) e.utf8[i] = (utf8 && utf8[i]) ? utf8[i] : 0;
    g_wm->handle_key(&e);
}

void nefuos_handle_scroll(int delta) {
    if (!s_inited || !s_booted) return;
    g_wm->handle_scroll(delta);
}

void nefuos_tick() {
    // idle auto-lock (configured in Settings -> idle_lock_sec)
    if (s_booted && !s_locked && g_settings.idle_lock_sec > 0 &&
        platform_tick_ms() - s_last_activity > (uint32_t)g_settings.idle_lock_sec * 1000u) {
        nefuos_lock_screen();
    }
    if (g_net.up) {
        net_poll();   // drain NIC
        // deferred link self-test: run once a few seconds after boot so the
        // cold-start path stays fast (ping/tcp waits must not block init)
        if (!s_net_selftest_done && s_booted &&
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
        }
    }
    // animation /
}

void nefuos_frame() {
    if (!s_inited) return;
    Surface fb = screen_surface();
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
    }
    // software cursor only on bare metal (no OS cursor there). On the Win32
    // host the real system cursor is used, so painting one here would leave
    // trails on the LVGL desktop (LVGL redraws only invalid regions).
    if (s_mx >= 0 && s_my >= 0 && platform_name()[0] == 'b') draw_cursor(fb, s_mx, s_my);
    platform_present();
}

} // namespace nefu
