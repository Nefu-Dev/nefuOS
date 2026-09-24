// =====================================================================
// nefuOS Browser v3.0 — professional-grade browser
//
// Architecture (Ladybird / LibHubbub / LibCSS inspired, four-stage pipeline):
//
//   ┌─────────────┐   navigate()   ┌────────────┐
//   │ BrowserApp  │ ─────────────► │  Loader    │  platform_http_get()
//   │ chrome / UI │ ◄───────────── │  (worker)  │  host: worker thread
//   └─────────────┘   page bytes   └─────┬──────┘  bare: synchronous inline
//                                        ▼
//   ┌─────────────┐   DOM (bounded) ┌────────────┐
//   │ Layout      │ ◄────────────── │ HTML Parser│  tokenizer → tree builder
//   │ block flow  │                 └────────────┘  (entities, script skip)
//   └──────┬──────┘
//          ▼  runs + link rects
//   ┌─────────────┐
//   │ Paint       │  cull + draw runs to the framebuffer, scroll-aware
//   └─────────────┘
//
// Design goals:
//  * SAFE     — every buffer bounded, DOM capped and depth-limited,
//               no out-of-bounds writes anywhere.
//  * FAST     — the page is parsed ONCE per navigation; the layout is
//               cached per tab and rebuilt only on width change.
//  * UX       — real tabs (open/close/switch), back/forward history,
//               clickable links, editable address bar with cursor,
//               loading state, proportional scrollbar, status bar.
//  * PORTABLE — one codebase for the Win32 host (worker thread) and the
//               bare-metal kernel (synchronous load; no scheduler yet).
//
// Keyboard (platform convention: Ctrl+letter arrives as the plain letter):
//   l focus address · t new tab · w close tab · r reload · h home · F5 reload
//   arrows / PgUp / PgDn / Home / End scroll the page
// =====================================================================
// Windows + OpenGL headers must come FIRST, before any C++ standard header:
// windows.h pulls in intrin.h -> mm_malloc.h -> <cstdlib> internally, and
// expanding that after libstdc++ state is set up breaks ::abs/::div_t etc.
// Bare builds skip this section entirely (no GPU driver stack there).
#ifndef NEFU_BARE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include "apps.h"
#include "jpeg.h"          // nefu::jpeg_decode — in-tree baseline-JPEG decoder
#include "stb_image_wrap.h" // nefu::stbi_decode_mem/info/sniff + nsvg SVG decode
#include "minijs.h"        // nefu::mini_js_run — tiny JS engine for <script>
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"
#include <cstring>
#include <cstdio>

namespace nefu {

namespace {

// ===================== layout constants =====================
const int TAB_H = 24;          // tab strip height
const int NAV_H = 22;          // navigation row height
const int BAR_H = 58;          // total chrome height (tabs + nav)
const int STAT_H = 24;         // status bar height
const int SB_W  = 10;          // scrollbar width
const int MAX_TABS  = 8;       // maximum open tabs
const int MAX_PAGE  = 34 * 1024 * 1024;   // page buffer cap (34 MB per tab)
const int DOM_MAX   = 16384;   // DOM node arena cap
const int DOM_DEPTH = 48;      // DOM nesting depth cap
const int HIST_MAX  = 24;      // per-tab history entries
const int RUN_MAX   = 32768;   // layout run cap

// ===================== image support =====================
const int IMG_MAX      = 12;    // cached images per tab
const int MAX_IMG_PIX  = 262144; // decoded pixel cap per image (e.g. 512x512)
const int MAX_IMG_DISP = 200;   // max display width (px); height scales

// ===================== CSS subset + JS =====================
const int CSS_MAX     = 64;      // style-sheet rules per tab
const int SCRIPT_MAX  = 8192;    // accumulated <script> bytes per page
const int JS_OUT_MAX  = 2048;    // JS console output buffer

// One parsed CSS rule: a selector + the properties this engine understands.
// 0 / false / -1 means "not set" so later rules can override earlier ones.
struct CSSRule {
    char sel[64];
    uint32_t color;      // text color (0 = not set)
    int font_size;       // px (0 = not set)
    bool bold_set;
    bool bold;
    bool none;           // display:none
    uint32_t bg;         // background color (0 = none)
    int align;           // 0 none, 1 center, 2 right
    int margin;          // px, -1 = not set (all four sides share one value)
    int padding;         // px, -1 = not set
    int width;           // px, -1 = auto
    int height;          // px, -1 = auto
    CSSRule() : color(0), font_size(0), bold_set(false), bold(false),
                none(false), bg(0), align(0), margin(-1), padding(-1),
                width(-1), height(-1) { sel[0] = 0; }
};

// ===================== colors (dark Ladybird-style chrome) =====================
const uint32_t C_TOOLBAR  = 0x0F172A;
const uint32_t C_TAB_ACT  = 0x3B82F6;
const uint32_t C_TAB_IDLE = 0x1E293B;
const uint32_t C_BTN      = 0x334155;
const uint32_t C_BTN_DIS  = 0x273244;
const uint32_t C_TEXT_LT  = 0x94A3B8;
const uint32_t C_TEXT_DK  = 0x1E293B;
const uint32_t C_ADDR_BG  = 0xF8FAFC;
const uint32_t C_ADDR_BD  = 0x94A3B8;
const uint32_t C_PAGE_BG  = 0xFFFFFF;
const uint32_t C_LINK     = 0x2563EB;
const uint32_t C_HEAD     = 0x0F172A;
const uint32_t C_GRAY     = 0x64748B;
const uint32_t C_HRLINE   = 0xCBD5E1;
const uint32_t C_IMG      = 0xE2E8F0;
const uint32_t C_SB_TRACK = 0xF1F5F9;
const uint32_t C_SB_THUMB = 0x94A3B8;
const uint32_t C_STATUS   = 0x0F172A;
const uint32_t C_ERR      = 0xDC2626;

// ===================== small helpers =====================
static char lower_char(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// case-insensitive equality against a string literal
static bool ci_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (lower_char(*a) != lower_char(*b)) return false;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

// case-insensitive prefix match
static bool ci_starts(const char* s, const char* pre) {
    while (*pre) {
        if (lower_char(*s) != lower_char(*pre)) return false;
        s++; pre++;
    }
    return true;
}

static int maxi(int a, int b) { return a > b ? a : b; }

// truncate a string to cap-1 bytes (always NUL-terminated)
static void clip_str(char* dst, int cap, const char* src) {
    if (cap <= 0) return;
    int i = 0;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// percent-encode a URL / search query
static void url_encode(const char* in, char* out, int cap) {
    int n = 0;
    for (const char* p = in; *p && n < cap - 4; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = c;
        } else {
            n += ksprintf(out + n, 8, "%%%02X", (unsigned char)c);
        }
    }
    out[n] = 0;
}

// extract the host part of a URL (for tab titles and status)
static void url_host(const char* url, char* out, int cap) {
    out[0] = 0;
    const char* p = url;
    if (ci_starts(p, "http://")) p += 7;
    else if (ci_starts(p, "https://")) p += 8;
    else if (ci_starts(p, "file://")) p += 7;
    int n = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && n < cap - 1) out[n++] = *p++;
    out[n] = 0;
}

// display width of a UTF-8 string in the 8x16 bitmap font
// (ASCII glyphs are 8 px wide, CJK glyphs 16 px)
static int utf8_char_len(const char* s) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0 && (unsigned char)s[1]) return 2;
    if ((c & 0xF0) == 0xE0 && (unsigned char)s[1] && (unsigned char)s[2]) return 3;
    return 1;
}

static int disp_w(const char* s, int len) {
    int w = 0, i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w += 8; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < len) { w += 16; i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < len) { w += 16; i += 3; }
        else { w += 8; i++; }
    }
    return w;
}

// ===================== DOM (bounded arena) =====================
enum class Tag : uint8_t {
    Root, Text,
    H1, H2, H3, H4, H5, H6,
    P, Div, Span, A, Img,
    Ul, Ol, Li, Br, Hr,
    B, Strong, I, Em, Pre, Blockquote,
    Table, Tr, Td, Th, Form, Button, Canvas,
    Script, Style, Unknown
};

static Tag tag_of(const char* name) {
    switch (lower_char(name[0])) {
        case 'h':
            if (ci_eq(name, "h1")) return Tag::H1;
            if (ci_eq(name, "h2")) return Tag::H2;
            if (ci_eq(name, "h3")) return Tag::H3;
            if (ci_eq(name, "h4")) return Tag::H4;
            if (ci_eq(name, "h5")) return Tag::H5;
            if (ci_eq(name, "h6")) return Tag::H6;
            if (ci_eq(name, "hr")) return Tag::Hr;
            break;
        case 'p':
            if (ci_eq(name, "p")) return Tag::P;
            if (ci_eq(name, "pre")) return Tag::Pre;
            break;
        case 'd':
            if (ci_eq(name, "div")) return Tag::Div;
            break;
        case 's':
            if (ci_eq(name, "span")) return Tag::Span;
            if (ci_eq(name, "strong")) return Tag::Strong;
            if (ci_eq(name, "script")) return Tag::Script;
            if (ci_eq(name, "style")) return Tag::Style;
            break;
        case 'a':
            if (ci_eq(name, "a")) return Tag::A;
            break;
        case 'i':
            if (ci_eq(name, "img")) return Tag::Img;
            if (ci_eq(name, "i")) return Tag::I;
            break;
        case 'u':
            if (ci_eq(name, "ul")) return Tag::Ul;
            break;
        case 'o':
            if (ci_eq(name, "ol")) return Tag::Ol;
            break;
        case 'l':
            if (ci_eq(name, "li")) return Tag::Li;
            break;
        case 'b':
            if (ci_eq(name, "b")) return Tag::B;
            if (ci_eq(name, "blockquote")) return Tag::Blockquote;
            if (ci_eq(name, "button")) return Tag::Button;
            break;
        case 'c':
            if (ci_eq(name, "canvas")) return Tag::Canvas;
            break;
        case 'e':
            if (ci_eq(name, "em")) return Tag::Em;
            break;
        case 't':
            if (ci_eq(name, "table")) return Tag::Table;
            if (ci_eq(name, "tr")) return Tag::Tr;
            if (ci_eq(name, "td")) return Tag::Td;
            if (ci_eq(name, "th")) return Tag::Th;
            break;
        case 'f':
            if (ci_eq(name, "form")) return Tag::Form;
            break;
        case 'n':
            if (ci_eq(name, "nav")) return Tag::Div;
            break;
        default: break;
    }
    // structural tags → Div, inline/unknown → Span
    if (ci_eq(name, "header") || ci_eq(name, "section") || ci_eq(name, "article") ||
        ci_eq(name, "footer") || ci_eq(name, "main") || ci_eq(name, "aside") ||
        ci_eq(name, "hgroup") || ci_eq(name, "address")) return Tag::Div;
    if (ci_eq(name, "code") || ci_eq(name, "kbd") || ci_eq(name, "samp") ||
        ci_eq(name, "small") || ci_eq(name, "mark") || ci_eq(name, "label") ||
        ci_eq(name, "u") || ci_eq(name, "sub") || ci_eq(name, "sup")) return Tag::Span;
    return Tag::Unknown;
}

static bool is_void_tag(const char* name) {
    return ci_eq(name, "br") || ci_eq(name, "hr") || ci_eq(name, "img") ||
           ci_eq(name, "input") || ci_eq(name, "meta") || ci_eq(name, "link") ||
           ci_eq(name, "source") || ci_eq(name, "base") || ci_eq(name, "wbr");
}

static bool is_block_tag(Tag t) {
    switch (t) {
        case Tag::P: case Tag::Div: case Tag::H1: case Tag::H2: case Tag::H3:
        case Tag::H4: case Tag::H5: case Tag::H6: case Tag::Ul: case Tag::Ol:
        case Tag::Li: case Tag::Pre: case Tag::Blockquote: case Tag::Table:
        case Tag::Tr: case Tag::Td: case Tag::Th: case Tag::Form: case Tag::Canvas:
            return true;
        default: return false;
    }
}

static bool is_inline_tag(Tag t) {
    switch (t) {
        case Tag::Span: case Tag::A: case Tag::B: case Tag::Strong:
        case Tag::I: case Tag::Em: case Tag::Button: case Tag::Unknown:
            return true;
        default: return false;
    }
}

struct DomNode {
    Tag tag = Tag::Unknown;
    String text;       // Text nodes only
    String href, src, alt;
    List<int> kids;
    char cls[48];      // class attribute (space-separated)
    char id[32];       // id attribute
    char onclick[96];  // inline onclick="" handler (mini JS)
    int cw, ch;        // canvas element size (width/height attrs, default 300x150)
    CSSRule inl;       // inline style="" (selector unused)
    bool has_inl;
    DomNode() : has_inl(false), cw(300), ch(150) { cls[0] = id[0] = onclick[0] = 0; }
};

struct DomArena {
    List<DomNode> nodes;
    List<int> stack;
    bool overflow = false;

    DomArena() {
        DomNode root;
        root.tag = Tag::Root;
        nodes.push(root);
        stack.push(0);
    }

    int top() const { return stack.size() > 0 ? stack[stack.size() - 1] : 0; }

    void open(Tag t, const char* href, const char* src, const char* alt,
              const char* cls = 0, const char* id = 0, const CSSRule* inl = 0,
              const char* onclick = 0) {
        if (overflow) return;
        if (nodes.size() >= DOM_MAX || stack.size() >= DOM_DEPTH) { overflow = true; return; }
        int idx = nodes.size();
        DomNode n;
        n.tag = t;
        if (href && href[0]) n.href = href;
        if (src && src[0]) n.src = src;
        if (alt && alt[0]) n.alt = alt;
        if (cls && cls[0]) clip_str(n.cls, sizeof(n.cls), cls);
        if (id && id[0]) clip_str(n.id, sizeof(n.id), id);
        if (onclick && onclick[0]) clip_str(n.onclick, sizeof(n.onclick), onclick);
        if (inl) { n.inl = *inl; n.has_inl = true; }
        nodes.push(n);
        nodes[top()].kids.push(idx);
        stack.push(idx);
    }

    // pop the stack up to (and including) the first node of tag t
    void pop_to(Tag t) {
        int i = stack.size() - 1;
        while (i > 0 && nodes[stack[i]].tag != t) i--;
        if (i > 0 && nodes[stack[i]].tag == t) {
            while (stack.size() > i) stack.pop();
        }
    }

    void close_top() { if (stack.size() > 1) stack.pop(); }

    void add_text(const char* s, int len) {
        if (overflow || len <= 0) return;
        if (nodes.size() >= DOM_MAX) { overflow = true; return; }
        int p = top();
        // merge into the previous text child when possible (fewer nodes)
        if (nodes[p].kids.size() > 0) {
            int k = nodes[p].kids[nodes[p].kids.size() - 1];
            if (nodes[k].tag == Tag::Text) {
                for (int i = 0; i < len; i++) nodes[k].text += s[i];
                return;
            }
        }
        DomNode n;
        n.tag = Tag::Text;
        // inherit onclick from the containing element so the run's node
        // hit-test finds the handler
        if (nodes[p].onclick[0]) clip_str(n.onclick, sizeof(n.onclick), nodes[p].onclick);
        for (int i = 0; i < len; i++) n.text += s[i];
        nodes.push(n);
        nodes[p].kids.push(nodes.size() - 1);
    }
};

