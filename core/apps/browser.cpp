// nefuOS built-in browser
// - file://  local VFS text/HTML viewer
// - http://   in-house TCP stack (bare) or WinINet (host)
// - search:   Bing (host) or local VFS filename search (bare)
// - self-contained HTML parser (no external library)

#include "browser.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include "../platform.h"
#include "../net/net.h"

namespace nefu {

namespace {

const int BAR_H = 30;
const int STAT_H = 20;

// ---- rendered output line (self-contained parser output) ----
struct Line {
    String s;
    int style;       // legacy: 0 normal, 1 title, 2 link
    int font_size;   // 16 / 20 / 24 / 32
    bool bold;
    uint32_t color;  // 0 = use default text color
    int indent;      // character columns
    Line() : style(0), font_size(16), bold(false), color(0), indent(0) {}
};

struct BrowserState {
    String input;
    String url;
    List<Line> lines;
    int scroll;
    int status;          // 0 idle 1 loading 2 done 3 error
    bool busy;
    int cursor;
    Button btns[3];
    Button* cur;
    uint8_t last_buttons;
};

// =====================================================================
// HTML entity decoder (named + numeric decimal + hex)
// =====================================================================
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Decodes one entity starting at s[0]=='&'. Returns decoded char (or '?'),
// sets *consumed to number of input bytes eaten.
static char decode_entity(const char* s, int* consumed) {
    if (s[0] != '&') { *consumed = 1; return s[0]; }
    if (!strncmp(s, "&nbsp;", 6)) { *consumed = 6; return ' '; }
    if (!strncmp(s, "&lt;",   4)) { *consumed = 4; return '<'; }
    if (!strncmp(s, "&gt;",   4)) { *consumed = 4; return '>'; }
    if (!strncmp(s, "&amp;",  5)) { *consumed = 5; return '&'; }
    if (!strncmp(s, "&quot;", 6)) { *consumed = 6; return '"'; }
    if (!strncmp(s, "&apos;", 6)) { *consumed = 6; return '\''; }
    if (!strncmp(s, "&copy;", 6)) { *consumed = 6; return 'C'; }
    if (!strncmp(s, "&reg;",  5)) { *consumed = 5; return 'R'; }
    if (!strncmp(s, "&hellip;",7)) { *consumed = 7; return '.'; }
    if (!strncmp(s, "&#", 2)) {
        int val = 0; int i = 2;
        if (s[2] == 'x' || s[2] == 'X') {
            i = 3;
            while (s[i] && s[i] != ';' && i < 12) { val = val * 16 + hex_val(s[i]); i++; }
        } else {
            while (s[i] && s[i] != ';' && i < 12) { val = val * 10 + (s[i] - '0'); i++; }
        }
        if (s[i] == ';') i++;
        *consumed = i;
        return (val >= 32 && val < 127) ? (char)val : '?';
    }
    *consumed = 1;
    return '&';
}

// =====================================================================
// Self-contained HTML parser -> List<Line>
// Supports: tags, attributes, comments, DOCTYPE, script/style/head skip,
// entity decode, whitespace collapse, block flow, headings, bold, links,
// lists (ul/ol), tables (tr/td), br, hr, img.
// =====================================================================
static bool is_void_tag(const char* t) {
    static const char* v[] = {"br","hr","img","input","meta","link","area",
                               "base","col","embed","source","track","wbr",0};
    for (int i = 0; v[i]; i++) if (!strcmp(t, v[i])) return true;
    return false;
}

static bool is_block_tag(const char* t) {
    static const char* b[] = {"p","div","section","article","header","footer",
                               "nav","aside","main","blockquote","pre","form",
                               "fieldset","figure","figcaption","address","center",0};
    for (int i = 0; b[i]; i++) if (!strcmp(t, b[i])) return true;
    return false;
}

static void html_parse(const char* html, int len, List<Line>& out) {
    String cur;
    int font_size = 16;
    bool bold = false;
    uint32_t color = 0;
    int indent = 0;
    int list_depth = 0;
    int ol_count[8] = {0,0,0,0,0,0,0,0};
    bool in_ul = false;
    bool in_skip = false;      // script / style / head
    char skip_tag[16] = {0};
    bool in_table = false;
    int td_count = 0;

    auto flush = [&]() {
        if (!cur.empty()) {
            Line l;
            l.s = cur;
            l.font_size = font_size;
            l.bold = bold;
            l.color = color;
            l.indent = indent;
            out.push(l);
            cur.clear();
        }
    };
    auto new_block = [&]() { flush(); };

    int i = 0;
    while (i < len) {
        // ---- skip script / style / head raw content ----
        if (in_skip) {
            char close[24] = "</";
            strcat(close, skip_tag);
            int clen = (int)strlen(close);
            int found = -1;
            for (int k = i; k + clen <= len; k++) {
                bool m2 = true;
                for (int c = 0; c < clen; c++) {
                    char a = html[k+c];
                    char b2 = close[c];
                    if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
                    if (b2 >= 'A' && b2 <= 'Z') b2 = b2 - 'A' + 'a';
                    if (a != b2) { m2 = false; break; }
                }
                if (m2) { found = k; break; }
            }
            if (found >= 0) { i = found; in_skip = false; }
            else { i = len; }
            continue;
        }

        if (html[i] == '<') {
            int j = i + 1;
            bool is_close = (j < len && html[j] == '/');
            if (is_close) j++;
            // comment <!-- -->
            if (j + 3 < len && html[j]=='!' && html[j+1]=='-' && html[j+2]=='-') {
                while (j + 2 < len && !(html[j]=='-'&&html[j+1]=='-'&&html[j+2]=='>')) j++;
                i = j + 3;
                continue;
            }
            // DOCTYPE / CDATA
            if (j < len && html[j] == '!') {
                while (j < len && html[j] != '>') j++;
                i = j + 1;
                continue;
            }
            // tag name (lowercase)
            char tname[32]; int tn = 0;
            while (j < len && tn < 31 && html[j]!='>' && html[j]!=' ' &&
                   html[j]!='\t' && html[j]!='\n' && html[j]!='/' && html[j]!='=') {
                char c = html[j++];
                if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                tname[tn++] = c;
            }
            tname[tn] = 0;
            // skip attributes
            while (j < len && html[j] != '>') j++;
            if (j < len && html[j] == '>') j++;
            i = j;
            if (tn == 0) continue;

            bool self_close = (j >= 2 && html[j-2] == '/');
            bool is_void = is_void_tag(tname);

            if (!is_close) {
                // ---- opening tag ----
                if (!strcmp(tname,"script") || !strcmp(tname,"style") ||
                    !strcmp(tname,"head") || !strcmp(tname,"title") ||
                    !strcmp(tname,"noscript")) {
                    in_skip = true;
                    strncpy(skip_tag, tname, 15); skip_tag[15]=0;
                    continue;
                }
                if (is_block_tag(tname)) new_block();
                if (!strcmp(tname,"h1")) { new_block(); font_size=32; bold=true; }
                else if (!strcmp(tname,"h2")) { new_block(); font_size=24; bold=true; }
                else if (!strcmp(tname,"h3")) { new_block(); font_size=20; bold=true; }
                else if (!strcmp(tname,"h4")||!strcmp(tname,"h5")||!strcmp(tname,"h6")) {
                    new_block(); font_size=16; bold=true;
                }
                else if (!strcmp(tname,"b")||!strcmp(tname,"strong")) bold=true;
                else if (!strcmp(tname,"a")) color=0x0000EE;
                else if (!strcmp(tname,"br")) new_block();
                else if (!strcmp(tname,"hr")) {
                    new_block();
                    Line l; l.s = "----------------------------------------";
                    l.font_size=16; out.push(l);
                }
                else if (!strcmp(tname,"ul")) { in_ul=true; list_depth++; indent+=2; }
                else if (!strcmp(tname,"ol")) { in_ul=false; list_depth++;
                    if (list_depth<8) ol_count[list_depth]=0; indent+=2; }
                else if (!strcmp(tname,"li")) {
                    new_block();
                    if (in_ul) cur += (const char*)"  ";
                    else {
                        if (list_depth<8) ol_count[list_depth]++;
                        char buf[16];
                        ksprintf(buf,sizeof(buf),"%d. ", list_depth<8?ol_count[list_depth]:1);
                        cur += buf;
                    }
                }
                else if (!strcmp(tname,"table")) { in_table=true; new_block(); }
                else if (!strcmp(tname,"tr")) { new_block(); td_count=0; }
                else if (!strcmp(tname,"td")||!strcmp(tname,"th")) {
                    if (td_count>0) cur += (const char*)" | ";
                    td_count++;
                    if (!strcmp(tname,"th")) bold=true;
                }
                else if (!strcmp(tname,"img")) cur += (const char*)"[IMG]";
                else if (!strcmp(tname,"li")) { /* handled above */ }
                if (is_void && !self_close) { /* void tags have no close */ }
            } else {
                // ---- closing tag ----
                if (!strcmp(tname,"script")||!strcmp(tname,"style")||
                    !strcmp(tname,"head")||!strcmp(tname,"title")||
                    !strcmp(tname,"noscript")) { in_skip=false; continue; }
                if (!strcmp(tname,"h1")||!strcmp(tname,"h2")||!strcmp(tname,"h3")||
                    !strcmp(tname,"h4")||!strcmp(tname,"h5")||!strcmp(tname,"h6")) {
                    new_block(); font_size=16; bold=false;
                }
                else if (!strcmp(tname,"b")||!strcmp(tname,"strong")) bold=false;
                else if (!strcmp(tname,"a")) color=0;
                else if (!strcmp(tname,"p")||!strcmp(tname,"div")||is_block_tag(tname)) new_block();
                else if (!strcmp(tname,"ul")||!strcmp(tname,"ol")) {
                    list_depth--; if (indent>=2) indent-=2; new_block();
                }
                else if (!strcmp(tname,"li")) new_block();
                else if (!strcmp(tname,"td")||!strcmp(tname,"th")) {
                    if (!strcmp(tname,"th")) bold=false;
                }
                else if (!strcmp(tname,"table")) { in_table=false; new_block(); }
                else if (!strcmp(tname,"tr")) new_block();
            }
        } else {
            // ---- text content ----
            char c = html[i];
            if (c == '&') {
                int consumed = 0;
                char dec = decode_entity(html + i, &consumed);
                if (consumed > 0) { cur += dec; i += consumed; continue; }
            }
            if (c==' '||c=='\t'||c=='\n'||c=='\r') {
                if (!cur.empty() && cur[cur.len()-1] != ' ') cur += ' ';
                i++;
                continue;
            }
            cur += c;
            i++;
        }
    }
    flush();
}

// legacy wrapper kept for callers that used the old name
static void html_to_lines(const char* html, int len, List<Line>& out) {
    html_parse(html, len, out);
}

// =====================================================================
// helpers
// =====================================================================
static void add_line(List<Line>& lines, const char* s, int style) {
    Line l;
    l.s = s ? s : "";
    l.style = style;
    if (style == 1) { l.font_size = 24; l.bold = true; }
    lines.push(l);
}

static const char* ltrim_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static bool parse_ip(const char* s, uint32_t* out) {
    int a=0,b=0,c=0,d=0;
    if (sscanf(s, "%d.%d.%d.%d", &a,&b,&c,&d) == 4 &&
        a>=0&&a<=255&&b>=0&&b<=255&&c>=0&&c<=255&&d>=0&&d<=255) {
        *out = ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)c<<8)|(uint32_t)d;
        return true;
    }
    return false;
}

