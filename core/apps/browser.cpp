// nefuOS built-in browser
// - file://  local VFS text/HTML viewer
// - http://   threaded download (host: WinINet, bare: in-house TCP)
// - search:   Bing (host) or local VFS (bare)
// - self-contained HTML parser with <img> rendering
// - all network I/O runs on background threads (UI never blocks)

#include "apps.h"
#include "minijs.h"
#include "../gui/gfx.h"   // gfx::char16x16 renders the shared CJK table
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
const int MAX_IMG_W = 300;   // max rendered image width
const int MAX_IMG_H = 200;   // max rendered image height

// ---- cached decoded image ----
struct CachedImage {
    String url;
    Surface* surf;
    bool loading;
    bool failed;
    CachedImage() : surf(0), loading(false), failed(false) {}
};

// ---- rendered output line ----
struct Line {
    String s;
    int style;       // 0 normal, 1 title, 2 link
    int font_size;   // 16 / 20 / 24 / 32
    bool bold;
    uint32_t color;  // 0 = use default text color
    int indent;      // character columns
    String image_url; // non-empty = this line is an image
    Surface* image;   // cached decoded image (null = not loaded yet)
    // form support: 0 none, 1 text input, 2 submit button
    int form_kind;
    String form_name;
    String form_val;
    int form_method;   // 0 = GET (query string), 1 = POST (body)
    Line() : style(0), font_size(16), bold(false), color(0), indent(0), image(0),
             form_kind(0), form_method(0) {}
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
    // ---- threaded page download state ----
    volatile bool load_done;
    volatile bool load_error;
    uint8_t* loaded_body;
    uint32_t loaded_len;
    String loaded_url;
    void* load_thread;
    // ---- image cache ----
    List<CachedImage*> img_cache;
    // ---- form editing ----
    int form_edit_row;      // row of the input being edited, -1 = none
    String form_edit_val;   // edited value
};