// decode HTML entities into a NUL-terminated buffer, returns decoded length
static int decode_entities(const char* in, int inlen, char* out, int cap) {
    int n = 0, i = 0;
    while (i < inlen && n < cap - 1) {
        char c = in[i];
        if (c == '&' && i + 1 < inlen) {
            int adv = 0;
            char r = 0;
            if (i + 4 < inlen && in[i+1]=='a' && in[i+2]=='m' && in[i+3]=='p' && in[i+4]==';') { r='&'; adv=5; }
            else if (i + 3 < inlen && in[i+1]=='l' && in[i+2]=='t' && in[i+3]==';') { r='<'; adv=4; }
            else if (i + 3 < inlen && in[i+1]=='g' && in[i+2]=='t' && in[i+3]==';') { r='>'; adv=4; }
            else if (i + 5 < inlen && in[i+1]=='q' && in[i+2]=='u' && in[i+3]=='o' && in[i+4]=='t' && in[i+5]==';') { r='"'; adv=6; }
            else if (i + 4 < inlen && in[i+1]=='#' && in[i+2]=='3' && in[i+3]=='9' && in[i+4]==';') { r='\''; adv=5; }
            else if (i + 5 < inlen && in[i+1]=='n' && in[i+2]=='b' && in[i+3]=='s' && in[i+4]=='p' && in[i+5]==';') { r=' '; adv=6; }
            else if (i + 3 < inlen && in[i+1]=='#' && in[i+2] >= '0' && in[i+2] <= '9') {
                int val = 0, j = i + 2;
                while (j < inlen && in[j] >= '0' && in[j] <= '9' && j - (i + 2) < 6) { val = val * 10 + (in[j] - '0'); j++; }
                if (j < inlen && in[j] == ';') {
                    r = (val >= 32 && val < 127) ? (char)val : ' ';
                    adv = j - i + 1;
                }
            }
            if (r != 0) { out[n++] = r; i += adv; continue; }
        }
        out[n++] = c;
        i++;
    }
    out[n] = 0;
    return n;
}

struct PageLayout;   // fwd (defined with the layout engine below)

// ===================== CSS subset =====================
// Parse #RGB / #RRGGBB / #RRGGBBAA / a few named colors -> 0xRRGGBB.
static uint32_t css_color(const char* v) {
    if (v[0] == '#') {
        int hx = 0, n = 0;
        while (v[n + 1] && n < 8) {
            char c = lower_char(v[n + 1]);
            int dv;
            if (c >= '0' && c <= '9') dv = c - '0';
            else if (c >= 'a' && c <= 'f') dv = c - 'a' + 10;
            else break;
            hx = (hx << 4) | dv;
            n++;
        }
        if (n == 3) {
            int r = (hx >> 8) & 0xF, g = (hx >> 4) & 0xF, b = hx & 0xF;
            return (uint32_t)((r << 16) | (r << 12) | (g << 8) | (g << 4) | b);
        }
        if (n == 6) return (uint32_t)hx;
        if (n == 8) return (uint32_t)(hx & 0xFFFFFF);
        return 0;
    }
    if (ci_eq(v, "red")) return 0xDC2626;
    if (ci_eq(v, "green")) return 0x16A34A;
    if (ci_eq(v, "blue")) return 0x2563EB;
    if (ci_eq(v, "black")) return 0x0F172A;
    if (ci_eq(v, "white")) return 0xFFFFFF;
    if (ci_eq(v, "gray") || ci_eq(v, "grey")) return 0x64748B;
    if (ci_eq(v, "yellow")) return 0xFACC15;
    if (ci_eq(v, "orange")) return 0xF97316;
    return 0;
}

// leading integer of "14px" / "120" (clamped to [0,512])
static int css_px(const char* v) {
    int n = 0;
    while (v[n] >= '0' && v[n] <= '9') n++;
    if (n == 0) return 0;
    int x = 0;
    for (int i = 0; i < n; i++) x = x * 10 + (v[i] - '0');
    if (x > 512) x = 512;
    return x;
}

// Does a selector match this node? Supports: tag, .class, #id, tag.class,
// tag#id, .class#id, tag.class#id and '*'. The rest is ignored.
static bool css_match(const char* sel, Tag tag, const char* cls, const char* id) {
    const char* s = sel;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    int sl = (int)strlen(s);
    while (sl > 0 && (s[sl-1]==' '||s[sl-1]=='\t'||s[sl-1]=='\n')) sl--;
    if (sl == 0) return false;
    if (sl == 1 && s[0] == '*') return true;
    char tagbuf[24]; int tn = 0;
    char clsbuf[48]; int cn = 0;
    char idbuf[32];  int in = 0;
    int mode = 0;   // 0 tag, 1 class, 2 id
    for (int i = 0; i < sl; i++) {
        char c = s[i];
        if (c == '.') { mode = 1; continue; }
        if (c == '#') { mode = 2; continue; }
        if (mode == 0 && tn < 23) tagbuf[tn++] = lower_char(c);
        else if (mode == 1 && cn < 47) clsbuf[cn++] = lower_char(c);
        else if (mode == 2 && in < 31) idbuf[in++] = lower_char(c);
    }
    tagbuf[tn] = 0; clsbuf[cn] = 0; idbuf[in] = 0;
    if (tn > 0 && tag_of(tagbuf) != tag) return false;
    if (cn > 0) {
        bool hit = false;
        int cl = (int)strlen(cls), p = 0;
        while (p <= cl) {
            int q = p;
            while (q < cl && cls[q] != ' ') q++;
            if (q - p == cn) {
                bool eq = true;
                for (int k = 0; k < cn; k++)
                    if (lower_char(cls[p + k]) != clsbuf[k]) { eq = false; break; }
                if (eq) { hit = true; break; }
            }
            p = q + 1;
        }
        if (!hit) return false;
    }
    if (in > 0) {
        if (id[0] == 0 || (int)strlen(id) != in) return false;
        for (int k = 0; k < in; k++)
            if (lower_char(id[k]) != idbuf[k]) return false;
    }
    return true;
}

// Parse a "prop: value; prop: value" declaration block into a CSSRule.
static void css_parse_decls(const char* d, int len, CSSRule& r) {
    int i = 0;
    while (i < len) {
        while (i < len && (d[i]==' '||d[i]=='\n'||d[i]=='\t'||d[i]=='\r'||d[i]==';')) i++;
        if (i >= len) break;
        char prop[24]; int pn = 0;
        while (i < len && d[i] != ':' && pn < 23) prop[pn++] = lower_char(d[i++]);
        prop[pn] = 0;
        while (i < len && (d[i]==' '||d[i]=='\t'||d[i]=='\n'||d[i]=='\r'||d[i]==':')) i++;
        char val[64]; int vn = 0;
        while (i < len && d[i] != ';' && vn < 63) val[vn++] = d[i++];
        val[vn] = 0;
        int a = 0, b = vn;
        while (a < b && (val[a]==' '||val[a]=='\t'||val[a]=='\n'||val[a]=='\r')) a++;
        while (b > a && (val[b-1]==' '||val[b-1]=='\t'||val[b-1]=='\n'||val[b-1]=='\r')) b--;
        memmove(val, val + a, (size_t)(b - a)); val[b - a] = 0;
        if (ci_eq(prop, "color")) {
            uint32_t c = css_color(val);
            if (c) r.color = c;
        } else if (ci_eq(prop, "font-size")) {
            int v = css_px(val);
            if (v > 0) r.font_size = v;
        } else if (ci_eq(prop, "font-weight")) {
            r.bold_set = true;
            r.bold = !(ci_eq(val, "normal") || ci_eq(val, "400") || ci_eq(val, "lighter"));
        } else if (ci_eq(prop, "display")) {
            if (ci_eq(val, "none")) r.none = true;
        } else if (ci_eq(prop, "background-color")) {
            uint32_t c = css_color(val);
            if (c) r.bg = c;
        } else if (ci_eq(prop, "text-align")) {
            if (ci_eq(val, "center")) r.align = 1;
            else if (ci_eq(val, "right")) r.align = 2;
        } else if (ci_eq(prop, "margin")) {
            int v = css_px(val);
            if (v > 0) r.margin = v;
        } else if (ci_eq(prop, "padding")) {
            int v = css_px(val);
            if (v > 0) r.padding = v;
        } else if (ci_eq(prop, "width")) {
            int v = css_px(val);
            if (v > 0) r.width = v;
        } else if (ci_eq(prop, "height")) {
            int v = css_px(val);
            if (v > 0) r.height = v;
        }
    }
}

// Parse a full stylesheet: "selector { decls }" repeated. Returns rule count.
static int css_parse(const char* css, int len, CSSRule* out, int max) {
    int n = 0, i = 0;
    while (i < len && n < max) {
        while (i < len && (css[i]==' '||css[i]=='\n'||css[i]=='\t'||css[i]=='\r')) i++;
        if (i >= len) break;
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {   // comment
            int e = i + 2;
            while (e + 1 < len && !(css[e]=='*' && css[e+1]=='/')) e++;
            i = (e + 1 < len) ? e + 2 : len;
            continue;
        }
        if (css[i] == '}' || css[i] == ';') { i++; continue; }
        char sel[64]; int sn = 0;
        while (i < len && css[i] != '{' && sn < 63) {
            if (css[i] != '\n' && css[i] != '\r') sel[sn++] = css[i];
            i++;
        }
        sel[sn] = 0;
        int a = 0, b = sn;
        while (a < b && (sel[a]==' '||sel[a]=='\t')) a++;
        while (b > a && (sel[b-1]==' '||sel[b-1]=='\t')) b--;
        memmove(sel, sel + a, (size_t)(b - a)); sel[b - a] = 0;
        if (i >= len) break;
        i++;   // '{'
        int dstart = i;
        while (i < len && css[i] != '}') i++;
        CSSRule r;
        clip_str(r.sel, sizeof(r.sel), sel);
        css_parse_decls(css + dstart, i - dstart, r);
        if (i < len) i++;   // '}'
        if (r.sel[0] && (r.color || r.font_size || r.bold_set || r.none || r.bg || r.align))
            out[n++] = r;
    }
    return n;
}

// stb decodes RGBA; the image cache blits BGRA — swap R and B in place.
static void bgra_swap(uint8_t* p, int npix) {
    for (int i = 0; i < npix; i++) {
        uint8_t t = p[0]; p[0] = p[2]; p[2] = t;
        p += 4;
    }
}

// ===================== HTML tokenizer + tree builder =====================
struct PageParser {
    const char* html;
    int len;
    int pos;
    DomArena dom;
    char tbuf[1024];
    int tlen;
    // worker-side outputs: accumulated <script> text and <style> text
    char* script_buf;
    int* script_len;
    char cssbuf[8192];
    int csslen;
    CSSRule* css_out;
    int* css_n;
    int css_max;

    PageParser(const char* h, int l, char* sbuf = 0, int* slen = 0,
               CSSRule* cso = 0, int* csn = 0, int csmax = 0)
        : html(h), len(l), pos(0), tlen(0), script_buf(sbuf), script_len(slen),
          csslen(0), css_out(cso), css_n(csn), css_max(csmax) {}

    void push_char(char c) {
        if (tlen >= (int)sizeof(tbuf) - 1) flush_text();
        tbuf[tlen++] = c;
    }

    void flush_text() {
        if (tlen == 0) return;
        // trim whitespace at both ends
        int s = 0, e = tlen;
        while (s < e && (tbuf[s]==' ' || tbuf[s]=='\t' || tbuf[s]=='\n' || tbuf[s]=='\r')) s++;
        while (e > s && (tbuf[e-1]==' ' || tbuf[e-1]=='\t' || tbuf[e-1]=='\n' || tbuf[e-1]=='\r')) e--;
        if (e > s) {
            char dec[2048];
            int dl = decode_entities(tbuf + s, e - s, dec, (int)sizeof(dec));
            dom.add_text(dec, dl);
        }
        tlen = 0;
    }

    void skip_until(const char* close_tag) {
        while (pos < len) {
            if (html[pos] == '<' && ci_starts(html + pos + 1, close_tag)) break;
            pos++;
        }
        while (pos < len && html[pos] != '>') pos++;
        if (pos < len) pos++;
    }

