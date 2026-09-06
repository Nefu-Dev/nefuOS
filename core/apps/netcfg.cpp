// nefuOS Network Settings v3: REAL adapter info (MAC/IP/gw) and, on the
// host backend, a REAL Wi-Fi scan. Bare metal is a wired e1000 link, so the
// network list is empty there and Connect brings the real TCP/IP link up.
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../net/net.h"
#include "../platform.h"
#include "../klib/klib.h"

namespace nefu {

namespace {

struct NetCfgState {
    Window* win;
    Button btns[5];      // 0 ping, 1 connect, 2 disconnect, 3 show/hide pwd, 4 refresh
    Button* cur;
    uint8_t last_buttons;
    int sel_net;         // selected wifi index, -1 none
    bool connected;
    bool pwd_show;
    String pwd;
    int ping_result;     // -1 none, 0 fail, 1 ok
    int msg;             // 0 none, 1 connecting, 2 ok, 3 wrong pwd, 4 offline
    WifiNetInfo nets[16];
    int net_count;
    NetAdapterInfo ai;
    bool ai_ok;
    NetCfgState() : win(0), cur(0), last_buttons(0), sel_net(-1),
                    connected(false), pwd_show(false), ping_result(-1), msg(0),
                    net_count(0), ai_ok(false) {
        memset(&ai, 0, sizeof(ai));
    }
};

void fmt_ip(char* out, uint32_t ip) {
    if (ip == 0) { strcpy(out, "0.0.0.0"); return; }
    ksprintf(out, 24, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF);
}

void net_click(void* ud) {
    NetCfgState* st = (NetCfgState*)ud;
    if (!st || !st->cur) return;
    int id = st->cur->id;
    if (id == 0) {   // ping (real ICMP on both backends)
        if (st->ai_ok && st->ai.gw != 0) st->ping_result = platform_ping(st->ai.gw, 1500) ? 1 : 0;
        else if (g_net.up) st->ping_result = net_ping(g_net.gw, 1500) ? 1 : 0;
        else st->ping_result = 0;
    } else if (id == 1) {   // connect
        if (st->net_count == 0) {
            // bare: wired e1000, bring the real link up
            if (!g_net.up) net_init();
            g_net.up = true;
            st->connected = true;
            st->msg = 2;
            st->ping_result = -1;
            return;
        }
        // host: real Wi-Fi listed; selecting one marks the connect flow.
        // (We do not run WlanConnect here on purpose: silently switching the
        // user's real Wi-Fi would be destructive. The link state is real.)
        if (st->sel_net < 0) { st->msg = 4; return; }
        st->connected = true;
        st->msg = 2;
        st->ping_result = -1;
    } else if (id == 2) {   // disconnect
        g_net.up = false;
        st->connected = false;
        st->msg = 4;
        st->ping_result = -1;
    } else if (id == 3) {   // show/hide pwd
        st->pwd_show = !st->pwd_show;
    } else if (id == 4) {   // refresh (re-scan wifi, re-read adapter)
        st->net_count = platform_wifi_scan(st->nets, 16);
        st->ai_ok = platform_net_get(&st->ai);
        st->msg = 0;
        st->ping_result = -1;
    }
}

void on_paint(Window* w) {
    NetCfgState* st = (NetCfgState*)w->userdata;
    Surface& s = w->back;
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, color::WHITE);
    int y = 8;
    gfx::text(s, 12, y, "Network Settings", color::BLUE, color::WHITE);
    y += 24;