// =====================================================================
// HTML entity decoder
// =====================================================================
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static char decode_entity(const char* s, int* consumed) {
    if (s[0] != '&') { *consumed = 1; return s[0]; }
    if (!strncmp(s, "&nbsp;", 6)) { *consumed = 6; return ' '; }
    if (!strncmp(s, "&lt;",   4)) { *consumed = 4; return '<'; }
    if (!strncmp(s, "&gt;",   4)) { *consumed = 4; return '>'; }
    if (!strncmp(s, "&amp;",  5)) { *consumed = 5; return '&'; }
    if (!strncmp(s, "&quot;", 6)) { *consumed = 6; return '"'; }
    if (!strncmp(s, "&apos;", 6)) { *consumed = 6; return '\''; }
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

// extract attribute value from tag text: e.g. src="http://..." -> returns value
static String get_attr(const char* tag_text, int tag_len, const char* attr_name) {
    String result;
    int alen = (int)strlen(attr_name);
    for (int i = 0; i + alen < tag_len; i++) {
        bool match = true;
        for (int j = 0; j < alen; j++) {
            char a = tag_text[i+j];
            char b2 = attr_name[j];
            if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
            if (b2 >= 'A' && b2 <= 'Z') b2 = b2 - 'A' + 'a';
            if (a != b2) { match = false; break; }
        }
        if (match) {
            int k = i + alen;
            while (k < tag_len && (tag_text[k] == ' ' || tag_text[k] == '\t')) k++;
            if (k < tag_len && tag_text[k] == '=') {
                k++;
                while (k < tag_len && (tag_text[k] == ' ' || tag_text[k] == '\t')) k++;
                char quote = 0;
                if (k < tag_len && (tag_text[k] == '"' || tag_text[k] == '\'')) {
                    quote = tag_text[k]; k++;
                }
                while (k < tag_len) {
                    if (quote && tag_text[k] == quote) break;
                    if (!quote && (tag_text[k] == ' ' || tag_text[k] == '>' || tag_text[k] == '\t')) break;
                    result += tag_text[k];
                    k++;
                }
                return result;
            }
        }
    }
    return result;
}

static void html_parse(const char* html, int len, List<Line>& out, char* out_title = 0, int title_cap = 0) {
    String cur;
    int font_size = 16;
    bool bold = false;
    uint32_t color = 0;
    int indent = 0;
    int list_depth = 0;
    int ol_count[8] = {0,0,0,0,0,0,0,0};
    bool in_ul = false;
    bool in_skip = false;
    char skip_tag[16] = {0};
    int script_start = -1;
    bool in_table = false;
    int td_count = 0;
    bool in_title = false;
    int title_len = 0;

    // browser engine choice from first-boot setup (/etc/nefu.conf):
    //   browser_engine=noscript disables <script> execution (HTML only).
    bool js_on = true;
    {
        FSNode* conf = g_vfs->resolve("/etc/nefu.conf");
        if (conf && conf->data && conf->size > 0) {
            char cbuf[1024];
            uint32_t cn = conf->size < 1023 ? conf->size : 1023;
            memcpy(cbuf, conf->data, cn);
            cbuf[cn] = 0;
            char* bp = strstr(cbuf, "browser_engine=");
            if (bp && strncmp(bp + 15, "noscript", 8) == 0) js_on = false;
        }
    }

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
            int tag_start = i;
            int j = i + 1;
            bool is_close = (j < len && html[j] == '/');
            if (is_close) j++;
            if (j + 3 < len && html[j]=='!' && html[j+1]=='-' && html[j+2]=='-') {
                while (j + 2 < len && !(html[j]=='-'&&html[j+1]=='-'&&html[j+2]=='>')) j++;
                i = j + 3;
                continue;
            }
            if (j < len && html[j] == '!') {
                while (j < len && html[j] != '>') j++;
                i = j + 1;
                continue;
            }
            char tname[32]; int tn = 0;
            while (j < len && tn < 31 && html[j]!='>' && html[j]!=' ' &&
                   html[j]!='\t' && html[j]!='\n' && html[j]!='/' && html[j]!='=') {
                char c = html[j++];
                if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                tname[tn++] = c;
            }
            tname[tn] = 0;
            int tag_text_end = j;
            while (tag_text_end < len && html[tag_text_end] != '>') tag_text_end++;
            int tag_text_len = tag_text_end - tag_start;
            while (j < len && html[j] != '>') j++;
            if (j < len && html[j] == '>') j++;
            i = j;
            if (tn == 0) continue;

            if (!is_close) {
                if (!strcmp(tname,"title")) { in_title = true; title_len = 0; continue; }
                if (!strcmp(tname,"script") || !strcmp(tname,"style") ||
                    !strcmp(tname,"head") || !strcmp(tname,"title") ||
                    !strcmp(tname,"noscript")) {
                    in_skip = true;
                    strncpy(skip_tag, tname, 15); skip_tag[15]=0;
                    if (!strcmp(tname,"script")) script_start = i;  // i is just past '>'
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
                else if (!strcmp(tname,"img")) {
                    // extract src attribute and create image line
                    String src = get_attr(html + tag_start, tag_text_len, "src");
                    if (!src.empty()) {
                        flush();
                        Line l;
                        l.image_url = src;
                        out.push(l);
                    } else {
                        cur += (const char*)"[IMG]";
                    }
                }
                else if (!strcmp(tname,"input")) {
                    // <input name=.. type=.. value=.. placeholder=..> -> form line
                    String name = get_attr(html + tag_start, tag_text_len, "name");
                    String type = get_attr(html + tag_start, tag_text_len, "type");
                    String val  = get_attr(html + tag_start, tag_text_len, "value");
                    String ph   = get_attr(html + tag_start, tag_text_len, "placeholder");
                    String fm   = get_attr(html + tag_start, tag_text_len, "method");
                    flush();
                    Line l;
                    if (!type.empty() && (type == "submit" || type == "button")) {
                        l.form_kind = 2;
                        l.form_name = name;
                        l.form_val = val.empty() ? (ph.empty() ? "Submit" : ph) : val;
                        l.form_method = (!fm.empty() && fm == "post") ? 1 : 0;
                    } else {
                        l.form_kind = 1;
                        l.form_name = name;
                        l.form_val = val.empty() ? ph : val;
                        l.s = " ";
                        l.style = 3;   // input row marker
                    }
                    out.push(l);
                }
                else if (!strcmp(tname,"button")) {
                    // <button>Label</button> -> submit line
                    String txt = get_attr(html + tag_start, tag_text_len, "value");
                    String fm  = get_attr(html + tag_start, tag_text_len, "method");
                    flush();
                    Line l;
                    l.form_kind = 2;
                    l.form_name = get_attr(html + tag_start, tag_text_len, "name");
                    l.form_val = txt.empty() ? "Submit" : txt;
                    l.form_method = (!fm.empty() && fm == "post") ? 1 : 0;
                    out.push(l);
                }
            } else {
                if (!strcmp(tname,"title")) { in_title = false; if (out_title) out_title[title_len] = 0; continue; }
                if (!strcmp(tname,"script")||!strcmp(tname,"style")||
                    !strcmp(tname,"head")||!strcmp(tname,"title")||
                    !strcmp(tname,"noscript")) {
                    if (!strcmp(tname,"script") && script_start >= 0) {
                        int slen = tag_start - script_start;
                        if (slen > 0 && slen < 600) {
                            char sbuf[600];
                            memcpy(sbuf, html + script_start, slen);
                            sbuf[slen] = 0;
                            char jsout[600];
                            int rc = js_on ? mini_js_run(sbuf, jsout, sizeof(jsout)) : 1;
                            if (!rc && jsout[0]) {
                                flush();
                                Line l;
                                l.s = jsout;
                                l.font_size = 16;
                                out.push(l);
                            }
                        }
                        script_start = -1;
                    }
                    in_skip=false; continue;
                }
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
            char c = html[i];
            if (in_title) {
                if (out_title && title_len < title_cap - 1) out_title[title_len++] = c;
                i++;
                continue;
            }
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
    int parts[4] = {0,0,0,0};
    int idx = 0;
    const char* p = s;
    while (*p && idx < 4) {
        int val = 0; int digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 3) {
            val = val * 10 + (*p - '0');
            p++; digits++;
        }
        if (digits == 0 || val > 255) return false;
        parts[idx++] = val;
        if (*p == '.') { p++; continue; }
        break;
    }
    if (idx != 4 || *p != 0) return false;
    *out = ((uint32_t)parts[0]<<24)|((uint32_t)parts[1]<<16)|((uint32_t)parts[2]<<8)|(uint32_t)parts[3];
    return true;
}

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
// image cache + background image download
// =====================================================================
static CachedImage* find_cached_image(BrowserState* st, const char* url) {
    for (int i = 0; i < st->img_cache.size(); i++) {
        if (!strcmp(st->img_cache[i]->url.c_str(), url)) return st->img_cache[i];
    }
    return 0;
}

static void browser_image_thread(void* arg) {
    CachedImage* img = (CachedImage*)arg;
    uint8_t* body = 0; uint32_t body_len = 0;
    if (platform_http_get(img->url.c_str(), &body, &body_len) && body && body_len > 0) {
        Surface* s = new Surface();
        if (platform_decode_image(body, body_len, *s)) {
            img->surf = s;
        } else {
            delete s;
            img->failed = true;
        }
        kfree(body);
    } else {
        img->failed = true;
    }
    img->loading = false;
}

static void ensure_image_loading(BrowserState* st, Line& l) {
    if (l.image_url.empty()) return;
    if (l.image) return; // already resolved
    CachedImage* ci = find_cached_image(st, l.image_url.c_str());
    if (!ci) {
        ci = new CachedImage();
        ci->url = l.image_url;
        ci->surf = 0;
        ci->loading = true;
        ci->failed = false;
        st->img_cache.push(ci);
        platform_thread_create(browser_image_thread, ci);
    }
    if (ci->surf) l.image = ci->surf;
    else if (ci->failed) l.image = (Surface*)-1; // mark as failed (won't retry)
}

// =====================================================================
// loaders (synchronous, called from background thread)
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
        char* buf = (char*)kalloc((size_t)f->size + 1);
        if (buf) {
            memcpy(buf, f->data, f->size);
            buf[f->size] = 0;
            char* line = buf;
            for (char* p = buf; ; p++) {
                if (*p == '\n' || *p == 0) {
                    char save = *p;
                    *p = 0;
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

// Append a "Cookie: host=value" header from the cookie jar
// (/var/lib/nefuos/cookies.txt, one "host=N; path=/; nefuOS" per line)
// that matches the request URL host. Returns the extra header length.
static int http_append_cookie(const char* url, char* req, int req_len, int cap) {
    const char* h = strstr(url, "://");
    const char* host = h ? h + 3 : url;
    char hb[96];
    int hn = 0;
    while (host[hn] && host[hn] != '/' && host[hn] != '?' && host[hn] != ':' && hn < 90) {
        hb[hn] = host[hn];
        hn++;
    }
    hb[hn] = 0;
    if (hn == 0) return 0;
    FSNode* ck = g_vfs->resolve("/var/lib/nefuos/cookies.txt");
    if (!ck || ck->size == 0) return 0;
    char* jar = (char*)kalloc((size_t)ck->size + 1);
    if (!jar) return 0;
    memcpy(jar, ck->data, ck->size);
    jar[ck->size] = 0;
    String out;
    char* p = jar;
    while (*p) {
        char* nl = p;
        while (*nl && *nl != '\n') nl++;
        char save = *nl;
        *nl = 0;
        // line "host=N; path=/; nefuOS"
        if (strncmp(p, hb, hn) == 0 && p[hn] == '=') {
            if (!out.empty()) out += '; ';
            out += hb;
            int v = 0;
            while (p[hn + 1 + v] && p[hn + 1 + v] != ';') v++;
            out += '=';
            for (int k = 0; k < v; k++) out += p[hn + 1 + k];
        }
        *nl = save;
        if (save == 0) break;
        p = nl + 1;
    }
    kfree(jar);
    if (out.empty()) return 0;
    // "Cookie: host=1; host=2\r\n"
    char hdr[256];
    int hl = ksprintf(hdr, sizeof(hdr), "Cookie: %s\r\n", out.c_str());
    if (req_len + hl >= cap) return 0;
    memcpy(req + req_len, hdr, hl);
    return hl;
}

// Send a POST request with the given URL-encoded body and render the reply.
static bool http_post(BrowserState* st, const char* url, const char* body) {
    // parse scheme://host:port/path
    const char* p = url;
    uint32_t ip = 0;
    uint16_t port = 80;
    const char* path = "/";
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;
    char hostbuf[128];
    int hn = 0;
    while (*p && *p != '/' && *p != ':' && hn < 120) hostbuf[hn++] = *p++;
    hostbuf[hn] = 0;
    if (*p == ':') {
        p++;
        int pn = 0;
        while (*p >= '0' && *p <= '9') { pn = pn * 10 + (*p - '0'); p++; }
        if (pn > 0) port = (uint16_t)pn;
    }
    if (*p == '/') path = p;
    if (!parse_ip(hostbuf, &ip)) return false;
    int fd = tcp_connect(ip, port, 5000);
    if (fd < 0) return false;
    char req[768];
    int bl = (int)strlen(body);
    int rl = ksprintf(req, sizeof(req),
        "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: nefuOS/1.0\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n"
        "Connection: close\r\n", path, hostbuf, bl);
    rl += http_append_cookie(url, req, rl, (int)sizeof(req));
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "\r\n%s", body);
    tcp_send(fd, req, rl);
    uint8_t* resp = 0;
    uint32_t resp_len = 0;
    uint32_t cap = 8192;
    resp = (uint8_t*)kalloc(cap);
    if (!resp) { tcp_close(fd); return false; }
    uint32_t start = platform_tick_ms();
    while (platform_tick_ms() - start < 8000) {
        char c;
        int n = tcp_recv(fd, &c, 1, 200);
        if (n == 1) {
            if (resp_len + 1 >= cap) {
                cap *= 2;
                uint8_t* nb = (uint8_t*)kalloc(cap);
                if (!nb) break;
                memcpy(nb, resp, resp_len);
                kfree(resp);
                resp = nb;
            }
            resp[resp_len++] = (uint8_t)c;
        } else if (n < 0) break;
    }
    tcp_close(fd);
    st->lines.erase_all();
    add_line(st->lines, "HTTP POST response", 1);
    add_line(st->lines, "", 0);
    if (resp_len > 0) {
        uint32_t hdr_end = 0;
        for (uint32_t k = 0; k + 3 < resp_len; k++) {
            if (resp[k]=='\r'&&resp[k+1]=='\n'&&resp[k+2]=='\r'&&resp[k+3]=='\n') {
                hdr_end = k + 4;
                break;
            }
        }
        char hdr[120];
        ksprintf(hdr, sizeof(hdr), "%u bytes received (header %u, body %u)",
                 (unsigned)resp_len, (unsigned)hdr_end, (unsigned)(resp_len - hdr_end));
        add_line(st->lines, hdr, 0);
        add_line(st->lines, "", 0);
        if (resp_len > hdr_end)
            html_parse((const char*)resp + hdr_end, (int)(resp_len - hdr_end), st->lines);
    } else {
        add_line(st->lines, "No response", 0);
    }
    kfree(resp);
    st->scroll = 0;
    st->status = 2;
    return true;
}

static bool load_http(BrowserState* st, uint32_t ip, uint16_t port, const char* path) {
    int fd = tcp_connect(ip, port, 5000);
    if (fd < 0) return false;
    char req[640];
    int rl = ksprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\nUser-Agent: nefuOS/1.0\r\nConnection: close\r\n",
        path, (ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF, port);
    rl += http_append_cookie(st->url.c_str(), req, rl, (int)sizeof(req) - 4);
    rl += ksprintf(req + rl, (int)sizeof(req) - rl, "\r\n");
    tcp_send(fd, req, rl);
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
        } else if (n < 0) break;
    }
    tcp_close(fd);
    if (body_len == 0) { kfree(body); return false; }
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
// local VFS search (bare fallback)
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

// When a page has no static text (JS-generated SPA), show a helpful
// fallback instead of a blank body.
static void page_fallback(List<Line>& lines, const char* url, const char* title) {
    int content = 0;
    for (int i = 4; i < lines.size(); i++) {   // skip status block (0..3)
        Line& l = lines[i];
        if (l.style == 1 || l.style == 2 || l.s.empty()) continue;
        content++;
        if (content > 1) break;
    }
    if (content > 0) return;
    lines.erase_all();
    add_line(lines, "Page has no static text", 1);
    add_line(lines, "", 0);
    if (title && title[0]) {
        char t[160];
        ksprintf(t, sizeof(t), "Title: %s", title);
        add_line(lines, t, 0);
        add_line(lines, "", 0);
    }
    add_line(lines, "This page is generated by JavaScript, which the", 0);
    add_line(lines, "nefuOS browser cannot execute yet.", 0);
    add_line(lines, "", 0);
    add_line(lines, "You can:", 2);
    add_line(lines, "  - search Bing: type a keyword (not a URL) in the", 0);
    add_line(lines, "    address bar and press Enter", 0);
    add_line(lines, "  - open the offline Wiki mirror from the Wiki app", 0);
    add_line(lines, "  - visit a plain-HTML site, e.g. bing.com", 0);
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
    FSNode* f = g_vfs->create_file(path);
    if (!f) return false;
    return g_vfs->write_file(f, (const uint8_t*)all.c_str(), (uint32_t)all.len());
}

// =====================================================================
// background page download thread
// =====================================================================
static void browser_page_thread(void* arg) {
    BrowserState* st = (BrowserState*)arg;
    const char* url = st->loaded_url.c_str();

    // search: URLs
    if (strncmp(url, "search:", 7) == 0) {
        browser_search(st, url + 7);
        st->load_done = true;
        return;
    }

    // http/https: try platform_http_get first
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        uint8_t* body = 0; uint32_t body_len = 0;
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
                char ttl[160]; ttl[0] = 0;
                html_parse(txt, (int)body_len, st->lines, ttl, (int)sizeof(ttl));
                kfree(txt);
                page_fallback(st->lines, url, ttl);
            }
            kfree(body);
            st->status = 2;
            st->scroll = 0;
            st->load_done = true;
            return;
        }
        // fall back to bare TCP for IP literals
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
            st->load_error = true;
            st->load_done = true;
            return;
        }
        if (!load_http(st, ip, port, path)) {
            st->load_error = true;
            st->load_done = true;
            return;
        }
        st->load_done = true;
        return;
    }

    // plain word -> search; path-like -> file
    if (url[0] == '/' || url[0] == '.') {
        load_file(st, url);
    } else {
        browser_search(st, url);
    }
    st->load_done = true;
}