// ---- network error page (never falls back to "file not found") ----
static void show_net_error(BrowserState* st, const char* url, const char* reason) {
    st->lines.erase_all();
    add_line(st->lines, "Unable to connect", 1);
    add_line(st->lines, "", 0);
    char buf[512];
    ksprintf(buf, sizeof(buf), "URL: %s", url ? url : "(empty)");
    add_line(st->lines, buf, 0);
    ksprintf(buf, sizeof(buf), "Error: %s", reason ? reason : "Connection failed");
    add_line(st->lines, buf, 0);
    add_line(st->lines, "", 0);
    add_line(st->lines, "Suggestions:", 0);
    add_line(st->lines, "  - Check your network connection", 0);
    add_line(st->lines, "  - Verify the URL is correct", 0);
    add_line(st->lines, "  - The server may be down or unreachable", 0);
    add_line(st->lines, "  - On bare metal, only IP literals are supported (no DNS)", 0);
    st->status = 3;
    st->scroll = 0;
    st->busy = false;
}

// =====================================================================
// loaders
// =====================================================================
static bool load_file(BrowserState* st, const char* path) {
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir) {
        st->lines.erase_all();
        add_line(st->lines, "File not found", 1);
        add_line(st->lines, "", 0);
        char buf[256];
        ksprintf(buf, sizeof(buf), "Path: %s", path);
        add_line(st->lines, buf, 0);
        add_line(st->lines, "", 0);
        add_line(st->lines, "Try: file:///home/user/Documents/nefuos.txt", 2);
        st->status = 3;
        return false;
    }
    st->lines.erase_all();
    bool is_html = false;
    const char* fn = f->name.c_str();
    int fl = f->name.len();
    if (fl > 5 && (!strcmp(fn+fl-5,".html") || !strcmp(fn+fl-5,".htm"))) is_html = true;
    if (fl > 4 && !strcmp(fn+fl-4,".xml")) is_html = true;
    if (is_html && f->size > 0) {
        html_parse((const char*)f->data, (int)f->size, st->lines);
    } else {
        // plain text: split by lines
        char* buf = (char*)kalloc((size_t)f->size + 1);
        if (buf) {
            memcpy(buf, f->data, f->size);
            buf[f->size] = 0;
            char* line = buf;
            for (char* p = buf; ; p++) {
                if (*p == '\n' || *p == 0) {
                    char save = *p;
                    *p = 0;
                    // strip trailing \r
                    int ll = (int)strlen(line);
                    if (ll > 0 && line[ll-1] == '\r') line[ll-1] = 0;
                    add_line(st->lines, line, 0);
                    if (save == 0) break;
                    line = p + 1;
                }
            }
            kfree(buf);
        }
    }
    st->scroll = 0;
    st->status = 2;
    return true;
}

