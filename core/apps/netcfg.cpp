// nefuOS Network Settings v3 - LVGL GUI
// REAL adapter info (MAC/IP/gw) and, on the host backend, a REAL Wi-Fi scan.
// Bare metal is a wired e1000 link, so the network list is empty there and
// Connect brings the real TCP/IP link up.
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../net/net.h"
#include "../platform.h"
#include "../klib/klib.h"

namespace nefu {

namespace {

struct NetCfgState {
    LvglWin* lw;
    lv_obj_t* canvas;
    lv_obj_t* pwd_ta;
    uint8_t* buf;
    int w, h;
    int sel_net;
    bool connected;
    bool pwd_show;
    String pwd;
    int ping_result;
    int msg;
    WifiNetInfo nets[16];
    int net_count;
    NetAdapterInfo ai;
    bool ai_ok;
    NetCfgState() : lw(0), canvas(0), pwd_ta(0), buf(0), w(0), h(0), sel_net(-1),
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

static NetCfgState* s_nc_st = 0;

void net_lv_draw(NetCfgState* st) {
    if (!st->canvas || !st->buf) return;
    int W = st->w, H = st->h;
    Surface s;
    s.addr = st->buf;
    s.width = W; s.height = H; s.pitch = W * 4;
    gfx::fillrect(s, 0, 0, W, H, color::WHITE);
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
    lv_obj_invalidate(st->canvas);
}

void net_lv_btn(lv_event_t* e) {
    NetCfgState* st = s_nc_st;
    if (!st) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e);
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
                if (st->pwd_ta) {
                    const char* p = lv_textarea_get_text(st->pwd_ta);
                    st->pwd = p ? p : "";
                }
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
        if (st->pwd_ta) lv_textarea_set_password_mode(st->pwd_ta, !st->pwd_show);
    } else if (id == 4) {
        st->net_count = platform_wifi_scan(st->nets, 16);
        st->ai_ok = platform_net_get(&st->ai);
        st->msg = 0;
        st->ping_result = -1;
    }
    net_lv_draw(st);
}

void net_lv_click(lv_event_t* e) {
    NetCfgState* st = (NetCfgState*)lv_event_get_user_data(e);
    if (!st) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int my = p.y;
    int y = 6 + 22 + 4 * 18 + 22 + 16;   // matches canvas layout
    int n = st->net_count > 0 ? st->net_count : 1;
    for (int i = 0; i < n && i < 8; i++) {
        if (my >= y && my < y + 22) {
            st->sel_net = i;
            st->msg = 0;
            st->ping_result = -1;
            if (st->pwd_ta) lv_textarea_set_text(st->pwd_ta, "");
            break;
        }
        y += 22;
    }
    net_lv_draw(st);
}

} // namespace

void netcfg_launch() {
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Network", x, y, 460, 440);
    if (!lw) return;
    NetCfgState* st = new NetCfgState();
    st->lw = lw;
    st->net_count = platform_wifi_scan(st->nets, 16);
    st->ai_ok = platform_net_get(&st->ai);
    lw->userdata = st;
    s_nc_st = st;

    const char* labels[5] = { "Ping", "Connect", "Disconnect", "Show", "Refresh" };
    for (int i = 0; i < 5; i++) {
        lv_obj_t* b = lv_button_create(lw->content);
        lv_obj_set_pos(b, 8 + i * 88, 6);
        lv_obj_set_size(b, 80, 24);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x5A6B8C), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x425069), LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, net_lv_btn, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* lbl = lv_label_create(b);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    }

    st->pwd_ta = lv_textarea_create(lw->content);
    lv_obj_set_pos(st->pwd_ta, 8, 38);
    lv_obj_set_size(st->pwd_ta, 300, 26);
    lv_textarea_set_password_mode(st->pwd_ta, true);
    lv_textarea_set_placeholder_text(st->pwd_ta, "Wi-Fi password");
    lv_obj_set_style_radius(st->pwd_ta, 4, 0);
    lv_obj_set_style_border_width(st->pwd_ta, 1, 0);
    lv_obj_set_style_border_color(st->pwd_ta, lv_color_hex(0xB0AFA8), 0);
    lv_obj_set_style_bg_color(st->pwd_ta, lv_color_hex(0xF0EFEA), 0);

    st->w = 444;
    st->h = 350;
    st->canvas = lv_canvas_create(lw->content);
    lv_obj_set_pos(st->canvas, 8, 72);
    lv_obj_set_size(st->canvas, st->w, st->h);
    int bufsz = lv_canvas_buf_size(st->w, st->h, 32, 4);
    st->buf = new uint8_t[bufsz];
    memset(st->buf, 0xFF, (size_t)bufsz);
    lv_canvas_set_buffer(st->canvas, st->buf, st->w, st->h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_flag(st->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(st->canvas, net_lv_click, LV_EVENT_CLICKED, st);

    lv_group_t* grp = lvgl_kb_group();
    if (grp) lv_group_add_obj(grp, st->pwd_ta);

    net_lv_draw(st);
}

} // namespace nefu