    void parse() {
        char tagbuf[24];
        char href[96], src[96], alt[64];
        while (pos < len && !dom.overflow) {
            if (html[pos] != '<') { push_char(html[pos]); pos++; continue; }

            flush_text();

            // comment <!-- ... -->
            if (pos + 3 < len && html[pos+1]=='!' && html[pos+2]=='-' && html[pos+3]=='-') {
                int e = pos + 4;
                while (e + 2 < len && !(html[e]=='-' && html[e+1]=='-' && html[e+2]=='>')) e++;
                pos = (e + 3 < len) ? e + 3 : len;
                continue;
            }
            // doctype / <!...>
            if (pos + 1 < len && html[pos+1] == '!') {
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                continue;
            }

            pos++; // skip '<'
            bool closing = false;
            if (pos < len && html[pos] == '/') { closing = true; pos++; }

            int nl = 0;
            while (pos < len && nl < 23 && html[pos] != ' ' && html[pos] != '>' &&
                   html[pos] != '/' && html[pos] != '\t' && html[pos] != '\n' && html[pos] != '\r') {
                tagbuf[nl++] = lower_char(html[pos++]);
            }
            tagbuf[nl] = 0;
            Tag t = tag_of(tagbuf);

            // <script>: collect raw text (run later through mini_js_run)
            if (!closing && t == Tag::Script) {
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                while (pos < len) {
                    if (html[pos] == '<' && ci_starts(html + pos + 1, "/script")) break;
                    if (script_buf && script_len && *script_len < SCRIPT_MAX - 1)
                        script_buf[(*script_len)++] = html[pos];
                    pos++;
                }
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                continue;
            }
            // <style>: collect raw CSS text for the style sheet
            if (!closing && t == Tag::Style) {
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                while (pos < len) {
                    if (html[pos] == '<' && ci_starts(html + pos + 1, "/style")) break;
                    if (csslen < (int)sizeof(cssbuf) - 1) cssbuf[csslen++] = html[pos];
                    pos++;
                }
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                continue;
            }
            // <title>: handled separately by extract_title(), skip its content
            if (!closing && ci_eq(tagbuf, "title")) {
                skip_until("/title");
                continue;
            }
            // <html>/<head>: no node
            if (!closing && (ci_eq(tagbuf, "html") || ci_eq(tagbuf, "head"))) {
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                continue;
            }
            if (!closing && ci_eq(tagbuf, "body")) t = Tag::Div;

            // attributes
            char cls[48] = {0}, id[32] = {0}, onclick[96] = {0};
            int cw = 300, ch = 150;
            CSSRule inl;
            bool has_inl = false;
            href[0] = src[0] = alt[0] = 0;
            while (pos < len && html[pos] != '>') {
                if (html[pos] == ' ' || html[pos] == '\t' || html[pos] == '\n' ||
                    html[pos] == '\r' || html[pos] == '/') { pos++; continue; }
                char aname[24];
                int an = 0;
                while (pos < len && an < 23 && html[pos] != '=' && html[pos] != '>' &&
                       html[pos] != ' ' && html[pos] != '\t' && html[pos] != '\n' && html[pos] != '\r') {
                    aname[an++] = lower_char(html[pos++]);
                }
                aname[an] = 0;
                while (pos < len && (html[pos]==' ' || html[pos]=='\t' || html[pos]=='\n' || html[pos]=='\r')) pos++;
                char aval[96] = {0};
                int av = 0;
                if (pos < len && html[pos] == '=') {
                    pos++;
                    while (pos < len && (html[pos]==' ' || html[pos]=='\t' || html[pos]=='\n' || html[pos]=='\r')) pos++;
                    if (pos < len && (html[pos]=='"' || html[pos]=='\'')) {
                        char q = html[pos++];
                        while (pos < len && html[pos] != q && av < 95) aval[av++] = html[pos++];
                        if (pos < len) pos++;
                    } else {
                        while (pos < len && av < 95 && html[pos] != ' ' && html[pos] != '>' && html[pos] != '\t') {
                            aval[av++] = html[pos++];
                        }
                    }
                }
                if (ci_eq(aname, "href")) clip_str(href, sizeof(href), aval);
                else if (ci_eq(aname, "src")) clip_str(src, sizeof(src), aval);
                else if (ci_eq(aname, "alt")) clip_str(alt, sizeof(alt), aval);
                else if (ci_eq(aname, "class")) clip_str(cls, sizeof(cls), aval);
                else if (ci_eq(aname, "id")) clip_str(id, sizeof(id), aval);
                else if (ci_eq(aname, "onclick")) clip_str(onclick, sizeof(onclick), aval);
                else if (ci_eq(aname, "width")) cw = atoi(aval);
                else if (ci_eq(aname, "height")) ch = atoi(aval);
                else if (ci_eq(aname, "style")) {
                    inl = CSSRule();
                    css_parse_decls(aval, (int)strlen(aval), inl);
                    has_inl = (inl.color || inl.font_size || inl.bold_set ||
                               inl.none || inl.bg || inl.align);
                    if (!has_inl) inl = CSSRule();
                }
            }
            if (pos < len && html[pos] == '>') pos++;

            if (closing) {
                if (t == Tag::Li) dom.pop_to(Tag::Li);
                else if (t == Tag::P) dom.pop_to(Tag::P);
                else if (t == Tag::Td || t == Tag::Th) dom.pop_to(Tag::Td);
                else if (t == Tag::Tr) dom.pop_to(Tag::Tr);
                else dom.pop_to(t);
                continue;
            }

            // void elements: br/hr/img produce a leaf node, others are dropped
            if (is_void_tag(tagbuf)) {
                if (t == Tag::Br || t == Tag::Hr || t == Tag::Img) {
                    dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick);
                    dom.close_top();
                }
                continue;
            }

            // block-level auto-closing (simplified HTML5 adoption rules)
            if (is_block_tag(t)) {
                if (t == Tag::Li) dom.pop_to(Tag::Li);
                if (t == Tag::P) dom.pop_to(Tag::P);
                if (t == Tag::Td || t == Tag::Th) dom.pop_to(Tag::Td);
                if (t == Tag::Tr) dom.pop_to(Tag::Tr);
                // close stray inline elements above the new block
                while (stack_top_is_inline()) dom.close_top();
            }

            dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick);
            if (t == Tag::Canvas) {
                DomNode& cn = dom.nodes[dom.top()];
                cn.cw = cw; cn.ch = ch;
            }
        }
        flush_text();
        // hand the collected <style> text to the caller as parsed rules
        if (csslen > 0 && css_out && css_n) {
            *css_n = css_parse(cssbuf, csslen, css_out, css_max);
        }
    }

private:
    bool stack_top_is_inline() const {
        if (dom.stack.size() <= 1) return false;
        int idx = dom.stack[dom.stack.size() - 1];
        if (idx < 0 || idx >= dom.nodes.size()) return false;
        return is_inline_tag(dom.nodes[idx].tag);
    }
};

struct ImgJob;   // fetch job; defined with LoadJob below

// fwd decl (defined below): layout calls it while resolving <img> src
static void resolve_url(const char* base, const char* href, char* out, int cap);

// ===================== images =====================
// Per-tab image cache. A slot starts as a fetch job (loading=true); the main
// thread decodes the bytes in poll_img_jobs and caches a downscaled BGRA
// buffer. The page layout is rebuilt once an image finishes so its box can
// grow from the placeholder to the real size.
struct Img {
    char url[256];
    ImgJob* job;        // in-flight fetch (owned by main thread)
    uint8_t* rgba;      // downscaled BGRA pixels (kalloc'd, 0 until done)
    int w, h;           // decoded size
    int disp_w, disp_h; // display size (capped by MAX_IMG_DISP)
    bool used;          // slot in use
    bool loading;       // fetch in flight
    bool done;          // decoded and cached
    bool fail;          // fetch / decode failed (incl. too large)

    Img() : job(0), rgba(0), w(0), h(0), disp_w(0), disp_h(0),
            used(false), loading(false), done(false), fail(false) {
        url[0] = 0;
    }
};

static void start_img_job(Img& im, const char* url);   // defined with the loader

// ===================== layout engine =====================
struct Run {
    String text;
    int x, y, w, h;
    uint32_t color;
    int kind;      // 0 = text, 1 = image, 2 = horizontal rule
    bool link;
    String href;
    int img_id;    // index into TabState::imgs, -1 = none
    int node;      // originating DomNode index, -1 = none (click hit-testing)
    char cid[32];  // canvas element id (kind==5 only)
};

struct PageLayout {
    List<Run> runs;
    int height = 0;
};

// Shift runs [s,e) so each visual line is centered / right-aligned in wrap_w.
static void align_runs(PageLayout* out, int s, int e, int align, int wrap_w) {
    if (wrap_w <= 0) return;
    int i = s;
    while (i < e) {
        int j = i, y = out->runs[i].y;
        int x0 = out->runs[i].x, x1 = out->runs[i].x + out->runs[i].w;
        j++;
        while (j < e && out->runs[j].y == y) {
            if (out->runs[j].x < x0) x0 = out->runs[j].x;
            if (out->runs[j].x + out->runs[j].w > x1) x1 = out->runs[j].x + out->runs[j].w;
            j++;
        }
        int total = x1 - x0;
        int shift = (align == 1) ? (wrap_w - total) / 2 : (wrap_w - total);
        if (shift < 0) shift = 0;
        for (int k = i; k < j; k++) out->runs[k].x += shift;
        i = j;
    }
}

struct Flow {
    PageLayout* out;
    int x, y, w;        // pen position + wrap width
    int indent;         // left indent (px)
    int line_h;         // current line height
    int node_now;       // DomNode index currently emitted (click mapping)
    uint32_t color;
    bool bold;
    bool in_link;
    String href;
    Img* imgs;          // per-tab image cache (0 for built-in pages)
    const char* base_url;
    const CSSRule* css; // per-tab style sheet (0 = none)
    int css_n;

    Flow(PageLayout* o, int width, Img* im = 0, const char* base = 0,
         const CSSRule* c = 0, int cn = 0)
        : out(o), x(0), y(4), w(width), indent(0), line_h(16),
          color(C_TEXT_DK), bold(false), in_link(false), imgs(im), base_url(base),
          css(c), css_n(cn), node_now(-1) {}

    void newline() { x = indent; y += line_h; line_h = 16; }

    void emit(const char* s, int len, int word_w, uint32_t col, bool link, const char* h, int kind) {
        Run r;
        r.x = x; r.y = y; r.w = word_w; r.h = 16;
        r.color = col; r.kind = kind; r.link = link;
        r.img_id = -1; r.node = node_now;
        if (link && h) r.href = h;
        for (int i = 0; i < len; i++) r.text += s[i];
        out->runs.push(r);
        x += word_w;
    }

    void add_word(const char* s, int len, uint32_t col, bool link, const char* h) {
        int word_w = disp_w(s, len);
        if (x + word_w > w - 8) newline();
        emit(s, len, word_w, col, link, h, 0);
    }
};

// word-wrapped text with hard breaks for words wider than the line
static void flow_text(Flow& f, const char* s) {
    int i = 0;
    while (s[i]) {
        int ws = i;
        while (s[ws] && s[ws] != ' ') ws++;
        int wlen = ws - i;
        if (wlen == 0) { i = ws + (s[ws] ? 1 : 0); continue; }
        int word_w = disp_w(s + i, wlen);
        if (word_w > f.w - 8 && wlen > 1) {
            // hard break: emit one UTF-8 character per run (CJK chars
            // are 3 bytes; splitting them would render as '?')
            int j = i;
            while (j < ws) {
                int clen = utf8_char_len(s + j);
                int cw = disp_w(s + j, clen);
                if (f.x + cw > f.w - 8) f.newline();
                f.emit(s + j, clen, cw, f.color, f.in_link, f.href.c_str(), 0);
                j += clen;
            }
        } else {
            f.add_word(s + i, wlen, f.color, f.in_link, f.href.c_str());
        }
        if (s[ws] == ' ') f.x += 8;   // advance past the space
        i = ws + (s[ws] ? 1 : 0);
    }
}

// Box-model + alignment + background carried into the inner layout pass.
struct Eff {
    uint32_t bg; int align;
    int margin, padding, width, height;
    Eff() : bg(0), align(0), margin(0), padding(0), width(-1), height(-1) {}
};

// Apply the per-node effective style (matched rules + inline style) around
// the inner layout pass; the inner pass itself recurses through this wrapper
// so children inherit colors / bold / line height.
static void layout_node_inner(const List<DomNode>& nodes, int idx, Flow& f,
                              int depth, const Eff& e);
static void layout_node(const List<DomNode>& nodes, int idx, Flow& f, int depth) {
    if (depth > 24 || f.out->runs.size() > RUN_MAX) return;
    const DomNode& n = nodes[idx];

    uint32_t col = 0; int fs = 0; bool bset = false, bold = false;
    bool none = false;
    Eff e;
    if (f.css) {
        for (int i = 0; i < f.css_n; i++) {
            const CSSRule& r = f.css[i];
            if (!css_match(r.sel, n.tag, n.cls, n.id)) continue;
            if (r.color) col = r.color;
            if (r.font_size) fs = r.font_size;
            if (r.bold_set) { bold = r.bold; bset = true; }
            if (r.none) none = true;
            if (r.bg) e.bg = r.bg;
            if (r.align) e.align = r.align;
            if (r.margin > 0) e.margin = r.margin;
            if (r.padding > 0) e.padding = r.padding;
            if (r.width > 0) e.width = r.width;
            if (r.height > 0) e.height = r.height;
        }
    }
    if (n.has_inl) {
        if (n.inl.color) col = n.inl.color;
        if (n.inl.font_size) fs = n.inl.font_size;
        if (n.inl.bold_set) { bold = n.inl.bold; bset = true; }
        if (n.inl.none) none = true;
        if (n.inl.bg) e.bg = n.inl.bg;
        if (n.inl.align) e.align = n.inl.align;
        if (n.inl.margin > 0) e.margin = n.inl.margin;
        if (n.inl.padding > 0) e.padding = n.inl.padding;
        if (n.inl.width > 0) e.width = n.inl.width;
        if (n.inl.height > 0) e.height = n.inl.height;
    }
    if (none) return;

    uint32_t old_col = f.color; bool old_bold = f.bold; int old_lh = f.line_h;
    if (col) f.color = col;
    else if (n.tag >= Tag::H1 && n.tag <= Tag::H6) f.color = C_HEAD;
    if (bset) f.bold = bold;
    if (fs) {
        if (fs < 16) fs = 16;
        if (fs > 48) fs = 48;
        f.line_h = fs;
    }
    layout_node_inner(nodes, idx, f, depth, e);
    f.color = old_col; f.bold = old_bold; f.line_h = old_lh;
}