static bool load_http(BrowserState* st, uint32_t ip, uint16_t port, const char* path) {
    int fd = tcp_connect(ip, port, 5000);
    if (fd < 0) return false;
    char req[512];
    int rl = ksprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\nUser-Agent: nefuOS/1.0\r\nConnection: close\r\n\r\n",
        path, (ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF, port);
    tcp_send(fd, req, rl);
    // read response
    uint8_t* body = 0;
    uint32_t body_len = 0;
    uint32_t cap = 8192;
    body = (uint8_t*)kalloc(cap);
    if (!body) { tcp_close(fd); return false; }
    uint32_t start = platform_tick_ms();
    while (platform_tick_ms() - start < 8000) {
        char c;
        int n = tcp_recv(fd, &c, 1, 200);
        if (n == 1) {
            if (body_len + 1 >= cap) {
                cap *= 2;
                uint8_t* nb = (uint8_t*)kalloc(cap);
                if (!nb) break;
                memcpy(nb, body, body_len);
                kfree(body);
                body = nb;
            }
            body[body_len++] = (uint8_t)c;
        } else if (n < 0) {
            break;
        }
    }
    tcp_close(fd);
    if (body_len == 0) { kfree(body); return false; }
    // skip HTTP headers (find \r\n\r\n)
    uint32_t hdr_end = 0;
    for (uint32_t k = 0; k + 3 < body_len; k++) {
        if (body[k]=='\r'&&body[k+1]=='\n'&&body[k+2]=='\r'&&body[k+3]=='\n') {
            hdr_end = k + 4;
            break;
        }
    }
    st->lines.erase_all();
    add_line(st->lines, "HTTP response", 1);
    add_line(st->lines, "", 0);
    char hdr[120];
    ksprintf(hdr, sizeof(hdr), "%u bytes received (header %u, body %u)",
             (unsigned)body_len, (unsigned)hdr_end, (unsigned)(body_len - hdr_end));
    add_line(st->lines, hdr, 0);
    add_line(st->lines, "", 0);
    if (body_len > hdr_end) {
        html_parse((const char*)body + hdr_end, (int)(body_len - hdr_end), st->lines);
    }
    kfree(body);
    st->scroll = 0;
    st->status = 2;
    return true;
}