// =====================================================================
// URL router (starts background thread for network URLs)
// =====================================================================
static void browser_load(BrowserState* st, const char* url) {
    url = ltrim_ws(url);
    st->url = url;
    st->status = 1;
    st->busy = true;
    st->load_done = false;
    st->load_error = false;
    st->loaded_body = 0;
    st->loaded_len = 0;
    st->loaded_url = url;
    // file:// loads synchronously (fast, no network)
    if (strncmp(url, "file://", 7) == 0) {
        const char* path = ltrim_ws(url + 7);
        if (path[0] == 0) path = "/";
        load_file(st, path);
        st->busy = false;
        st->load_done = true;
        return;
    }
    // everything else (http/https/search/plain word) goes to background thread
    st->load_thread = platform_thread_create(browser_page_thread, st);
}

// =====================================================================
// UTF-8 decode for address bar rendering
// =====================================================================
static uint32_t decode_utf8(const char*& p) {
    uint8_t c = (uint8_t)*p;
    if (c < 0x80) { p++; return c; }
    if ((c & 0xE0) == 0xC0) {
        uint32_t r = (c & 0x1F) << 6; p++;
        if (((uint8_t)*p & 0xC0) == 0x80) { r |= ((uint8_t)*p & 0x3F); p++; }
        return r;
    }
    if ((c & 0xF0) == 0xE0) {
        uint32_t r = (c & 0x0F) << 12; p++;
        if (((uint8_t)*p & 0xC0) == 0x80) { r |= ((uint8_t)*p & 0x3F) << 6; p++; }
        if (((uint8_t)*p & 0xC0) == 0x80) { r |= ((uint8_t)*p & 0x3F); p++; }
        return r;
    }
    p++;
    return 0xFFFD;
}

