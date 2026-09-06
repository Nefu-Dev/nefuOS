// nefuOS Browser v2: file:// + http:// + built-in search + save to disk
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../vfs/vfs.h"
#include "../net/net.h"
#include "../platform.h"
#include "../klib/klib.h"

namespace nefu {

namespace {

const int BAR_H = 30;
const int STAT_H = 20;

struct Line {
    String s;
    int style;   // 0 normal, 1 title, 2 link
    Line() : style(0) {}
};

struct BrowserState {
    String input;        // address bar text
    String url;          // last loaded url
    List<Line> lines;
    int scroll;
    int status;          // 0 idle 1 loading 2 done 3 error
    bool busy;
    int cursor;
    int last_link;       // last clicked link index
    Button btns[3];      // Go / Search / Save
    Button* cur;
    uint8_t last_buttons;
    BrowserState() : scroll(0), status(0), busy(false), cursor(0), last_link(-1),
                     cur(0), last_buttons(0) {}
};

bool parse_ip(const char* s, uint32_t* out) {
    uint32_t ip = 0;
    int seg = 0, val = 0;
    const char* p = s;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return false;
        } else if (*p == '.') {
            if (seg > 3) return false;
            ip = (ip << 8) | (uint32_t)val;
            val = 0; seg++;
        } else return false;
        p++;
    }
    if (seg != 3) return false;
    ip = (ip << 8) | (uint32_t)val;
    *out = ip;
    return true;
}

void add_line(List<Line>& lines, const char* s, int style) {
    Line l;
    l.s = s;
    l.style = style;
    lines.push(l);
}

void html_to_lines(const char* html, int len, List<Line>& out) {
    String text;
    int style = 0;
    int i = 0;
    char buf[8];
    while (i < len) {
        char c = html[i];
        if (c == '<') {
            int j = i + 1;
            bool closing = (j < len && html[j] == '/');
            if (closing) j++;
            int k = 0;
            while (j < len && k < 6 && html[j] != '>' && html[j] != ' ' && html[j] != '\n') {
                buf[k++] = html[j]; j++;
            }
            buf[k] = 0;
            while (j < len && html[j] != '>') j++;
            if (j < len) j++;
            i = j;
            if (!text.empty()) {
                add_line(out, text.c_str(), style);
                text.clear();
            }
            if (k == 0) { style = 0; continue; }
            char t = (buf[0] >= 'A' && buf[0] <= 'Z') ? (char)(buf[0] + 32) : buf[0];
            if (closing) { style = 0; continue; }
            if (t == 'h') { style = 1; add_line(out, "", 0); }
            else if (t == 'a') { style = 2; }
            else if (t == 'p' || t == 'b' || t == 'd' || t == 'l' || t == 't') {
                if (t == 'l' || t == 'p' || t == 'd') add_line(out, "", 0);
            } else if (t == 't') { style = 0; }
            else style = 0;
        } else if (c == '&') {
            int j = i + 1;
            char ent[16];
            int k = 0;
            while (j < len && k < 14 && html[j] != ';' && html[j] != '&') ent[k++] = html[j++];
            if (j < len && html[j] == ';') j++;
            ent[k] = 0;
            if (strcmp(ent, "amp") == 0) text += '&';
            else if (strcmp(ent, "lt") == 0) text += '<';
            else if (strcmp(ent, "gt") == 0) text += '>';
            else if (strcmp(ent, "quot") == 0) text += '"';
            else if (strcmp(ent, "nbsp") == 0) text += ' ';
            else if (strcmp(ent, "#39") == 0) text += '\'';
            i = j;
        } else if (c == '\n' || c == '\r') {
            i++;
        } else {
            text += c;
            i++;
        }
    }
    if (!text.empty()) add_line(out, text.c_str(), style);
    if (out.empty()) add_line(out, "(empty page)", 0);
}

bool load_file(BrowserState* st, const char* path) {
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir) { st->status = 3; return false; }
    st->lines.erase_all();
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) { st->status = 3; return false; }
    memcpy(buf, f->data, f->size);
    buf[f->size] = 0;
    html_to_lines(buf, (int)f->size, st->lines);
    kfree(buf);
    st->scroll = 0;
    st->status = 2;
    return true;
}