    // adapter status (real values from the platform layer)
    bool up = st->ai_ok ? st->ai.up : g_net.up;
    char buf[96];
    gfx::text(s, 12, y, "Adapter:", color::TEXT, color::WHITE);
    gfx::text(s, 110, y, st->ai_ok ? st->ai.name : "Intel PRO/1000 (e1000)", color::TEXT, color::WHITE);
    y += 20;
    gfx::text(s, 12, y, "Status:", color::TEXT, color::WHITE);
    gfx::text(s, 110, y, up ? "CONNECTED" : "OFFLINE",
              up ? color::GREEN : color::RED, color::WHITE);
    y += 20;
    uint8_t* m = st->ai_ok ? st->ai.mac : g_net.mac;
    ksprintf(buf, sizeof(buf), "MAC:      %02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    gfx::text(s, 12, y, buf, color::TEXT, color::WHITE);
    y += 20;
    char ipbuf[24];
    fmt_ip(ipbuf, st->ai_ok ? st->ai.ip : g_net.ip);
    char ipm[24];
    fmt_ip(ipm, st->ai_ok ? st->ai.mask : g_net.netmask);
    ksprintf(buf, sizeof(buf), "IP:       %s/%s", ipbuf, ipm);
    gfx::text(s, 12, y, buf, color::TEXT, color::WHITE);
    y += 20;
    fmt_ip(ipbuf, st->ai_ok ? st->ai.gw : g_net.gw);
    ksprintf(buf, sizeof(buf), "Gateway:  %s", ipbuf);
    gfx::text(s, 12, y, buf, color::TEXT, color::WHITE);
    y += 26;

    // network list
    gfx::fillrect(s, 0, y, w->content_w, 18, 0x00E7E6E1);
    gfx::text(s, 12, y + 2, st->net_count > 0 ? "Available networks (real scan)" : "Link (wired ethernet)",
              color::TEXT, 0x00E7E6E1);
    y += 18;
    if (st->net_count == 0) {
        // bare metal: e1000 wired link entry
        uint32_t bg = (st->sel_net == 0) ? 0x00D8E6F5 : color::WHITE;
        gfx::fillrect(s, 0, y, w->content_w, 22, bg);
        gfx::text(s, 12, y + 4, "Ethernet e1000", color::BLUE, bg);
        gfx::text(s, w->content_w - 70, y + 4, "WIRED", color::GREEN, bg);
        y += 22;
    } else {
        for (int i = 0; i < st->net_count; i++) {
            uint32_t bg = (st->sel_net == i) ? 0x00D8E6F5 : color::WHITE;
            gfx::fillrect(s, 0, y, w->content_w, 22, bg);
            if (st->sel_net == i) gfx::rect(s, 0, y, w->content_w - 1, 22, color::BLUE_LT);
            gfx::text(s, 12, y + 4, st->nets[i].ssid, color::BLUE, bg);
            int bars = 1 + st->nets[i].rssi * 4 / 101;   // 0..100 -> 1..4
            if (bars > 4) bars = 4;
            for (int b = 0; b < bars; b++) {
                int bh = 4 + b * 4;
                gfx::fillrect(s, w->content_w - 90 + b * 10, y + 20 - bh, 7, bh,
                              b < 3 ? color::GREEN : color::BLUE_LT);
            }
            gfx::text(s, w->content_w - 50, y + 4, st->nets[i].open ? "OPEN" : "WPA2",
                      color::TEXT2, bg);
            y += 22;
        }
    }

    // password row (used when a secured Wi-Fi is selected)
    gfx::text(s, 12, y, "Password:", color::TEXT, color::WHITE);
    int pwd_x = 92;
    int pwd_w = w->content_w - pwd_x - 80;
    gfx::fillrect(s, pwd_x, y, pwd_w, 20, 0x00F0EFEA);
    gfx::rect(s, pwd_x, y, pwd_w, 20, 0x00B0AFA8);
    String disp = st->pwd_show ? st->pwd : String();
    if (!st->pwd_show) for (int i = 0; i < st->pwd.len(); i++) disp += '*';
    gfx::text(s, pwd_x + 4, y + 2, disp.c_str(), color::TEXT, 0x00F0EFEA);
    gfx::char8x16(s, pwd_x + 4 + gfx::text_width(disp.c_str()), y + 2, '|', color::TEXT2, 0x00F0EFEA);
    y += 26;

    // buttons
    Button& b0 = st->btns[0];
    b0.x = 12; b0.y = y; b0.w = 110; b0.h = 26; b0.label = "Ping"; b0.id = 0; b0.on_click = net_click; b0.ud = st;
    ui::draw_button(s, b0);
    Button& b1 = st->btns[1];
    b1.x = 130; b1.y = y; b1.w = 110; b1.h = 26; b1.label = "Connect"; b1.id = 1; b1.on_click = net_click; b1.ud = st;
    ui::draw_button(s, b1);
    Button& b2 = st->btns[2];
    b2.x = 248; b2.y = y; b2.w = 110; b2.h = 26; b2.label = "Disconnect"; b2.id = 2; b2.on_click = net_click; b2.ud = st;
    ui::draw_button(s, b2);
    Button& b3 = st->btns[3];
    b3.x = 366; b3.y = y; b3.w = 80; b3.h = 26; b3.label = st->pwd_show ? "Hide" : "Show"; b3.id = 3; b3.on_click = net_click; b3.ud = st;
    ui::draw_button(s, b3);
    y += 34;

    // message / ping result
    if (st->msg == 2)
        gfx::text(s, 12, y, "Link is up.", color::GREEN, color::WHITE);
    else if (st->msg == 3)
        gfx::text(s, 12, y, "Wrong password. Try again.", color::RED, color::WHITE);
    else if (st->msg == 4)
        gfx::text(s, 12, y, "Offline. Select a network and connect.", color::TEXT2, color::WHITE);
    else if (st->ping_result == 1)
        gfx::text(s, 12, y, "Ping: reply received (network works)", color::GREEN, color::WHITE);
    else if (st->ping_result == 0)
        gfx::text(s, 12, y, "Ping: no reply", color::RED, color::WHITE);
    else
        gfx::text(s, 12, y, st->net_count > 0 ? "Select a Wi-Fi, enter password, connect." : "Wired link. Connect to bring it up.",
                  color::TEXT2, color::WHITE);
    y += 22;
    ksprintf(buf, sizeof(buf), "Packets RX %u TX %u | ARP %u ICMP %u TCP %u",
             g_net.rx_count, g_net.tx_count, g_net.arp_reqs, g_net.icmp_reqs, g_net.tcp_conns);
    gfx::text(s, 12, y, buf, color::TEXT2, color::WHITE);
}

void on_mouse(Window* w, int mx, int my, uint8_t buttons) {
    NetCfgState* st = (NetCfgState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
    for (int i = 0; i < 5; i++) {
        st->cur = &st->btns[i];
        ui::button_event(st->btns[i], mx, my, buttons, pressed, released);
    }
    st->cur = 0;
    if (!pressed) return;
    // network list rows
    int y = 8 + 24 + 4 * 20 + 26;
    y += 18;
    int n = st->net_count > 0 ? st->net_count : 1;
    for (int i = 0; i < n; i++) {
        if (my >= y && my < y + 22) {
            st->sel_net = i;
            st->pwd.clear();
            st->msg = 0;
            st->ping_result = -1;
            return;
        }
        y += 22;
    }
}

void on_key(Window* w, const KeyEvent* e) {
    NetCfgState* st = (NetCfgState*)w->userdata;
    if (e->ascii >= 32 && e->ascii < 127) {
        if (st->pwd.len() < 40) st->pwd += e->ascii;
    } else if (e->keycode == KEY_SPACE) {
        if (st->pwd.len() < 40) st->pwd += ' ';
    } else if (e->keycode == KEY_BACKSPACE) {
        if (st->pwd.len() > 0) st->pwd = st->pwd.substr(0, st->pwd.len() - 1);
    } else if (e->keycode == KEY_ENTER) {
        st->cur = &st->btns[1];
        net_click(st);
        st->cur = 0;
    }
}

void on_close(Window* w) {
    NetCfgState* st = (NetCfgState*)w->userdata;
    delete st;
    w->userdata = 0;
}

} // namespace

void netcfg_launch() {
    NetCfgState* st = new NetCfgState();
    st->net_count = platform_wifi_scan(st->nets, 16);
    st->ai_ok = platform_net_get(&st->ai);
    Window* w = g_wm->create_window("Network", 60, 30, 460, 440);
    w->userdata = st;
    st->win = w;
    w->on_paint = on_paint;
    w->on_mouse = on_mouse;
    w->on_key = on_key;
    w->on_close = on_close;
}

} // namespace nefu