static void layout_node_inner(const List<DomNode>& nodes, int idx, Flow& f,
                              int depth, const Eff& e) {
    if (depth > 24 || f.out->runs.size() > RUN_MAX) return;
    const DomNode& n = nodes[idx];

    switch (n.tag) {
        case Tag::Text:
            f.node_now = idx;
            flow_text(f, n.text.c_str());
            return;
        case Tag::Canvas: {
            f.newline();
            Run r;
            r.x = f.x; r.y = f.y;
            r.w = n.cw; if (r.w < 8) r.w = 8;
            r.h = n.ch; if (r.h < 8) r.h = 8;
            r.color = C_GRAY; r.kind = 5; r.link = false; r.img_id = -1; r.node = idx;
            r.cid[0] = 0;
            clip_str(r.cid, sizeof(r.cid), n.id);
            f.out->runs.push(r);
            f.y += r.h;
            f.newline();
            return;
        }
        case Tag::Br:
            f.newline();
            return;
        case Tag::Hr: {
            f.newline();
            Run r;
            r.x = f.x; r.y = f.y;
            r.w = f.w - f.indent - 8; if (r.w < 8) r.w = 8;
            r.h = 1; r.color = C_HRLINE; r.kind = 2; r.link = false; r.node = idx;
            f.out->runs.push(r);
            f.y += 10;
            f.newline();
            return;
        }
        case Tag::Img: {
            int img_id = -1;
            if (f.imgs && n.src[0]) {
                char u[256];
                resolve_url(f.base_url ? f.base_url : "", n.src.c_str(), u, sizeof(u));
                if (u[0]) {
                    for (int k = 0; k < IMG_MAX; k++) {
                        if (f.imgs[k].used && strcmp(f.imgs[k].url, u) == 0) { img_id = k; break; }
                    }
                    if (img_id < 0) {
                        for (int k = 0; k < IMG_MAX; k++) {
                            if (!f.imgs[k].used) {
                                Img& im = f.imgs[k];
                                im.used = true;
                                im.loading = true;
                                clip_str(im.url, sizeof(im.url), u);
                                start_img_job(im, u);
                                img_id = k;
                                break;
                            }
                        }
                    }
                }
            }
            Run r;
            r.x = f.x; r.y = f.y;
            r.color = C_GRAY; r.kind = 1; r.link = false; r.img_id = img_id; r.node = idx;
            const char* alt = n.alt.c_str();
            if (img_id >= 0 && f.imgs[img_id].done && f.imgs[img_id].rgba) {
                r.w = f.imgs[img_id].disp_w;
                r.h = f.imgs[img_id].disp_h;
            } else {
                r.w = 64; r.h = 48;
            }
            if (img_id >= 0 && f.imgs[img_id].loading) r.text = "[loading...]";
            else if (img_id >= 0 && f.imgs[img_id].fail) r.text = "[image failed]";
            else if (alt && alt[0]) { for (int i = 0; alt[i] && i < 55; i++) r.text += alt[i]; }
            else r.text = "[img]";
            f.out->runs.push(r);
            f.x += r.w + 8;
            if (f.line_h < r.h + 4) f.line_h = r.h + 4;
            return;
        }
        case Tag::H1: case Tag::H2: case Tag::H3:
        case Tag::H4: case Tag::H5: case Tag::H6: {
            f.newline();
            if (e.margin) f.y += e.margin;
            f.y += 8;
            uint32_t old_col = f.color;
            bool old_bold = f.bold;
            f.bold = true;
            f.line_h = 20;
            int saved_w = f.w;
            int bx = f.x, by = f.y;
            int s = f.out->runs.size();
            if (e.padding) f.indent += e.padding;
            if (e.width > 0 && f.x + e.width < f.w) f.w = f.x + e.width;
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            int ex = f.x, ey = f.y;
            if (e.padding) f.indent -= e.padding;
            f.w = saved_w;
            f.color = old_col;
            f.bold = old_bold;
            f.y += 8;
            f.newline();
            if (e.bg && f.out->runs.size() < RUN_MAX) {
                Run b;
                b.x = bx; b.y = by;
                b.w = (e.width > 0 && bx + e.width < f.w) ? e.width : (f.w - bx);
                if (b.w < 8) b.w = 8;
                b.h = (ey + f.line_h) - by; if (b.h < 4) b.h = 4;
                if (e.height > 0 && b.h < e.height) b.h = e.height;
                b.color = e.bg; b.kind = 3; b.link = false; b.img_id = -1; b.node = idx;
                f.out->runs.push(b);
            }
            if (e.align) align_runs(f.out, s, f.out->runs.size(), e.align, f.w);
            return;
        }
        case Tag::P: case Tag::Div: case Tag::Pre: case Tag::Blockquote: case Tag::Form: {
            f.newline();
            if (e.margin) f.y += e.margin;
            if (n.tag == Tag::Pre || n.tag == Tag::Blockquote) f.indent += 16;
            int s = f.out->runs.size();
            int bx = f.x, by = f.y;
            int saved_w = f.w;
            if (e.padding) f.indent += e.padding;
            if (e.width > 0 && f.x + e.width < f.w) f.w = f.x + e.width;
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            int ex = f.x, ey = f.y;
            if (e.padding) f.indent -= e.padding;
            f.w = saved_w;
            if (n.tag == Tag::Pre || n.tag == Tag::Blockquote) f.indent -= 16;
            f.newline();
            if (e.bg && f.out->runs.size() < RUN_MAX) {
                Run b;
                b.x = bx; b.y = by;
                b.w = (e.width > 0 && bx + e.width < f.w) ? e.width : (f.w - bx);
                if (b.w < 8) b.w = 8;
                b.h = (ey + f.line_h) - by; if (b.h < 4) b.h = 4;
                if (e.height > 0 && b.h < e.height) b.h = e.height;
                b.color = e.bg; b.kind = 3; b.link = false; b.img_id = -1; b.node = idx;
                f.out->runs.push(b);
            }
            if (e.align) align_runs(f.out, s, f.out->runs.size(), e.align, f.w);
            return;
        }
        case Tag::Ul: case Tag::Ol: {
            f.newline();
            f.indent += 12;
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.indent -= 12;
            f.newline();
            return;
        }
        case Tag::Li: {
            f.newline();
            if (f.x + 12 <= f.w - 8) f.emit("*", 1, 8, C_GRAY, false, 0, 0);
            f.x += 4;
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.newline();
            return;
        }
        case Tag::Table: {
            f.newline();
            f.indent += 8;
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.indent -= 8;
            f.newline();
            return;
        }
        case Tag::Tr: {
            f.newline();
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.newline();
            return;
        }
        case Tag::Td: case Tag::Th: {
            bool old_bold = f.bold;
            if (n.tag == Tag::Th) f.bold = true;
            f.add_word("|", 1, C_GRAY, false, 0);
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.bold = old_bold;
            return;
        }
        case Tag::A: {
            bool old_link = f.in_link;
            String old_href = f.href;
            uint32_t old_col = f.color;
            f.in_link = true;
            f.href = n.href;
            f.color = C_LINK;
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.in_link = old_link;
            f.href = old_href;
            f.color = old_col;
            return;
        }
        case Tag::B: case Tag::Strong: {
            bool old = f.bold;
            f.bold = true;
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.bold = old;
            return;
        }
        case Tag::I: case Tag::Em:
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            return;
        default:   // Root / Span / Button / Unknown
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            return;
    }
}

static void layout_page(const List<DomNode>& nodes, int width, PageLayout& out,
                        Img* imgs = 0, const char* base_url = 0,
                        const CSSRule* css = 0, int css_n = 0) {
    out.runs.erase_all();
    out.height = 0;
    if (nodes.size() == 0) return;
    Flow f(&out, width, imgs, base_url, css, css_n);
    layout_node(nodes, 0, f, 0);
    out.height = f.y + f.line_h + 4;
}

// ===================== built-in pages =====================
static void build_home(PageLayout& out, int width) {
    out.runs.erase_all();
    out.height = 0;
    int cx = width / 2;
    Flow f(&out, width);

    const char* big = "nefuOS";
    int blen = (int)strlen(big);
    f.x = cx - blen * 8 / 2;
    f.y = 40;
    f.line_h = 20;
    f.emit(big, blen, blen * 8, C_HEAD, false, 0, 0);
    f.newline();

    const char* sub = "Browser";
    int slen = (int)strlen(sub);
    f.x = cx - slen * 8 / 2;
    f.emit(sub, slen, slen * 8, C_TAB_ACT, false, 0, 0);
    f.newline();
    f.y += 14;

    const char* hint = "Type an address or search, then press Enter";
    int hlen = (int)strlen(hint);
    f.x = cx - hlen * 8 / 2;
    f.emit(hint, hlen, hlen * 8, C_GRAY, false, 0, 0);
    f.newline();
    f.y += 12;

    const char* ql = "Quick Links";
    int qlen = (int)strlen(ql);
    f.x = cx - qlen * 8 / 2;
    f.line_h = 18;
    f.emit(ql, qlen, qlen * 8, C_HEAD, false, 0, 0);
    f.newline();
    f.y += 4;

    struct Link { const char* name; const char* url; };
    static const Link links[] = {
        { "GitHub",      "https://github.com" },
        { "Bing Search", "https://www.bing.com" },
        { "DuckDuckGo",  "https://duckduckgo.com" },
        { "Ladybird",    "https://ladybird.org" },
    };
    for (int i = 0; i < 4; i++) {
        int ll = (int)strlen(links[i].name);
        f.x = cx - ll * 8 / 2;
        f.emit(links[i].name, ll, ll * 8, C_LINK, true, links[i].url, 0);
        f.newline();
        f.y += 2;
    }
    out.height = f.y + 20;
}

static void build_error(PageLayout& out, int width, const char* url, const char* msg) {
    out.runs.erase_all();
    out.height = 0;
    Flow f(&out, width);
    f.x = 16;
    f.y = 24;
    f.line_h = 18;
    f.emit("Failed to load page", 20, 20 * 8, C_ERR, false, 0, 0);
    f.newline();
    f.y += 4;
    if (url && url[0]) {
        f.emit(url, (int)strlen(url), disp_w(url, (int)strlen(url)), C_TEXT_DK, false, 0, 0);
        f.newline();
        f.y += 4;
    }
    if (msg && msg[0]) {
        f.emit(msg, (int)strlen(msg), disp_w(msg, (int)strlen(msg)), C_GRAY, false, 0, 0);
        f.newline();
    }
    out.height = f.y + 20;
}

// ===================== navigation & loading =====================
// A LoadJob is owned by the main thread (tab.job). The worker thread only
// fills it and sets done=1; the main thread commits, frees, and deletes it.
// On bare metal platform_thread_create() runs the worker inline, so the job
// is already done when start_job() returns — same code path, no #ifdef.
struct LoadJob {
    volatile int done;   // 0 running, 1 finished
    volatile int phase;  // 0 downloading, 1 parsing
    bool ok;
    bool cancelled;
    bool truncated;
    bool push_hist;
    uint32_t start_ms;
    char url[256];
    char* data;          // kalloc'd page bytes (cap MAX_PAGE)
    uint32_t len;
    DomArena* dom;       // DOM built on the worker thread (0 = parse skipped)
    char title[96];      // <title> extracted on the worker thread
    char script[SCRIPT_MAX];   // accumulated <script> bytes (NUL at end)
    int script_len;
    char js_out[JS_OUT_MAX];   // mini_js_run console output
    CSSRule css[CSS_MAX];      // parsed <style> sheet
    int css_n;
};

// Image fetch job — same ownership pattern as LoadJob: the worker fills it
// and sets done; the main thread decodes, frees, and deletes it.
struct ImgJob {
    volatile int done;   // 0 running, 1 finished
    bool ok;
    bool cancelled;
    char url[256];
    uint8_t* data;       // raw bytes (ownership handed to the main thread)
    uint32_t len;
};

struct BrowserState;   // fwd (defined below, after TabState)
struct TabState;
static void clear_imgs(TabState& tab);

struct TabState {
    char input[160];
    int input_len;
    int input_cursor;
    char url[256];
    char title[96];
    char err[128];
    char* page;          // kalloc'd page buffer
    int page_len;
    int page_cap;
    bool used;           // tab slot is open
    bool loaded;
    bool is_home;
    bool truncated;
    int scroll;
    int max_scroll;
    int layout_w;        // viewport width the cached layout was built for
    PageLayout layout;
    List<DomNode> dom;
    List<String> back;
    List<String> fwd;
    LoadJob* job;
    char queued[256];    // next URL once the current job finishes
    bool has_queued;
    uint32_t load_ms;
    Img imgs[IMG_MAX];   // per-tab image cache
    CSSRule css[CSS_MAX]; // style sheet (fixed array, copied from the job)
    int css_n;
    char js_msg[160];     // last JS console output (shown in the status bar)

    TabState()
        : input_len(0), input_cursor(0), page(0), page_len(0), page_cap(0),
          used(false), loaded(false), is_home(false), truncated(false),
          scroll(0), max_scroll(0), layout_w(-1), job(0), has_queued(false),
          load_ms(0), css_n(0) {
        input[0] = url[0] = title[0] = err[0] = queued[0] = 0;
        js_msg[0] = 0;
    }

    ~TabState() {
        if (page) kfree(page);
        layout.runs.erase_all();
        dom.erase_all();
        back.erase_all();
        fwd.erase_all();
        if (job) {
            job->cancelled = true;
            // bounded wait for the worker so we can free it (host thread);
            // on bare the load is synchronous, so it is already done.
            for (int i = 0; i < 60 && !job->done; i++) platform_thread_sleep(5);
            if (job->done) {
                if (job->data) kfree(job->data);
                if (job->dom) delete job->dom;
                delete job;
            }
        }
        clear_imgs(*this);
    }
};

struct BrowserState {
    TabState tabs[MAX_TABS];
    int active;
    int tab_count;
    bool input_focus;

    BrowserState() : active(0), tab_count(1), input_focus(false) {}
};

static void start_job(TabState& tab, const char* url, bool push_hist);
static void load_worker(void* arg);
static void commit_page(TabState& tab, LoadJob* j);

// smart URL detection: scheme → as-is; domain-like → http:// ; else search
static void smart_url(const char* in, char* out, int cap) {
    out[0] = 0;
    while (*in == ' ') in++;
    if (!in[0]) return;
    if (ci_starts(in, "http://") || ci_starts(in, "https://") || ci_starts(in, "file://")) {
        clip_str(out, cap, in);
        return;
    }
    bool has_dot = false, has_space = false;
    for (const char* p = in; *p; p++) {
        if (*p == '.') has_dot = true;
        else if (*p == ' ') has_space = true;
    }
    if (has_dot && !has_space && strlen(in) > 3) {
        ksprintf(out, cap, "http://%s", in);
    } else {
        char enc[160];
        url_encode(in, enc, sizeof(enc));
        ksprintf(out, cap, "https://www.bing.com/search?q=%s", enc);
    }
}

// resolve a possibly-relative href against the current page URL
static void resolve_url(const char* base, const char* href, char* out, int cap) {
    out[0] = 0;
    if (!href || !href[0] || cap <= 1) return;
    if (ci_starts(href, "http://") || ci_starts(href, "https://") || ci_starts(href, "file://")) {
        clip_str(out, cap, href);
        return;
    }
    if (!base || !base[0]) return;

    char base_s[256];
    clip_str(base_s, sizeof(base_s), base);
    char* q = strchr(base_s, '?'); if (q) *q = 0;
    char* f = strchr(base_s, '#'); if (f) *f = 0;

    // protocol-relative: //host/path
    if (href[0] == '/' && href[1] == '/') {
        int n = 0;
        while (base_s[n] && base_s[n] != ':' && n < cap - 1) { out[n] = base_s[n]; n++; }
        if (base_s[n] == ':' && n < cap - 1) out[n++] = ':';
        int h = 0;
        while (href[h] && n < cap - 1) out[n++] = href[h++];
        out[n] = 0;
        return;
    }
    // absolute path: /path
    if (href[0] == '/') {
        const char* host = base_s;
        if (ci_starts(base_s, "http://")) host = base_s + 7;
        else if (ci_starts(base_s, "https://")) host = base_s + 8;
        const char* slash = 0;
        for (const char* p = host; *p; p++) if (*p == '/') { slash = p; break; }
        int pre = slash ? (int)(slash - base_s) : (int)strlen(base_s);
        int n = 0;
        for (int i = 0; i < pre && n < cap - 1; i++) out[n++] = base_s[i];
        int h = 0;
        while (href[h] && n < cap - 1) out[n++] = href[h++];
        out[n] = 0;
        return;
    }
    // directory-relative: path/foo
    int last = -1;
    for (int i = 0; base_s[i]; i++) if (base_s[i] == '/') last = i;
    int dir = last + 1;
    int n = 0;
    for (int i = 0; i < dir && n < cap - 1; i++) out[n++] = base_s[i];
    int h = 0;
    while (href[h] && n < cap - 1) out[n++] = href[h++];
    out[n] = 0;
}

// extract <title>…</title> from the raw page bytes
static void extract_title(const char* page, int len, char* out, int cap) {
    out[0] = 0;
    int i = 0;
    while (i + 6 < len && !(page[i] == '<' && ci_starts(page + i + 1, "title"))) i++;
    if (i + 6 >= len) return;
    while (i < len && page[i] != '>') i++;
    i++;
    int n = 0;
    bool seen = false;
    for (; i < len && n < cap - 1; i++) {
        if (page[i] == '<') {
            if (ci_starts(page + i + 1, "/title")) break;
            continue;
        }
        char c = page[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!seen) continue;
            if (n > 0 && out[n - 1] == ' ') continue;
            c = ' ';
        } else {
            seen = true;
        }
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
    out[n] = 0;
}