bool load_http(BrowserState* st, uint32_t ip, uint16_t port, const char* path) {
    int fd = tcp_connect(ip, port, 3000);
    if (fd < 0) { st->status = 3; return false; }
    char req[512];
    int rl = ksprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\n\r\n",
                      path, (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
    tcp_send(fd, req, rl);
    char hdr[4096];
    int hl = 0;
    uint32_t start = platform_tick_ms();
    while (hl < 4095 && platform_tick_ms() - start < 3000) {
        char c;
        int n = tcp_recv(fd, &c, 1, 100);
        if (n == 1) { hdr[hl++] = c; if (hl >= 4 && memcmp(hdr + hl - 4, "\r\n\r\n", 4) == 0) break; }
        else if (n == 0) break;
    }
    hdr[hl] = 0;
    bool ok = (strstr(hdr, " 200") != 0);
    if (!ok) {
        tcp_close(fd);
        st->status = 3;
        return false;
    }
    uint8_t* body = (uint8_t*)kalloc(65536);
    if (!body) { tcp_close(fd); st->status = 3; return false; }
    int bl = 0;
    start = platform_tick_ms();
    while (bl < 65535 && platform_tick_ms() - start < 3000) {
        int n = tcp_recv(fd, body + bl, 4096, 100);
        if (n > 0) bl += n;
        else if (n == 0) break;
        else if (n < 0) break;
    }
    tcp_close(fd);
    st->lines.erase_all();
    html_to_lines((const char*)body, bl, st->lines);
    kfree(body);
    st->scroll = 0;
    st->status = 2;
    return true;
}

// ---- built-in search (Bing-style): scan VFS filenames ----
struct WalkCtx {
    BrowserState* st;
    const char* q;
    int hits;
    String last_hit_path;
};

static void walk_search(FSNode* n, WalkCtx* c) {
    if (!c || c->hits > 24) return;
    for (int i = 0; i < n->children.size(); i++) {
        FSNode* ch = n->children[i];
        if (ch->name.find(c->q) >= 0) {
            char buf[160];
            String p = node_path(ch);
            if (ch->is_dir)
                ksprintf(buf, sizeof(buf), "%s/  (folder)", p.c_str());
            else
                ksprintf(buf, sizeof(buf), "%s  (%u bytes)", p.c_str(), (unsigned)ch->size);
            add_line(c->st->lines, buf, 2);
            c->hits++;
            if (c->hits <= 1) c->last_hit_path = p;
        }
        if (ch->is_dir) walk_search(ch, c);
    }
}

void browser_search(BrowserState* st, const char* q) {
    st->lines.erase_all();
    add_line(st->lines, "Bing search: ", 0);
    char head[96];
    ksprintf(head, sizeof(head), "'%s'", q);
    add_line(st->lines, head, 1);
    add_line(st->lines, "", 0);
    WalkCtx ctx;
    ctx.st = st;
    ctx.q = q;
    ctx.hits = 0;
    walk_search(g_vfs->root(), &ctx);
    if (ctx.hits == 0) {
        add_line(st->lines, "No results in the file system.", 0);
        add_line(st->lines, "", 0);
        add_line(st->lines, "Try: file:// /home/user/Documents/nefuos.txt", 2);
        add_line(st->lines, "     http:// 10.0.2.2:8000/ (host web server)", 2);
    } else {
        char s[64];
        ksprintf(s, sizeof(s), "%d result(s). Click a line to open.", ctx.hits);
        add_line(st->lines, s, 0);
    }
    st->scroll = 0;
    st->status = 2;
    st->url = "search:";
    st->url += q;
}

// ---- save current page to /usr/downloads ----
bool browser_save_page(BrowserState* st) {
    g_vfs->mkdir("/usr/downloads");
    // sanitize only the file-name part; keep the leading directory intact.
    // (older versions flattened the whole path and created stray nodes
    // like "_usr_downloads_page_..." in the cwd)
    String name = "page_";
    name += st->url;
    String clean;
    for (int i = 0; i < name.len(); i++) {
        char c = name[i];
        if (c == '/' || c == ':' || c == '\\' || c == ' ' || c == '?') c = '_';
        clean += c;
    }
    if (clean.len() > 48) clean = clean.substr(0, 48);
    clean += ".html";
    String path = "/usr/downloads/";
    path += clean;
    FSNode* f = g_vfs->create_file(path.c_str());
    if (!f) return false;
    // build html from lines
    char* buf = (char*)kalloc(65536);
    if (!buf) return false;
    int bl = 0;
    for (int i = 0; i < st->lines.size(); i++) {
        const Line& l = st->lines[i];
        if (l.style == 1) {
            bl += ksprintf(buf + bl, 65536 - bl, "<h3>%s</h3>\n", l.s.c_str());
        } else if (l.style == 2) {
            bl += ksprintf(buf + bl, 65536 - bl, "<a>%s</a><br>\n", l.s.c_str());
        } else {
            bl += ksprintf(buf + bl, 65536 - bl, "<p>%s</p>\n", l.s.c_str());
        }
        if (bl > 65000) break;
    }
    bool ok = g_vfs->write_file(f, (const uint8_t*)buf, (uint32_t)bl);
    kfree(buf);
    return ok;
}

void browser_load(BrowserState* st, const char* url) {
    st->url = url;
    st->status = 1;
    st->busy = true;

    if (strncmp(url, "file://", 7) == 0) {
        const char* path = url + 7;
        if (path[0] == 0) path = "/";
        load_file(st, path);
        st->busy = false;
        return;
    }

    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        // host backend: real HTTP(S) via WinINet (DNS + TLS included).
        // bare metal falls back to the in-house TCP stack (IP literals only).
        uint8_t* body = 0;
        uint32_t body_len = 0;
        if (platform_http_get(url, &body, &body_len) && body && body_len > 0) {
            st->lines.erase_all();
            add_line(st->lines, "HTTP fetch: ", 0);
            add_line(st->lines, url, 1);
            char hdr[80];
            ksprintf(hdr, sizeof(hdr), "%u bytes received", (unsigned)body_len);
            add_line(st->lines, hdr, 0);
            // render a rough text view of the body
            char* txt = (char*)kalloc((size_t)body_len + 1);
            if (txt) {
                memcpy(txt, body, body_len);
                txt[body_len] = 0;
                html_to_lines(txt, (int)body_len, st->lines);
                kfree(txt);
            }
            kfree(body);
            st->status = 2;
            st->busy = false;
            st->scroll = 0;
            return;
        }
        const char* p = url + 7;
        char host[64];
        char path[256];
        int hi = 0;
        while (*p && *p != ':' && *p != '/' && hi < 63) host[hi++] = *p++;
        host[hi] = 0;
        uint16_t port = 80;
        if (*p == ':') {
            p++;
            int pi = 0;
            while (*p >= '0' && *p <= '9' && pi < 4) { port = (uint16_t)(port * 10 + (*p - '0')); p++; pi++; }
        }
        if (*p != '/') path[0] = '/', path[1] = 0;
        else {
            int pi = 0;
            while (*p && pi < 255) path[pi++] = *p++;
            path[pi] = 0;
        }
        uint32_t ip;
        if (!parse_ip(host, &ip)) {
            // hostname not resolvable (no DNS on bare) -> local search
            browser_search(st, url);
            st->busy = false;
            return;
        }
        load_http(st, ip, port, path);
        st->busy = false;
        return;
    }

    if (strncmp(url, "search:", 7) == 0) {
        browser_search(st, url + 7);
        st->busy = false;
        return;
    }

    // plain word -> Bing on host, VFS search on bare; path-like -> file
    if (url[0] == '/' || url[0] == '.') {
        load_file(st, url);
    } else {
        browser_search(st, url);
    }
    st->busy = false;
}