// =====================================================================
// local VFS search (bare metal fallback when Bing is unreachable)
// =====================================================================
struct WalkCtx { BrowserState* st; const char* q; int hits; };

static void walk_search(FSNode* n, WalkCtx* c) {
    if (!n || !c->st) return;
    if (!n->is_dir) {
        const char* nm = n->name.c_str();
        if (strstr(nm, c->q)) {
            char buf[256];
            ksprintf(buf, sizeof(buf), "  %s  (%u bytes)", nm, (unsigned)n->size);
            add_line(c->st->lines, buf, 2);
            c->hits++;
            if (c->hits > 50) return;
        }
    }
    if (n->is_dir) {
        for (int i = 0; i < n->children.size(); i++) {
            walk_search(n->children[i], c);
            if (c->hits > 50) return;
        }
    }
}

static void browser_search(BrowserState* st, const char* q) {
    // try real Bing first (host backend via WinINet)
    char surl[512];
    ksprintf(surl, sizeof(surl), "https://www.bing.com/search?q=%s", q ? q : "");
    for (char* p = surl; *p; p++) if (*p == ' ') *p = '+';
    uint8_t* body = 0; uint32_t body_len = 0;
    if (platform_http_get(surl, &body, &body_len) && body && body_len > 0) {
        st->lines.erase_all();
        add_line(st->lines, "Bing search", 1);
        char head[96];
        ksprintf(head, sizeof(head), "Query: %s", q ? q : "");
        add_line(st->lines, head, 0);
        add_line(st->lines, "", 0);
        char* txt = (char*)kalloc((size_t)body_len + 1);
        if (txt) {
            memcpy(txt, body, body_len);
            txt[body_len] = 0;
            html_parse(txt, (int)body_len, st->lines);
            kfree(txt);
        }
        kfree(body);
        st->scroll = 0; st->status = 2; st->url = "search:";
        return;
    }
    // fall back to local VFS filename search (bare metal)
    st->lines.erase_all();
    add_line(st->lines, "Search (local VFS)", 1);
    char head[96];
    ksprintf(head, sizeof(head), "Query: %s", q ? q : "");
    add_line(st->lines, head, 0);
    add_line(st->lines, "", 0);
    WalkCtx ctx;
    ctx.st = st; ctx.q = q ? q : ""; ctx.hits = 0;
    walk_search(g_vfs->root(), &ctx);
    if (ctx.hits == 0) {
        add_line(st->lines, "No matching files in the local file system.", 0);
        add_line(st->lines, "", 0);
        add_line(st->lines, "Note: Bing search requires the host build with network access.", 0);
    } else {
        char s[64];
        ksprintf(s, sizeof(s), "%d result(s).", ctx.hits);
        add_line(st->lines, s, 0);
    }
    st->scroll = 0;
    st->status = 2;
    st->url = "search:";
}

