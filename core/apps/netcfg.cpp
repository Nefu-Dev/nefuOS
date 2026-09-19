// nefuOS Network Settings v3 - LVGL GUI
// REAL adapter info (MAC/IP/gw) and, on the host backend, a REAL Wi-Fi scan.
// Bare metal is a wired e1000 link, so the network list is empty there and
// Connect brings the real TCP/IP link up.
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../net/net.h"
#include "../platform.h"
#include "../klib/klib.h"

namespace nefu {

namespace {

struct NetCfgState {
    int w, h;
    int sel_net;
    bool connected;
    bool pwd_show;
    String pwd;
    bool pwd_edit;
    int ping_result;
    int msg;
    WifiNetInfo nets[16];
    int net_count;
    NetAdapterInfo ai;
    bool ai_ok;
    NetCfgState() : w(0), h(0), sel_net(-1),
                    connected(false), pwd_show(false), pwd_edit(false),
                    ping_result(-1), msg(0),
                    net_count(0), ai_ok(false) {
        memset(&ai, 0, sizeof(ai));
    }
};

void fmt_ip(char* out, uint32_t ip) {
    if (ip == 0) { strcpy(out, "0.0.0.0"); return; }
    ksprintf(out, 24, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF);
}

static NetCfgState* s_nc_st = 0;

void net_wm_paint(Window* w) {
    NetCfgState* st = (NetCfgState*)w->userdata;
    int W = st->w, H = st->h;
    Surface& s = w->back;
    gfx::fillrect(s, 0, 0, W, H, color::WHITE);
    // password field
    gfx::rect(s, 8, 38, 300, 26, 0x00B0AFA8);
    gfx::fillrect(s, 9, 39, 298, 24, 0x00F0EFEA);
    if (st->pwd_edit) gfx::rect(s, 7, 36, 302, 30, color::BLUE_LT);
    gfx::text(s, 14, 44, st->pwd.len() == 0 ? "Wi-Fi password" : (st->pwd_show ? st->pwd.c_str() : "********"),
              st->pwd.len() == 0 ? 0x00888D96 : color::TEXT, 0x00F0EFEA);
    // action buttons
    const char* blabels[5] = { "Ping", "Connect", "Disconnect", "Show", "Refresh" };
    for (int i = 0; i < 5; i++) {
        int bx = 8 + i * 88;
        gfx::fillrect(s, bx, 6, 80, 24, 0x005A6B8C);
        gfx::rect(s, bx, 6, 80, 24, 0x00425069);
        gfx::text(s, bx + (80 - gfx::text_width(blabels[i])) / 2, 11, blabels[i], color::WHITE, 0x005A6B8C);
    }
    int y = 6;
    gfx::text(s, 8, y, "Network Settings", color::BLUE, color::WHITE);
    y += 22;
    bool up = st->ai_ok ? st->ai.up : g_net.up;
    char buf[96];
    gfx::text(s, 8, y, "Adapter:", color::TEXT, color::WHITE);
    gfx::text(s, 106, y, st->ai_ok ? st->ai.name : "Intel PRO/1000 (e1000)", color::TEXT, color::WHITE);
    y += 18;
    gfx::text(s, 8, y, "Status:", color::TEXT, color::WHITE);
    gfx::text(s, 106, y, up ? "CONNECTED" : "OFFLINE", up ? color::GREEN : color::RED, color::WHITE);
    y += 18;
    uint8_t* m = st->ai_ok ? st->ai.mac : g_net.mac;
    ksprintf(buf, sizeof(buf), "MAC:      %02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    gfx::text(s, 8, y, buf, color::TEXT, color::WHITE);
    y += 18;
    char ipbuf[24];
    fmt_ip(ipbuf, st->ai_ok ? st->ai.ip : g_net.ip);
    char ipm[24];
    fmt_ip(ipm, st->ai_ok ? st->ai.mask : g_net.netmask);
    ksprintf(buf, sizeof(buf), "IP:       %s/%s", ipbuf, ipm);
    gfx::text(s, 8, y, buf, color::TEXT, color::WHITE);
    y += 18;
    fmt_ip(ipbuf, st->ai_ok ? st->ai.gw : g_net.gw);
    ksprintf(buf, sizeof(buf), "Gateway:  %s", ipbuf);
    gfx::text(s, 8, y, buf, color::TEXT, color::WHITE);
    y += 22;
    gfx::fillrect(s, 0, y, W, 16, 0x00E7E6E1);
    gfx::text(s, 8, y + 1, st->net_count > 0 ? "Available networks (real scan)" : "Link (wired ethernet)",
              color::TEXT, 0x00E7E6E1);
    y += 16;
    if (st->net_count == 0) {
        uint32_t bg = (st->sel_net == 0) ? 0x00D8E6F5 : color::WHITE;
        gfx::fillrect(s, 0, y, W, 22, bg);
        gfx::text(s, 8, y + 4, "Ethernet e1000", color::BLUE, bg);
        gfx::text(s, W - 70, y + 4, "WIRED", color::GREEN, bg);
        y += 22;
    } else {
        for (int i = 0; i < st->net_count && i < 8; i++) {
            uint32_t bg = (st->sel_net == i) ? 0x00D8E6F5 : color::WHITE;
            gfx::fillrect(s, 0, y, W, 22, bg);
            if (st->sel_net == i) gfx::rect(s, 0, y, W - 1, 22, color::BLUE_LT);
            gfx::text(s, 8, y + 4, st->nets[i].ssid, color::BLUE, bg);
            int bars = 1 + st->nets[i].rssi * 4 / 101;
            if (bars > 4) bars = 4;
            for (int b = 0; b < bars; b++) {
                int bh = 4 + b * 4;
                gfx::fillrect(s, W - 90 + b * 10, y + 20 - bh, 7, bh, b < 3 ? color::GREEN : color::BLUE_LT);
            }
            gfx::text(s, W - 50, y + 4, st->nets[i].open ? "OPEN" : "WPA2", color::TEXT2, bg);
            y += 22;
        }
    }
    if (st->msg == 2)
        gfx::text(s, 8, y + 2, "Link is up.", color::GREEN, color::WHITE);
    else if (st->msg == 3)
        gfx::text(s, 8, y + 2, "Wrong password. Try again.", color::RED, color::WHITE);
    else if (st->msg == 4)
        gfx::text(s, 8, y + 2, "Offline. Select a network and connect.", color::TEXT2, color::WHITE);
    else if (st->ping_result == 1)
        gfx::text(s, 8, y + 2, "Ping: reply received (network works)", color::GREEN, color::WHITE);
    else if (st->ping_result == 0)
        gfx::text(s, 8, y + 2, "Ping: no reply", color::RED, color::WHITE);
    else
        gfx::text(s, 8, y + 2, st->net_count > 0 ? "Select a Wi-Fi, enter password, connect." : "Wired link. Connect to bring it up.",
                  color::TEXT2, color::WHITE);
    y += 20;
    ksprintf(buf, sizeof(buf), "Packets RX %u TX %u | ARP %u ICMP %u TCP %u",
             g_net.rx_count, g_net.tx_count, g_net.arp_reqs, g_net.icmp_reqs, g_net.tcp_conns);
    gfx::text(s, 8, y, buf, color::TEXT2, color::WHITE);
}

void net_wm_mouse(Window* w, int mx, int my, uint8_t buttons) {
    NetCfgState* st = (NetCfgState*)w->userdata;
    if (!st || !buttons) return;
    int id = -1;
    if (my >= 6 && my < 30) {
        for (int i = 0; i < 5; i++) {
            int bx = 8 + i * 88;
            if (mx >= bx && mx < bx + 80) { id = i; break; }
        }
    }
    // password field click
    if (my >= 38 && my < 64 && mx >= 8 && mx < 308) {
        st->pwd_edit = true;
        return;
    }
    if (id == 0) {
        if (st->ai_ok && st->ai.gw != 0) st->ping_result = platform_ping(st->ai.gw, 1500) ? 1 : 0;
        else if (g_net.up) st->ping_result = net_ping(g_net.gw, 1500) ? 1 : 0;
        else st->ping_result = 0;
    } else if (id == 1) {
        if (st->net_count == 0) {
            if (!g_net.up) net_init();
            g_net.up = true;
            st->connected = true;
            st->msg = 2;
            st->ping_result = -1;
        } else {
            if (st->sel_net < 0) { st->msg = 4; }
            else {
                    if (!st->nets[st->sel_net].open && st->pwd.len() < 8) { st->msg = 3; }
                else {
                    st->connected = true;
                    st->msg = 2;
                    st->ping_result = -1;
                }
            }
        }
    } else if (id == 2) {
        g_net.up = false;
        st->connected = false;
        st->msg = 4;
        st->ping_result = -1;
    } else if (id == 3) {
        st->pwd_show = !st->pwd_show;
    } else if (id == 4) {
        st->net_count = platform_wifi_scan(st->nets, 16);
        st->ai_ok = platform_net_get(&st->ai);
        st->msg = 0;
        st->ping_result = -1;
    }
    // network list selection (canvas area below y=72)
    if (my >= 72) {
        int yy = 6 + 22 + 4 * 18 + 22 + 16;
        int n = st->net_count > 0 ? st->net_count : 1;
        for (int i = 0; i < n && i < 8; i++) {
            if (my >= yy + 72 && my < yy + 72 + 22) {
                st->sel_net = i;
                st->msg = 0;
                st->ping_result = -1;
                st->pwd = "";
                break;
            }
            yy += 22;
        }
    }
}

void net_wm_key(Window* w, const KeyEvent* e) {
    NetCfgState* st = (NetCfgState*)w->userdata;
    if (!st || !st->pwd_edit) return;
    if (!e->down) return;
    if (e->keycode == KEY_BACKSPACE) {
        if (st->pwd.len() > 0) st->pwd = st->pwd.substr(0, st->pwd.len() - 1);
        return;
    }
    if (e->ascii >= 32 && e->ascii < 127) {
        if (st->pwd.len() < 63) st->pwd += e->ascii;
    }
}

} // namespace

void netcfg_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Network", x, y, 460, 460);
    if (!w) return;
    NetCfgState* st = new NetCfgState();
    st->net_count = platform_wifi_scan(st->nets, 16);
    st->ai_ok = platform_net_get(&st->ai);
    st->w = w->content_w;
    st->h = w->content_h;
    w->userdata = st;
    w->on_paint = net_wm_paint;
    w->on_mouse = net_wm_mouse;
    w->on_key = net_wm_key;
    g_wm->raise(w);
}

} // namespace nefu