void draw_text_clip(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg, int maxx) {
    int cx = x;
    for (const char* p = str; *p; p++) {
        if (cx + 8 > maxx) break;
        gfx::char8x16(s, cx, y, *p, fg, bg);
        cx += 8;
    }
}

void on_paint(Window* w) {
    BrowserState* st = (BrowserState*)w->userdata;
    Surface& s = w->back;
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, color::WHITE);
    // address bar
    gfx::fillrect(s, 0, 0, w->content_w, BAR_H, color::PANEL);
    gfx::rect(s, 3, 3, w->content_w - 6 - 150, BAR_H - 6, color::BORDER);
    String disp = st->input.empty() ? st->url : st->input;
    draw_text_clip(s, 8, 7, disp.c_str(), color::TEXT, color::PANEL, w->content_w - 170);
    int tw = gfx::text_width(disp.c_str());
    gfx::char8x16(s, 8 + tw, 7, '|', color::TEXT2, color::PANEL);
    // buttons: Go / Search / Save
    const char* labs[3] = { "Go", "S", "DL" };
    for (int i = 0; i < 3; i++) {
        Button& b = st->btns[i];
        b.x = w->content_w - 144 + i * 48;
        b.y = 3;
        b.w = 44;
        b.h = 24;
        b.label = labs[i];
        b.id = i;
        b.on_click = 0;
        ui::draw_button(s, b);
    }
    // content
    int y0 = BAR_H;
    int row_h = 16;
    int vis = (w->content_h - y0 - STAT_H) / row_h;
    if (vis < 0) vis = 0;
    int start = st->scroll;
    int cy = y0 + 2;
    for (int i = start; i < st->lines.size() && (i - start) < vis + 1; i++) {
        const Line& l = st->lines[i];
        uint32_t fg = color::TEXT;
        int scale = 1;
        if (l.style == 1) { fg = color::BLUE; scale = 2; }
        else if (l.style == 2) { fg = color::BLUE_LT; }
        if (scale == 2) {
            if (cy + 32 <= w->content_h - STAT_H) {
                gfx::text_scale(s, 6, cy, l.s.c_str(), fg, color::WHITE, 2);
                cy += 32;
            }
        } else {
            if (cy + row_h <= w->content_h - STAT_H) {
                if (l.style == 2) {
                    gfx::fillrect(s, 0, cy, w->content_w, row_h, 0x00EAF2FB);
                }
                draw_text_clip(s, 6, cy, l.s.c_str(), fg, color::WHITE, w->content_w - 8);
                cy += row_h;
            }
        }
    }
    // status bar
    gfx::fillrect(s, 0, w->content_h - STAT_H, w->content_w, STAT_H, color::PANEL);
    const char* stxt = "Ready";
    if (st->status == 1) stxt = "Loading...";
    else if (st->status == 3) stxt = "Error: cannot load";
    else if (st->status == 2) stxt = "Done";
    draw_text_clip(s, 6, w->content_h - STAT_H + 2, stxt, color::TEXT2, color::PANEL, w->content_w - 12);
    char hint[64];
    ksprintf(hint, sizeof(hint), "input URL or keyword to search");
    draw_text_clip(s, w->content_w - gfx::text_width(hint) - 8, w->content_h - STAT_H + 2,
                   hint, color::TEXT2, color::PANEL, w->content_w);
}