static bool browser_save_page(BrowserState* st) {
    g_vfs->mkdir("/usr/downloads");
    char path[128];
    ksprintf(path, sizeof(path), "/usr/downloads/page_%u.html", (unsigned)platform_tick_ms());
    String all;
    for (int i = 0; i < st->lines.size(); i++) {
        all += st->lines[i].s;
        all += (const char*)"\n";
    }
    FSNode* f = g_vfs->create_file(path, (const uint8_t*)all.c_str(), (uint32_t)all.len());
    return f != 0;
}

// =====================================================================
// URL router
// =====================================================================
static void browser_load(BrowserState* st, const char* url) {
    url = ltrim_ws(url);
    st->url = url;
    st->status = 1;
    st->busy = true;

    if (strncmp(url, "file://", 7) == 0) {
        const char* path = ltrim_ws(url + 7);
        if (path[0] == 0) path = "/";
        load_file(st, path);
        st->busy = false;
        return;
    }

    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        // host backend: real HTTP(S) via WinINet (DNS + TLS included)
        uint8_t* body = 0;
        uint32_t body_len = 0;
        if (platform_http_get(url, &body, &body_len) && body && body_len > 0) {
            st->lines.erase_all();
            add_line(st->lines, "HTTP fetch", 1);
            add_line(st->lines, url, 2);
            char hdr[80];
            ksprintf(hdr, sizeof(hdr), "%u bytes received", (unsigned)body_len);
            add_line(st->lines, hdr, 0);
            add_line(st->lines, "", 0);
            char* txt = (char*)kalloc((size_t)body_len + 1);
            if (txt) {
                memcpy(txt, body, body_len);
                txt[body_len] = 0;
                html_parse(txt, (int)body_len, st->lines);
                kfree(txt);
            }
            kfree(body);
            st->status = 2;
            st->busy = false;
            st->scroll = 0;
            return;
        }
        // platform_http_get failed: on host this means a real network error;
        // on bare it always returns false, so fall through to the in-house TCP
        // stack (IP literals only).
        int off = (strncmp(url, "http://", 7) == 0) ? 7 : 8;
        const char* p = ltrim_ws(url + off);
        char host[64];
        char path[256];
        int hi = 0;
        while (*p && *p != ':' && *p != '/' && hi < 63) host[hi++] = *p++;
        host[hi] = 0;
        uint16_t port = 80;
        if (*p == ':') {
            p++;
            int pi = 0;
            while (*p >= '0' && *p <= '9' && pi < 4) { port = (uint16_t)(port*10 + (*p-'0')); p++; pi++; }
        }
        if (*p != '/') { path[0]='/'; path[1]=0; }
        else {
            int pi = 0;
            while (*p && pi < 255) path[pi++] = *p++;
            path[pi] = 0;
        }
        uint32_t ip;
        if (!parse_ip(host, &ip)) {
            // hostname not resolvable (no DNS on bare) -> this is a real error,
            // NOT a file lookup.
            show_net_error(st, url, "DNS resolution unavailable (bare metal supports IP literals only)");
            return;
        }
        if (!load_http(st, ip, port, path)) {
            show_net_error(st, url, "Connection refused or timed out");
            return;
        }
        st->busy = false;
        return;
    }

    if (strncmp(url, "search:", 7) == 0) {
        browser_search(st, url + 7);
        st->busy = false;
        return;
    }

    // plain word -> search; path-like (starts with / or .) -> file
    if (url[0] == '/' || url[0] == '.') {
        load_file(st, url);
    } else {
        browser_search(st, url);
    }
    st->busy = false;
}