static void start_job(TabState& tab, const char* url, bool push_hist) {
    if (tab.job) {
        // a load is already in flight → queue (the latest URL wins)
        tab.job->cancelled = true;
        clip_str(tab.queued, sizeof(tab.queued), url);
        tab.has_queued = true;
        return;
    }
    LoadJob* j = new LoadJob();
    j->done = 0;
    j->ok = false;
    j->cancelled = false;
    j->truncated = false;
    j->push_hist = push_hist;
    j->start_ms = platform_tick_ms();
    j->data = 0;
    j->len = 0;
    clip_str(j->url, sizeof(j->url), url);
    tab.job = j;
    platform_thread_create(load_worker, j);
}

// ---- image pipeline -------------------------------------------------

static void img_worker(void* arg) {
    ImgJob* j = (ImgJob*)arg;
    uint8_t* body = 0;
    uint32_t body_len = 0;
    bool ok = platform_http_get(j->url, &body, &body_len);
    if (ok && body && body_len > 0 && !j->cancelled) {
        j->data = body;   // hand ownership to the main thread
        j->len = body_len;
        j->ok = true;
    } else {
        if (body) kfree(body);
    }
    j->done = 1;
}

static void start_img_job(Img& im, const char* url) {
    if (im.job) return;   // already in flight
    ImgJob* j = new ImgJob();
    j->done = 0;
    j->ok = false;
    j->cancelled = false;
    j->data = 0;
    j->len = 0;
    clip_str(j->url, sizeof(j->url), url);
    im.job = j;
    platform_thread_create(img_worker, j);
}

// Scan JPEG markers for a SOF0..SOF3 segment and return its dimensions.
// Kept separate so we can reject huge images BEFORE spending a full decode.
static bool jpeg_probe_size(const uint8_t* d, uint32_t len, int* w, int* h) {
    *w = *h = 0;
    if (len < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    uint32_t i = 2;
    while (i + 4 < len) {
        if (d[i] != 0xFF) { i++; continue; }
        uint8_t m = d[i + 1];
        if (m == 0xD9 || m == 0xDA) return false;   // EOI / SOS: no SOF found
        if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) ||
            (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
            if (i + 9 < len) {
                *h = (int)((d[i + 5] << 8) | d[i + 6]);
                *w = (int)((d[i + 7] << 8) | d[i + 8]);
                return true;
            }
            return false;
        }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        if (i + 3 < len) {
            uint16_t seg = (uint16_t)((d[i + 2] << 8) | d[i + 3]);
            i += 2 + seg;
        } else return false;
    }
    return false;
}

// nearest-neighbour downscale (integer math only, bare-metal safe)
static void scale_surface(Surface& dst, const Surface& src) {
    for (int y = 0; y < dst.height; y++) {
        int sy = (y * src.height) / dst.height;
        for (int x = 0; x < dst.width; x++) {
            int sx = (x * src.width) / dst.width;
            const uint8_t* pp = src.addr + (size_t)sy * (size_t)src.pitch + (size_t)sx * 4;
            uint32_t c = (uint32_t)pp[0] | ((uint32_t)pp[1] << 8) | ((uint32_t)pp[2] << 16);
            dst.setpx(x, y, c);
        }
    }
}

// blit a cached image 1:1 into the page surface at (dx,dy)
static void draw_img(Surface& s, int dx, int dy, const Img& im) {
    if (!im.rgba || im.disp_w <= 0 || im.disp_h <= 0) return;
    for (int y = 0; y < im.disp_h; y++) {
        int gy = dy + y;
        if (gy < 0 || gy >= s.height) continue;
        const uint8_t* row = im.rgba + (size_t)y * (size_t)im.disp_w * 4;
        for (int x = 0; x < im.disp_w; x++) {
            int gx = dx + x;
            if (gx < 0 || gx >= s.width) continue;
            const uint8_t* pp = row + (size_t)x * 4;
            uint32_t c = (uint32_t)pp[0] | ((uint32_t)pp[1] << 8) | ((uint32_t)pp[2] << 16);
            s.setpx(gx, gy, c);
        }
    }
}

static void poll_img_jobs(BrowserState* st) {
    for (int t = 0; t < MAX_TABS; t++) {
        TabState& tab = st->tabs[t];
        if (!tab.used) continue;
        for (int k = 0; k < IMG_MAX; k++) {
            Img& im = tab.imgs[k];
            ImgJob* j = im.job;
            if (!j || !j->done) continue;
            im.job = 0;
            im.loading = false;
            if (!j->cancelled && j->ok && j->data && j->len > 0) {
                nefu::ImgFmt fmt = nefu::stbi_sniff(j->data, j->len);
                if (fmt == nefu::IMG_SVG) {
                    // vector: rasterize directly at display size (nanosvg)
                    int w = 0, h = 0;
                    uint8_t* out = 0;
                    if (nefu::nsvg_decode_mem(j->data, j->len, MAX_IMG_DISP,
                                              &w, &h, &out)) {
                        bgra_swap(out, w * h);
                        im.w = w; im.h = h;
                        im.disp_w = w; im.disp_h = h;
                        im.rgba = out;
                        im.done = true;
                    } else {
                        im.fail = true;
                    }
                } else if (fmt != nefu::IMG_UNKNOWN) {
                    int w = 0, h = 0;
                    uint8_t* out = 0;
                    bool dec_ok = false;
                    if (fmt == nefu::IMG_WEBP) {
                        // WebP via libwebp bridge (RGBA)
                        unsigned char* rgba = 0;
                        if (nefu_webp_decode(j->data, (unsigned)j->len, &w, &h, &rgba)) {
                            out = rgba;
                            dec_ok = true;
                        }
                    } else if (nefu::stbi_info_mem(j->data, j->len, &w, &h) &&
                               w > 0 && h > 0 && w * h <= MAX_IMG_PIX) {
                        dec_ok = nefu::stbi_decode_mem(j->data, j->len, &w, &h, &out);
                    }
                    if (dec_ok) {
                            bgra_swap(out, w * h);   // RGBA -> BGRA cache
                            im.w = w; im.h = h;
                            im.disp_w = im.w;
                            if (im.disp_w > MAX_IMG_DISP) im.disp_w = MAX_IMG_DISP;
                            if (im.disp_w < 1) im.disp_w = 1;
                            im.disp_h = (im.h * im.disp_w) / im.w;
                            if (im.disp_h < 1) im.disp_h = 1;
                            uint32_t need = (uint32_t)im.disp_w * (uint32_t)im.disp_h * 4;
                            uint8_t* buf = (uint8_t*)kalloc(need);
                            if (buf) {
                                Surface small;
                                small.addr = buf;
                                small.width = im.disp_w;
                                small.height = im.disp_h;
                                small.pitch = im.disp_w * 4;
                                Surface big;
                                big.addr = out;
                                big.width = w; big.height = h; big.pitch = w * 4;
                                scale_surface(small, big);
                                im.rgba = buf;
                                im.done = true;
                            } else {
                                im.fail = true;
                            }
                            kfree(out);
                        } else {
                            im.fail = true;
                        }
                } else {
                    im.fail = true;
                }
            } else {
                im.fail = true;
            }
            if (j->data) kfree(j->data);
            delete j;
            if (tab.used && (im.done || im.fail)) {
                tab.layout_w = -1;   // image box changed -> re-layout
            }
        }
    }
}

// cancel in-flight jobs and free cached pixels for a tab
static void clear_imgs(TabState& tab) {
    for (int k = 0; k < IMG_MAX; k++) {
        Img& im = tab.imgs[k];
        if (im.job) {
            im.job->cancelled = true;
            for (int i = 0; i < 60 && !im.job->done; i++) platform_thread_sleep(5);
            if (im.job->done) {
                if (im.job->data) kfree(im.job->data);
                delete im.job;
            }
            im.job = 0;
        }
        if (im.rgba) kfree(im.rgba);
        im.job = 0;
        im.rgba = 0;
        im.used = im.loading = im.done = im.fail = false;
        im.url[0] = 0;
    }
}

static void load_worker(void* arg) {
    LoadJob* j = (LoadJob*)arg;
    uint8_t* body = 0;
    uint32_t body_len = 0;
    bool ok = platform_http_get(j->url, &body, &body_len);
    if (ok && body && body_len > 0 && !j->cancelled) {
        j->truncated = body_len > MAX_PAGE - 1;
        uint32_t cap = body_len + 1;
        if (cap > MAX_PAGE) { cap = MAX_PAGE; j->truncated = true; }
        char* p = (char*)kalloc(cap);
        // arena may be tight (bare: 64 MB shared with every other tab and
        // the DOM/layout caches): halve the page until an allocation fits,
        // never fail the whole load just because one big page was too big.
        if (!p) {
            j->truncated = true;
            uint32_t try_cap = cap / 2;
            while (try_cap >= 65536 && !p) {
                p = (char*)kalloc(try_cap);
                if (p) cap = try_cap;
                else try_cap /= 2;
            }
        }
        if (p) {
            memcpy(p, body, cap - 1);
            p[cap - 1] = 0;
            j->data = p;
            j->len = cap - 1;
            j->ok = true;
        }
    }
    if (body) kfree(body);
    // parse the HTML here too, so a 34 MB page never freezes the UI thread:
    // the worker builds the DOM, the main thread only takes ownership.
    if (j->ok && j->data && j->len > 0) {
        j->phase = 1;   // parsing (host: worker thread; bare: same inline path)
        DomArena* ar = new DomArena();
        if (ar) {
            PageParser pp(j->data, j->len, j->script, &j->script_len,
                          j->css, &j->css_n, CSS_MAX);
            pp.parse();
            ar->nodes = pp.dom.nodes;
            j->dom = ar;
        }
        extract_title(j->data, j->len, j->title, sizeof(j->title));
        // execute the accumulated <script> body through the mini JS engine;
        // print/alert/document.write land in js_out (merged into the page)
        if (j->script_len > 0) {
            j->script[j->script_len] = 0;
            nefu::mini_js_run(j->script, j->js_out, (int)sizeof(j->js_out));
        }
    }
    j->done = 1;
}

// Apply one \x01-prefixed DOM command from the JS output stream to the DOM.
// ===================== WebGL canvas (Windows host only) =====================
// WebGL2 maps onto desktop GL 4.x through a hidden window + WGL context; the
// iGPU driver compiles "#version 300 es" shaders directly. All GL entry points
// are loaded at runtime from opengl32.dll (no link dependency). The JS side
// encodes calls as "\x01gl<cmd>|cid|args" control lines with URL percent
// encoding; this decoder executes them on the GL context and keeps the last
// rendered frame for the page blit.
#ifndef NEFU_BARE
struct NfuGl {
    int ready;
    void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glClear)(GLbitfield);
    void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
    void (*glFinish)(void);
    void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    unsigned int (*glCreateShader)(GLenum);
    void (*glShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
    void (*glCompileShader)(GLuint);
    unsigned int (*glCreateProgram)(void);
    void (*glAttachShader)(GLuint, GLuint);
    void (*glLinkProgram)(GLuint);
    void (*glUseProgram)(GLuint);
    void (*glGenBuffers)(GLsizei, GLuint*);
    void (*glBindBuffer)(GLenum, GLuint);
    void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void (*glEnableVertexAttribArray)(GLuint);
    void (*glDrawArrays)(GLenum, GLint, GLsizei);
    unsigned int (*glGetError)(void);
    const unsigned char* (*glGetString)(GLenum);
    void (*glGetShaderiv)(GLuint, GLenum, GLint*);
    void (*glGetProgramiv)(GLuint, GLenum, GLint*);
    void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*);
    void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*);
    void (*glGenVertexArrays)(GLsizei, GLuint*);
    void (*glBindVertexArray)(GLuint);
    HGLRC (*wglCreateContext)(HDC);
    BOOL (*wglMakeCurrent)(HDC, HGLRC);
    BOOL (*wglDeleteContext)(HGLRC);
    PROC (*wglGetProcAddress)(LPCSTR);
};
static NfuGl g_gl;

static void* gl_sym(const char* n) {
    void* p = (void*)g_gl.wglGetProcAddress(n);
    if (!p) p = (void*)GetProcAddress(GetModuleHandleA("opengl32.dll"), n);
    return p;
}

// stage 1: WGL entry points from the opengl32.dll export table (no context)
static void gl_load_wgl() {
    if (g_gl.wglCreateContext) return;
    HMODULE op = LoadLibraryA("opengl32.dll");
    if (!op) return;
    g_gl.wglGetProcAddress = (PROC (*)(LPCSTR))GetProcAddress(op, "wglGetProcAddress");
    g_gl.wglCreateContext = (HGLRC (*)(HDC))GetProcAddress(op, "wglCreateContext");
    g_gl.wglMakeCurrent = (BOOL (*)(HDC, HGLRC))GetProcAddress(op, "wglMakeCurrent");
    g_gl.wglDeleteContext = (BOOL (*)(HGLRC))GetProcAddress(op, "wglDeleteContext");
    if (!g_gl.wglGetProcAddress || !g_gl.wglCreateContext) return;
}

// stage 2: GL entry points; must run with a current context
// (wglGetProcAddress returns NULL otherwise)
static void gl_load_ext() {
    if (g_gl.ready) return;
    #define GLF(n, ...) g_gl.n = (__VA_ARGS__)gl_sym(#n)
    GLF(glClearColor, void (*)(GLfloat, GLfloat, GLfloat, GLfloat));
    GLF(glClear, void (*)(GLbitfield));
    GLF(glViewport, void (*)(GLint, GLint, GLsizei, GLsizei));
    GLF(glFinish, void (*)(void));
    GLF(glReadPixels, void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*));
    GLF(glCreateShader, unsigned int (*)(GLenum));
    GLF(glShaderSource, void (*)(GLuint, GLsizei, const char* const*, const GLint*));
    GLF(glCompileShader, void (*)(GLuint));
    GLF(glCreateProgram, unsigned int (*)(void));
    GLF(glAttachShader, void (*)(GLuint, GLuint));
    GLF(glLinkProgram, void (*)(GLuint));
    GLF(glUseProgram, void (*)(GLuint));
    GLF(glGenBuffers, void (*)(GLsizei, GLuint*));
    GLF(glBindBuffer, void (*)(GLenum, GLuint));
    GLF(glBufferData, void (*)(GLenum, GLsizeiptr, const void*, GLenum));
    GLF(glVertexAttribPointer, void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*));
    GLF(glEnableVertexAttribArray, void (*)(GLuint));
    GLF(glDrawArrays, void (*)(GLenum, GLint, GLsizei));
    GLF(glGetError, unsigned int (*)(void));
    GLF(glGetString, const unsigned char* (*)(GLenum));
    GLF(glGetShaderiv, void (*)(GLuint, GLenum, GLint*));
    GLF(glGetProgramiv, void (*)(GLuint, GLenum, GLint*));
    GLF(glGetShaderInfoLog, void (*)(GLuint, GLsizei, GLsizei*, char*));
    GLF(glGetProgramInfoLog, void (*)(GLuint, GLsizei, GLsizei*, char*));
    GLF(glGenVertexArrays, void (*)(GLsizei, GLuint*));
    GLF(glBindVertexArray, void (*)(GLuint));
    #undef GLF
    g_gl.ready = 1;
}

