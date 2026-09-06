// nefuOS system main entry：init、input dispatch、frame loop
#include "platform.h"
#include "klib/klib.h"
#include "gui/desktop.h"
#include "gui/gfx.h"
#include "gui/wm.h"
#include "vfs/vfs.h"
#include "apps/apps.h"
#include "sys/settings.h"
#include "net/net.h"

namespace nefu {

static bool s_inited = false;
static bool s_booted = false;
static uint32_t s_boot_start = 0;
static int s_mx = -8, s_my = -8;

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
        g_vfs->ensure_standard_dirs();   // an old save must never hide /usr /tmp ...
        klogf("VFS standard dirs ensured\n");
    } else {
        g_vfs->create_default_tree();
        klogf("VFS default tree created\n");
    }
    // even after loading an old snapshot the standard hierarchy must exist
    g_vfs->ensure_standard_dirs();
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
            // link self-test: ARP resolve + ICMP echo to gateway
            uint32_t t0 = platform_tick_ms();
            bool pong = net_ping(g_net.gw, 2000);
            klogf("net: ping gateway %s (%u ms)\n", pong ? "OK" : "FAIL",
                  (unsigned)(platform_tick_ms() - t0));
            klogf("net: rx=%u tx=%u arp=%u icmp=%u tcp=%u\n",
                  g_net.rx_count, g_net.tx_count, g_net.arp_reqs, g_net.icmp_reqs, g_net.tcp_conns);
            // TCP HTTP self-test: fetch "/" from the host web server (10.0.2.2:8000)
            {
                int sock = tcp_connect(g_net.gw, 8000, 3000);
                if (sock >= 0) {
                    tcp_send(sock, "GET / HTTP/1.0\r\n\r\n", 18);
                    char tbuf[512];
                    int got = tcp_recv(sock, tbuf, sizeof(tbuf) - 1, 3000);
                    if (got > 0) {
                        tbuf[got] = 0;
                        if (got > 60) tbuf[60] = 0;
                        klogf("net: http %d bytes: %s\n", got, tbuf);
                    }
                    else klogf("net: http recv empty\n");
                    tcp_close(sock);
                } else {
                    klogf("net: tcp connect FAIL (no listener on 10.0.2.2:8000)\n");
                }
            }
        } else {
            klogf("net: no e1000 found\n");
        }
    }
    s_boot_start = platform_tick_ms();
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
    if (!desktop_handle_mouse(x, y, buttons)) {
        g_wm->handle_mouse(x, y, buttons);
    }
}

void nefuos_handle_key(int keycode, char ascii, bool down, const char* utf8) {
    if (!s_inited || !s_booted) return;
    if (!down) return;
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
    if (g_net.up) net_poll();   // drain NIC
    // ：animation/
}

void nefuos_frame() {
    if (!s_inited) return;
    Surface fb = screen_surface();
    uint32_t now = platform_tick_ms();
    if (!s_booted) {
        desktop_paint_boot(fb);
        if (now - s_boot_start > 1600) s_booted = true;
    } else {
        desktop_paint(fb);
        g_wm->paint_all(fb);
        g_wm->cleanup();
    }
    if (s_mx >= 0 && s_my >= 0) draw_cursor(fb, s_mx, s_my);
    platform_present();
}

} // namespace nefu