// =====================================================================
// painting
// =====================================================================
static void draw_text_clip(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg, int maxx) {
    int cx = x;
    for (const char* p = str; *p; p++) {
        if (cx + 8 > maxx) break;
        gfx::char8x16(s, cx, y, *p, fg, bg);
        cx += 8;
    }
}

// draw text with font_size and faux-bold
static void draw_line_text(Surface& s, int x, int y, const char* str,
                           int font_size, bool bold, uint32_t color, int maxx) {
    uint32_t fg = color ? color : color::TEXT;
    uint32_t bg = color::WHITE;
    if (font_size <= 16) {
        int cx = x;
        for (const char* p = str; *p; p++) {
            if (cx + 8 > maxx) break;
            gfx::char8x16(s, cx, y, *p, fg, bg);
            if (bold) gfx::char8x16(s, cx+1, y, *p, fg, bg);
            cx += 8;
        }
    } else {
        // scale: font_size 20=1.25x, 24=1.5x, 32=2x
        int scale = (font_size >= 32) ? 2 : (font_size >= 24 ? 2 : 1);
        int cw = 8 * scale;
        int cx = x;
        for (const char* p = str; *p; p++) {
            if (cx + cw > maxx) break;
            // draw scaled by drawing each source pixel as scale x scale block
            for (int row = 0; row < 16; row++) {
                for (int col = 0; col < 8; col++) {
                    // use char8x16 glyph data indirectly: draw char then scale is complex;
                    // simpler: use gfx::text_scale if available, else draw at 16px
                }
            }
            gfx::char8x16(s, cx, y, *p, fg, bg);
            if (bold) gfx::char8x16(s, cx+1, y, *p, fg, bg);
            cx += cw;
        }
    }
}