struct GlShader { unsigned used, sid, id; int type; };   // sid = JS-side id, id = GL object id
struct GlProg { unsigned used, sid, id; };
struct GlBuf { unsigned used, sid, id; int target; };

struct GlCanvas {
    bool used;
    char cid[32];
    int w, h;
    HWND hwnd; HDC hdc; HGLRC rc;
    uint8_t* fb; int fbw, fbh;   // last rendered frame (BGRA, bottom-up)
    float cr, cg, cb, ca;
    GlShader sh[16]; GlProg pr[8]; GlBuf bu[16];
    unsigned cur_prog;
    GLuint vao;
};
static GlCanvas s_glcv[4];

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void url_decode(const char* in, char* out, int cap) {
    int n = 0;
    while (*in && n < cap - 1) {
        if (*in == '%' && in[1] && in[2]) {
            out[n++] = (char)(hexv(in[1]) * 16 + hexv(in[2]));
            in += 3;
        } else out[n++] = *in++;
    }
    out[n] = 0;
}

static int parse_floats(const char* s, float* out, int maxn) {
    int n = 0;
    while (*s && n < maxn) {
        out[n++] = (float)atof(s);
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
    }
    return n;
}

static int parse_ints(const char* s, int* out, int maxn) {
    int n = 0;
    while (*s && n < maxn) {
        out[n++] = atoi(s);
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
    }
    return n;
}

static GlCanvas* gl_find_or_create(const char* cid, const DomNode* nodes, int nn) {
    for (int i = 0; i < 4; i++)
        if (s_glcv[i].used && ci_eq(s_glcv[i].cid, cid)) return &s_glcv[i];
    int w = 300, h = 150;
    for (int i = 0; i < nn; i++)
        if (nodes[i].tag == Tag::Canvas && ci_eq(nodes[i].id, cid)) { w = nodes[i].cw; h = nodes[i].ch; break; }
    for (int i = 0; i < 4; i++) {
        if (s_glcv[i].used) continue;
        GlCanvas& c = s_glcv[i];
        memset(&c, 0, sizeof(c));
        c.used = true;
        clip_str(c.cid, sizeof(c.cid), cid);
        c.w = w; c.h = h;
        c.cr = 0.f; c.cg = 0.f; c.cb = 0.f; c.ca = 1.f;
        c.hwnd = CreateWindowExA(0, "STATIC", "gl", WS_POPUP, 0, 0, w, h, 0, 0, GetModuleHandleA(0), 0);
        if (!c.hwnd) { c.used = false; return 0; }
        c.hdc = GetDC(c.hwnd);
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
        int pf = ChoosePixelFormat(c.hdc, &pfd);
        if (!pf || !SetPixelFormat(c.hdc, pf, &pfd)) { c.used = false; return 0; }
        c.rc = g_gl.wglCreateContext(c.hdc);
        if (!c.rc) { c.used = false; return 0; }
        return &c;
    }
    return 0;
}

static void gl_render_readback(GlCanvas& c) {
    if (!c.fb || c.fbw != c.w || c.fbh != c.h) {
        if (c.fb) kfree(c.fb);
        c.fb = 0;
        c.fbw = c.fbh = 0;
        uint8_t* nb = (uint8_t*)kalloc((size_t)c.w * (size_t)c.h * 4);
        if (!nb) return;
        c.fb = nb; c.fbw = c.w; c.fbh = c.h;
    }
    if (!c.fb) return;
    g_gl.glFinish();
    g_gl.glReadPixels(0, 0, c.w, c.h, GL_BGRA, GL_UNSIGNED_BYTE, c.fb);
}

// runs with tab.dom as the node source (for canvas sizes)
static void gl_cmd(TabState& tab, const char* cmd, const char* cid, const char* val) {
    char dv[4096];
    url_decode(val, dv, sizeof(dv));
    val = dv;
    gl_load_wgl();
    GlCanvas* c = 0;
    for (int i = 0; i < 4; i++)
        if (s_glcv[i].used && ci_eq(s_glcv[i].cid, cid)) { c = &s_glcv[i]; break; }
    if (!c) {
        if (ci_eq(cmd, "ctx")) c = gl_find_or_create(cid, tab.dom.data(), tab.dom.size());
        if (!c) {
            clip_str(tab.js_msg, sizeof(tab.js_msg), "GL: canvas create failed");
            return;
        }
        char dbg[96];
        ksprintf(dbg, sizeof(dbg), "GL: ctx %s %dx%d rc=%p", cid, c->w, c->h, (void*)c->rc);
        clip_str(tab.js_msg, sizeof(tab.js_msg), dbg);
    }
    g_gl.wglMakeCurrent(c->hdc, c->rc);
    if (!g_gl.ready) gl_load_ext();   // needs a current context for wglGetProcAddress
    if (ci_eq(cmd, "glc")) {
        float f[4]; int n = parse_floats(val, f, 4);
        if (n >= 3) { c->cr = f[0]; c->cg = f[1]; c->cb = f[2]; c->ca = n > 3 ? f[3] : 1.f; }
        return;
    }
    if (ci_eq(cmd, "glclr")) {
        g_gl.glClearColor(c->cr, c->cg, c->cb, c->ca);
        g_gl.glClear((GLbitfield)atoi(val));
        gl_render_readback(*c);
        return;
    }
    if (ci_eq(cmd, "v")) {
        int v[4]; int n = parse_ints(val, v, 4);
        if (n >= 4) g_gl.glViewport(v[0], v[1], v[2], v[3]);
        return;
    }
    if (ci_eq(cmd, "sh")) {   // sid,type,src(encoded)
        int sid = atoi(val);
        const char* p = val; while (*p && *p != ',') p++; if (*p) p++;
        int type = atoi(p); while (*p && *p != ',') p++; if (*p) p++;
        char src[2048]; clip_str(src, sizeof(src), p);
        for (int i = 0; i < 16; i++) {
            if (c->sh[i].used && c->sh[i].sid == (unsigned)sid) return;
        }
        unsigned gid = g_gl.glCreateShader((GLenum)type);
        if (!gid) return;
        const char* srcp = src;
        g_gl.glShaderSource(gid, 1, &srcp, 0);
        for (int i = 0; i < 16; i++) {
            if (!c->sh[i].used) {
                c->sh[i].used = 1; c->sh[i].sid = (unsigned)sid; c->sh[i].id = gid; c->sh[i].type = type;
                return;
            }
        }
        return;
    }
    if (ci_eq(cmd, "csh")) {
        unsigned sid = (unsigned)atoi(val);
        for (int i = 0; i < 16; i++)
            if (c->sh[i].used && c->sh[i].sid == sid) {
                g_gl.glCompileShader(c->sh[i].id);
                return;
            }
        return;
    }
    if (ci_eq(cmd, "prog")) {
        unsigned pid = (unsigned)atoi(val);
        for (int i = 0; i < 8; i++) if (c->pr[i].used && c->pr[i].sid == pid) return;
        unsigned gid = g_gl.glCreateProgram();
        for (int i = 0; i < 8; i++) {
            if (!c->pr[i].used) { c->pr[i].used = 1; c->pr[i].sid = pid; c->pr[i].id = gid; return; }
        }
        return;
    }
    if (ci_eq(cmd, "att")) {   // pid,sid (JS ids)
        int v[2]; parse_ints(val, v, 2);
        GlProg* pr = 0; GlShader* sh = 0;
        for (int i = 0; i < 8; i++) if (c->pr[i].used && c->pr[i].sid == (unsigned)v[0]) pr = &c->pr[i];
        for (int i = 0; i < 16; i++) if (c->sh[i].used && c->sh[i].sid == (unsigned)v[1]) sh = &c->sh[i];
        if (pr && sh) g_gl.glAttachShader(pr->id, sh->id);
        return;
    }
    if (ci_eq(cmd, "link")) {
        unsigned pid = (unsigned)atoi(val);
        for (int i = 0; i < 8; i++)
            if (c->pr[i].used && c->pr[i].sid == pid) {
                g_gl.glLinkProgram(c->pr[i].id);
                return;
            }
        return;
    }
    if (ci_eq(cmd, "use")) {
        unsigned pid = (unsigned)atoi(val);
        for (int i = 0; i < 8; i++)
            if (c->pr[i].used && c->pr[i].sid == pid) {
                g_gl.glUseProgram(c->pr[i].id); c->cur_prog = pid; return;
            }
        return;
    }
    if (ci_eq(cmd, "bindbuf")) {   // bid,target (bid is JS id)
        int v[2]; parse_ints(val, v, 2);
        for (int i = 0; i < 16; i++) {
            if (c->bu[i].used && c->bu[i].sid == (unsigned)v[0]) {
                c->bu[i].target = v[1];
                g_gl.glBindBuffer((GLenum)v[1], c->bu[i].id);
                return;
            }
        }
        unsigned gid; g_gl.glGenBuffers(1, &gid);
        for (int i = 0; i < 16; i++) {
            if (!c->bu[i].used) {
                c->bu[i].used = 1; c->bu[i].sid = (unsigned)v[0]; c->bu[i].id = gid; c->bu[i].target = v[1];
                g_gl.glBindBuffer((GLenum)v[1], gid);
                return;
            }
        }
        return;
    }
    if (ci_eq(cmd, "buf")) {   // bid,target,data
        int bid = atoi(val);
        const char* p = val; while (*p && *p != ',') p++; if (*p) p++;
        int target = atoi(p); while (*p && *p != ',') p++; if (*p) p++;
        float f[512];
        int n = parse_floats(p, f, 512);
        for (int i = 0; i < 16; i++) {
            if (c->bu[i].used && c->bu[i].sid == (unsigned)bid) {
                c->bu[i].target = target;
                g_gl.glBindBuffer((GLenum)target, c->bu[i].id);
                g_gl.glBufferData((GLenum)target, (GLsizeiptr)(n * 4), n ? f : 0, GL_STATIC_DRAW);
                return;
            }
        }
        unsigned gid; g_gl.glGenBuffers(1, &gid);
        for (int i = 0; i < 16; i++) {
            if (!c->bu[i].used) {
                c->bu[i].used = 1; c->bu[i].sid = (unsigned)bid; c->bu[i].id = gid; c->bu[i].target = target;
                g_gl.glBindBuffer((GLenum)target, gid);
                g_gl.glBufferData((GLenum)target, (GLsizeiptr)(n * 4), n ? f : 0, GL_STATIC_DRAW);
                return;
            }
        }
        return;
    }
    if (ci_eq(cmd, "attptr")) {   // loc,size,stride,offset
        int v[4]; parse_ints(val, v, 4);
        g_gl.glVertexAttribPointer((GLuint)v[0], v[1], GL_FLOAT, GL_FALSE, v[2], (const void*)(size_t)v[3]);
        g_gl.glEnableVertexAttribArray((GLuint)v[0]);
        return;
    }
    if (ci_eq(cmd, "draw")) {   // mode,count
        int v[2]; parse_ints(val, v, 2);
        g_gl.glViewport(0, 0, c->w, c->h);
        g_gl.glDrawArrays((GLenum)v[0], 0, v[1]);
        gl_render_readback(*c);
        return;
    }
}
#else
static void gl_cmd(TabState& tab, const char* cmd, const char* cid, const char* val) {
    (void)tab; (void)cmd; (void)cid; (void)val;
}
#endif

static void dom_cmd(TabState& tab, const char* cmd, const char* id, const char* val) {
    for (int i = 0; i < tab.dom.size(); i++) {
        DomNode& n = tab.dom[i];
        if (!n.id[0] || !ci_eq(n.id, id)) continue;
        if (ci_eq(cmd, "setText")) {
            n.kids.erase_all();
            DomNode t;
            t.tag = Tag::Text;
            for (const char* s2 = val; *s2; s2++) t.text += *s2;
            n.kids.push(tab.dom.size());
            tab.dom.push(t);
        } else if (ci_eq(cmd, "setColor")) {
            uint32_t c = css_color(val);
            if (c) { n.inl.color = c; n.has_inl = true; }
        } else if (ci_eq(cmd, "hide")) {
            n.inl.none = true; n.has_inl = true;
        } else if (ci_eq(cmd, "show")) {
            n.inl.none = false; n.has_inl = true;
        }
        break;   // first id match wins
    }
}

// Parse the JS console output: \x01cmd|id|val lines mutate the DOM, plain
// text becomes page text. Called after the worker DOM was copied into the tab.
static void apply_js_commands(TabState& tab, const char* js) {
    for (const char* c = js; *c; ) {
        if (*c == '\x01') {
            const char* cmd = c + 1;
            const char* sep1 = 0, *sep2 = 0;
            const char* q = cmd;
            while (*q && *q != '\n') {
                if (*q == '|' && !sep1) sep1 = q;
                else if (*q == '|') { sep2 = q; break; }
                q++;
            }
            if (sep1 && sep2) {
                char cbuf[24]; int cn = 0;
                for (const char* t = cmd; t < sep1 && cn < 23; cn++) cbuf[cn] = *t++;
                cbuf[cn] = 0;
                char ibuf[64]; int in2 = 0;
                for (const char* t = sep1 + 1; t < sep2 && in2 < 63; in2++) ibuf[in2] = *t++;
                ibuf[in2] = 0;
                if (cbuf[0] == 'g' && cbuf[1] == 'l') {
                    // GL commands: the value extends to the end of the line and
                    // may be long (shader sources, vertex data)
                    
                    char vbig[4096]; int vn3 = 0;
                    for (const char* t = sep2 + 1; *t && *t != '\n' && vn3 < 4095; vn3++) vbig[vn3] = *t++;
                    vbig[vn3] = 0;
                    gl_cmd(tab, cbuf + 2, ibuf, vbig);   // skip the "gl" dispatch prefix
                    while (*c && *c != '\n') c++;
                    if (*c) c++;
                    continue;
                }
                char vbuf[80]; int vn2 = 0;
                for (const char* t = sep2 + 1; *t && *t != '\n' && vn2 < 79; vn2++) vbuf[vn2] = *t++;
                vbuf[vn2] = 0;
                dom_cmd(tab, cbuf, ibuf, vbuf);
            }
            while (*c && *c != '\n') c++;
            if (*c) c++;
            continue;
        }
        c++;
    }
}