#include "../gui/gfx.h"   // gfx::char16x16 renders the shared CJK table
static void draw_text_clip(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg, int maxx) {
    int cx = x;
    const char* p = str;
    while (*p) {
        uint32_t uc = decode_utf8(p);
        int cw = (uc < 0x80) ? 8 : 16;
        if (cx + cw > maxx) break;
        if (uc < 0x80) gfx::char8x16(s, cx, y, (char)uc, fg, bg);
        else {
            // non-ASCII (CJK): render from the built-in 16x16 bitmap font
            gfx::char16x16(s, cx, y, uc, fg, bg);
        }
        cx += cw;
    }
}

// Record the visited URL in the browser history (/home/user/.nefu_history).
static void history_add(BrowserState* st, const char* url) {
    if (!url || !*url) return;
    FSNode* f = g_vfs->resolve("/home/user/.nefu_history");
    if (!f) f = g_vfs->create_file("/home/user/.nefu_history");
    if (!f) return;
    char* buf = (char*)kalloc(f->size + 1);
    if (!buf) return;
    memcpy(buf, f->data, f->size);
    buf[f->size] = 0;
    bool dup = (strstr(buf, url) != 0);
    if (!dup) {
        int lines = 1;
        for (uint32_t i = 0; i < f->size; i++) if (buf[i] == '\n') lines++;
        String nb;
        if (lines >= 50) {
            // keep last 49 lines
            const char* p = buf;
            int skip = lines - 49;
            while (skip > 0 && *p) { if (*p == '\n') skip--; p++; }
            if (*p == '\n') p++;
            nb = p;
        }
        nb += url;
        nb += '\n';
        g_vfs->write_file(f, (const uint8_t*)nb.c_str(), (uint32_t)nb.len());
    }
    kfree(buf);
    // cookie jar: count visits per site (host part of the URL)
    const char* h = strstr(url, "://");
    const char* host = h ? h + 3 : url;
    char hb[96];
    int hn = 0;
    while (host[hn] && host[hn] != '/' && host[hn] != '?' && hn < 90) { hb[hn] = host[hn]; hn++; }
    hb[hn] = 0;
    if (hn > 0) {
        FSNode* ck = g_vfs->resolve("/var/lib/nefuos/cookies.txt");
        if (!ck) ck = g_vfs->create_file("/var/lib/nefuos/cookies.txt");
        if (ck) {
            char* cb = (char*)kalloc(ck->size + 1);
            if (cb) {
                memcpy(cb, ck->data, ck->size);
                cb[ck->size] = 0;
                char line[140];
                int n = ksprintf(line, sizeof(line), "%s=%d; path=/; nefuOS\n", hb, 1);
                // append (simple jar: one line per visit record)
                String jar = cb;
                jar += line;
                g_vfs->write_file(ck, (const uint8_t*)jar.c_str(), (uint32_t)jar.len());
                kfree(cb);
            }
        }
    }
    (void)st;
}