void on_key(Window* w, const KeyEvent* e) {
    BrowserState* st = (BrowserState*)w->userdata;
    // Note: no busy guard here -- browser_load() snapshots st->url into its own
    // copy, so typing while a page is loading is safe and never gets swallowed.
    if (e->ascii >= 32 && e->ascii < 127) {
        if (st->input.len() < 120) st->input += e->ascii;
    } else if (e->keycode == KEY_SPACE) {
        if (st->input.len() < 120) st->input += ' ';
    } else if (e->keycode == KEY_BACKSPACE) {
        if (st->input.len() > 0) st->input = st->input.substr(0, st->input.len() - 1);
    } else if (e->keycode == KEY_ENTER) {
        if (!st->input.empty()) browser_load(st, st->input.c_str());
    } else if (e->keycode == KEY_ESC) {
        st->input.clear();
    }
}

void on_mouse(Window* w, int mx, int my, uint8_t buttons) {
    BrowserState* st = (BrowserState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
    // buttons
    for (int i = 0; i < 3; i++) {
        st->cur = &st->btns[i];
        if (ui::button_event(st->btns[i], mx, my, buttons, pressed, released)) {
            if (st->btns[i].id == 0 && !st->input.empty()) browser_load(st, st->input.c_str());
            else if (st->btns[i].id == 1 && !st->input.empty()) {
                String q = "search:";
                q += st->input;
                browser_load(st, q.c_str());
            } else if (st->btns[i].id == 2) {
                st->status = browser_save_page(st) ? 2 : 3;
            }
        }
    }
    st->cur = 0;
    if (!pressed) return;
    if (my < BAR_H || my >= w->content_h - STAT_H) return;
    // click a link line
    int row_h = 16;
    int idx = st->scroll + (my - BAR_H) / row_h;
    if (idx < 0 || idx >= st->lines.size()) return;
    const Line& l = st->lines[idx];
    if (l.style != 2) return;
    // link text: open if looks like path/url, else search
    const char* t = l.s.c_str();
    if (strncmp(t, "file:// ", 7) == 0 || strncmp(t, "http://", 7) == 0 ||
        t[0] == '/' || strncmp(t, "Bing", 4) == 0 || strncmp(t, "Try:", 4) == 0) {
        // strip trailing " (folder)" / " (N bytes)"
        String clean = l.s;
        int sp = clean.find("  (");
        if (sp >= 0) clean = clean.substr(0, sp);
        if (!clean.empty()) browser_load(st, clean.c_str());
    } else {
        browser_load(st, t);
    }
}

void on_scroll(Window* w, int delta) {
    BrowserState* st = (BrowserState*)w->userdata;
    int row_h = 16;
    int vis = (w->content_h - BAR_H - STAT_H) / row_h;
    int max_scroll = st->lines.size() - vis - 1;
    if (max_scroll < 0) max_scroll = 0;
    st->scroll -= delta;
    if (st->scroll < 0) st->scroll = 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
}

void on_close(Window* w) {
    BrowserState* st = (BrowserState*)w->userdata;
    st->lines.erase_all();
    delete st;
    w->userdata = 0;
}

} // namespace

void browser_launch() {
    BrowserState* st = new BrowserState();
    st->input = "file:// /home/user/Documents/nefuos.txt";
    Window* w = g_wm->create_window("Browser", 40, 30, 640, 440);
    w->userdata = st;
    w->on_paint = on_paint;
    w->on_key = on_key;
    w->on_mouse = on_mouse;
    w->on_scroll = on_scroll;
    w->on_close = on_close;
    browser_load(st, st->input.c_str());
}

} // namespace nefu