static void commit_page(TabState& tab, LoadJob* j) {
    clear_imgs(tab);   // previous page's images are gone
    if (j->data && j->len > 0) {
        // zero-copy: the worker buffer becomes the page buffer (j->data is
        // NULLed here so poll_jobs does not free it again)
        if (tab.page) kfree(tab.page);
        tab.page = j->data;
        tab.page_cap = (int)j->len + 1;
        tab.page_len = (int)j->len;
        j->data = 0;
    }
    bool ok_page = tab.page && tab.page_len > 0;

    // history: remember the page we are leaving (only on successful navigation)
    if (j->push_hist) {
        if (ok_page && tab.url[0] && tab.loaded && !tab.is_home && !tab.err[0]) {
            if (tab.back.size() >= HIST_MAX) tab.back.remove(0);
            tab.back.push(tab.url);
        }
        tab.fwd.erase_all();
    }

    clip_str(tab.url, sizeof(tab.url), j->url);
    if (ok_page) {
        tab.loaded = true;
        tab.is_home = false;
        tab.truncated = j->truncated;
        tab.err[0] = 0;
        // the DOM was built by the worker thread; the layout is rebuilt lazily
        // in paint (tab.dom = shallow copy of the node list, same as before)
        tab.dom.erase_all();
        if (j->dom) {
            tab.dom = j->dom->nodes;
            clip_str(tab.title, sizeof(tab.title), j->title);
            tab.css_n = j->css_n;
            for (int k = 0; k < j->css_n && k < CSS_MAX; k++) tab.css[k] = j->css[k];
            if (j->js_out[0]) {
                
                // \x01 DOM commands mutate the committed DOM; plain text from
                // print/alert/document.write becomes a trailing text node.
                apply_js_commands(tab, j->js_out);
                char vis[JS_OUT_MAX];
                int vn = 0;
                for (const char* c = j->js_out; *c && vn < JS_OUT_MAX - 1; ) {
                    if (*c == '\x01') {
                        while (*c && *c != '\n') c++;
                        if (*c) c++;
                        continue;
                    }
                    vis[vn++] = *c++;
                }
                vis[vn] = 0;
                if (vis[0]) {
                    DomNode n;
                    n.tag = Tag::Text;
                    for (int i = 0; i < (int)strlen(vis) && n.text.len() < 4000; i++)
                        n.text += vis[i];
                    tab.dom[0].kids.push(tab.dom.size());
                    tab.dom.push(n);
                    clip_str(tab.js_msg, sizeof(tab.js_msg), vis);
                } else {
                    tab.js_msg[0] = 0;
                }
            } else {
                tab.js_msg[0] = 0;
            }
        }
        if (!tab.title[0]) url_host(j->url, tab.title, sizeof(tab.title));
    } else {
        tab.loaded = true;
        tab.is_home = false;
        tab.truncated = false;
        tab.page_len = 0;
        tab.err[0] = 0;
        ksprintf(tab.err, sizeof(tab.err), "Cannot reach %s", j->url);
        clip_str(tab.title, sizeof(tab.title), "Error");
    }
    clip_str(tab.input, sizeof(tab.input), tab.url);
    tab.input_len = (int)strlen(tab.input);
    tab.input_cursor = tab.input_len;
    tab.layout_w = -1;   // force a re-layout
    tab.scroll = 0;
    tab.load_ms = platform_tick_ms() - j->start_ms;
}

static void poll_jobs(BrowserState* st) {
    poll_img_jobs(st);
    for (int t = 0; t < MAX_TABS; t++) {
        TabState& tab = st->tabs[t];
        LoadJob* j = tab.job;
        if (!j || !j->done) continue;
        tab.job = 0;
        if (tab.used && !j->cancelled && j->ok && j->data) {
            commit_page(tab, j);
        } else if (tab.used && !j->cancelled) {
            // load failed
            tab.loaded = true;
            tab.is_home = false;
            tab.page_len = 0;
            tab.err[0] = 0;
            ksprintf(tab.err, sizeof(tab.err), "Cannot reach %s", j->url);
            clip_str(tab.title, sizeof(tab.title), "Error");
            clip_str(tab.url, sizeof(tab.url), j->url);
            tab.layout_w = -1;
            tab.scroll = 0;
        }
        if (j->data) kfree(j->data);
        if (j->dom) delete j->dom;
        delete j;
        if (tab.used && tab.has_queued) {
            tab.has_queued = false;
            char u[256];
            clip_str(u, sizeof(u), tab.queued);
            start_job(tab, u, true);
        }
    }
}

static void navigate_url(TabState& tab, const char* url, bool push_hist) {
    if (!url || !url[0]) return;
    clip_str(tab.input, sizeof(tab.input), url);
    tab.input_len = (int)strlen(tab.input);
    tab.input_cursor = tab.input_len;
    tab.has_queued = false;
    tab.err[0] = 0;
    start_job(tab, url, push_hist);
}

static void navigate_input(TabState& tab) {
    char u[256];
    smart_url(tab.input, u, sizeof(u));
    if (!u[0]) return;
    navigate_url(tab, u, true);
}

static void go_home(BrowserState* st, TabState& tab) {
    if (tab.job) tab.job->cancelled = true;
    tab.has_queued = false;
    clear_imgs(tab);
    if (tab.page) { kfree(tab.page); tab.page = 0; }
    tab.page_len = 0;
    tab.page_cap = 0;
    tab.loaded = true;
    tab.is_home = true;
    tab.truncated = false;
    tab.err[0] = 0;
    tab.url[0] = 0;
    clip_str(tab.title, sizeof(tab.title), "Home");
    tab.layout_w = -1;
    tab.scroll = 0;
    tab.max_scroll = 0;
    tab.input[0] = 0;
    tab.input_len = 0;
    tab.input_cursor = 0;
    st->input_focus = true;
}

static void reload(TabState& tab) {
    if (tab.is_home) return;
    if (!tab.url[0]) return;
    navigate_url(tab, tab.url, false);
}

static void go_back(TabState& tab) {
    if (tab.back.size() == 0) return;
    String u = tab.back.pop();
    if (tab.url[0] && tab.loaded && !tab.is_home && !tab.err[0]) {
        if (tab.fwd.size() >= HIST_MAX) tab.fwd.remove(0);
        tab.fwd.push(tab.url);
    }
    navigate_url(tab, u.c_str(), false);
}

static void go_fwd(TabState& tab) {
    if (tab.fwd.size() == 0) return;
    String u = tab.fwd.pop();
    if (tab.url[0] && tab.loaded && !tab.is_home && !tab.err[0]) {
        if (tab.back.size() >= HIST_MAX) tab.back.remove(0);
        tab.back.push(tab.url);
    }
    navigate_url(tab, u.c_str(), false);
}

static void new_tab(BrowserState* st) {
    int idx = -1;
    for (int i = 0; i < MAX_TABS; i++) {
        if (!st->tabs[i].used) { idx = i; break; }
    }
    if (idx < 0) return;
    st->tabs[idx].used = true;
    st->active = idx;
    st->tab_count++;
    go_home(st, st->tabs[idx]);
}

static void close_tab(BrowserState* st) {
    if (st->tab_count <= 1) {
        go_home(st, st->tabs[st->active]);
        return;
    }
    TabState& tab = st->tabs[st->active];
    if (tab.job) tab.job->cancelled = true;
    tab.~TabState();               // free page / dom / history (waits for worker)
    new (&tab) TabState();         // fresh empty slot (used == false)
    st->tab_count--;
    if (st->active > 0 && st->tabs[st->active - 1].used) {
        st->active--;
    } else {
        for (int i = 0; i < MAX_TABS; i++) {
            if (st->tabs[i].used) { st->active = i; break; }
        }
    }
    st->input_focus = false;
}

// ===================== geometry (shared by paint & hit-testing) =====================
struct Rect { int x, y, w, h; };

static Rect tab_rect(int i)      { return { 4 + i * 88, 4, 84, TAB_H }; }
static Rect tab_close_rect(int i){ return { 4 + i * 88 + 84 - 16, 4, 14, TAB_H }; }
static Rect plus_rect()          { return { 4 + MAX_TABS * 88 + 6, 4, 26, TAB_H }; }
static Rect back_rect()          { return { 4, 32, 24, NAV_H }; }
static Rect fwd_rect()           { return { 32, 32, 24, NAV_H }; }
static Rect reload_rect()        { return { 60, 32, 24, NAV_H }; }
static Rect home_rect()          { return { 88, 32, 24, NAV_H }; }
static Rect addr_rect(int cw)    { return { 116, 32, maxi(40, cw - 120), NAV_H }; }

// ===================== painting =====================
static void paint_tab(Surface& s, const TabState& tab, int i, int active, int cw) {
    Rect rc = tab_rect(i);
    if (rc.x >= cw) return;
    uint32_t bg = (i == active) ? C_TAB_ACT : C_TAB_IDLE;
    uint32_t fg = (i == active) ? 0xFFFFFF : C_TEXT_LT;
    gfx::fillrect(s, rc.x, rc.y, rc.w, rc.h, bg);

    const char* label = tab.title[0] ? tab.title : (i == 0 ? "Home" : "New Tab");
    int maxc = (rc.w - 22) / 8;
    if (maxc < 1) maxc = 1;
    int len = (int)strlen(label);
    int n = len < maxc ? len : maxc;
    for (int k = 0; k < n; k++) {
        unsigned char ch = (unsigned char)label[k];
        if (ch < 128) gfx::char8x16(s, rc.x + 6 + k * 8, rc.y + 5, (char)ch, fg, bg);
    }
    gfx::char8x16(s, rc.x + rc.w - 14, rc.y + 5, 'x', (i == active) ? 0xFFFFFF : 0x64748B, bg);
}

static void paint_nav_btn(Surface& s, const Rect& rc, char glyph, bool enabled) {
    uint32_t bg = enabled ? C_BTN : C_BTN_DIS;
    uint32_t fg = enabled ? 0xFFFFFF : 0x64748B;
    gfx::fillrect(s, rc.x, rc.y, rc.w, rc.h, bg);
    gfx::char8x16(s, rc.x + 8, rc.y + 4, glyph, fg, bg);
}

static void paint_address(Surface& s, const TabState& tab, bool focus, int cw) {
    Rect rc = addr_rect(cw);
    gfx::fillrect(s, rc.x, rc.y, rc.w, rc.h, C_ADDR_BG);
    gfx::rect(s, rc.x, rc.y, rc.w, rc.h, focus ? C_TAB_ACT : C_ADDR_BD);

    int text_x = rc.x + 6;
    int avail = rc.w - 12;
    int total_w = tab.input_len * 8;
    int disp = (total_w > avail) ? total_w - avail : 0;

    if (tab.input_len > 0) {
        for (int k = 0; k < tab.input_len; k++) {
            int px = text_x + k * 8 - disp;
            if (px < rc.x + 4 || px + 8 > rc.x + rc.w - 4) continue;
            unsigned char ch = (unsigned char)tab.input[k];
            if (ch < 128) gfx::char8x16(s, px, rc.y + 4, (char)ch, C_TEXT_DK, C_ADDR_BG);
        }
    } else {
        // placeholder (dimmed while focused) so the bar is never blank
        gfx::text(s, text_x, rc.y + 4, "Search or enter address",
                  focus ? 0xCBD5E1 : C_ADDR_BD, C_ADDR_BG);
    }
    if (focus) {
        int cx = text_x + tab.input_cursor * 8 - disp;
        if (cx >= rc.x + 4 && cx < rc.x + rc.w - 4)
            gfx::char8x16(s, cx, rc.y + 4, '|', C_TAB_ACT, C_ADDR_BG);
    }
}

static void paint_page(Surface& s, const PageLayout& lay, int scroll,
                       int vx, int vy, int vw, int vh, const Img* imgs = 0) {
    // pass 1: block backgrounds (kind 3) underneath everything else
    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        if (r.kind != 3) continue;
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;
        gfx::fillrect(s, vx + r.x, vy + sy, r.w, r.h, r.color);
    }
    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;   // vertical culling

        if (r.kind == 2) {   // horizontal rule
            int x0 = vx + r.x, x1 = vx + r.x + r.w;
            if (x0 < vx) x0 = vx;
            if (x1 > vx + vw) x1 = vx + vw;
            if (x1 > x0) gfx::hline(s, x0, x1 - 1, vy + sy, r.color);
            continue;
        }
        if (r.kind == 1) {   // image
            if (r.img_id >= 0 && imgs && imgs[r.img_id].done && imgs[r.img_id].rgba) {
                draw_img(s, vx + r.x, vy + sy, imgs[r.img_id]);
                gfx::rect(s, vx + r.x, vy + sy, r.w, r.h, C_HRLINE);
            } else {
                gfx::fillrect(s, vx + r.x, vy + sy, r.w, r.h, C_IMG);
                gfx::rect(s, vx + r.x, vy + sy, r.w, r.h, C_HRLINE);
                int maxc = (r.w - 4) / 8;
                if (maxc > r.text.len()) maxc = r.text.len();
                for (int c = 0; c < maxc; c++) {
                    unsigned char ch = (unsigned char)r.text[c];
                    if (ch < 128) gfx::char8x16(s, vx + r.x + 4 + c * 8, vy + sy + r.h / 2 - 6, (char)ch, C_GRAY, C_IMG);
                }
            }
            continue;
        }
        if (r.kind == 5) {   // canvas (WebGL frame or placeholder)
            bool cv_matched = false;
#ifndef NEFU_BARE
            if (r.cid[0]) {
                const char* cid = r.cid;
                for (int i = 0; i < 4; i++) {
                    const GlCanvas& gc = s_glcv[i];
                    
                    if (!gc.used || !gc.fb || !ci_eq(gc.cid, cid)) continue;
                    int bw = gc.w < r.w ? gc.w : r.w;
                    int bh = gc.h < r.h ? gc.h : r.h;
                    for (int yy = 0; yy < bh; yy++) {
                        int py = vy + sy + yy;
                        if (py < 0 || py >= s.height) continue;
                        const uint8_t* row = gc.fb + (size_t)(gc.h - 1 - yy) * (size_t)gc.w * 4;
                        for (int xx = 0; xx < bw; xx++) {
                            int px = vx + r.x + xx;
                            if (px < 0 || px >= s.width) continue;
                            const uint8_t* pp = row + (size_t)xx * 4;
                            uint32_t cc = (uint32_t)pp[0] | ((uint32_t)pp[1] << 8) | ((uint32_t)pp[2] << 16);
                            s.setpx(px, py, cc);
                        }
                    }
                    cv_matched = true;
                    break;
                }
            }
#endif
            if (!cv_matched) {
                gfx::fillrect(s, vx + r.x, vy + sy, r.w, r.h, C_IMG);
                gfx::rect(s, vx + r.x, vy + sy, r.w, r.h, C_HRLINE);
            }
            continue;
        }
        // text run (UTF-8 aware via gfx::text; glyphs are clipped by gfx)
        if (r.text.len() > 0) {
            gfx::text(s, vx + r.x, vy + sy, r.text.c_str(), r.color, C_PAGE_BG);
            if (r.link) {
                int x0 = vx + r.x, x1 = vx + r.x + r.w - 1;
                if (x0 < vx) x0 = vx;
                if (x1 > vx + vw - 1) x1 = vx + vw - 1;
                if (x1 > x0) gfx::hline(s, x0, x1, vy + sy + 15, r.color);
            }
        }
    }
}

static void paint_scrollbar(Surface& s, int sbx, int vy, int vh, int scroll, int content_h) {
    gfx::fillrect(s, sbx, vy, SB_W, vh, C_SB_TRACK);
    if (content_h <= vh) return;
    int thumb_h = vh * vh / content_h;
    if (thumb_h < 10) thumb_h = 10;
    if (thumb_h > vh) thumb_h = vh;
    int max_scroll = content_h - vh;
    int thumb_y = (vh - thumb_h) * scroll / max_scroll;
    gfx::fillrect(s, sbx + 2, vy + thumb_y, SB_W - 4, thumb_h, C_SB_THUMB);
}