static void on_paint(Window* w) {
    BrowserState* st = (BrowserState*)w->userdata;
    Surface& s = w->back;
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, color::WHITE);

    // address bar
    gfx::fillrect(s, 0, 0, w->content_w, BAR_H, color::PANEL);
    gfx::rect(s, 3, 3, w->content_w - 6 - 150, BAR_H - 6, color::BORDER);
    String disp = st->input.empty() ? st->url : st->input;
    draw_text_clip(s, 8, 7, disp.c_str(), color::TEXT, color::PANEL, w->content_w - 170);
    int cur_x = 8 + st->cursor * 8;
    if (cur_x > w->content_w - 170) cur_x = w->content_w - 170;
    gfx::char8x16(s, cur_x, 7, '|', color::TEXT2, color::PANEL);

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
    int vis = (w->content_h - y0 - STAT_H) / 18;
    if (vis < 0) vis = 0;
    int start = st->scroll;
    int cy = y0 + 2;
    for (int i = start; i < st->lines.size() && (i - start) < vis + 1; i++) {
        const Line& l = st->lines[i];
        if (l.s.empty()) { cy += 10; continue; }
        uint32_t fg = l.color ? l.color : color::TEXT;
        if (l.style == 2) fg = color::BLUE;
        int xoff = 6 + l.indent * 8;
        if (l.font_size >= 24) {
            gfx::text_scale(s, xoff, cy, l.s.c_str(), fg, color::WHITE, 2);
            if (l.bold) gfx::text_scale(s, xoff+1, cy, l.s.c_str(), fg, color::WHITE, 2);
            cy += 36;
        } else if (l.font_size >= 20) {
            gfx::text_scale(s, xoff, cy, l.s.c_str(), fg, color::WHITE, 1);
            if (l.bold) gfx::char8x16(s, xoff+1, cy, l.s[0], fg, color::WHITE);
            gfx::text(s, xoff, cy, l.s.c_str(), fg, color::WHITE);
            cy += 22;
        } else {
            gfx::text(s, xoff, cy, l.s.c_str(), fg, color::WHITE);
            if (l.bold) gfx::text(s, xoff+1, cy, l.s.c_str(), fg, color::WHITE);
            cy += 18;
        }
    }

    // status bar
    gfx::fillrect(s, 0, w->content_h - STAT_H, w->content_w, STAT_H, color::PANEL);
    const char* sttxt = "idle";
    if (st->status == 1) sttxt = "loading...";
    else if (st->status == 2) sttxt = "done";
    else if (st->status == 3) sttxt = "error";
    gfx::text(s, 6, w->content_h - STAT_H + 3, sttxt, color::TEXT2, color::PANEL);
    char cnt[64];
    ksprintf(cnt, sizeof(cnt), "%d lines", st->lines.size());
    gfx::text(s, w->content_w - 80, w->content_h - STAT_H + 3, cnt, color::TEXT2, color::PANEL);
}