// =====================================================================
// painting (checks background thread completion, renders images)
// =====================================================================
static void on_paint(Window* w) {
    BrowserState* st = (BrowserState*)w->userdata;

    // ---- check if background page download finished ----
    if (st->busy && st->load_done) {
        if (st->load_error) {
            show_net_error(st, st->url.c_str(), "Connection failed or timed out");
        } else {
            history_add(st, st->url.c_str());
        }
        // lines already populated by the background thread
        st->busy = false;
        st->load_done = false;
        if (st->loaded_body) { kfree(st->loaded_body); st->loaded_body = 0; }
    }

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

    // buttons
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

    // content: render lines with variable height (text lines + images)
    int y0 = BAR_H;
    int content_bottom = w->content_h - STAT_H;
    int cy = y0 + 2;
    int line_height = 18;

    // first pass: calculate y positions and start image loads
    // We render from scroll position
    int skipped = 0;
    bool started = false;
    for (int i = 0; i < st->lines.size(); i++) {
        const Line& l = st->lines[i];
        int this_h = line_height;
        if (!l.image_url.empty()) {
            this_h = MAX_IMG_H + 4;
            if (l.image && l.image != (Surface*)-1 && l.image->addr) {
                int ih = l.image->height;
                if (ih > MAX_IMG_H) ih = MAX_IMG_H;
                this_h = ih + 4;
            }
        }
        if (!started) {
            if (skipped < st->scroll) { skipped++; cy += this_h; continue; }
            started = true;
        }
        if (cy + this_h > content_bottom) break;

        if (!l.image_url.empty()) {
            // image line
            ensure_image_loading(st, (Line&)l);
            if (l.image && l.image != (Surface*)-1 && l.image->addr) {
                // render image scaled to fit
                Surface* img = l.image;
                int iw = img->width, ih = img->height;
                if (iw > MAX_IMG_W) { ih = ih * MAX_IMG_W / iw; iw = MAX_IMG_W; }
                if (ih > MAX_IMG_H) { iw = iw * MAX_IMG_H / ih; ih = MAX_IMG_H; }
                int ix = 6 + l.indent * 8;
                // draw image background
                gfx::fillrect(s, ix, cy, iw, ih, 0xEEEEEE);
                // copy pixels (simple nearest-neighbor scale)
                for (int yy = 0; yy < ih; yy++) {
                    for (int xx = 0; xx < iw; xx++) {
                        int sx = xx * img->width / iw;
                        int sy = yy * img->height / ih;
                        if (sx < img->width && sy < img->height) {
                            uint32_t px = img->getpx(sx, sy);
                            if (cy + yy >= 0 && cy + yy < s.height && ix + xx >= 0 && ix + xx < s.width) {
                                s.setpx(ix + xx, cy + yy, px);
                            }
                        }
                    }
                }
                gfx::rect(s, ix, cy, iw, ih, color::BORDER);
            } else if (l.image == (Surface*)-1) {
                // failed to load
                gfx::fillrect(s, 6, cy, MAX_IMG_W, 40, 0xFFEEEE);
                gfx::text(s, 10, cy + 12, "[image failed to load]", 0xCC0000, 0xFFEEEE);
            } else {
                // loading
                gfx::fillrect(s, 6, cy, MAX_IMG_W, 40, 0xF0F0F0);
                gfx::text(s, 10, cy + 12, "[loading image...]", color::TEXT2, 0xF0F0F0);
            }
            cy += this_h;
        } else {
            // text line
            if (l.s.empty() && l.form_kind == 0) { cy += 10; continue; }
            uint32_t fg = l.color ? l.color : color::TEXT;
            if (l.style == 2) fg = color::BLUE;
            int xoff = 6 + l.indent * 8;
            if (l.form_kind == 1) {
                // text input box
                const char* val = (i == st->form_edit_row && !st->form_edit_val.empty())
                                  ? st->form_edit_val.c_str() : l.form_val.c_str();
                int bx = xoff + 6, bw = 200;
                if (bw > w->content_w - bx - 8) bw = w->content_w - bx - 8;
                bool edit = (i == st->form_edit_row);
                gfx::fillrect(s, bx, cy, bw, 17, edit ? 0x00FFF8DC : color::WHITE);
                gfx::rect(s, bx, cy, bw, 17, edit ? 0x00000080 : color::BORDER);
                draw_text_clip(s, bx + 3, cy + 1, val, color::TEXT, edit ? 0x00FFF8DC : color::WHITE, bw - 6);
                if (edit) gfx::char8x16(s, bx + 3 + st->form_edit_val.len() * 8, cy + 1, '|', color::TEXT2, 0x00FFF8DC);
                // label (name attribute)
                if (!l.form_name.empty()) {
                    gfx::text(s, xoff, cy + 1, l.form_name.c_str(), color::TEXT2, color::WHITE);
                }
                cy += 20;
            } else if (l.form_kind == 2) {
                // submit button
                int bw = (int)strlen(l.form_val.c_str()) * 8 + 16;
                if (bw < 64) bw = 64;
                gfx::fillrect(s, xoff + 6, cy, bw, 20, 0x003E7CB1);
                gfx::rect(s, xoff + 6, cy, bw, 20, 0x002F6FB6);
                gfx::text(s, xoff + 14, cy + 3, l.form_val.c_str(), color::WHITE, 0x003E7CB1);
                cy += 24;
            } else if (l.font_size >= 24) {
                gfx::text_scale(s, xoff, cy, l.s.c_str(), fg, color::WHITE, 2);
                if (l.bold) gfx::text_scale(s, xoff+1, cy, l.s.c_str(), fg, color::WHITE, 2);
                cy += 36;
            } else if (l.font_size >= 20) {
                gfx::text(s, xoff, cy, l.s.c_str(), fg, color::WHITE);
                if (l.bold) gfx::text(s, xoff+1, cy, l.s.c_str(), fg, color::WHITE);
                cy += 22;
            } else {
                gfx::text(s, xoff, cy, l.s.c_str(), fg, color::WHITE);
                if (l.bold) gfx::text(s, xoff+1, cy, l.s.c_str(), fg, color::WHITE);
                cy += 18;
            }
        }
    }

    // status bar
    gfx::fillrect(s, 0, w->content_h - STAT_H, w->content_w, STAT_H, color::PANEL);
    const char* sttxt = "idle";
    if (st->busy) sttxt = "loading...";
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
    // ---- form field editing takes priority over the address bar ----
    if (st->form_edit_row >= 0) {
        if (e->utf8[0]) {
            int n = 0;
            while (n < 7 && e->utf8[n]) n++;
            if (st->form_edit_val.len() + n <= 200) {
                for (int i = 0; i < n; i++) st->form_edit_val += e->utf8[i];
            }
        } else if (e->ascii >= 32 && e->ascii < 127) {
            if (st->form_edit_val.len() < 200) st->form_edit_val += (char)e->ascii;
        } else if (e->keycode == KEY_SPACE) {
            if (st->form_edit_val.len() < 200) st->form_edit_val += ' ';
        } else if (e->keycode == KEY_BACKSPACE) {
            if (st->form_edit_val.len() > 0) {
                st->form_edit_val = st->form_edit_val.substr(0, st->form_edit_val.len() - 1);
            }
        } else if (e->keycode == KEY_ENTER) {
            // blur field
            st->form_edit_row = -1;
            st->form_edit_val.clear();
        } else if (e->keycode == KEY_ESC) {
            st->form_edit_row = -1;
            st->form_edit_val.clear();
        }
        return;
    }
    // UTF-8 IME input
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

// Build a GET query from the form fields and navigate to it.
// POST submit runs on a background thread so the UI never blocks.
struct PostCtx { BrowserState* st; String url; String body; };
static void browser_post_thread(void* arg) {
    PostCtx* c = (PostCtx*)arg;
    bool ok = http_post(c->st, c->url.c_str(), c->body.c_str());
    c->st->status = ok ? 2 : 3;
    c->st->busy = false;
    c->st->load_done = true;
    c->st->load_error = !ok;
    delete c;
}

static void browser_submit_form(BrowserState* st, int submit_row) {
    if (st->loaded_url.empty()) return;
    String q;
    int method = 0;
    for (int i = 0; i < st->lines.size(); i++) {
        const Line& l = st->lines[i];
        if (l.form_kind == 2 && i == submit_row) method = l.form_method;
        if (l.form_kind != 1 || l.form_name.empty()) continue;
        if (!q.empty()) q += '&';
        q += l.form_name;
        q += '=';
        String v = (i == st->form_edit_row && !st->form_edit_val.empty())
                   ? st->form_edit_val : l.form_val;
        q += v;
    }
    st->form_edit_row = -1;
    st->form_edit_val.clear();
    String target = st->loaded_url;
    if (method == 1) {
        // POST: send the form fields as the request body
        PostCtx* c = new PostCtx();
        c->st = st;
        c->url = target;
        c->body = q;
        st->url = target;
        st->status = 1;
        st->busy = true;
        st->load_done = false;
        st->load_error = false;
        st->loaded_url = target;
        st->load_thread = platform_thread_create(browser_post_thread, c);
        return;
    }
    // GET: navigate to "target?query"
    if (!q.empty()) {
        target += (target.find('?') >= 0) ? '&' : '?';
        target += q;
    }
    browser_load(st, target.c_str());
}

static void on_mouse(Window* w, int mx, int my, uint8_t buttons) {
    BrowserState* st = (BrowserState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
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

    if (pressed && my >= 3 && my < BAR_H - 3 && mx >= 3 && mx < w->content_w - 150) {
        int pos = (mx - 8) / 8;
        if (pos < 0) pos = 0;
        if (pos > st->input.len()) pos = st->input.len();
        st->cursor = pos;
    }

    if (!pressed) return;
    if (my < BAR_H || my >= w->content_h - STAT_H) return;
    int row_h = 18;
    int idx = st->scroll + (my - BAR_H) / row_h;
    if (idx < 0 || idx >= st->lines.size()) return;
    const Line& l = st->lines[idx];
    // ---- form interactions ----
    if (l.form_kind == 1) {
        st->form_edit_row = idx;
        st->form_edit_val = l.form_val;
        st->cursor = 0;
        return;
    }
    if (l.form_kind == 2) {
        browser_submit_form(st, idx);
        return;
    }
    if (l.style == 2 || l.color == 0x0000EE) {
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
    // free image cache
    for (int i = 0; i < st->img_cache.size(); i++) {
        if (st->img_cache[i]->surf) {
            if (st->img_cache[i]->surf->addr) kfree(st->img_cache[i]->surf->addr);
            delete st->img_cache[i]->surf;
        }
        delete st->img_cache[i];
    }
    delete st;
    w->userdata = 0;
}

} // anonymous namespace

void browser_launch_url(const char* url) {
    BrowserState* st = new BrowserState();
    st->input = url ? url : "file:///README.txt";
    st->cursor = st->input.len();
    st->form_edit_row = -1;
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

void browser_launch() {
    browser_launch_url("file:///README.txt");
}

} // namespace nefu