static void paint_status(Surface& s, const TabState& tab, int cw, int ch) {
    int sy = ch - STAT_H;
    gfx::fillrect(s, 0, sy, cw, STAT_H, C_STATUS);

    char left[320];
    if (tab.js_msg[0]) {
        ksprintf(left, sizeof(left), "JS: %s", tab.js_msg);
    } else if (tab.job) {
        char u[128];
        url_host(tab.url, u, sizeof(u));
        if (!u[0]) clip_str(u, sizeof(u), tab.url);
        if (tab.job->phase == 1)
            ksprintf(left, sizeof(left), "Parsing %s (%d KB) ...", u[0] ? u : "...",
                     (int)(tab.job->len / 1024));
        else
            ksprintf(left, sizeof(left), "Loading %s ...", u[0] ? u : "...");
    } else if (tab.err[0]) {
        clip_str(left, sizeof(left), tab.err);
    } else if (tab.is_home || !tab.loaded) {
        ksprintf(left, sizeof(left), "Ready");
    } else if (tab.url[0]) {
        clip_str(left, sizeof(left), tab.url);
    } else {
        ksprintf(left, sizeof(left), "Ready");
    }
    int lw = (int)strlen(left);
    int maxl = (cw - 140) / 8;
    if (maxl < 4) maxl = 4;
    for (int k = 0; k < lw && k < maxl; k++) {
        unsigned char c = (unsigned char)left[k];
        if (c < 128) gfx::char8x16(s, 6 + k * 8, sy + 4, (char)c, C_TEXT_LT, C_STATUS);
    }

    char right[96];
    if (tab.loaded && tab.page_len > 0 && !tab.is_home) {
        int kb = (tab.page_len + 1023) / 1024;
        ksprintf(right, sizeof(right), "%d KB | %d ms", kb, (int)tab.load_ms);
    } else if (tab.err[0]) {
        ksprintf(right, sizeof(right), "Error");
    } else {
        ksprintf(right, sizeof(right), "nefuOS v3.0");
    }
    int rlen = (int)strlen(right);
    int rx = cw - 6 - rlen * 8;
    for (int k = 0; k < rlen; k++) {
        unsigned char c = (unsigned char)right[k];
        if (c < 128) gfx::char8x16(s, rx + k * 8, sy + 4, (char)c, C_TAB_ACT, C_STATUS);
    }
}

static void on_paint(Window* w) {
    Surface* s = &w->back;
    BrowserState* st = (BrowserState*)w->userdata;
    if (!st) return;

    poll_jobs(st);   // commit finished loads (also starts queued ones)

    TabState& tab = st->tabs[st->active];
    int cw = w->content_w;
    int ch = w->content_h;

    // ---- chrome ----
    gfx::fillrect(*s, 0, 0, cw, BAR_H, C_TOOLBAR);

    for (int i = 0; i < MAX_TABS; i++) {
        if (st->tabs[i].used) paint_tab(*s, st->tabs[i], i, st->active, cw);
    }
    if (st->tab_count < MAX_TABS) {
        Rect pr = plus_rect();
        if (pr.x < cw) {
            gfx::fillrect(*s, pr.x, pr.y, pr.w, pr.h, C_BTN);
            gfx::char8x16(*s, pr.x + 9, pr.y + 5, '+', 0xFFFFFF, C_BTN);
        }
    }

    paint_nav_btn(*s, back_rect(), '<', tab.back.size() > 0);
    paint_nav_btn(*s, fwd_rect(), '>', tab.fwd.size() > 0);
    paint_nav_btn(*s, reload_rect(), 'R', true);
    paint_nav_btn(*s, home_rect(), 'H', true);
    paint_address(*s, tab, st->input_focus, cw);

    // ---- content ----
    int vx = 2, vy = BAR_H + 2;
    int vw = cw - SB_W - 4;
    int vh = ch - STAT_H - vy - 2;
    if (vw < 10) vw = 10;
    if (vh < 10) vh = 10;
    gfx::fillrect(*s, 0, BAR_H, cw, ch - STAT_H - BAR_H, C_PAGE_BG);

    // build / rebuild the layout lazily (once per navigation or resize)
    if (tab.layout_w != vw) {
        tab.layout.runs.erase_all();
        if (!tab.loaded || tab.is_home) {
            build_home(tab.layout, vw);
        } else if (tab.err[0] && !tab.page) {
            build_error(tab.layout, vw, tab.url, tab.err);
        } else if (tab.page && tab.page_len > 0) {
            layout_page(tab.dom, vw, tab.layout, tab.imgs, tab.url,
                        tab.css_n > 0 ? tab.css : 0, tab.css_n);
            if (tab.truncated) {
                Run tr;
                tr.x = 8;
                tr.y = tab.layout.height + 4;
                tr.w = 200;
                tr.h = 16;
                tr.color = C_GRAY;
                tr.kind = 0;
                tr.link = false;
                tr.img_id = -1;
                char m[64];
                ksprintf(m, sizeof(m), "[page truncated at %d MB]",
                         (tab.page_cap + 524288) / 1048576);
                tr.text = m;
                tab.layout.runs.push(tr);
                tab.layout.height += 24;
            }
        }
        tab.layout_w = vw;
    }

    tab.max_scroll = maxi(0, tab.layout.height - vh);
    if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll;

    paint_page(*s, tab.layout, tab.scroll, vx, vy, vw, vh, tab.imgs);
    paint_scrollbar(*s, cw - SB_W, vy, vh, tab.scroll, tab.layout.height);

    // ---- status ----
    paint_status(*s, tab, cw, ch);
}

// ===================== input handlers =====================
static void on_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    BrowserState* st = (BrowserState*)w->userdata;
    if (!st) return;
    TabState& tab = st->tabs[st->active];

    if (st->input_focus) {
        // ---- address bar editing ----
        if (e->keycode == KEY_ENTER) {
            st->input_focus = false;
            navigate_input(tab);
            return;
        }
        if (e->keycode == KEY_BACKSPACE) {
            if (tab.input_cursor > 0) {
                memmove(tab.input + tab.input_cursor - 1, tab.input + tab.input_cursor,
                        (size_t)(tab.input_len - tab.input_cursor) + 1);
                tab.input_len--;
                tab.input_cursor--;
            }
            return;
        }
        if (e->keycode == KEY_DEL) {
            if (tab.input_cursor < tab.input_len) {
                memmove(tab.input + tab.input_cursor, tab.input + tab.input_cursor + 1,
                        (size_t)(tab.input_len - tab.input_cursor));
                tab.input_len--;
            }
            return;
        }
        if (e->keycode == KEY_LEFT) { if (tab.input_cursor > 0) tab.input_cursor--; return; }
        if (e->keycode == KEY_RIGHT) { if (tab.input_cursor < tab.input_len) tab.input_cursor++; return; }
        if (e->keycode == KEY_HOME) { tab.input_cursor = 0; return; }
        if (e->keycode == KEY_END) { tab.input_cursor = tab.input_len; return; }
        if (e->keycode == KEY_TAB || e->keycode == KEY_UP || e->keycode == KEY_DOWN) {
            st->input_focus = false;
            return;
        }
        if ((unsigned char)e->ascii >= 32 && (unsigned char)e->ascii < 127) {
            if (tab.input_len < (int)sizeof(tab.input) - 1) {
                memmove(tab.input + tab.input_cursor + 1, tab.input + tab.input_cursor,
                        (size_t)(tab.input_len - tab.input_cursor) + 1);
                tab.input[tab.input_cursor] = e->ascii;
                tab.input_len++;
                tab.input_cursor++;
            }
            return;
        }
        return;
    }

    // ---- page shortcuts ----
    int vh = w->content_h - STAT_H - BAR_H - 2;
    if (e->keycode == KEY_UP) { tab.scroll -= 24; if (tab.scroll < 0) tab.scroll = 0; return; }
    if (e->keycode == KEY_DOWN) { tab.scroll += 24; if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll; return; }
    if (e->keycode == KEY_PGUP) { tab.scroll -= vh; if (tab.scroll < 0) tab.scroll = 0; return; }
    if (e->keycode == KEY_PGDN) { tab.scroll += vh; if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll; return; }
    if (e->keycode == KEY_HOME) { tab.scroll = 0; return; }
    if (e->keycode == KEY_END) { tab.scroll = tab.max_scroll; return; }
    if (e->keycode == KEY_F5) { reload(tab); return; }

    // platform convention: Ctrl+letter arrives as the plain letter
    if (e->ascii == 'l') { st->input_focus = true; tab.input_cursor = tab.input_len; return; }
    if (e->ascii == 't') { new_tab(st); return; }
    if (e->ascii == 'w') { close_tab(st); return; }
    if (e->ascii == 'r') { reload(tab); return; }
    if (e->ascii == 'h') { go_home(st, tab); return; }
}

static void on_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    BrowserState* st = (BrowserState*)w->userdata;
    if (!st) return;
    TabState& tab = st->tabs[st->active];
    int cw = w->content_w;

    // ---- tab strip ----
    if (my < 30) {
        for (int i = 0; i < MAX_TABS; i++) {
            if (!st->tabs[i].used) continue;
            Rect rc = tab_rect(i);
            if (rc.x >= cw) break;
            Rect cr = tab_close_rect(i);
            if (mx >= cr.x && mx < cr.x + cr.w && my >= rc.y && my < rc.y + rc.h) {
                st->active = i;
                close_tab(st);
                return;
            }
            if (mx >= rc.x && mx < rc.x + rc.w) {
                st->active = i;
                st->input_focus = false;
                return;
            }
        }
        Rect pr = plus_rect();
        if (mx >= pr.x && mx < pr.x + pr.w && my >= pr.y && my < pr.y + pr.h) {
            new_tab(st);
            return;
        }
        return;
    }

    // ---- navigation row ----
    if (my >= 32 && my < 54) {
        Rect b = back_rect();
        if (mx >= b.x && mx < b.x + b.w) { go_back(tab); return; }
        Rect f = fwd_rect();
        if (mx >= f.x && mx < f.x + f.w) { go_fwd(tab); return; }
        Rect r = reload_rect();
        if (mx >= r.x && mx < r.x + r.w) { reload(tab); return; }
        Rect h = home_rect();
        if (mx >= h.x && mx < h.x + h.w) { go_home(st, tab); return; }
        Rect a = addr_rect(cw);
        if (mx >= a.x && mx < a.x + a.w) {
            st->input_focus = true;
            int idx = (mx - (a.x + 6)) / 8;
            if (idx < 0) idx = 0;
            if (idx > tab.input_len) idx = tab.input_len;
            tab.input_cursor = idx;
            return;
        }
        return;
    }

    // ---- content ----
    int vx = 2, vy = BAR_H + 2;
    int vw = cw - SB_W - 4;
    int vh = w->content_h - STAT_H - vy - 2;
    if (my < vy || my >= vy + vh) return;

    // scrollbar
    if (mx >= vx + vw && mx < cw) {
        if (tab.max_scroll > 0) {
            int content_h = vh + tab.max_scroll;
            int thumb_h = vh * vh / content_h;
            if (thumb_h < 10) thumb_h = 10;
            if (thumb_h > vh) thumb_h = vh;
            int thumb_y = (vh - thumb_h) * tab.scroll / tab.max_scroll;
            if (my < vy + thumb_y) tab.scroll -= vh / 2;
            else tab.scroll += vh / 2;
            if (tab.scroll < 0) tab.scroll = 0;
            if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll;
        }
        return;
    }

    st->input_focus = false;

    // run an inline onclick handler: mini JS executes, DOM commands from the
    // output stream mutate the DOM, then the layout is invalidated
    auto exec_onclick = [&](const char* script) {
        if (!script || !script[0]) return;
        char out[JS_OUT_MAX];
        out[0] = 0;
        mini_js_run(script, out, (int)sizeof(out));
        if (out[0]) {
            apply_js_commands(tab, out);
            char vis[JS_OUT_MAX];
            int vn = 0;
            for (const char* c = out; *c && vn < JS_OUT_MAX - 1; ) {
                if (*c == '\x01') { while (*c && *c != '\n') c++; if (*c) c++; continue; }
                vis[vn++] = *c++;
            }
            vis[vn] = 0;
            if (vis[0]) clip_str(tab.js_msg, sizeof(tab.js_msg), vis);
        }
        tab.layout_w = -1;   // re-layout on next paint
    };

    // hit-testing (reverse order: topmost run wins); onclick wins over href
    int px = mx - vx;
    int py = my - vy + tab.scroll;
    for (int i = tab.layout.runs.size() - 1; i >= 0; i--) {
        const Run& r = tab.layout.runs[i];
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
            if (r.node >= 0 && r.node < tab.dom.size()) {
                const char* oc = tab.dom[r.node].onclick;
                if (oc[0]) { exec_onclick(oc); return; }
            }
            if (!r.link) continue;
            // ignore pseudo-protocols and in-page anchors (no anchor scrolling
            // yet); navigating to javascript:/mailto:/tel:/data: would either
            // be a wasted request or a security smell
            if (r.href[0] == '#' ||
                ci_starts(r.href.c_str(), "javascript:") ||
                ci_starts(r.href.c_str(), "mailto:") ||
                ci_starts(r.href.c_str(), "tel:") ||
                ci_starts(r.href.c_str(), "data:") ||
                ci_starts(r.href.c_str(), "about:")) return;
            char u[256];
            resolve_url(tab.url, r.href.c_str(), u, sizeof(u));
            if (u[0]) navigate_url(tab, u, true);
            return;
        }
    }
}

static void on_scroll(Window* w, int delta) {
    BrowserState* st = (BrowserState*)w->userdata;
    if (!st) return;
    TabState& tab = st->tabs[st->active];
    tab.scroll += delta * 25;
    if (tab.scroll < 0) tab.scroll = 0;
    if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll;
    // keep the chrome fixed: the WM auto-scrolls w->scroll_y, reset it
    w->scroll_y = 0;
}

static void on_close(Window* w) {
    BrowserState* st = (BrowserState*)w->userdata;
    if (st) {
        delete st;   // TabState destructors free pages / DOM / history / jobs
        w->userdata = 0;
    }
}

} // namespace

// ===================== entry points =====================


void browser_launch_url(const char* url) {
    BrowserState* st = new BrowserState();
    st->tab_count = 1;
    st->tabs[0].used = true;
    st->input_focus = true;
    go_home(st, st->tabs[0]);

    if (url && *url) {
        char u[256];
        smart_url(url, u, sizeof(u));
        if (u[0]) navigate_url(st->tabs[0], u, false);
    }

    Window* w = g_wm->create_window("nefuOS Browser", 20, 20, 760, 560);
    if (!w) { delete st; return; }

    w->userdata = st;
    w->on_paint = on_paint;
    w->on_key = on_key;
    w->on_mouse = on_mouse;
    w->on_scroll = on_scroll;
    w->on_close = on_close;

    g_wm->raise(w);
}

void browser_launch() {
    browser_launch_url(0);
}

} // namespace nefu