// =====================================================================
// input
// =====================================================================
static void on_key(Window* w, const KeyEvent* e) {
    BrowserState* st = (BrowserState*)w->userdata;
    // UTF-8 IME input (CJK, etc.) -- insert multi-byte sequence at cursor
    if (e->utf8[0]) {
        int n = 0;
        while (n < 7 && e->utf8[n]) n++;
        if (st->input.len() + n <= 120) {
            String ns = st->input.substr(0, st->cursor);
            for (int i = 0; i < n; i++) ns += e->utf8[i];
            ns += st->input.substr(st->cursor, st->input.len() - st->cursor);
            st->input = ns;
            st->cursor += n;
        }
        return;
    }
    // Note: no busy guard -- browser_load() snapshots st->url into its own copy.
    if (e->ascii >= 32 && e->ascii < 127) {
        if (st->input.len() < 120) {
            String ns = st->input.substr(0, st->cursor);
            ns += (char)e->ascii;
            ns += st->input.substr(st->cursor, st->input.len() - st->cursor);
            st->input = ns;
            st->cursor++;
        }
    } else if (e->keycode == KEY_SPACE) {
        if (st->input.len() < 120) {
            String ns = st->input.substr(0, st->cursor);
            ns += ' ';
            ns += st->input.substr(st->cursor, st->input.len() - st->cursor);
            st->input = ns;
            st->cursor++;
        }
    } else if (e->keycode == KEY_BACKSPACE) {
        if (st->cursor > 0) {
            String ns = st->input.substr(0, st->cursor - 1);
            ns += st->input.substr(st->cursor, st->input.len() - st->cursor);
            st->input = ns;
            st->cursor--;
        }
    } else if (e->keycode == KEY_DEL) {
        if (st->cursor < st->input.len()) {
            String ns = st->input.substr(0, st->cursor);
            ns += st->input.substr(st->cursor + 1, st->input.len() - st->cursor - 1);
            st->input = ns;
        }
    } else if (e->keycode == KEY_LEFT) {
        if (st->cursor > 0) st->cursor--;
    } else if (e->keycode == KEY_RIGHT) {
        if (st->cursor < st->input.len()) st->cursor++;
    } else if (e->keycode == KEY_HOME) {
        st->cursor = 0;
    } else if (e->keycode == KEY_END) {
        st->cursor = st->input.len();
    } else if (e->keycode == KEY_ENTER) {
        if (!st->input.empty()) browser_load(st, st->input.c_str());
    } else if (e->keycode == KEY_ESC) {
        st->input.clear();
        st->cursor = 0;
    }
}

static void on_mouse(Window* w, int mx, int my, uint8_t buttons) {
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

    // address bar click: place the text cursor at the clicked column
    if (pressed && my >= 3 && my < BAR_H - 3 && mx >= 3 && mx < w->content_w - 150) {
        int pos = (mx - 8) / 8;
        if (pos < 0) pos = 0;
        if (pos > st->input.len()) pos = st->input.len();
        st->cursor = pos;
    }

    if (!pressed) return;
    if (my < BAR_H || my >= w->content_h - STAT_H) return;
    // click a link line
    int row_h = 18;
    int idx = st->scroll + (my - BAR_H) / row_h;
    if (idx < 0 || idx >= st->lines.size()) return;
    const Line& l = st->lines[idx];
    if (l.style == 2 || l.color == 0x0000EE) {
        // clickable: if it looks like a path, open it
        const char* txt = l.s.c_str();
        while (*txt == ' ') txt++;
        if (txt[0] == '/' || txt[0] == '.') {
            browser_load(st, txt);
        }
    }
}

static void on_scroll(Window* w, int delta) {
    BrowserState* st = (BrowserState*)w->userdata;
    st->scroll += delta;
    if (st->scroll < 0) st->scroll = 0;
    int max = st->lines.size() - 5;
    if (max < 0) max = 0;
    if (st->scroll > max) st->scroll = max;
}

static void on_close(Window* w) {
    BrowserState* st = (BrowserState*)w->userdata;
    delete st;
    w->userdata = 0;
}

} // namespace

void browser_launch() {
    BrowserState* st = new BrowserState();
    st->input = "file:///home/user/Documents/nefuos.txt";
    st->cursor = st->input.len();
    Window* w = g_wm->create_window("Browser", 40, 30, 640, 440);
    w->userdata = st;
    w->on_paint = on_paint;
    w->on_key = on_key;
    w->on_mouse = on_mouse;
    w->on_scroll = on_scroll;
    w->on_close = on_close;
    g_wm->raise(w);
    browser_load(st, st->input.c_str());
}

} // namespace nefu
