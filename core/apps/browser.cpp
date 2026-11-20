/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
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
#ifdef NEFU_BARE
#include "swgl.h"          // nefu::swgl_* — software GLES 2.0 (WebGL1) for the bare kernel
#endif
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"
#include "php2html.h"   // nfuphp::php_convert — PHP template mini-transpiler
#include "../lib/json.h" // nefu::json — JSON responses render as a tree, not HTML
#include "../cryptlib/b64.h" // nefu::crypt::base64_decode — data: URI payloads
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace nefu {

// Test-harness hook state: --nfuclick N dispatches a DOM click on the element
// with data-nfu="N" in the active tab (independent of viewport geometry).
static int s_debug_nfu_click = -1;

// Viewport size of the last painted page (content area, without the
// browser chrome). A WebGL canvas is rasterized at this size: a game canvas
// is styled to fill the window and carries no width/height attributes, so
// the DOM would otherwise report the 300x150 default.
static int s_view_w = 0, s_view_h = 0;
// Header height probe (NF_CARD_DBG): the pen position when .HomeHeader was
// entered. bloxd's header leaves a ~2500px gap before its next sibling while
// every visible child of it sits on the first line, so both the entry and the
// exit pen position have to be measured, not guessed.
static int s_hdr_y = -1;
static int s_flexfin_once = 0;
static int s_kidio_once = 0;
static int s_hexit_once = 0;
// Pending page actions. Kept as text rather than a DOM index because an index
// only survives one particular render, while a label survives a re-mount.
//
// A page often needs several actions in a row - bloxd.io stacks a floating
// dialog ("Turn on graphics acceleration") over a privacy consent dialog
// ("Accept Privacy Policy"), and only then offers the "Play" button. The
// actions are kept in a list and performed one after another.
//
// Crucially, an action is retried until it actually hits: the consent dialog
// only renders once the dialog in front of it was dismissed, so a one-shot
// attempt would skip it forever.
//
// Action syntax (one per entry):
//   "key:<Name>"    press a key          ("key:Escape")
//   "text:<Label>"  click visible text   ("text:Play")
//   "class:<Frag>"  click by class name  ("class:PromptGenericPopupBodyButton")
#define ACT_MAX 8
static char s_actions[ACT_MAX][64];
static int  s_action_count = 0;
static int  s_action_next = 0;
static uint32_t s_action_at = 0;
static int  s_action_tries = 0;
// Scripted typing (terminal "type:<text>" / browser_send_text): characters are
// fed one at a time through the normal key path once a field has focus.
static char s_type_buf[160];
static int  s_type_len = 0;
static int  s_type_enter = 0;      // press Enter once the buffer is empty
static uint32_t s_type_at = 0;

namespace {

// ===================== layout constants =====================
const int TAB_H = 24;          // tab strip height
const int NAV_H = 22;          // navigation row height
const int BAR_H = 58;          // total chrome height (tabs + nav)
const int STAT_H = 24;         // status bar height
const int SB_W  = 10;          // scrollbar width
const int MAX_TABS  = 8;       // maximum open tabs
const int MAX_PAGE  = 1024 * 1024 * 1024;  // page buffer cap (1 GB per tab; 2 GB
                                           // would overflow int and go negative,
                                           // silently disabling stream growth)
const int DOM_MAX   = 30000;    // DOM node arena cap (pages beyond this are
                               // truncated: overflow is logged, rest of page
                               // keeps rendering)
const int DOM_DEPTH = 256;     // DOM nesting depth cap (React trees nest deep)
const int HIST_MAX  = 24;      // per-tab history entries
const int RUN_MAX   = 32768;   // layout run cap

// ===================== image support =====================
// host raises the caps: news-portal hero images are 400-700 px wide and
// squashing everything to 200 px was the single biggest "doesn't look like
// Edge" gap. bare keeps the small arena budget (8 tabs x IMG_MAX images).
#ifdef NEFU_BARE
const int IMG_MAX      = 12;    // cached images per tab
const int MAX_IMG_PIX  = 262144; // decoded pixel cap per image (e.g. 512x512)
#else
const int IMG_MAX      = 16;    // cached images per tab
const int MAX_IMG_PIX  = 1048576; // decoded pixel cap per image (1024x1024)
#endif
const int MAX_IMG_DISP = 640;   // max display width (px); height scales
const int SVG_MARKUP_MAX = 24576; // inline <svg> markup kept verbatim (bytes)
const int SVG_MAX_DISP   = 256;   // inline svg box cap (px)
const int MAX_BG_DISP  = 1280;  // background-image decode cap: cover needs >=
                                // the viewport, never the raw file resolution

// ===================== CSS subset + JS =====================
// Deepest z value the painter sorts by. Out-of-flow (position:fixed)
// overlays get z = 5000 + 10*z_index + DOM depth, so the cap must sit
// above every boosted layer or the overlays silently never paint.
const int Z_PAINT_MAX = 12000;
#ifdef NEFU_BARE
    // bare: TabState tabs[8] live in the kernel BSS; 4096 rules/tab would
    // push the image past the stub staging base 0xB00000 (build fails the
    // memmap check). 2048 still fits bloxd's whole sheet.
    const int CSS_MAX     = 2048;
#else
    const int CSS_MAX     = 8192;    // style-sheet rules per tab (bloxd's 310KB
                                 // sheet alone fills 500+; leetcode.cn ships a
                                 // 2025-rule sheet and jd.com two sheets that
                                 // sum past 2100 — 2048 dropped their second
                                 // <link> entirely, blanking the page. 4096
                                 // rules cost ~1.4 MB per tab in CSSRule
                                 // storage; TabState/LoadJob are heap-allocated.
                                 // baidu's home page alone carries ~250 KB of
                                 // style text, so 4096 clipped its tail.)
#endif
// Stylesheet text captured from <style> bodies before it is parsed. This used
// to be a 16 KB stack array, which silently dropped everything past the first
// block — baidu ships 250 KB in ten blocks, so its whole layout sheet was
// thrown away and the page rendered as bare text.
#ifdef NEFU_BARE
static const int CSS_TEXT_CAP = 320 * 1024;
#else
static const int CSS_TEXT_CAP = 2048 * 1024;
#endif
const int SCRIPT_MAX  = 524288;  // accumulated <script> bytes per page (bloxd
                                 // head inline scripts concat to ~275KB)
const int JS_OUT_MAX  = 8192;    // JS console output buffer. A page can print a
                                  // lot in one turn (bloxd's own banner is
                                  // ~700 bytes); a small buffer silently cut
                                  // off everything that came after it.

// One parsed CSS rule: a selector + the properties this engine understands.
// 0 / false / -1 means "not set" so later rules can override earlier ones.
static const int kPosNone = -999999;   // "offset not set" sentinel (see CSSRule)

struct CSSRule {
    char sel[128];   // framework sheets carry long combinator selectors
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
    char bgimg[256];     // background-image url ("" = none)
    uint32_t bg_hash;    // long url() payload interned in the CSS url arena
    int bg_len;          // payload length for bg_hash (0 = none)
    uint8_t disp_flex;   // display: flex / inline-flex
    uint8_t flex_dir;    // 0 row, 1 column
    uint8_t justify;     // 0 start, 1 center, 2 end, 3 space-between
    uint8_t align_i;     // 0 start/stretch, 1 center, 2 end
    int gap;             // flex gap px
    uint8_t flex_wrap;   // flex-wrap: wrap
    int flex_grow;       // flex-grow: N (child property)
    int flex_shrink;     // flex-shrink: N (child property, -1 = unset)
    int border_w;        // border width px (0 = none)
    uint32_t border_col; // border color (0 = not set)
    int line_h;          // line-height px (-1 = not set)
    uint8_t pos_fixed;   // position: fixed / absolute (out-of-flow)
    // "not set" is kPosNone, NOT -1: a real offset can be negative (bloxd's
    // .WholeAppBackground pins itself with top:-50vh / left:-50vw, and the old
    // -1 sentinel read those as "no offset", so the fixed layer fell back to
    // its static position and stayed in flow — pushing the whole home screen
    // 1300px below the viewport).
    int pos_top;         // top px (kPosNone = not set)
    int pos_bottom;      // bottom px (kPosNone = not set)
    int pos_left;        // left px (kPosNone = not set)
    int pos_right;       // right px (kPosNone = not set)
    int z_index;         // z-index (-1 = not set)
    int width_pct;       // width: N% (0 = not a percentage)
    int height_pct;      // height: N% (0 = not a percentage)
    int width_off;       // calc(N% +/- Mpx): the px offset that rides with the %
    int max_w, max_w_pct, max_w_off;  // max-width (px / % / offset), 0 = unset
    int min_w;           // min-width px (0 = unset)
    int max_h;           // max-height px (0 = unset)
    int min_h;           // min-height px (0 = unset) — min-h-screen & co
    uint8_t box_border;  // box-sizing: border-box
    uint8_t nowrap;      // white-space: nowrap / pre
    uint8_t under;       // text-decoration: 1 underline, 2 none
    uint8_t clip;        // overflow: hidden / clip
    int radius;          // border-radius px (0 = square)
    uint8_t disp_grid;   // display: grid / inline-grid
    uint8_t disp_contents;  // display: contents — the box is skipped, kids
                            // participate in the GRANDPARENT's layout. Bloxd's
                            // GameListRow relies on it to flatten its cards
                            // into the carousel's flex row.
    int grid_cols;       // grid-template-columns track count (0 = auto)
    int grid_min;        // minmax() floor px for auto-fit tracks (0 = none)
    uint32_t gr_a, gr_b; // linear-gradient() endpoint colours (0 = no gradient)
    uint8_t gr_dir;      // 0 = vertical (to bottom / 180deg), 1 = horizontal
    int tr_pct_x, tr_pct_y;  // transform: translate(N%, N%) — negative centres
    uint8_t fl;          // float: 0 none, 1 left, 2 right
    // display, as a tri-state that follows the cascade: 0 = not set by this
    // rule, 1 = none, 2 = block/inline/table (visible), 3 = flex, 4 = grid.
    // The old boolean "none" could only ever hide: once any matching rule said
    // display:none (a mobile @media block, a hidden overlay) nothing later
    // could bring the element back, so a 1800-rule sheet erased the whole page.
    uint8_t disp;
    int pos_left_pct, pos_top_pct;   // left/top as percentages (0 = not set)
    // Per-side margins. `margin` above is the top margin (shorthand) and keeps
    // its -1 "unset" sentinel; these three are signed px and default to 0, so
    // a negative margin (margin-left:-135px centring, negative card overlap)
    // can actually be represented.
    int mar_l, mar_r, mar_b;
    CSSRule() : color(0), font_size(0), bold_set(false), bold(false),
                none(false), bg(0), align(0), margin(-1), padding(-1),
                width(-1), height(-1), border_w(0), border_col(0), line_h(-1),
                pos_fixed(0), pos_top(kPosNone), pos_bottom(kPosNone),
                pos_left(kPosNone), z_index(-1),
                width_pct(0), height_pct(0), width_off(0),
                max_w(0), max_w_pct(0), max_w_off(0), min_w(0), max_h(0), min_h(0),
                pos_right(kPosNone), box_border(0), nowrap(0), under(0), clip(0),
                radius(0)
                { sel[0] = 0; bgimg[0] = 0; bg_hash = 0; bg_len = 0;
                  disp_flex = 0; flex_dir = 0; justify = 0; align_i = 0; gap = 0;
                  flex_wrap = 0; flex_grow = 0; flex_shrink = -1;
                  disp_grid = 0; grid_cols = 0; grid_min = 0; disp_contents = 0;
                  gr_a = 0; gr_b = 0; gr_dir = 0; tr_pct_x = 0; tr_pct_y = 0;
                  fl = 0; disp = 0; mar_l = 0; mar_r = 0; mar_b = 0;
                  pos_left_pct = 0; pos_top_pct = 0; }
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
const uint32_t C_INPUT_BG = 0xFFFFFF;   // text field background
const uint32_t C_INPUT_EDGE = 0x94A3B8; // text field border (unfocused)
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

// True when "url" points at "host" (or a subdomain of it). Used to refuse
// page-initiated navigations into SDK vendor domains (e.g. bloxd.io's ad
// recovery flow redirects to page-kit.io/modal?error=..., an interstitial
// the user can neither read nor leave).
static bool nav_host_is(const char* url, const char* host) {
    if (!url) return false;
    const char* s = strstr(url, "://");
    if (!s) return false;
    s += 3;
    size_t n = strlen(host);
    for (const char* p = s; *p; p++) {
        if (*p == '/' || *p == ':' || *p == '?' || *p == '#') {
            size_t len = (size_t)(p - s);
            return len >= n && ci_starts(s + (len - n), host);
        }
    }
    return false;
}

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

// ===================== search engines =====================
// Keyword search routes through one of these providers. The default
// (DuckDuckGo HTML) returns plain <a> links, so results render and stay
// clickable even without JavaScript — this is what makes keyword search work
// reliably. Baidu/Google/Bing/YouTube are also available; pick one with the
// "g/b/bd/ddg/yt " address-bar prefix or cycle with Ctrl+E.
struct SearchEngine { const char* name; const char* get_fmt; };
static const SearchEngine kSearchEngines[] = {
    { "DuckDuckGo", "https://html.duckduckgo.com/html/?q=%s" },
    { "Google",     "https://www.google.com/search?q=%s" },
    { "Bing",       "https://www.bing.com/search?q=%s" },
    { "Baidu",      "https://www.baidu.com/s?wd=%s" },
    { "YouTube",    "https://www.youtube.com/results?search_query=%s" },
};
static const int kSearchEngineCount  = 5;
static const int kDefaultSearchEngine = 0; // DuckDuckGo HTML

// Build a percent-encoded search URL for `query` using engine `idx`.
static void build_search_url(const char* query, int idx, char* out, int cap) {
    if (idx < 0 || idx >= kSearchEngineCount) idx = kDefaultSearchEngine;
    char enc[1600];
    url_encode(query, enc, (int)sizeof(enc));
    ksprintf(out, cap, kSearchEngines[idx].get_fmt, enc);
}

// ===================== DOM (bounded arena) =====================
enum class Tag : uint8_t {
    Root, Text,
    H1, H2, H3, H4, H5, H6,
    P, Div, Span, A, Img,
    Ul, Ol, Li, Br, Hr,
    B, Strong, I, Em, Pre, Blockquote,
    Table, Tr, Td, Th, Form, Button, Canvas, Input, Textarea,
    Select, Option, Svg,
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
            if (ci_eq(name, "svg")) return Tag::Svg;
            if (ci_eq(name, "strong")) return Tag::Strong;
            if (ci_eq(name, "script")) return Tag::Script;
            if (ci_eq(name, "style")) return Tag::Style;
            if (ci_eq(name, "select")) return Tag::Select;
            break;
        case 'a':
            if (ci_eq(name, "a")) return Tag::A;
            break;
        case 'i':
            if (ci_eq(name, "img")) return Tag::Img;
            if (ci_eq(name, "i")) return Tag::I;
            if (ci_eq(name, "input")) return Tag::Input;
            break;
        case 'u':
            if (ci_eq(name, "ul")) return Tag::Ul;
            break;
        case 'o':
            if (ci_eq(name, "ol")) return Tag::Ol;
            if (ci_eq(name, "option")) return Tag::Option;
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
            if (ci_eq(name, "textarea")) return Tag::Textarea;
            break;
        case 'f':
            if (ci_eq(name, "form")) return Tag::Form;
            break;
        case 'n':
            // <noscript> renders ONLY when scripting is disabled. We always
            // run the page's JS, so drawing it pasted React's raw
            // "You need to enable JavaScript to run this app." string over
            // bloxd's real UI. Route it to Script: parsed, never laid out.
            if (ci_eq(name, "noscript")) return Tag::Script;
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

// tag-name boundary: the char right after a matched name must not continue
// the name (so "template" does not match "<templateX")
static bool tag_name_end(char c) {
    return c == 0 || c == ' ' || c == '>' || c == '\t' || c == '\n' ||
           c == '\r' || c == '/';
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
    String value;      // <input>/<textarea> live value
    String ph;         // placeholder attribute
    String fname;      // form field name (query-string key)
    String action;     // <form action="">
    List<int> kids;
    char cls[48];      // class attribute (space-separated)
    char id[32];       // id attribute
    char onclick[96];  // inline onclick="" handler (mini JS)
    char tag_name[24]; // lowercased tag name (selector fidelity for body/html)
    char ftype[16];    // input type="" (text/search/hidden/submit/...)
    char fmethod[8];   // <form method=""> (get/post) — also formmethod on a button
    String svg;       // Tag::Svg: the element's own markup, rasterized by nanosvg
    int cw, ch;        // canvas element size (width/height attrs, default 300x150)
    int parent;        // enclosing node, -1 for the root (form submit walks up)
    bool checked;      // <input type=checkbox|radio checked> / <option selected>
    uint8_t battrs;    // boolean attributes (hidden/disabled/readonly/...) — see BA_*
    CSSRule inl;       // inline style="" (selector unused)
    bool has_inl;
    int nfu_idx;       // data-nfu index (JS DOM shim element), -1 = none
    DomNode() : cw(300), ch(150), parent(-1), checked(false), battrs(0),
                has_inl(false), nfu_idx(-1)
        { cls[0] = id[0] = onclick[0] = tag_name[0] = ftype[0] = fmethod[0] = 0; }
};

// A field the user can type into. Everything else that shares the <input> tag
// (submit / button / reset / checkbox / radio / range / image / hidden) is
// either a control with its own behaviour or not rendered at all. type="file"
// is rendered as a text box holding a filesystem path: a real file picker
// needs a dialog stack this browser does not have, and a typed path goes
// through exactly the same focus/keyboard/value plumbing.
static bool field_is_text(const DomNode& d) {
    if (d.tag == Tag::Textarea) return true;
    if (d.tag != Tag::Input) return false;
    if (!d.ftype[0]) return true;
    return ci_eq(d.ftype, "text") || ci_eq(d.ftype, "search") ||
           ci_eq(d.ftype, "email") || ci_eq(d.ftype, "url") ||
           ci_eq(d.ftype, "tel") || ci_eq(d.ftype, "password") ||
           ci_eq(d.ftype, "number") || ci_eq(d.ftype, "date") ||
           ci_eq(d.ftype, "datetime-local") || ci_eq(d.ftype, "time") ||
           ci_eq(d.ftype, "file");
}

// A button-looking <input> (submit / button / reset / image): drawn as a
// raised control with its label instead of a sunken text box.
static bool field_is_button(const DomNode& d) {
    if (d.tag != Tag::Input) return false;
    return ci_eq(d.ftype, "submit") || ci_eq(d.ftype, "button") ||
           ci_eq(d.ftype, "reset") || ci_eq(d.ftype, "image");
}

struct DomArena {
    List<DomNode> nodes;
    List<int> stack;
    bool overflow = false;
    int ovf_src = 0;   // where overflow tripped: 1=open cap 2=text cap 3x=stream
    int ovf_need = 0;  // bytes the failed stream alloc wanted

    DomArena() {
        DomNode root;
        root.tag = Tag::Root;
        nodes.push(root);
        stack.push(0);
    }

    int top() const { return stack.size() > 0 ? stack[stack.size() - 1] : 0; }

    void open(Tag t, const char* href, const char* src, const char* alt,
              const char* cls = 0, const char* id = 0, const CSSRule* inl = 0,
              const char* onclick = 0, const char* tagname = 0) {
        if (overflow) return;
        if (nodes.size() >= DOM_MAX || stack.size() >= DOM_DEPTH) { overflow = true; ovf_src = 1; return; }
        int idx = nodes.size();
        DomNode n;
        n.tag = t;
        n.parent = top();
        if (tagname && tagname[0]) clip_str(n.tag_name, sizeof(n.tag_name), tagname);
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
        if (nodes.size() >= DOM_MAX) { overflow = true; ovf_src = 2; return; }
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

// serialize the parsed DOM back to HTML; used to seed the JS DOM shim so
// scripts can find real page elements (getElementById) and the shim tree
// matches the rendered page. Script/Style nodes are skipped (CSS/JS already
// captured by the browser pipeline; their bodies would confuse the seed).
static const char* tag_name_of(Tag t) {
    switch (t) {
        case Tag::H1: return "h1"; case Tag::H2: return "h2"; case Tag::H3: return "h3";
        case Tag::H4: return "h4"; case Tag::H5: return "h5"; case Tag::H6: return "h6";
        case Tag::P: return "p"; case Tag::Div: return "div"; case Tag::Span: return "span";
        case Tag::A: return "a"; case Tag::Img: return "img"; case Tag::Ul: return "ul";
        case Tag::Ol: return "ol"; case Tag::Li: return "li"; case Tag::Br: return "br";
        case Tag::Hr: return "hr"; case Tag::B: return "b"; case Tag::Strong: return "strong";
        case Tag::I: return "i"; case Tag::Em: return "em"; case Tag::Pre: return "pre";
        case Tag::Blockquote: return "blockquote"; case Tag::Table: return "table";
        case Tag::Tr: return "tr"; case Tag::Td: return "td"; case Tag::Th: return "th";
        case Tag::Form: return "form"; case Tag::Button: return "button";
        case Tag::Canvas: return "canvas";
        case Tag::Svg: return "svg";
        case Tag::Input: return "input"; case Tag::Textarea: return "textarea";
        case Tag::Select: return "select"; case Tag::Option: return "option";
        default: return "";
    }
}
static bool tag_is_void(Tag t) {
    return t == Tag::Br || t == Tag::Hr || t == Tag::Img || t == Tag::Input;
}
// Emit one ` k="v"` (or bare ` k` when v is null) into the seed buffer. The
// JS DOM shim is seeded from this markup, so anything a page reads back off an
// element (type / name / value / checked) has to survive the trip.
// Pull key="value" out of an opening tag's attribute span [a, a+al). Used by
// the <svg> capture path, which never reaches the main attribute scanner (the
// subtree is lifted verbatim instead of being tokenized). Returns the value
// length; `out` is always NUL-terminated.
static int attr_span_get(const char* a, int al, const char* key, char* out, int cap) {
    if (!a || !key || !out || cap <= 0) return 0;
    out[0] = 0;
    int kl = (int)strlen(key);
    for (int i = 0; i + kl <= al; i++) {
        if (i > 0) {
            const char p = a[i - 1];
            if (!(p == ' ' || p == '\t' || p == '\n' || p == '\r' || p == '/')) continue;
        }
        if (!ci_starts(a + i, key)) continue;
        int q = i + kl;
        while (q < al && (a[q] == ' ' || a[q] == '\t')) q++;
        if (q < al && a[q] != '=') continue;
        while (q < al && (a[q] == '=' || a[q] == ' ' || a[q] == '\t')) q++;
        char quote = 0;
        if (q < al && (a[q] == '"' || a[q] == '\'')) { quote = a[q]; q++; }
        int n = 0;
        while (q < al && n < cap - 1) {
            const char c = a[q];
            if (quote ? (c == quote)
                      : (c == ' ' || c == '\t' || c == '>' || c == '\n' || c == '\r')) break;
            out[n++] = c;
            q++;
        }
        out[n] = 0;
        return n;
    }
    return 0;
}

static void html_attr_put(const char* k, const char* v, char* out, int& n, int cap) {
    if (n + 6 >= cap) return;
    out[n++] = ' ';
    for (const char* p = k; *p && n < cap - 2; p++) out[n++] = *p;
    if (!v) return;
    out[n++] = '=';
    if (n < cap - 1) out[n++] = '"';
    for (const char* p = v; *p && n < cap - 2; p++) out[n++] = (*p == '"') ? '\'' : *p;
    if (n < cap - 1) out[n++] = '"';
}

static void dom_html_rec(const DomArena& dom, int idx, char* out, int& n, int cap) {
    if (n >= cap - 16) return;
    const DomNode& nd = dom.nodes[idx];
    if (nd.tag == Tag::Text) {
        for (int i = 0; i < nd.text.len() && n < cap - 1; i++) out[n++] = nd.text[i];
        return;
    }
    if (nd.tag == Tag::Root) {
        for (int k = 0; k < nd.kids.size(); k++) dom_html_rec(dom, nd.kids[k], out, n, cap);
        return;
    }
    if (nd.tag == Tag::Script || nd.tag == Tag::Style) return;
    const char* nm = tag_name_of(nd.tag);
    if (!nm[0]) {
        // unknown container (html/head/body/...): still descend into kids
        // so real page elements under them reach the JS DOM shim
        for (int k = 0; k < nd.kids.size(); k++) dom_html_rec(dom, nd.kids[k], out, n, cap);
        return;
    }
    bool vd = tag_is_void(nd.tag);
    out[n++] = '<';
    for (const char* c = nm; *c && n < cap - 2; c++) out[n++] = *c;
    if (nd.id[0]) {
        const char* a = " id=\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (const char* c = nd.id; *c && n < cap - 2; c++) out[n++] = *c;
        out[n++] = '"';
    }
    if (nd.cls[0]) {
        const char* a = " class=\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (const char* c = nd.cls; *c && n < cap - 2; c++) out[n++] = *c;
        out[n++] = '"';
    }
    // NEFU-FIX: canvas backing-store size never reached the JS DOM model, so
    // canvas.width/height read the 300x150 default and getContext() created a
    // surface that size while the page had asked for something else. Every WebGL
    // page was therefore rendering into a mismatched, mostly off-screen buffer.
    if (nd.tag == Tag::Canvas) {
        char wh[64];
        ksprintf(wh, sizeof(wh), " width=\"%d\" height=\"%d\"", (int)nd.cw, (int)nd.ch);
        for (const char* c = wh; *c && n < cap - 2; c++) out[n++] = *c;
    }
    if (nd.href.len() > 0) {
        const char* a = " href=\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (int i = 0; i < nd.href.len() && n < cap - 2; i++) out[n++] = nd.href[i];
        out[n++] = '"';
    }
    if (nd.src.len() > 0) {
        const char* a = " src=\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (int i = 0; i < nd.src.len() && n < cap - 2; i++) out[n++] = nd.src[i];
        out[n++] = '"';
    }
    // form-control state (the shim reads these back through el.type/.name/.value)
    if (nd.ftype[0]) html_attr_put("type", nd.ftype, out, n, cap);
    if (nd.fname.len() > 0) html_attr_put("name", nd.fname.c_str(), out, n, cap);
    if (nd.value.len() > 0 && !vd) html_attr_put("value", nd.value.c_str(), out, n, cap);
    if (nd.checked) html_attr_put("checked", 0, out, n, cap);
    if (nd.checked && nd.tag == Tag::Option) html_attr_put("selected", 0, out, n, cap);
    // Keep the submission method across a JS re-render: the shim rebuilds the
    // tree from this markup, and a lost method="post" silently turns every
    // later submit back into a GET.
    if (nd.fmethod[0])
        html_attr_put(nd.tag == Tag::Form ? "method" : "formmethod",
                      nd.fmethod, out, n, cap);
    // Inline vector graphics keep their intrinsic box (the shim has no layout
    // engine, but pages do read getBoundingClientRect off icons).
    if (nd.tag == Tag::Svg) {
        char box[24];
        ksprintf(box, sizeof(box), "%d", nd.cw);
        html_attr_put("width", box, out, n, cap);
        ksprintf(box, sizeof(box), "%d", nd.ch);
        html_attr_put("height", box, out, n, cap);
    }
    if (vd) { out[n++] = '/'; }
    out[n++] = '>';
    if (!vd) {
        for (int k = 0; k < nd.kids.size(); k++) dom_html_rec(dom, nd.kids[k], out, n, cap);
        if (n < cap - 2) { out[n++] = '<'; out[n++] = '/'; }
        for (const char* c = nm; *c && n < cap - 2; c++) out[n++] = *c;
        if (n < cap - 1) out[n++] = '>';
    }
}
static int dom_to_html(const DomArena& dom, char* out, int cap) {
    int n = 0;
    dom_html_rec(dom, 0, out, n, cap);
    if (n >= cap) n = cap - 1;
    out[n] = 0;
    return n;
}


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
            // expand each nibble by 17 (0xF -> 0xFF); the old shift-and-or
            // produced 0x0FFFFF for #fff — a CYAN page background, which is
            // why half the portals in the sweep painted solid cyan
            int r = (hx >> 8) & 0xF, g = (hx >> 4) & 0xF, b = hx & 0xF;
            return (uint32_t)((r * 17) << 16) | ((g * 17) << 8) | (uint32_t)(b * 17);
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
    if (ci_eq(v, "purple")) return 0x9333EA;
    if (ci_eq(v, "pink")) return 0xEC4899;
    if (ci_eq(v, "cyan") || ci_eq(v, "aqua")) return 0x06B6D4;
    if (ci_eq(v, "magenta") || ci_eq(v, "fuchsia")) return 0xE879F9;
    if (ci_eq(v, "brown")) return 0x92400E;
    if (ci_eq(v, "silver")) return 0xC0C0C0;
    if (ci_eq(v, "navy")) return 0x1E3A8A;
    if (ci_eq(v, "teal")) return 0x0D9488;
    if (ci_eq(v, "olive")) return 0x7C8C1F;
    if (ci_eq(v, "maroon")) return 0x800000;
    if (ci_eq(v, "lime")) return 0x84CC16;
    if (ci_eq(v, "lightgray") || ci_eq(v, "lightgrey")) return 0xD3D3D3;
    if (ci_eq(v, "darkgray") || ci_eq(v, "darkgrey")) return 0x555555;
    return 0;
}

// color inside a border shorthand ("1px solid #ccc", "solid red 2px", ...):
// the first #hex wins, else the first named-color token
static uint32_t css_border_col(const char* val) {
    for (const char* p = val; *p; p++)
        if (*p == '#') return css_color(p);
    const char* names[] = {"red","green","blue","black","white","gray","grey",
                           "yellow","orange","purple","pink","cyan","magenta",
                           "brown","silver","navy","teal","olive","maroon",
                           "aqua","lime","fuchsia","transparent",0};
    for (int i = 0; names[i]; i++) {
        int L = (int)strlen(names[i]);
        const char* q = val;
        while (*q) {
            while (*q == ' ' || *q == '\t' || *q == ',') q++;
            const char* st = q;
            while (*q && *q != ' ' && *q != '\t' && *q != ',') q++;
            int l = (int)(q - st);
            if (l == L) {
                bool eq = true;
                for (int k = 0; k < L; k++)
                    if (lower_char(st[k]) != names[i][k]) { eq = false; break; }
                if (eq) return css_color(names[i]);
            }
        }
    }
    return 0;
}

static int css_px(const char* v);   // defined below (grid floor parsing)

// Colour at the head of a value span, including rgb()/rgba(). The gradient
// parser hands over comma-split stops that are not NUL-terminated, and every
// modern sheet writes its stops as rgb().
static uint32_t css_color_span(const char* v, int n) {
    while (n > 0 && (*v == ' ' || *v == '\t' || *v == '\n')) { v++; n--; }
    if (n >= 3 && lower_char(v[0]) == 'r') {
        int p = 0;
        while (p < n && v[p] != '(') p++;
        if (p < n) {
            int c[4] = {0, 0, 0, 255}, ci = 0;
            p++;
            while (p < n && ci < 4) {
                while (p < n && (v[p] == ' ' || v[p] == ',' || v[p] == '\t')) p++;
                int vl = 0; bool any = false;
                while (p < n && v[p] >= '0' && v[p] <= '9') {
                    vl = vl * 10 + (v[p] - '0'); p++; any = true;
                }
                if (p < n && v[p] == '.') {          // 0.5 alpha
                    p++;
                    while (p < n && v[p] >= '0' && v[p] <= '9') p++;
                }
                if (p < n && v[p] == '%') { p++; vl = vl * 255 / 100; }
                if (!any) break;
                c[ci++] = vl;
            }
            return ((uint32_t)(c[0] & 255) << 16) | ((uint32_t)(c[1] & 255) << 8)
                   | (uint32_t)(c[2] & 255);
        }
    }
    char buf[40]; int k = 0;
    while (k < n && k < 39) { buf[k] = v[k]; k++; }
    buf[k] = 0;
    return css_color(buf);
}

// linear-gradient(...) -> two endpoint colours + axis. Hero bands, buttons and
// card headers are painted with it; before this they came out flat white.
static void css_grad_span(const char* v, int n, CSSRule& r) {
    int gi = -1;
    for (int k = 0; k + 16 < n; k++) {
        if (lower_char(v[k]) == 'l' && lower_char(v[k+1]) == 'i' &&
            lower_char(v[k+2]) == 'n' && lower_char(v[k+3]) == 'e' &&
            lower_char(v[k+4]) == 'a' && lower_char(v[k+5]) == 'r' &&
            v[k+6] == '-' && lower_char(v[k+7]) == 'g' && v[k+8] == 'r' &&
            lower_char(v[k+9]) == 'a' && lower_char(v[k+10]) == 'd') { gi = k; break; }
    }
    if (gi < 0) return;
    int p = gi;
    while (p < n && v[p] != '(') p++;
    if (p >= n) return;
    p++;
    int sa[8], sb[8], ns = 0, depth = 0, cur = p;
    bool dir_h = false;
    while (p <= n && ns < 8) {
        char c = (p < n) ? v[p] : ')';
        if (c == '(') depth++;
        else if (c == ')') { if (depth > 0) depth--; }
        if ((c == ',' && depth == 0) || c == ')') {
            int a = cur, b = p;
            while (a < b && (v[a] == ' ' || v[a] == '\t' || v[a] == '\n')) a++;
            while (b > a && (v[b-1] == ' ' || v[b-1] == '\t' || v[b-1] == '\n')) b--;
            if (b > a) {
                // direction stop: "to right", "90deg", "0.25turn"
                bool is_dir = false;
                for (int q = a; q < b; q++)
                    if ((v[q] == 'd' || v[q] == 'D') && q + 2 < b &&
                        (v[q+1] == 'e' || v[q+1] == 'E') && (v[q+2] == 'g' || v[q+2] == 'G'))
                        is_dir = true;
                if (b - a >= 2 && (v[a] == 't' || v[a] == 'T') && (v[a+1] == 'o' || v[a+1] == 'O'))
                    is_dir = true;
                if (is_dir) {
                    for (int q = a; q + 5 < b; q++)
                        if ((v[q] == 'r' || v[q] == 'R') && (v[q+1] == 'i' || v[q+1] == 'I') &&
                            (v[q+2] == 'g' || v[q+2] == 'G') && (v[q+3] == 'h') && (v[q+4] == 't'))
                            dir_h = true;
                    for (int q = a; q + 1 < b; q++)
                        if (v[q] == '9' && v[q+1] == '0') dir_h = true;   // 90deg
                } else {
                    sa[ns] = a; sb[ns] = b; ns++;
                }
            }
            cur = p + 1;
            if (c == ')') break;
        }
        p++;
    }
    if (ns < 2) return;
    uint32_t c0 = css_color_span(v + sa[0], sb[0] - sa[0]);
    uint32_t c1 = css_color_span(v + sa[ns - 1], sb[ns - 1] - sa[ns - 1]);
    if (!c0 && !c1) return;
    if (!c0) c0 = c1;
    if (!c1) c1 = c0;
    r.gr_a = c0; r.gr_b = c1; r.gr_dir = dir_h ? 1 : 0;
}

// grid-template-columns -> track count. "repeat(3, 1fr)" and
// "1fr 1fr 1fr" both mean three columns; repeat(auto-fit, minmax(240px,1fr))
// keeps the 240px floor so the layout can size the columns to the container.
static void css_grid_cols(const char* v, int n, CSSRule& r) {
    int cols = 0, mn = 0;
    int i = 0;
    // repeat(N, ...) / auto-fit|auto-fill with minmax()
    while (i < n) {
        if (lower_char(v[i]) == 'r' && i + 6 < n &&
            lower_char(v[i+1]) == 'e' && lower_char(v[i+2]) == 'p' &&
            lower_char(v[i+3]) == 'e' && lower_char(v[i+4]) == 'a' &&
            lower_char(v[i+5]) == 't' && v[i+6] == '(') {
            int p = i + 7;
            while (p < n && (v[p] == ' ' || v[p] == '\t')) p++;
            if (p < n && v[p] >= '0' && v[p] <= '9') {
                int d = 0;
                while (p < n && v[p] >= '0' && v[p] <= '9') { d = d * 10 + (v[p] - '0'); p++; }
                if (d > 0) cols = d > 6 ? 6 : d;
            } else {
                // auto-fit / auto-fill: look for the minmax() floor
                for (int q = p; q + 7 < n; q++)
                    if (lower_char(v[q]) == 'm' && lower_char(v[q+1]) == 'i' &&
                        lower_char(v[q+2]) == 'n' && lower_char(v[q+3]) == 'm' &&
                        lower_char(v[q+4]) == 'a' && lower_char(v[q+5]) == 'x') {
                        int e = q + 6;
                        while (e < n && v[e] != '(') e++;
                        mn = css_px(v + e + 1);
                        break;
                    }
                if (mn < 80) mn = 240;     // no floor given: sane default
                cols = 0;                  // size to the container at layout time
            }
            i = p;
        } else i++;
    }
    if (!cols) {
        // plain track list: count whitespace-separated tokens
        int cnt = 0, tok = 0;
        for (int k = 0; k < n; k++) {
            char c = v[k];
            bool sp = (c == ' ' || c == '\t' || c == '\n');
            if (!sp && !tok) { tok = 1; cnt++; }
            else if (sp) tok = 0;
        }
        cols = cnt;
    }
    if (cols < 1) cols = 1;
    if (cols > 6) cols = 6;
    r.grid_cols = cols;
    r.grid_min = mn;
}

// Length of a CSS value in px, clamped to [0,512]. Handles decimals and the
// units app pages actually use: px, em/rem (16px root), pt, vh/vw/vmin/vmax.
// Tailwind-style sheets express almost every spacing as "0.5rem"/"1.5rem", and
// reading only the leading integer collapsed them all to 0 or 1 px.
static int g_css_root_font = 16;   // html{font-size}: the base for rem units
// NF_DUMP_HIDE=1: log every node a stylesheet hides, with the selector that
// did it. A page that suddenly renders as nothing is almost always a
// display:none rule matching too broadly.
static bool nfu_debug_hide() {
    static int v = -1;
    if (v < 0) { const char* e = getenv("NF_DUMP_HIDE"); v = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return v != 0;
}
static bool nfu_debug_box() {
    static int v = -1;
    if (v < 0) { const char* e = getenv("NF_DUMP_BOX"); v = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return v != 0;
}
static int s_mq_dbg_env = -1;   // NF_DUMP_MQ: log every @media verdict
static int css_px(const char* v) {
    while (*v == ' ' || *v == '\t') v++;
    bool neg = false;
    if (*v == '-') { neg = true; v++; }
    int n = 0;
    while (v[n] >= '0' && v[n] <= '9') n++;
    double x = 0;
    for (int i = 0; i < n; i++) x = x * 10 + (v[i] - '0');
    if (v[n] == '.') {
        int j = n + 1;
        double f = 0.1;
        while (v[j] >= '0' && v[j] <= '9') { x += (v[j] - '0') * f; f /= 10.0; j++; }
        if (j > n + 1) n = j;
    }
    if (n == 0) return 0;
    if (neg) x = -x;
    const char* u = v + n;
    int px;
    if (ci_starts(u, "dvh") || ci_starts(u, "svh") || ci_starts(u, "lvh")) {
        // dynamic/small/large viewport height: same static approximation
        int h = s_view_h > 100 ? s_view_h : 560;
        px = (int)(x * h / 100.0);
    } else if (ci_starts(u, "vh")) {
        int h = s_view_h > 100 ? s_view_h : 560;
        px = (int)(x * h / 100.0);
    } else if (ci_starts(u, "vmax")) {
        int m = (s_view_w > s_view_h) ? s_view_w : s_view_h;
        if (m < 100) m = 560;
        px = (int)(x * m / 100.0);
    } else if (ci_starts(u, "vw")) {
        int w2 = s_view_w > 100 ? s_view_w : 780;
        px = (int)(x * w2 / 100.0);
    } else if (ci_starts(u, "vmin")) {
        int m = (s_view_w > 0 && s_view_w < s_view_h) ? s_view_w : s_view_h;
        if (m < 100) m = 560;
        px = (int)(x * m / 100.0);
    } else if (ci_starts(u, "rem") || ci_starts(u, "em")) {
        // rem is relative to html{font-size}; a page that sets it to 100px
        // (baidu) sizes everything in rem, so the fixed 16 was 7x too small.
        px = (int)(x * (double)(g_css_root_font > 0 ? g_css_root_font : 16));
    } else if (ci_starts(u, "pt")) {
        px = (int)(x * 4.0 / 3.0);
    } else if (ci_starts(u, "%")) {
        // A bare percentage where a length is expected (padding, gap, ...)
        // cannot be resolved here; the caller treats a percentage separately.
        px = 0;
    } else {
        px = (int)(x + 0.5);         // px, or a unitless number
    }
    if (neg && px > 0) px = -px;
    // The old +-512 cap was meant to contain runaway values, but real pages use
    // 100vw / calc(100vh - 1.5rem) / 970px-wide ad slots every day. Clamping
    // those to 512 collapsed bloxd's whole app shell into a 512x409 block and
    // pushed baidu's layout to insane offsets. Keep a guard, but one no sane
    // page can trip.
    const int kLenCap = 20000;
    if (px > kLenCap) px = kLenCap;
    if (px < -kLenCap) px = -kLenCap;
    return px;
}

// ---- length expressions: calc(), min(), max(), clamp(), percentages -------
// A length resolves to `pct` percent of the containing block plus `px`, which
// covers "100%", "calc(100% - 32px)" and friends — the shape container widths
// take on every real site.
struct Len { int pct = 0; int px = 0; int cap = 0; int floor = 0; };

static bool len_term(const char* v, int n, Len& L) {
    while (n > 0 && (v[0]==' '||v[0]=='\t')) { v++; n--; }
    while (n > 0 && (v[n-1]==' '||v[n-1]=='\t')) n--;
    if (n <= 0) return false;
    // percentage term?
    char buf[48]; int k = 0;
    for (int i = 0; i < n && k < 47; i++) buf[k++] = v[i];
    buf[k] = 0;
    if (buf[k-1] == '%') {
        L.pct = atoi(buf);
        return true;
    }
    L.px += css_px(buf);
    return true;
}

// Split on top-level commas.
static int split_args(const char* v, int n, const char** out, int* lens, int cap) {
    int cnt = 0, st = 0, depth = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || (v[i] == ',' && depth == 0)) {
            if (cnt < cap) { out[cnt] = v + st; lens[cnt] = i - st; cnt++; }
            st = i + 1;
        } else if (v[i] == '(') depth++;
        else if (v[i] == ')') { if (depth > 0) depth--; }
    }
    return cnt;
}

static bool len_expr(const char* v, int n, Len& L) {
    while (n > 0 && (v[0]==' '||v[0]=='\t')) { v++; n--; }
    while (n > 0 && (v[n-1]==' '||v[n-1]=='\t')) n--;
    if (n <= 0) return false;
    // function form
    int k = 0;
    while (k < n && ((v[k] >= 'a' && v[k] <= 'z') || (v[k] >= 'A' && v[k] <= 'Z'))) k++;
    if (k > 0 && k < n && v[k] == '(' && v[n-1] == ')') {
        char fn[8]; int fl = 0;
        for (int i = 0; i < k && fl < 7; i++) fn[fl++] = lower_char(v[i]);
        fn[fl] = 0;
        const char* inner = v + k + 1;
        int inl = n - k - 2;
        if (ci_eq(fn, "calc")) {
            // sum of signed terms
            bool first = true; int sign = 1;
            int i = 0;
            while (i < inl) {
                char c = inner[i];
                if (c == ' ' || c == '\t') { i++; continue; }
                if (c == '+' || c == '-') { sign = (c == '-') ? -1 : 1; i++; continue; }
                int st = i, depth = 0;
                while (i < inl) {
                    char d = inner[i];
                    if (d == '(') depth++;
                    else if (d == ')') { if (depth > 0) depth--; }
                    else if (depth == 0 && (d == '+' || d == '-')) break;
                    i++;
                }
                Len t;
                if (!len_term(inner + st, i - st, t)) return false;
                if (first) { L.pct = 0; L.px = 0; first = false; }
                L.pct += sign * t.pct;
                L.px += sign * t.px;
                sign = 1;
            }
            return true;
        }
        const char* a[4]; int al[4];
        int ac = split_args(inner, inl, a, al, 4);
        if (ac <= 0) return false;
        if (ci_eq(fn, "min") || ci_eq(fn, "max")) {
            bool is_min = ci_eq(fn, "min");
            Len best; bool have = false; int pxcap = 0, pctv = 0; bool pct_seen = false;
            for (int i = 0; i < ac; i++) {
                Len t;
                if (!len_expr(a[i], al[i], t)) continue;
                if (t.pct == 0 && !pct_seen) {
                    if (!have) { best = t; have = true; }
                    else if (is_min ? (t.px < best.px) : (t.px > best.px)) best = t;
                    pxcap = is_min ? (pxcap ? (t.px < pxcap ? t.px : pxcap) : t.px)
                                   : (t.px > pxcap ? t.px : pxcap);
                } else if (t.pct) {
                    pct_seen = true;
                    if (is_min ? (pctv == 0 || t.pct < pctv) : (t.pct > pctv)) pctv = t.pct;
                }
            }
            if (pct_seen) {
                // min(1200px, 100%) == 100% capped at 1200px
                L.pct = pctv ? pctv : 100;
                L.px = 0;
                if (pxcap > 0) { if (is_min) L.cap = pxcap; else L.floor = pxcap; }
                return true;
            }
            if (!have) return false;
            L.pct = 0; L.px = best.px;
            return true;
        }
        if (ci_eq(fn, "clamp") && ac == 3) {
            // clamp(min, preferred, max): keep the preferred, bounded
            Len lo, pr, hi;
            len_expr(a[0], al[0], lo);
            len_expr(a[1], al[1], pr);
            len_expr(a[2], al[2], hi);
            L = pr;
            if (lo.px > 0) L.floor = lo.px;
            if (hi.px > 0 && hi.pct == 0) L.cap = hi.px;
            else if (hi.pct && L.pct > hi.pct) L.pct = hi.pct;
            return true;
        }
        return false;
    }
    return len_term(v, n, L);
}

// ---- selector matching ---------------------------------------------------
// Supported: tag, *, .class, #id; attribute selectors [a], [a=v], [a~=v],
// [a^=v], [a$=v], [a*=v], [a|=v]; structural pseudo-classes :root,
// :first-child, :last-child, :only-child, :nth-child(n|odd|even|An+B),
// :first-of-type, :last-of-type, :nth-of-type(...), :empty; functional
// :not(...), :is(...), :where(...); selector groups "a, b, c".
//
// State pseudo-classes (:hover/:focus/:active/:visited/...) never match: their
// declarations are transient, and applying them permanently would fight the
// element's base rule. Pseudo-ELEMENTS (::before/::after) generate no box
// here, so their rules are skipped as well.
//
// Combinators are reduced to their LAST simple-sequence segment (".A .B"
// matches as ".B"). Real ancestor walking costs a full parent chain per rule
// per node, and framework sheets survive on the tail match.
static bool cl_token(const char* cls, const char* tok, int tn) {
    int cl = (int)strlen(cls), p = 0;
    while (p <= cl) {
        int q = p;
        while (q < cl && cls[q] != ' ') q++;
        if (q - p == tn) {
            bool eq = true;
            for (int k = 0; k < tn; k++)
                if (lower_char(cls[p + k]) != lower_char(tok[k])) { eq = false; break; }
            if (eq) return true;
        }
        p = q + 1;
    }
    return false;
}

// Boolean attributes the tokenizer records as flags (presence == value).
enum { BA_HIDDEN = 1, BA_DISABLED = 2, BA_READONLY = 4,
       BA_REQUIRED = 8, BA_OPEN = 16, BA_SELECTED = 32 };

// Value of an attribute, or 0 when absent. `buf` is used for the synthetic
// ones (flags render as an empty string, which is what CSS sees).
static const char* node_attr(const DomNode& n, const char* name, int nlen, char* buf, int cap) {
    buf[0] = 0;
    if (nlen <= 0) return 0;
    char nm[24]; int k = 0;
    for (; k < nlen && k < 23; k++) nm[k] = lower_char(name[k]);
    nm[k] = 0;
    if (ci_eq(nm, "class")) return n.cls[0] ? n.cls : 0;
    if (ci_eq(nm, "id")) return n.id[0] ? n.id : 0;
    if (ci_eq(nm, "type")) return n.ftype[0] ? n.ftype : 0;
    if (ci_eq(nm, "href")) return n.href.len() ? n.href.c_str() : 0;
    if (ci_eq(nm, "src")) return n.src.len() ? n.src.c_str() : 0;
    if (ci_eq(nm, "alt")) return n.alt.len() ? n.alt.c_str() : 0;
    if (ci_eq(nm, "value")) return n.value.len() ? n.value.c_str() : 0;
    if (ci_eq(nm, "placeholder")) return n.ph.len() ? n.ph.c_str() : 0;
    if (ci_eq(nm, "name")) return n.fname.len() ? n.fname.c_str() : 0;
    if (ci_eq(nm, "action")) return n.action.len() ? n.action.c_str() : 0;
    if (ci_eq(nm, "style")) return n.has_inl ? "/" : 0;
    if (ci_eq(nm, "checked") || ci_eq(nm, "selected"))
        return (n.checked || (n.battrs & BA_SELECTED)) ? "/" : 0;
    if (ci_eq(nm, "hidden")) return (n.battrs & BA_HIDDEN) ? "/" : 0;
    if (ci_eq(nm, "disabled")) return (n.battrs & BA_DISABLED) ? "/" : 0;
    if (ci_eq(nm, "readonly")) return (n.battrs & BA_READONLY) ? "/" : 0;
    if (ci_eq(nm, "required")) return (n.battrs & BA_REQUIRED) ? "/" : 0;
    if (ci_eq(nm, "open")) return (n.battrs & BA_OPEN) ? "/" : 0;
    (void)buf; (void)cap;
    return 0;
}

// Element position (1-based) among the element children of the parent, plus
// the total element count. Text/comment nodes are not elements.
static int elem_pos(const List<DomNode>& nodes, int idx, bool same_type, int* total) {
    if (total) *total = 0;
    const DomNode& n = nodes[idx];
    if (n.parent < 0) { if (total) *total = 1; return 1; }
    const DomNode& p = nodes[n.parent];
    int pos = 0, seen = 0;
    for (int i = 0; i < p.kids.size(); i++) {
        int ki = p.kids[i];
        const DomNode& k = nodes[ki];
        if (k.tag == Tag::Text) continue;
        if (k.tag == Tag::Root) continue;
        if (same_type && k.tag != n.tag) { if (ki == idx) return 0; continue; }
        seen++;
        if (ki == idx) pos = seen;
    }
    if (total) *total = seen;
    return pos;
}

static bool match_simple(const char* s, int sl, const List<DomNode>& nodes, int idx);

// "odd" / "even" / "3" / "2n+1" / "-n+3"
static bool nth_ok(const char* a, int al, int pos) {
    char b[32]; int k = 0;
    for (int i = 0; i < al && k < 31; i++)
        if (a[i] != ' ') b[k++] = lower_char(a[i]);
    b[k] = 0;
    if (!k) return false;
    if (!strcmp(b, "odd")) return (pos % 2) == 1;
    if (!strcmp(b, "even")) return (pos % 2) == 0;
    const char* nn = strchr(b, 'n');
    if (!nn) return pos == atoi(b);
    int A = (nn == b) ? 1 : atoi(b);
    const char* rest = nn + 1;
    int B = 0;
    if (*rest == '+' || *rest == '-') B = atoi(rest);
    if (A == 0) return pos == B;
    int d = pos - B;
    if (d % A) return false;
    return (d / A) >= 0;
}

static bool match_simple(const char* s, int sl, const List<DomNode>& nodes, int idx) {
    const DomNode& n = nodes[idx];
    int i = 0;
    // optional tag name (or '*') first
    if (i < sl && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') {
        int st = i;
        while (i < sl && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') i++;
        int tl = i - st;
        char tb[24]; int tn = 0;
        for (int k = st; k < tl + st && tn < 23; k++) tb[tn++] = lower_char(s[k]);
        tb[tn] = 0;
        if (!(tn == 1 && tb[0] == '*')) {
            bool ok = (tag_of(tb) == n.tag);
            if (!ok && n.tag_name[0]) ok = ci_eq(tb, n.tag_name);
            if (!ok) return false;
        }
    }
    while (i < sl) {
        const char c = s[i];
        if (c == '.') {
            i++;
            int st = i;
            while (i < sl && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') i++;
            if (!cl_token(n.cls, s + st, i - st)) return false;
        } else if (c == '#') {
            i++;
            int st = i;
            while (i < sl && s[i] != '.' && s[i] != '#' && s[i] != '[' && s[i] != ':') i++;
            int al = i - st;
            if ((int)strlen(n.id) != al) return false;
            for (int k = 0; k < al; k++)
                if (lower_char(n.id[k]) != lower_char(s[st + k])) return false;
        } else if (c == '[') {
            i++;
            int st = i;
            while (i < sl && s[i] != ']') i++;
            int al = i - st;
            if (i < sl) i++;      // skip ']'
            // split on the first operator (=, ~=, ^=, $=, *=, |=)
            int op = 0, vstart = -1;
            for (int k = st; k < st + al; k++) {
                if (s[k] == '=') {
                    op = 1; vstart = k + 1;
                    if (k > st) {
                        char p = s[k - 1];
                        if (p == '~') { op = 2; vstart = k + 1; }
                        else if (p == '^') { op = 3; }
                        else if (p == '$') { op = 4; }
                        else if (p == '*') { op = 5; }
                        else if (p == '|') { op = 6; }
                    }
                    break;
                }
            }
            int nend = (op) ? vstart - 1 : st + al;
            if (op >= 2) nend = vstart - 2;      // drop the operator's first char
            while (nend > st && s[nend - 1] == ' ') nend--;
            char abuf[16];
            const char* av = node_attr(n, s + st, nend - st, abuf, sizeof(abuf));
            if (!op) { if (!av) return false; continue; }
            if (!av) return false;
            int vs = vstart, ve = st + al;
            while (vs < ve && (s[vs] == ' ' || s[vs] == '"' || s[vs] == '\'')) vs++;
            while (ve > vs && (s[ve - 1] == ' ' || s[ve - 1] == '"' || s[ve - 1] == '\'')) ve--;
            int vl = ve - vs;
            int al2 = (int)strlen(av);
            if (op == 1) {
                if (al2 != vl) return false;
                for (int k = 0; k < vl; k++)
                    if (lower_char(av[k]) != lower_char(s[vs + k])) return false;
            } else if (op == 2) {
                if (!cl_token(av, s + vs, vl)) return false;
            } else if (op == 3) {
                if (vl > al2) return false;
                for (int k = 0; k < vl; k++)
                    if (lower_char(av[k]) != lower_char(s[vs + k])) return false;
            } else if (op == 4) {
                if (vl > al2) return false;
                for (int k = 0; k < vl; k++)
                    if (lower_char(av[al2 - vl + k]) != lower_char(s[vs + k])) return false;
            } else if (op == 5) {
                if (vl > al2) return false;
                bool hit = false;
                for (int k = 0; k + vl <= al2 && !hit; k++) {
                    bool eq = true;
                    for (int j = 0; j < vl; j++)
                        if (lower_char(av[k + j]) != lower_char(s[vs + j])) { eq = false; break; }
                    if (eq) hit = true;
                }
                if (!hit) return false;
            } else if (op == 6) {
                // lang|=xx: exact or "xx-..." 
                if (al2 < vl) return false;
                for (int k = 0; k < vl; k++)
                    if (lower_char(av[k]) != lower_char(s[vs + k])) return false;
                if (al2 > vl && av[vl] != '-') return false;
            }
        } else if (c == ':') {
            i++;
            if (i < sl && s[i] == ':') return false;   // pseudo-element: no box
            int st = i;
            while (i < sl && (lower_char(s[i]) >= 'a' && lower_char(s[i]) <= 'z' ||
                              s[i] == '-' || (s[i] >= '0' && s[i] <= '9'))) i++;
            int pl = i - st;
            char pn[24]; int k2 = 0;
            for (int k = st; k < st + pl && k2 < 23; k++) pn[k2++] = lower_char(s[k]);
            pn[k2] = 0;
            // functional form
            const char* arg = 0; int argl = 0;
            if (i < sl && s[i] == '(') {
                int depth = 0, as = i + 1, ae = i + 1;
                for (; ae < sl; ae++) {
                    if (s[ae] == '(') depth++;
                    else if (s[ae] == ')') { if (--depth == 0) break; }
                }
                arg = s + as; argl = ae - as;
                i = (ae < sl) ? ae + 1 : sl;
            }
            if (ci_eq(pn, "root")) {
                if (!(n.tag_name[0] ? ci_eq(n.tag_name, "html") : n.tag == Tag::Root))
                    return false;
            } else if (ci_eq(pn, "first-child") || ci_eq(pn, "last-child") ||
                       ci_eq(pn, "only-child") || ci_eq(pn, "nth-child")) {
                int total = 0, pos = elem_pos(nodes, idx, false, &total);
                if (!pos) return false;
                if (ci_eq(pn, "first-child") && pos != 1) return false;
                if (ci_eq(pn, "last-child") && pos != total) return false;
                if (ci_eq(pn, "only-child") && total != 1) return false;
                if (ci_eq(pn, "nth-child") && !(arg && nth_ok(arg, argl, pos))) return false;
            } else if (ci_eq(pn, "first-of-type") || ci_eq(pn, "last-of-type") ||
                       ci_eq(pn, "nth-of-type")) {
                int total = 0, pos = elem_pos(nodes, idx, true, &total);
                if (!pos) return false;
                if (ci_eq(pn, "first-of-type") && pos != 1) return false;
                if (ci_eq(pn, "last-of-type") && pos != total) return false;
                if (ci_eq(pn, "nth-of-type") && !(arg && nth_ok(arg, argl, pos))) return false;
            } else if (ci_eq(pn, "empty")) {
                for (int k = 0; k < n.kids.size(); k++)
                    if (nodes[n.kids[k]].tag != Tag::Text) return false;
            } else if (ci_eq(pn, "not")) {
                // split the argument on top-level commas: none may match
                int st2 = 0, depth = 0;
                for (int k = 0; k <= argl && arg; k++) {
                    if (k == argl || (arg[k] == ',' && depth == 0)) {
                        int a = st2, b = k;
                        while (a < b && arg[a] == ' ') a++;
                        while (b > a && arg[b - 1] == ' ') b--;
                        if (b > a && match_simple(arg + a, b - a, nodes, idx)) return false;
                        st2 = k + 1;
                    } else if (arg[k] == '(') depth++;
                    else if (arg[k] == ')') depth--;
                }
            } else if (ci_eq(pn, "is") || ci_eq(pn, "where") || ci_eq(pn, "matches")) {
                int st2 = 0, depth = 0; bool any = false;
                for (int k = 0; k <= argl && arg; k++) {
                    if (k == argl || (arg[k] == ',' && depth == 0)) {
                        int a = st2, b = k;
                        while (a < b && arg[a] == ' ') a++;
                        while (b > a && arg[b - 1] == ' ') b--;
                        if (b > a && match_simple(arg + a, b - a, nodes, idx)) any = true;
                        st2 = k + 1;
                    } else if (arg[k] == '(') depth++;
                    else if (arg[k] == ')') depth--;
                }
                if (!any) return false;
            } else {
                // state pseudos (:hover/:focus/:checked/...) or unknown:
                // never match — a transient state must not style the element
                return false;
            }
        } else {
            i++;   // stray character (whitespace inside a simple sequence)
        }
    }
    return true;
}

// One comma-separated alternative: full combinator support. Right-to-left:
// the tail compound must match the node itself, then '>' requires the exact
// parent and ' ' walks the ancestor chain. (+/~ degrade to descendant — the
// sibling-indexed cases they express are rare and low-value.) The old engine
// matched ONLY the tail compound, so ".auto div" hit every <div> on the page
// and ".card h2" styled every h2 — visually random styling everywhere.
static bool css_match_part(const char* part, int len,
                           const List<DomNode>& nodes, int idx) {
    struct Seg { int start, len; char comb; };   // comb = operator on its left
    Seg segs[12];
    int ns = 0, depth = 0, cs = -1;
    char pend = 0;
    for (int k = 0; k <= len; k++) {
        char c = (k < len) ? part[k] : ' ';
        bool sep = false;
        if (depth == 0) {
            if (c == '>' ) { sep = true; pend = '>'; }
            else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') sep = true;
            else if (c == '+' || c == '~') { sep = true; if (!pend) pend = ' '; }
        } else if (c == '(') depth++;
        else if (c == ')') { if (depth > 0) depth--; }
        if (depth == 0 && c == '(') depth++;
        if (sep) {
            if (cs >= 0 && k > cs && ns < 12) {
                segs[ns].start = cs; segs[ns].len = k - cs;
                segs[ns].comb = (ns == 0) ? 0 : pend;
                ns++;
            }
            if (c == '>' && !pend) pend = '>';
            if (!pend) pend = ' ';
            cs = -1;
            // a '>' can be followed directly by its compound
            if (c == '>') cs = -1;
        } else if (cs < 0 && c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '>') {
            cs = k;
        }
    }
    if (ns <= 0) return false;
    // trim trailing whitespace off each compound
    for (int i = 0; i < ns; i++) {
        int b = segs[i].start + segs[i].len;
        while (b > segs[i].start && (part[b-1]==' '||part[b-1]=='\t'||part[b-1]=='\n'||part[b-1]=='\r')) b--;
        segs[i].len = b - segs[i].start;
    }
    int cur = idx;
    for (int i = ns - 1; i >= 0; i--) {
        if (!match_simple(part + segs[i].start, segs[i].len, nodes, cur))
            return false;
        if (i == 0) break;
        if (segs[i].comb == '>') {
            cur = nodes[cur].parent;
            if (cur < 0) return false;
        } else {
            int p = nodes[cur].parent, found = -1;
            while (p >= 0) {
                if (match_simple(part + segs[i-1].start, segs[i-1].len, nodes, p)) {
                    found = p; break;
                }
                p = nodes[p].parent;
            }
            if (found < 0) return false;
            cur = found;
        }
    }
    return true;
}

static bool css_match(const char* sel, const List<DomNode>& nodes, int idx) {
    // Selector group: any comma-separated part may match. Dropping groups
    // outright (the old behaviour) discarded whole resets like
    // "h1, h2, h3 { margin: 0 }".
    int depth = 0;
    const char* part = sel;
    for (const char* q = sel; ; q++) {
        if (*q == '(') depth++;
        else if (*q == ')') { if (depth > 0) depth--; }
        if ((*q == ',' && depth == 0) || *q == 0) {
            int len = (int)(q - part);
            if (css_match_part(part, len, nodes, idx)) return true;
            if (!*q) break;
            part = q + 1;
        }
    }
    return false;
}

// ---- selector index ------------------------------------------------------
// A layout pass used to evaluate every rule against every box. On a bloxd
// page that is 8600 boxes x 1520 rules = 12.7 M full selector evaluations,
// roughly 2.3 s per pass, and the page mutates every frame so it repeats.
//
// The rightmost compound of a selector is a hard prerequisite: when it does
// not match the element, no ancestor or sibling context can rescue the
// selector. So rules are bucketed by the keys that rightmost compound demands
// and a box only evaluates the rules reachable from its own tag / id / classes.
// Selectors whose rightmost compound is anything more exotic (attribute,
// pseudo-class, universal, escape) land in the catch-all bucket and are still
// evaluated for every box - correctness first, speed second.
//
// Cascade order still comes from the rule array itself: the bitmap only
// decides *skip*, the loop still walks 0..css_n in order.

#define NF_CSS_KEYLEN 52
#define NF_CSS_K_ALL '*'
#define NF_CSS_K_TAG 't'
#define NF_CSS_K_CLS 'c'
#define NF_CSS_K_ID  'i'

struct CssKeyEnt { char key[NF_CSS_KEYLEN]; int rule; };

static CssKeyEnt* s_ck = 0;
static int s_ck_n = 0, s_ck_cap = 0;
static uint8_t* s_ck_mark = 0;
static int s_ck_mark_n = 0;
static const CSSRule* s_ck_src = 0;
static int s_ck_src_n = -1;

static int s_ck_cand = 0;   // rules left after skipping (per layout_node)

static void css_key_push(char kind, const char* name, int nlen, int rule) {
    if (s_ck_n >= s_ck_cap) {
        int nc = s_ck_cap ? s_ck_cap * 2 : 4096;
        CssKeyEnt* nn = (CssKeyEnt*)realloc(s_ck, (size_t)nc * sizeof(CssKeyEnt));
        if (!nn) return;
        s_ck = nn;
        s_ck_cap = nc;
    }
    CssKeyEnt& e = s_ck[s_ck_n++];
    int p = 0;
    e.key[p++] = kind;
    for (int i = 0; i < nlen && p < NF_CSS_KEYLEN - 1; i++) {
        char ch = name[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        e.key[p++] = ch;
    }
    e.key[p] = 0;
    e.rule = rule;
}

// Bucket the rightmost compound of one comma-separated group. False means the
// group is not safely bucketable and must be tested against every box.
static bool css_bucket_group(const char* s, int len, int rule) {
    int depth = 0, last = -1;
    for (int i = 0; i < len; i++) {
        char ch = s[i];
        if (ch == '[' || ch == '(') depth++;
        else if (ch == ']' || ch == ')') { if (depth > 0) depth--; }
        else if (depth == 0 && (ch == ' ' || ch == '\t' || ch == '>' || ch == '+' || ch == '~'))
            last = i;
    }
    const int st = last + 1;
    if (st >= len) return false;
    for (int i = st; i < len; i++) {
        char ch = s[i];
        if (ch == ':' || ch == '[' || ch == '(' || ch == '\\' || ch == '*' ||
            ch == ' ' || ch == '>' || ch == '+' || ch == '~')
            return false;
    }
    int added = 0, i = st;
    if (s[i] != '.' && s[i] != '#') {
        int b = i;
        while (i < len && s[i] != '.' && s[i] != '#') i++;
        if (i > b) { css_key_push(NF_CSS_K_TAG, s + b, i - b, rule); added++; }
    }
    while (i < len) {
        const char kind = s[i];
        if (kind != '.' && kind != '#') return false;
        int b = ++i;
        while (i < len && s[i] != '.' && s[i] != '#') i++;
        if (i == b) return false;
        css_key_push(kind == '.' ? NF_CSS_K_CLS : NF_CSS_K_ID, s + b, i - b, rule);
        added++;
    }
    return added > 0;
}

static void css_bucket_rule(const char* sel, int rule) {
    int depth = 0;
    const char* part = sel;
    for (const char* q = sel; ; q++) {
        if (*q == '(') depth++;
        else if (*q == ')') { if (depth > 0) depth--; }
        if ((*q == ',' && depth == 0) || *q == 0) {
            const int len = (int)(q - part);
            if (len > 0 && !css_bucket_group(part, len, rule))
                css_key_push(NF_CSS_K_ALL, "", 0, rule);
            if (!*q) break;
            part = q + 1;
        }
    }
}

static int css_key_cmp(const void* a, const void* b) {
    return strcmp(((const CssKeyEnt*)a)->key, ((const CssKeyEnt*)b)->key);
}

static void css_index_build(const CSSRule* css, int n) {
    if (s_ck_src == css && s_ck_src_n == n) return;
    s_ck_n = 0;
    for (int i = 0; i < n; i++) css_bucket_rule(css[i].sel, i);
    if (s_ck_n > 1) qsort(s_ck, (size_t)s_ck_n, sizeof(CssKeyEnt), css_key_cmp);
    if (s_ck_mark_n < n) {
        uint8_t* m = (uint8_t*)realloc(s_ck_mark, (size_t)n);
        if (m) { s_ck_mark = m; s_ck_mark_n = n; }
    }
    s_ck_src = css;
    s_ck_src_n = n;
}

static void css_mark_key(char kind, const char* name, int nlen) {
    if (s_ck_n <= 0) return;
    char k[NF_CSS_KEYLEN];
    int p = 0;
    k[p++] = kind;
    for (int i = 0; i < nlen && p < NF_CSS_KEYLEN - 1; i++) {
        char ch = name[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        k[p++] = ch;
    }
    k[p] = 0;
    int lo = 0, hi = s_ck_n - 1, hit = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) >> 1;
        const int c = strcmp(s_ck[mid].key, k);
        if (c < 0) lo = mid + 1;
        else if (c > 0) hi = mid - 1;
        else { hit = mid; break; }
    }
    if (hit < 0) return;
    int s0 = hit;
    while (s0 > 0 && strcmp(s_ck[s0 - 1].key, k) == 0) s0--;
    for (int i = s0; i < s_ck_n && strcmp(s_ck[i].key, k) == 0; i++)
        s_ck_mark[s_ck[i].rule] = 1;
}

// Fill the skip bitmap for one box. Returns false when the index cannot be
// built (allocation failure); callers then test every rule, never "no rules".
static bool css_mark_node(const DomNode& n, const CSSRule* css, int css_n) {
    css_index_build(css, css_n);
    if (!s_ck_mark || s_ck_mark_n < css_n) return false;
    memset(s_ck_mark, 0, (size_t)css_n);
    css_mark_key(NF_CSS_K_ALL, "", 0);
    int tl = 0; while (n.tag_name[tl]) tl++;
    if (tl) css_mark_key(NF_CSS_K_TAG, n.tag_name, tl);
    int il = 0; while (n.id[il]) il++;
    if (il) css_mark_key(NF_CSS_K_ID, n.id, il);
    const char* cl = n.cls;
    int i = 0;
    while (cl[i]) {
        while (cl[i] == ' ' || cl[i] == '\t') i++;
        if (!cl[i]) break;
        int b = i;
        while (cl[i] && cl[i] != ' ' && cl[i] != '\t') i++;
        if (i > b) css_mark_key(NF_CSS_K_CLS, cl + b, i - b);
    }
    return true;
}

// ---- long url() payloads -------------------------------------------------
// A stylesheet routinely carries the artwork inside the sheet itself:
//   .icon { background-image: url("data:image/svg+xml,...") }
// Those payloads are kilobytes long, and CSSRule lives in a 512-entry array
// that is copied per tab, so the rule cannot own the string. Long targets are
// interned once in a shared arena; the rule keeps a hash + length and the
// layout pass resolves it back to the bytes.
const int CSSURL_BYTES = 512 * 1024;   // arena for inline background payloads
const int CSSURL_ENT   = 128;          // distinct long urls kept per arena
struct CssUrlEntry { uint32_t hash; int off; int len; };
static char*        g_cssurl_buf  = 0;
static int          g_cssurl_used = 0;
static CssUrlEntry  g_cssurl[CSSURL_ENT];
static int          g_cssurl_n    = 0;

static uint32_t css_url_hash(const char* s, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h ? h : 1u;
}
// Drops every payload. Callers holding a hash simply stop resolving (no
// background) instead of reading freed memory, so this is always safe.
static void css_url_reset() { g_cssurl_used = 0; g_cssurl_n = 0; }

static uint32_t css_url_intern(const char* s, int n) {
    if (!s || n <= 0 || n >= CSSURL_BYTES) return 0;
    const uint32_t h = css_url_hash(s, n);
    for (int i = 0; i < g_cssurl_n; i++)
        if (g_cssurl[i].hash == h && g_cssurl[i].len == n &&
            memcmp(g_cssurl_buf + g_cssurl[i].off, s, (size_t)n) == 0)
            return h;
    if (!g_cssurl_buf) {
        g_cssurl_buf = (char*)kalloc((uint32_t)CSSURL_BYTES);
        if (!g_cssurl_buf) return 0;
    }
    if (g_cssurl_n >= CSSURL_ENT || g_cssurl_used + n + 1 > CSSURL_BYTES) {
        // Sites that inline every asset as a data: URI can outgrow the arena.
        // Start over rather than refuse the new sheet; older rules just lose
        // their artwork.
        klogf("browser: css url arena full (%d bytes), recycling\n", g_cssurl_used);
        css_url_reset();
    }
    if (g_cssurl_used + n + 1 > CSSURL_BYTES) return 0;
    CssUrlEntry& e = g_cssurl[g_cssurl_n++];
    e.hash = h; e.off = g_cssurl_used; e.len = n;
    memcpy(g_cssurl_buf + g_cssurl_used, s, (size_t)n);
    g_cssurl_buf[g_cssurl_used + n] = 0;      // NUL-terminated: callers use strlen
    g_cssurl_used += n + 1;
    return h;
}
static const char* css_url_get(uint32_t hash, int len) {
    if (!hash || len <= 0) return 0;
    for (int i = 0; i < g_cssurl_n; i++)
        if (g_cssurl[i].hash == hash && g_cssurl[i].len == len)
            return g_cssurl_buf + g_cssurl[i].off;
    return 0;
}
// Scrub space for one style="" attribute. Inline backgrounds are as long as
// the sheet ones, so they get their own buffer instead of the 96-byte
// scratch field every other attribute shares.
static char g_inl_style[16384];

// Extract the target of a url(...) from a declaration value. The value is
// passed as a span because it may be tens of kilobytes long; short targets go
// into the rule's own field, long ones into the arena.
static void css_bg_span(const char* v, int n, CSSRule& r) {
    r.bgimg[0] = 0; r.bg_hash = 0; r.bg_len = 0;
    for (int k = 0; k + 3 < n; k++) {
        if (lower_char(v[k]) == 'u' && lower_char(v[k+1]) == 'r' &&
            lower_char(v[k+2]) == 'l' && v[k+3] == '(') {
            int p = k + 4;
            while (p < n && (v[p]==' ' || v[p]=='\t' || v[p]=='\n')) p++;
            char q = 0;
            if (p < n && (v[p]=='"' || v[p]=='\'')) { q = v[p]; p++; }
            int s = p;
            while (p < n) {
                if (q) { if (v[p] == q) break; }
                else if (v[p] == ')') break;
                p++;
            }
            int ul = p - s;
            while (ul > 0 && (v[s]==' ' || v[s]=='\t' || v[s]=='\n')) { s++; ul--; }
            while (ul > 0 && (v[s+ul-1]==' ' || v[s+ul-1]=='\t' || v[s+ul-1]=='\n')) ul--;
            if (ul <= 0) return;
            if (ul < (int)sizeof(r.bgimg)) {
                memcpy(r.bgimg, v + s, (size_t)ul);
                r.bgimg[ul] = 0;
            } else {
                r.bg_hash = css_url_intern(v + s, ul);
                r.bg_len = r.bg_hash ? ul : 0;
            }
            return;
        }
    }
}

// ---- CSS custom properties (var()) -----------------------------------------
// Modern sheets (baidu, bilibili, github, ...) keep their spacing and sizes in
// :root custom properties and reference them everywhere ("padding:var(--x)").
// A parser that only understands literal px treats those values as garbage and
// drops the rule, which is exactly what happened on baidu: 250 KB of style text
// produced 117 usable rules and a page with no layout at all. The values are
// static for one document, so resolve them at parse time, not at layout time.
// Also: "rem" resolves against html{font-size}, not a fixed 16px — baidu sets
// html{font-size:100px} and sizes everything in rem, so 0.14rem must be 14px,
// not 2px.
struct CssVarDef { char name[40]; char val[96]; };
// Modern sheets (github primer, apple, microsoft) define several hundred
// custom properties per theme; 160 slots silently dropped the rest, and every
// rule that referenced a dropped one lost its value. 1024 slots is ~140 KB
// static and covers the biggest sheets measured so far.
static CssVarDef g_css_vars[1024];
static int g_css_var_n = 0;
static const char* g_css_cur_sel = "";

static void css_vars_reset() { g_css_var_n = 0; g_css_root_font = 16; }

static void css_var_set(const char* name, const char* val) {
    int n = (int)strlen(name);
    if (n <= 0 || n >= 40) return;
    for (int i = 0; i < g_css_var_n; i++) {
        if (strcmp(g_css_vars[i].name, name) == 0) {
            clip_str(g_css_vars[i].val, sizeof(g_css_vars[i].val), val);
            return;
        }
    }
    if (g_css_var_n >= (int)(sizeof(g_css_vars) / sizeof(g_css_vars[0]))) return;
    clip_str(g_css_vars[g_css_var_n].name, sizeof(g_css_vars[g_css_var_n].name), name);
    clip_str(g_css_vars[g_css_var_n].val, sizeof(g_css_vars[g_css_var_n].val), val);
    g_css_var_n++;
}

static const char* css_var_get(const char* name) {
    for (int i = 0; i < g_css_var_n; i++)
        if (strcmp(g_css_vars[i].name, name) == 0) return g_css_vars[i].val;
    return 0;
}

// Replace var(--name[, fallback]) in a declaration value. Returns the output
// length. Nesting is handled (the replacement is itself re-scanned once).
static int css_var_subst(const char* in, int n, char* out, int cap) {
    int oi = 0, i = 0;
    while (i < n && oi < cap - 1) {
        if ((in[i] == 'v' || in[i] == 'V') && i + 3 < n &&
            (in[i+1] == 'a' || in[i+1] == 'A') && (in[i+2] == 'r' || in[i+2] == 'R') &&
            in[i+3] == '(') {
            int j = i + 4, st = j, depth = 1, comma = -1;
            while (j < n && depth > 0) {
                if (in[j] == '(') depth++;
                else if (in[j] == ')') depth--;
                else if (in[j] == ',' && depth == 1 && comma < 0) comma = j;
                if (depth > 0) j++;
            }
            int ne = (comma >= 0 && comma < j) ? comma : j;
            char nm[48]; int nml = ne - st;
            if (nml > 47) nml = 47;
            for (int k = 0; k < nml; k++) nm[k] = in[st + k];
            nm[nml] = 0;
            // trim
            int a2 = 0, b2 = nml;
            while (a2 < b2 && (nm[a2] == ' ')) a2++;
            while (b2 > a2 && (nm[b2-1] == ' ')) b2--;
            nm[b2] = 0;
            const char* v = css_var_get(nm + a2);
            if (v) {
                for (const char* c = v; *c && oi < cap - 1; c++) out[oi++] = *c;
            } else if (comma >= 0) {
                for (int k = comma + 1; k < j && oi < cap - 1; k++) out[oi++] = in[k];
            }
            i = (j < n) ? j + 1 : n;
            continue;
        }
        out[oi++] = in[i++];
    }
    out[oi] = 0;
    return oi;
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
        if (i < len && d[i] == ':') i++;
        while (i < len && (d[i]==' '||d[i]=='\t'||d[i]=='\n'||d[i]=='\r'||d[i]==':')) i++;
        // The value runs to the next top-level ';'. Parentheses and quotes are
        // tracked: a data: URI carries a ';' of its own
        // ("data:image/svg+xml;base64,...") and splitting there would leave the
        // parser staring at garbage such as "base64,..." as a property name.
        int vs = i, depth = 0; char qc = 0;
        while (i < len) {
            const char c = d[i];
            if (qc) { if (c == qc) qc = 0; }
            else if (c == '"' || c == '\'') qc = c;
            else if (c == '(') depth++;
            else if (c == ')') { if (depth > 0) depth--; }
            else if (c == ';' && depth == 0) break;
            i++;
        }
        int ve = i;
        if (i < len) i++;                       // skip the ';'
        while (vs < ve && (d[vs]==' '||d[vs]=='\t'||d[vs]=='\n'||d[vs]=='\r')) vs++;
        while (ve > vs && (d[ve-1]==' '||d[ve-1]=='\t'||d[ve-1]=='\n'||d[ve-1]=='\r')) ve--;
        // Short copy for the scalar properties; url() targets are read straight
        // from the span because they can be tens of kilobytes long.
        char ev[256];
        int en = css_var_subst(d + vs, ve - vs, ev, (int)sizeof(ev) - 1);
        // "!important" carries no value: cut it before anything is parsed
        for (int ei = 0; ei + 10 <= en; ei++) {
            if (ev[ei] == '!' && (ev[ei+1] == 'i' || ev[ei+1] == 'I')) { en = ei; break; }
        }
        while (en > 0 && (ev[en-1] == ' ' || ev[en-1] == '\t')) en--;
        ev[en] = 0;
        char val[64]; int vn = 0;
        while (vn < 63 && vn < en) { val[vn] = ev[vn]; vn++; }
        val[vn] = 0;
        // custom property definition: remembered for later rules of this sheet
        if (prop[0] == '-' && prop[1] == '-') {
            css_var_set(prop, ev);
            continue;
        }
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
            if (ci_eq(val, "none")) { r.none = true; r.disp = 1; }
            else if (ci_eq(val, "flex") || ci_eq(val, "inline-flex")) { r.disp_flex = 1; r.disp = 3; }
            else if (ci_eq(val, "grid") || ci_eq(val, "inline-grid")) { r.disp_grid = 1; r.disp = 4; }
            else if (ci_eq(val, "contents")) { r.disp_contents = 1; r.disp = 2; }
            else r.disp = 2;    // block / inline / table: visible
        } else if (ci_eq(prop, "visibility")) {
            if (ci_eq(val, "hidden") || ci_eq(val, "collapse")) { r.none = true; r.disp = 1; }
            else if (ci_eq(val, "visible")) r.disp = 2;
        } else if (ci_eq(prop, "grid-template-columns") ||
                   ci_eq(prop, "grid-template") || ci_eq(prop, "grid-auto-columns")) {
            css_grid_cols(d + vs, ve - vs, r);
        } else if (ci_eq(prop, "grid-column") || ci_eq(prop, "grid-area")) {
            // span N: leave placement to order, only the count matters
            if (r.grid_cols < 1) r.grid_cols = 1;
        } else if (ci_eq(prop, "transform")) {
            // translate(-50%,-50%) is how modals and centred overlays place
            // themselves; without it they hang off the corner.
            int p = 0;
            while (p + 8 < vn) {
                if (lower_char(val[p]) == 't' && lower_char(val[p+1]) == 'r' &&
                    lower_char(val[p+2]) == 'a' && lower_char(val[p+3]) == 'n' &&
                    lower_char(val[p+4]) == 's' && lower_char(val[p+5]) == 'l' &&
                    lower_char(val[p+6]) == 'a' && lower_char(val[p+7]) == 't') {
                    int q = p + 8;
                    while (q < vn && val[q] != '(') q++;
                    int a = q + 1, b = a, cnt = 0;
                    while (b <= vn && cnt < 2) {
                        if (b == vn || val[b] == ',' || val[b] == ')') {
                            int x = a, y = b;
                            while (x < y && (val[x] == ' ' || val[x] == '\t')) x++;
                            int sgn = 1, k = x;
                            if (k < y && val[k] == '-') { sgn = -1; k++; }
                            int iv = 0; bool any = false;
                            while (k < y && val[k] >= '0' && val[k] <= '9') { iv = iv * 10 + (val[k] - '0'); k++; any = true; }
                            if (any && k < y && val[k] == '%') {
                                if (cnt == 0) r.tr_pct_x = sgn * iv;
                                else r.tr_pct_y = sgn * iv;
                            }
                            cnt++; a = b + 1;
                            if (b == vn || val[b] == ')') break;
                        }
                        b++;
                    }
                    break;
                }
                p++;
            }
        } else if (ci_eq(prop, "flex-basis")) {
            Len L;
            if (len_expr(val, (int)strlen(val), L)) {
                if (L.pct > 0) { r.width_pct = L.pct; r.width_off = L.px; }
                else if (L.px > 0) r.width = L.px;
            }
        } else if (ci_eq(prop, "visibility")) {
            // bloxd's LoadingOverlay hides itself with inline
            // visibility:hidden + opacity:0; without this the overlay paints
            // on top of the fully rendered home screen.
            if (ci_eq(val, "hidden") || ci_eq(val, "collapse")) r.none = true;
        } else if (ci_eq(prop, "position")) {
            // fixed/absolute are out-of-flow: the layout engine paints the
            // subtree against the viewport instead of stacking it (bloxd's
            // overlays are fixed full-screen boxes; painting them in-flow
            // buried the whole home screen)
            if (ci_eq(val, "fixed") || ci_eq(val, "absolute")) r.pos_fixed = 1;
        } else if (ci_eq(prop, "float")) {
            // float:left/right is how traditional portals (sina / 163 / ifeng
            // / sohu) lay out their nav bars and news columns; without it
            // every column stacks vertically into a left sliver.
            if (ci_eq(val, "left")) r.fl = 1;
            else if (ci_eq(val, "right")) r.fl = 2;
        } else if (ci_eq(prop, "top")) {
            // signed: a negative top is a real offset (a relative box pulled up)
            if (strchr(val, '%')) r.pos_top_pct = atoi(val);
            else r.pos_top = (val[0] == '-' || val[0] == '+' || (val[0] >= '0' && val[0] <= '9')) ? css_px(val) : kPosNone;
        } else if (ci_eq(prop, "bottom")) {
            // Cookie banners / footers pin themselves with bottom:N; without
            // this they paint at the current flow y and stack on top of each
            // other (deepseek's footer text overlapped into one blob).
            r.pos_bottom = (val[0] == '-' || val[0] == '+' || (val[0] >= '0' && val[0] <= '9')) ? css_px(val) : kPosNone;
        } else if (ci_eq(prop, "left")) {
            // "left:50%" is the classic centring idiom (with a negative
            // margin-left to pull the box back over itself): keep the
            // percentage, since px-only parsing silently dropped it.
            if (strchr(val, '%')) r.pos_left_pct = atoi(val);
            else r.pos_left = (val[0] == '-' || val[0] == '+' || (val[0] >= '0' && val[0] <= '9')) ? css_px(val) : kPosNone;
        } else if (ci_eq(prop, "z-index")) {
            if (val[0] >= '0' && val[0] <= '9') r.z_index = atoi(val);
        } else if (ci_eq(prop, "inset")) {
            // "0" or "0 auto 0 0"-style shorthand: use the first value for all
            if (val[0] >= '0' && val[0] <= '9') r.pos_top = r.pos_left = atoi(val);
        } else if (ci_eq(prop, "opacity")) {
            // "0", "0.0", "0.000" all mean fully transparent -> skip painting
            const char* p = val;
            while (*p == ' ' || *p == '\t') p++;
            if (p[0] == '0') {
                bool allzero = true;
                for (const char* q = p + 1; *q; q++)
                    if (*q != '0' && *q != '.') { allzero = false; break; }
                if (allzero) r.none = true;
            }
        } else if (ci_eq(prop, "flex-direction")) {
            if (ci_eq(val, "column")) r.flex_dir = 1;
        } else if (ci_eq(prop, "gap") || ci_eq(prop, "column-gap") ||
                   ci_eq(prop, "row-gap")) {
            int v = css_px(val);
            if (v > 0) r.gap = v;
        } else if (ci_eq(prop, "justify-content")) {
            if (ci_eq(val, "center")) r.justify = 1;
            else if (ci_eq(val, "flex-end") || ci_eq(val, "end")) r.justify = 2;
            else if (ci_eq(val, "space-between")) r.justify = 3;
        } else if (ci_eq(prop, "align-items")) {
            if (ci_eq(val, "center")) r.align_i = 1;
            else if (ci_eq(val, "flex-end") || ci_eq(val, "end")) r.align_i = 2;
        } else if (ci_eq(prop, "flex-wrap")) {
            if (ci_eq(val, "wrap")) r.flex_wrap = 1;
        } else if (ci_eq(prop, "flex-grow")) {
            int v = css_px(val);
            if (v > 0) r.flex_grow = v;
        } else if (ci_eq(prop, "flex-shrink")) {
            int v = css_px(val);
            if (v >= 0) r.flex_shrink = v;
        } else if (ci_eq(prop, "flex")) {
            // shorthand: "flex: 1" -> grow=1 + shrink=1 (basis unsupported;
            // the kid keeps its measured size and only grows)
            int v = css_px(val);
            if (v > 0) { r.flex_grow = v; if (r.flex_shrink < 0) r.flex_shrink = 1; }
        } else if (ci_eq(prop, "background-color")) {
            uint32_t c = css_color(val);
            if (c) r.bg = c;
        } else if (ci_eq(prop, "background-image")) {
            css_bg_span(d + vs, ve - vs, r);
            css_grad_span(d + vs, ve - vs, r);
        } else if (ci_eq(prop, "background")) {
            css_bg_span(d + vs, ve - vs, r);
            css_grad_span(d + vs, ve - vs, r);
            uint32_t c = css_color(val);
            if (c) r.bg = c;
        } else if (ci_eq(prop, "text-align")) {
            if (ci_eq(val, "center")) r.align = 1;
            else if (ci_eq(val, "right")) r.align = 2;
        } else if (ci_eq(prop, "margin")) {
            // shorthand: first value is the top margin; "0 auto" (horizontal
            // centring) is the common idiom and carries no vertical offset
            int v = css_px(val);
            if (v != 0) r.margin = v;
        } else if (ci_eq(prop, "margin-left")) {
            r.mar_l = css_px(val);
        } else if (ci_eq(prop, "margin-right")) {
            r.mar_r = css_px(val);
        } else if (ci_eq(prop, "margin-bottom")) {
            r.mar_b = css_px(val);
        } else if (ci_eq(prop, "margin-top")) {
            int v = css_px(val);
            if (v != 0) r.margin = v;
        } else if (ci_eq(prop, "padding")) {
            int v = css_px(val);
            if (v > 0) r.padding = v;
        } else if (ci_eq(prop, "width")) {
            Len L;
            if (len_expr(val, (int)strlen(val), L)) {
                if (L.pct) { r.width_pct = L.pct; r.width_off = L.px; }
                else if (L.px > 0) r.width = L.px;
                if (L.cap > 0) r.max_w = L.cap;
                if (L.floor > 0) r.min_w = L.floor;
            }
        } else if (ci_eq(prop, "height")) {
            Len L;
            if (len_expr(val, (int)strlen(val), L)) {
                if (L.pct) { r.height_pct = L.pct; }
                else if (L.px > 0) r.height = L.px;
                if (L.cap > 0) r.max_h = L.cap;
            }
        } else if (ci_eq(prop, "max-width") || ci_eq(prop, "max-inline-size")) {
            Len L;
            if (len_expr(val, (int)strlen(val), L)) {
                if (L.pct) { r.max_w_pct = L.pct; r.max_w_off = L.px; }
                else if (L.px > 0) r.max_w = L.px;
            }
        } else if (ci_eq(prop, "min-width")) {
            Len L;
            if (len_expr(val, (int)strlen(val), L)) {
                if (L.px > 0) r.min_w = L.px;
                else if (L.pct && !r.min_w) r.min_w = 1;   // "min-width:100%" — keep as set
            }
        } else if (ci_eq(prop, "min-height")) {
            Len L;
            if (len_expr(val, (int)strlen(val), L)) {
                if (L.px > 0) r.min_h = L.px;
                else if (L.pct) r.min_h = -1;   // %-of-auto: keep as set
            }
        } else if (ci_eq(prop, "max-height")) {
            Len L;
            if (len_expr(val, (int)strlen(val), L)) {
                if (L.px > 0) r.max_h = L.px;
                else if (L.pct && !r.max_h) r.max_h = 1;
            }
        } else if (ci_eq(prop, "box-sizing")) {
            if (ci_eq(val, "border-box")) r.box_border = 1;
        } else if (ci_eq(prop, "white-space")) {
            if (ci_eq(val, "nowrap") || ci_eq(val, "pre")) r.nowrap = 1;
        } else if (ci_eq(prop, "text-decoration") ||
                   ci_eq(prop, "text-decoration-line")) {
            // the shorthand also carries colour/style ("underline dotted red")
            const char* p = val;
            bool has = false, non = false;
            while (*p) {
                while (*p == ' ') p++;
                if (ci_starts(p, "underline") || ci_starts(p, "line-through") ||
                    ci_starts(p, "overline")) { has = true; }
                if (ci_starts(p, "none")) { non = true; }
                while (*p && *p != ' ') p++;
            }
            if (non) r.under = 2; else if (has) r.under = 1;
        } else if (ci_eq(prop, "overflow") || ci_eq(prop, "overflow-x") ||
                   ci_eq(prop, "overflow-y")) {
            if (ci_eq(val, "hidden") || ci_eq(val, "clip") || ci_eq(val, "auto")) r.clip = 1;
        } else if (ci_eq(prop, "border-radius")) {
            // "12px", "12px 8px" (first value wins), "50%" (pill handled at paint)
            const char* pc = strchr(val, '%');
            if (pc && pc == val + strlen(val) - 1) r.radius = -1;   // percent: pill
            else {
                int v = css_px(val);
                if (v > 0) r.radius = v;
            }
        } else if (ci_eq(prop, "right")) {
            if (val[0] == '-' || val[0] == '+' || (val[0] >= '0' && val[0] <= '9')) r.pos_right = css_px(val);
        } else if (ci_eq(prop, "border") || ci_eq(prop, "border-top") ||
                   ci_eq(prop, "border-bottom") || ci_eq(prop, "border-left") ||
                   ci_eq(prop, "border-right")) {
            int v = css_px(val);
            if (v > 0) r.border_w = v;
            uint32_t bc = css_border_col(val);
            if (bc) r.border_col = bc;
        } else if (ci_eq(prop, "border-width")) {
            int v = css_px(val);
            if (v > 0) r.border_w = v;
        } else if (ci_eq(prop, "border-color")) {
            uint32_t bc = css_color(val);
            if (bc) r.border_col = bc;
        } else if (ci_eq(prop, "line-height")) {
            int v = css_px(val);
            if (v > 0) r.line_h = v;
        }
    }
}

// ---- media queries -------------------------------------------------------// A sheet's conditional blocks decide whether the desktop layout exists at
// all. Two ways to get this wrong, both seen on baidu: ignoring the condition
// (the mobile block then hides everything at desktop width), and treating an
// unknown feature as true (baidu ships
//   @media screen and (-ms-high-contrast:active),(-webkit-min-device-pixel-ratio:2)
// to hide the legacy logo, which is FALSE on a normal 1x display — accepting
// it removed the logo, the search wrapper and the top-right links).
// So: evaluate the features we know, answer FALSE for the ones we do not
// (they are almost always platform-specific opt-outs), OR across the comma
// list, AND inside one query.
static bool css_media_feature(const char* f, int n) {
    char b[96]; int k = 0;
    for (int i = 0; i < n && k < 95; i++) {
        char ch = f[i];
        b[k++] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
    }
    b[k] = 0;
    while (k > 0 && (b[k-1] == ' ')) b[--k] = 0;
    int sp = 0; while (sp < k && b[sp] == ' ') sp++;
    char name[40]; int nn = 0;
    int i2 = sp;
    while (i2 < k && b[i2] != ':' && nn < 39) name[nn++] = b[i2++];
    name[nn] = 0;
    while (nn > 0 && name[nn-1] == ' ') name[--nn] = 0;
    char val[40]; val[0] = 0;
    if (i2 < k && b[i2] == ':') {
        int j = i2 + 1, vn = 0;
        while (j < k && b[j] == ' ') j++;
        while (j < k && vn < 39) val[vn++] = b[j++];
        val[vn] = 0;
        while (vn > 0 && val[vn-1] == ' ') val[--vn] = 0;
    }
    // The media-query viewport must be correct AT CSS PARSE TIME: rules are
    // filtered while the sheet is read, long before the first paint assigns
    // s_view_w. Reading a stale 0 there fell back to 780x560 and threw away
    // every mobile branch (bloxd's max-width:667px card grid among them), so
    // the phone layout never existed at all. Resolve it lazily from the
    // override / window, then from whatever paint has recorded since.
    static int mq_vw = -2, mq_vh = -2;
    if (mq_vw == -2) {
        const char* a = getenv("NF_VIEW_W"); const char* b = getenv("NF_VIEW_H");
        mq_vw = (a && a[0]) ? atoi(a) : 0;
        mq_vh = (b && b[0]) ? atoi(b) : 0;
    }
    int vw = (mq_vw > 50 && mq_vw < 4096) ? mq_vw : (s_view_w > 100 ? s_view_w : 780);
    int vh = (mq_vh > 50 && mq_vh < 4096) ? mq_vh : (s_view_h > 100 ? s_view_h : 560);
    // width / height, with the min-/max- prefixes
    bool is_min = true; const char* dim = name;
    if (ci_starts(name, "min-")) { is_min = true; dim = name + 4; }
    else if (ci_starts(name, "max-")) { is_min = false; dim = name + 4; }
    if (ci_eq(dim, "width") || ci_eq(dim, "device-width")) {
        int v = atoi(val);
        return is_min ? (vw >= v) : (vw <= v);
    }
    if (ci_eq(dim, "height") || ci_eq(dim, "device-height")) {
        int v = atoi(val);
        return is_min ? (vh >= v) : (vh <= v);
    }
    // pixel density: this build renders at 1x
    if (ci_eq(dim, "device-pixel-ratio") || ci_eq(dim, "resolution")) {
        double v = 0; bool any = false;
        for (const char* p = val; *p; p++) {
            if (*p >= '0' && *p <= '9') { any = true; break; }
        }
        if (!any) return false;
        double dpr = 1.0;
        if (strstr(val, "dpi")) dpr = 96.0;
        else if (strstr(val, "dpcm")) dpr = 37.8;
        v = atof(val);
        double cur = 1.0;
        if (strstr(val, "dpi")) cur = 96.0;
        else if (strstr(val, "dpcm")) cur = 37.8;
        return is_min ? (cur >= v) : (cur <= v);
    }
    if (ci_eq(name, "orientation")) {
        return ci_eq(val, "landscape") ? (vw >= vh) : (vw < vh);
    }
    if (ci_eq(name, "hover") || ci_eq(name, "any-hover")) return ci_eq(val, "hover");
    if (ci_eq(name, "pointer") || ci_eq(name, "any-pointer"))
        return ci_eq(val, "fine");
    if (ci_eq(name, "prefers-color-scheme")) return ci_eq(val, "light");
    if (ci_eq(name, "prefers-reduced-motion")) return ci_eq(val, "no-preference");
    if (ci_eq(name, "min-aspect-ratio") || ci_eq(name, "max-aspect-ratio")) {
        // "16/9": compare against the viewport ratio, loosely
        int a = atoi(val), b2 = 0;
        const char* sl = strchr(val, '/');
        if (sl) b2 = atoi(sl + 1);
        if (!a || !b2) return true;
        bool min_ok = (vw * b2 >= vh * a);
        bool max_ok = (vw * b2 <= vh * a);
        return ci_starts(name, "min-") ? min_ok : max_ok;
    }
    // unknown / platform-specific (-ms-high-contrast, -webkit-...): do not apply
    return false;
}

static bool css_media_query(const char* q, int n) {
    char type[24]; type[0] = 0;
    int i = 0;
    while (i < n && (q[i] == ' ' || q[i] == '\t')) i++;
    int tn = 0;
    while (i < n && ((q[i] >= 'a' && q[i] <= 'z') || (q[i] >= 'A' && q[i] <= 'Z') ||
                     q[i] == '-') && tn < 23) {
        char ch = q[i];
        type[tn++] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
        i++;
    }
    type[tn] = 0;
    if (ci_eq(type, "print") || ci_eq(type, "speech")) return false;
    if (ci_eq(type, "only")) {
        // "only screen" — read the real type after it
        int tn2 = 0; char t2[24];
        while (i < n && q[i] == ' ') i++;
        while (i < n && ((q[i] >= 'a' && q[i] <= 'z') || (q[i] >= 'A' && q[i] <= 'Z')) && tn2 < 23) {
            t2[tn2++] = (char)((q[i] >= 'A' && q[i] <= 'Z') ? (q[i] - 'A' + 'a') : q[i]);
            i++;
        }
        t2[tn2] = 0;
        if (ci_eq(t2, "print") || ci_eq(t2, "speech")) return false;
    }
    // every parenthesised feature must hold
    while (i < n) {
        while (i < n && q[i] != '(') i++;
        if (i >= n) break;
        int st = i + 1, d = 1, j = st;
        while (j < n && d > 0) {
            if (q[j] == '(') d++;
            else if (q[j] == ')') d--;
            if (d > 0) j++;
        }
        if (!css_media_feature(q + st, j - st)) return false;
        i = j + 1;
    }
    return true;
}

static bool css_media_ok(const char* c, int n) {
    int i = 0, st = 0, d = 0;
    for (i = 0; i <= n; i++) {
        if (i == n || (c[i] == ',' && d == 0)) {
            if (css_media_query(c + st, i - st)) return true;
            st = i + 1;
        } else if (c[i] == '(') d++;
        else if (c[i] == ')') { if (d > 0) d--; }
    }
    return false;
}

// Parse a full stylesheet: "selector { decls }" repeated. Returns rule count.
static int css_parse(const char* css, int len, CSSRule* out, int max,
                     bool vars_only = false) {
    int n = 0, i = 0;
    if (s_mq_dbg_env < 0) {
        const char* e = getenv("NF_DUMP_MQ");
        s_mq_dbg_env = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    const bool s_mq_dbg = s_mq_dbg_env != 0;
    while (i < len && (vars_only || n < max)) {
        while (i < len && (css[i]==' '||css[i]=='\n'||css[i]=='\t'||css[i]=='\r')) i++;
        if (i >= len) break;
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {   // comment
            int e = i + 2;
            while (e + 1 < len && !(css[e]=='*' && css[e+1]=='/')) e++;
            i = (e + 1 < len) ? e + 2 : len;
            continue;
        }
        if (css[i] == '}' || css[i] == ';') { i++; continue; }
        // At-rules. Only @media and @supports wrap ordinary rules; the others
        // (@keyframes, @font-face, @page, @charset, @import) carry content of
        // their own kind and must be skipped whole — treating their insides as
        // style rules produced garbage selectors and burned rule slots.
        if (css[i] == '@') {
            int j = i + 1, ns = j;
            while (j < len && ((css[j] >= 'a' && css[j] <= 'z') ||
                               (css[j] >= 'A' && css[j] <= 'Z') || css[j] == '-')) j++;
            int nl = j - ns;
            int st = j;
            while (st < len && css[st] != '{' && css[st] != ';') st++;
            if (st >= len) break;
            if (css[st] == ';') { i = st + 1; continue; }   // @charset / @import ...
            bool is_med = (nl == 5 && ci_starts(css + ns, "media"));
            bool is_sup = (nl == 8 && ci_starts(css + ns, "supports"));
            // @layer is a cascade-order wrapper, not an at-rule with its own
            // content: skipping it whole threw away every rule inside
            // ("@layer primer-css-base{...}"), which is how github's and
            // apple's component styles vanished while the sheets looked parsed.
            bool is_layer = (nl == 5 && ci_starts(css + ns, "layer"));
            if (is_med && s_mq_dbg) {
                bool okq = css_media_ok(css + j, st - j);
                klogf("cssmq: %s  [%.70s]", okq ? "MATCH  " : "no-match", css + j);
            }
            if ((is_med && !css_media_ok(css + j, st - j)) || nl <= 0) {
                int d2 = 1, q2 = st + 1;
                while (q2 < len && d2 > 0) {
                    if (css[q2] == '{') d2++;
                    else if (css[q2] == '}') d2--;
                    q2++;
                }
                i = q2; continue;
            }
            if (is_med || is_sup || is_layer) { i = st + 1; continue; }  // enter the block
            int d2 = 1, q2 = st + 1;                         // anything else: skip
            while (q2 < len && d2 > 0) {
                if (css[q2] == '{') d2++;
                else if (css[q2] == '}') d2--;
                q2++;
            }
            i = q2; continue;
        }
        // Long selector text is normal on framework sites (":where(a,b,c) > x"
        // chains run past 200 chars). The cap only bounds what we KEEP; the
        // scan must still reach the '{' or the rest of the sheet is parsed as
        // one giant selector.
        char sel[384]; int sn = 0;
        while (i < len && css[i] != '{') {
            if (css[i] != '\n' && css[i] != '\r' && sn < 383) sel[sn++] = css[i];
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
        // html{font-size:100px} is what 1rem means for the rest of the sheet;
        // it must be picked up before the rules that use rem are parsed.
        if (r.font_size > 0 && r.sel[0]) {
            const char* sp = r.sel;
            while (*sp == ' ') sp++;
            if (ci_starts(sp, "html") || ci_starts(sp, ":root")) g_css_root_font = r.font_size;
        }
        {
            static int css_dbg = -1;
            if (css_dbg < 0) {
                const char* e = getenv("NF_CSS_DBG");
                css_dbg = (e && e[0] && e[0] != '0') ? 1 : 0;
            }
            if (css_dbg && r.bg)
                klogf("cssdbg: bg=%06x sel=%.90s", r.bg & 0xFFFFFF, r.sel);
        }
        if (vars_only) continue;     // pass 1: definitions only, no rule slots
        if (r.sel[0] && (r.color || r.font_size || r.bold_set || r.none || r.disp || r.bg ||
                        r.align || r.bgimg[0] || r.bg_hash || r.border_w ||
                        r.width > 0 || r.height > 0 ||
                        r.padding > 0 || r.margin > 0 ||
                        r.disp_flex || r.flex_dir || r.gap || r.justify || r.align_i ||
                        r.flex_wrap || r.flex_grow || r.flex_shrink >= 0 ||
                        r.width_pct || r.height_pct || r.width_off ||
                        r.max_w || r.max_w_pct || r.min_w || r.max_h ||
                        r.min_h || r.line_h > 0 ||
                        r.pos_fixed || r.pos_top != kPosNone || r.pos_bottom != kPosNone ||
                        r.pos_left != kPosNone || r.z_index >= 0 ||
                        r.pos_right != kPosNone || r.box_border || r.nowrap ||
                        r.under || r.clip || r.radius ||
                        r.disp_grid || r.grid_cols || r.grid_min ||
                        r.gr_a || r.gr_b || r.tr_pct_x || r.tr_pct_y ||
                        r.fl))
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
    const char* html;   // parse cursor source (points into sstream when streaming)
    int len;
    int pos;
    DomArena dom;
    char tbuf[1024];
    int tlen;
    // worker-side outputs: accumulated <script> text and <style> text
    char* script_buf;
    int* script_len;
    bool in_script_;   // inside <script> body across chunk boundaries
    bool in_skipscript_; // inside a non-JS <script type=...> body (consumed)
    bool in_style_;    // inside a <style> body across chunk boundaries
    bool in_textarea_; // inside a <textarea> body (raw text, may span chunks)
    int tpl_depth_;    // inside <template> subtree across chunk boundaries (0=none)
    // A sheet that inlines artwork (url(data:...)) is worth parsing past the
    // first few rules, so the capture buffer is comfortably larger than a
    // hand-written stylesheet needs.
    char* cssbuf;      // heap: a real site's style text runs to hundreds of KB
    int csscap;
    int csslen;
    // Flushing must land on a rule boundary. A flush taken mid-@media leaves the
    // first half unclosed (the rest of that chunk is dropped) and the second half
    // is then parsed as if it were top level — that is how baidu's IE-only
    // "@media ... { #lg{display:none} }" leaked in and removed the logo.
    int css_depth;
    int css_safe;      // length up to which the captured text is balanced
    CSSRule* css_out;
    int* css_n;
    int css_max;
    // streaming state: owned accumulation buffer; a partial tag/comment tail
    // is held back until the next chunk completes it, so chunk boundaries
    // never split a tag
    char* sstream;
    int scap, slen;
    int ovf_need = 0;  // bytes wanted by the failed stream growth
    char* title_out;    // <title> text sink (tab title)
    int title_cap;
    char (*srcs)[256];   // external <script src> urls (document order)
    int* src_n;
    int src_cap;
    // Document-order script markers: kind 0 = external srcs[idx], kind 1 =
    // inline segment seg_off[idx][0..1] in script_buf. The old "all inline
    // first, then all external" order broke baidu (its head inline scripts
    // use $ before the jQuery bundle loaded).
    uint8_t smark_kind[64];
    uint16_t smark_idx[64];
    int smark_n;
    int seg_off[24][2];
    int seg_n;
    // external <link rel=stylesheet> urls (document order); load_worker
    // fetches each one synchronously and appends the rules to the sheet
    char (*clinks)[256];
    int* clink_n;
    int clink_cap;

    PageParser(char* sbuf0 = 0, int* slen0 = 0, CSSRule* cso = 0,
               int* csn = 0, int csmax = 0,
               char (*srs)[256] = 0, int* srn = 0, int srcap = 0,
               char (*clr)[256] = 0, int* cln = 0, int clcap = 0)
        : html(0), len(0), pos(0), tlen(0), script_buf(sbuf0), script_len(slen0),
          in_script_(false), in_skipscript_(false), in_style_(false), in_textarea_(false), tpl_depth_(0),
          cssbuf(0), csscap(0), csslen(0), css_depth(0), css_safe(0),
          css_out(cso), css_n(csn), css_max(csmax),
          sstream(0), scap(0), slen(0), title_out(0), title_cap(0),
          srcs(srs), src_n(srn), src_cap(srcap),
          clinks(clr), clink_n(cln), clink_cap(clcap),
          smark_n(0), seg_n(0) {}

    ~PageParser() { if (cssbuf) { kfree(cssbuf); cssbuf = 0; } }

    // Parse what has been captured so far into the tab's rule array and start
    // a new buffer. Called at the end of the page and whenever the buffer fills,
    // so a page with more style text than the cap still styles its later blocks
    // instead of losing them silently.
    void css_flush() {
        if (csslen <= 0 || !css_out || !css_n) { csslen = 0; css_depth = 0; css_safe = 0; return; }
        if (*css_n >= css_max) { csslen = 0; css_depth = 0; css_safe = 0; return; }
        // Pass 1 harvests custom-property definitions, pass 2 builds the rules
        // with those values substituted. A single pass resolves only variables
        // defined earlier in the same text, and a sheet that puts its :root
        // block after its first rules (or a framework sheet loaded after the
        // component sheet) lost every var() rule.
        css_parse(cssbuf, csslen, 0, 0, true);
        *css_n += css_parse(cssbuf, csslen, css_out + *css_n, css_max - *css_n);
        csslen = 0; css_depth = 0; css_safe = 0;
    }

    // Append one character of <style> text, keeping the buffer's rule balance.
    void css_push(char c) {
        if (!cssbuf) {
            cssbuf = (char*)kalloc((uint32_t)CSS_TEXT_CAP);
            csscap = cssbuf ? CSS_TEXT_CAP : 0;
        }
        if (csscap <= 0) return;
        if (c == '{') css_depth++;
        else if (c == '}') { if (css_depth > 0) css_depth--; }
        if (csslen >= csscap - 1) {
            int cut = css_safe;                  // only complete top-level rules
            if (cut <= 0) cut = csslen;          // no balanced point: best effort
            if (css_out && css_n && *css_n < css_max) {
                css_parse(cssbuf, cut, 0, 0, true);
                *css_n += css_parse(cssbuf, cut, css_out + *css_n, css_max - *css_n);
            }
            int rest = csslen - cut;
            for (int i2 = 0; i2 < rest; i2++) cssbuf[i2] = cssbuf[cut + i2];
            csslen = rest;
            css_safe = (css_depth == 0) ? csslen : 0;
        }
        cssbuf[csslen++] = c;
        if (css_depth == 0) css_safe = csslen;
    }

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

    void parse() { parse_until(len); }

    // streaming feed: append bytes, parse up to a safe boundary (an unclosed
    // '<' tail is held back), return the number of bytes still pending
    int feed(const char* data, int n, bool final) {
        if (dom.overflow) return 0;
        if (!sstream) {
            scap = 65536;
            sstream = (char*)balloc((size_t)scap);
            if (!sstream) { dom.overflow = true; dom.ovf_src = 31; return 0; }
        }
        if (slen + n > scap) {
            while (slen + n > scap && scap < MAX_PAGE) scap *= 2;
            if (slen + n > scap) { ovf_need = slen + n; dom.overflow = true; dom.ovf_src = 32; return 0; }   // page cap
            char* nb = (char*)balloc((size_t)scap);
            if (!nb) { ovf_need = scap; dom.overflow = true; dom.ovf_src = 33; return 0; }
            memcpy(nb, sstream, (size_t)slen);
            bfree(sstream);
            sstream = nb;
        }
        memcpy(sstream + slen, data, (size_t)n);
        slen += n;
        int limit = slen;
        if (!final) {
            // hold back a partial tag/comment at the tail: the last '<' with
            // no '>' after it stays buffered until the next chunk completes it
            for (int i = slen - 1; i >= 0; i--) {
                if (sstream[i] == '<') {
                    bool closed = false;
                    for (int j = i + 1; j < slen; j++)
                        if (sstream[j] == '>') { closed = true; break; }
                    if (!closed) { limit = i; break; }
                }
            }
        }
        html = sstream;
        parse_until(limit);
        // keep whatever the parser did not consume. Normally that is exactly
        // the held-back tail [limit, slen); the inert-<template> skipper can
        // also stop early (a '<' too close to the boundary to classify), so
        // resume from pos, never from limit — resuming from limit with
        // pos < limit would drop bytes and desync the skip state.
        int rest = slen - pos;
        if (rest > 0 && !dom.overflow)
            memmove(sstream, sstream + pos, (size_t)rest);
        slen = rest;
        pos = 0;
        return rest;
    }

    void skip_until(const char* close_tag) {
        while (pos < len) {
            if (html[pos] == '<' && ci_starts(html + pos + 1, close_tag)) break;
            pos++;
        }
        while (pos < len && html[pos] != '>') pos++;
        if (pos < len) pos++;
    }

    // Find the '>' that really closes an opening tag. Quoted attribute
    // values must be skipped: bloxd.io's ad-recovery <script> carries
    // onload="... (r, o, n) => { ... } ..." and the '>' of the arrow
    // function would otherwise end the tag mid-attribute, leaking the
    // rest of the handler onto the page as visible text nodes.
    int find_tag_end(int from) {
        int p = from;
        while (p < len) {
            char q = html[p];
            if (q == '"' || q == '\'') {
                p++;
                while (p < len && html[p] != q) p++;
                p++;   // past the closing quote (or len)
                continue;
            }
            if (q == '>') return p;
            p++;
        }
        return len;
    }

    void smarks_push(uint8_t kind, uint16_t idx) {
        if (smark_n >= (int)(sizeof(smark_kind)/sizeof(smark_kind[0]))) return;
        smark_kind[smark_n] = kind;
        smark_idx[smark_n] = idx;
        smark_n++;
        if (kind == 1 && idx < 24) {
            seg_off[idx][0] = script_len ? *script_len : 0;
            seg_off[idx][1] = seg_off[idx][0];   // closed at </script>
            seg_n++;
        }
    }

    void parse_until(int limit) {
        len = limit;   // all internal bounds checks use the current limit
        char tagbuf[24];
        // Buffers large enough for real-world URLs: search-result redirect links
        // and modern site URLs routinely exceed 256 bytes, and clipping them
        // here silently breaks navigation. href/src/alt are copied into String
        // fields (unbounded) downstream, so only this scratch size matters.
        // src is far larger than a URL: an inline data: image carries the whole
        // payload in the attribute (the scanner below routes long values here).
        char href[2048], src[8192], alt[256];
        while (pos < len && !dom.overflow) {
            if (in_script_) {
                // continuing a <script> body from a previous chunk: keep
                // collecting until the real </script> (or end of this chunk)
                if (html[pos] == '<' && ci_starts(html + pos + 1, "/script")) {
                    in_script_ = false;
                    if (seg_n > 0 && seg_n - 1 < 24)
                        seg_off[seg_n - 1][1] = script_len ? *script_len : 0;
                    if (script_buf && script_len && *script_len < SCRIPT_MAX - 3) {
                        script_buf[(*script_len)++] = ';';
                        script_buf[(*script_len)++] = '\n';
                    }
                    while (pos < len && html[pos] != '>') pos++;
                    if (pos < len) pos++;
                    continue;
                }
                if (script_buf && script_len && *script_len < SCRIPT_MAX - 1)
                    script_buf[(*script_len)++] = html[pos];
                pos++;
                continue;
            }
            if (in_skipscript_) {
                // continuing a non-JS <script type=...> body (JSON-LD etc.)
                // from a previous chunk: consume until the real </script>
                if (html[pos] == '<' && ci_starts(html + pos + 1, "/script")) {
                    in_skipscript_ = false;
                    while (pos < len && html[pos] != '>') pos++;
                    if (pos < len) pos++;
                    continue;
                }
                pos++;
                continue;
            }
            if (in_textarea_) {
                // raw text body: no markup, no stylesheet, no script inside
                if (html[pos] == '<' && ci_starts(html + pos + 1, "/textarea")) {
                    in_textarea_ = false;
                    dom.pop_to(Tag::Textarea);
                    while (pos < len && html[pos] != '>') pos++;
                    if (pos < len) pos++;
                    continue;
                }
                pos++;
                continue;
            }
            if (in_style_) {
                // continuing a <style> body from a previous chunk
                if (html[pos] == '<' && ci_starts(html + pos + 1, "/style")) {
                    in_style_ = false;
                    // Each <style> block is its own stylesheet: a stray brace in
                    // one block must not shift the parse of the next one. Parsing
                    // per block also keeps @media nesting well formed.
                    css_flush();
                    while (pos < len && html[pos] != '>') pos++;
                    if (pos < len) pos++;
                    continue;
                }
                css_push(html[pos]);
                pos++;
                continue;
            }
            if (tpl_depth_ > 0) {
                // inside an inert <template> subtree (may span chunks): the
                // whole subtree is skipped — never rendered, scripts inside
                // never run (real browsers keep template content in a separate
                // document fragment). bloxd.io hides a huge obfuscated
                // honeypot in one; rendering it buried the page.
                if (html[pos] == '<') {
                    // a '<' within 10 bytes of the chunk boundary cannot be
                    // classified yet — stop and resume when more bytes have
                    // arrived (consuming it here would miss a </template> and
                    // swallow the rest of the page)
                    if (pos + 10 >= len) return;
                    if (ci_starts(html + pos + 1, "template") &&
                        tag_name_end(html[pos + 9])) {
                        tpl_depth_++;   // nested: count it, name flows as skipped text
                        pos++;
                    } else if (ci_starts(html + pos + 1, "/template") &&
                               tag_name_end(html[pos + 10])) {
                        tpl_depth_--;
                        while (pos < len && html[pos] != '>') pos++;
                        if (pos < len) pos++;   // consume "</template>" whole
                    } else {
                        pos++;          // other tag: consume just the '<'
                    }
                } else {
                    pos++;
                }
                continue;
            }
            if (html[pos] != '<') { push_char(html[pos]); pos++; continue; }

            flush_text();
            const int tag_start = pos;   // '<' of the tag being parsed (svg capture)

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

            // <script>: inline text is collected; a src=... attribute is
            // remembered (download + run in document order after parsing)
            if (!closing && t == Tag::Script) {
                int tagend = find_tag_end(pos);
                char srcur[256]; int srn2 = 0; int srl = 0;
                int js_type = 1;   // assume JS unless an explicit non-JS type
                {
                    const char* a = html + pos;
                    int al = tagend - pos;
                    for (int ai = 0; ai + 5 <= al; ai++) {
                        if (ci_starts(a + ai, "src=")) {
                            int q = ai + 4;
                            char quote = 0;
                            while (q < al && (a[q] == ' ' || a[q] == '\t')) q++;
                            if (q < al && (a[q] == '"' || a[q] == '\'')) { quote = a[q]; q++; }
                            int q2 = q;
                            while (q2 < al && a[q2] != '>' && a[q2] != ' ' && a[q2] != '\t' &&
                                   (!quote || a[q2] != quote)) q2++;
                            int ul = q2 - q;
                            if (ul > 0) { if (ul > 255) ul = 255; memcpy(srcur, a + q, (size_t)ul); srcur[ul] = 0; srn2 = 1; }
                            break;
                        }
                    }
                    // non-JS mime types (JSON-LD, templates, ...) must not be
                    // concatenated into the JS program stream
                    for (int ai = 0; ai + 5 <= al; ai++) {
                        if (ci_starts(a + ai, "type=")) {
                            int q = ai + 5;
                            char quote = 0;
                            while (q < al && (a[q] == ' ' || a[q] == '\t')) q++;
                            if (q < al && (a[q] == '"' || a[q] == '\'')) { quote = a[q]; q++; }
                            int q2 = q;
                            while (q2 < al && a[q2] != '>' && a[q2] != ' ' && a[q2] != '\t' &&
                                   (!quote || a[q2] != quote)) q2++;
                            int tl = q2 - q;
                            if (tl > 0) {
                                if (!((tl >= 15 && ci_starts(a + q, "application/javascript")) ||
                                      (tl >= 15 && ci_starts(a + q, "application/ecmascript")) ||
                                      (tl >= 8 && ci_starts(a + q, "text/javascript")) ||
                                      (tl >= 9 && ci_starts(a + q, "text/ecmascript")) ||
                                      (tl >= 6 && ci_starts(a + q, "module")) ||
                                      (tl >= 5 && ci_starts(a + q, "text/js"))))
                                    js_type = 0;
                            }
                            break;
                        }
                    }
                }
                pos = tagend;
                if (pos < len) pos++;
                if (srn2) {
                    if (srcs && src_n && src_cap && *src_n < src_cap) {
                        clip_str(srcs[*src_n], 256, srcur);
                        if (smark_n < (int)(sizeof(smark_kind)/sizeof(smark_kind[0])))
                            smarks_push(0, (uint16_t)*src_n);
                        (*src_n)++;
                    }
                    continue;
                }
                if (!js_type) {
                    // non-JS inline (JSON-LD, templates): consume the body so
                    // it never becomes visible text nodes. The body may span
                    // chunks, so the consume is stateful (in_skipscript_).
                    in_skipscript_ = true;
                    continue;
                }
                in_script_ = true;
                if (seg_n < 24 && smark_n < (int)(sizeof(smark_kind)/sizeof(smark_kind[0])))
                    smarks_push(1, (uint16_t)seg_n);
                while (pos < len) {
                    if (html[pos] == '<' && ci_starts(html + pos + 1, "/script")) {
                        in_script_ = false;
                        if (seg_n > 0 && seg_n - 1 < 24)
                            seg_off[seg_n - 1][1] = script_len ? *script_len : 0;
                        break;
                    }
                    if (script_buf && script_len && *script_len < SCRIPT_MAX - 1)
                        script_buf[(*script_len)++] = html[pos];
                    pos++;
                }
                // separator between concatenated inline scripts (each <script>
                // block is an independent program in real browsers) — ONLY when
                // this block ended inside the current chunk. If it ran off the
                // chunk end (in_script_ still true), the cross-chunk path
                // (in_script_ branch) appends the separator at the real
                // </script>; appending here would split a string literal at a
                // chunk boundary and break bloxd's deobfuscator.
                if (!in_script_ && script_buf && script_len && *script_len < SCRIPT_MAX - 3) {
                    script_buf[(*script_len)++] = ';';
                    script_buf[(*script_len)++] = '\n';
                }
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                continue;
            }
            // <style>: collect raw CSS text for the style sheet (stateful, the
            // body may span chunks)
            if (!closing && t == Tag::Style) {
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                in_style_ = true;
                if (css_depth <= 0) css_safe = csslen;
                continue;
            }
            // <title>: capture the text for the tab title, then skip content
            if (!closing && ci_eq(tagbuf, "title")) {
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                while (pos < len) {
                    if (html[pos] == '<' && ci_starts(html + pos + 1, "/title")) break;
                    if (title_out) {
                        int tl = (int)strlen(title_out);
                        if (tl < title_cap - 1 && html[pos] != '\n' && html[pos] != '\r') {
                            title_out[tl] = html[pos];
                            title_out[tl + 1] = 0;
                        }
                    }
                    pos++;
                }
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;
                continue;
            }
            // <template>: its content is inert in real browsers — never
            // rendered, scripts inside never run. bloxd.io hides a huge
            // obfuscated honeypot in one (venatusAdBlockRecovery); rendering
            // the subtree buried the whole page under decoy text.
            if (!closing && ci_eq(tagbuf, "template")) {
                // enter inert-skip mode: consume the open tag here (quoted
                // attribute values may contain '>'), the skipper above takes
                // everything up to the matching </template>
                tpl_depth_ = 1;
                pos = find_tag_end(pos);
                if (pos < len) pos++;
                continue;
            }
            // <svg>: an inline vector graphic. Its subtree is XML (path / g /
            // defs), not HTML — the tree builder has no layout story for it and
            // the tokenizer would leak the subtree's text as page copy. Capture
            // the markup verbatim and hand it to nanosvg (the same decoder
            // <img src="*.svg"> uses), so the icon fonts of GitHub / DeepSeek
            // draw instead of leaving holes in the UI.
            if (!closing && t == Tag::Svg) {
                int tagend = find_tag_end(pos);
                char scls[48] = {0}, sid[32] = {0}, swv[16] = {0}, shv[16] = {0};
                char vb[64] = {0}, sst[160] = {0};
                {
                    const char* a = html + pos;
                    int al = tagend - pos;
                    attr_span_get(a, al, "class", scls, sizeof(scls));
                    attr_span_get(a, al, "id", sid, sizeof(sid));
                    attr_span_get(a, al, "width", swv, sizeof(swv));
                    attr_span_get(a, al, "height", shv, sizeof(shv));
                    attr_span_get(a, al, "viewBox", vb, sizeof(vb));
                    attr_span_get(a, al, "style", sst, sizeof(sst));
                }
                // style="color:..." drives fill="currentColor" in the art
                CSSRule sinl;
                bool s_has_inl = false;
                if (sst[0]) {
                    css_parse_decls(sst, (int)strlen(sst), sinl);
                    s_has_inl = (sinl.color || sinl.width > 0 || sinl.height > 0);
                }
                const bool self_closed = (tagend > tag_start && html[tagend - 1] == '/');
                int body = (tagend < len) ? tagend + 1 : len;
                int markup_end = len;
                if (self_closed) {
                    markup_end = (tagend < len) ? tagend + 1 : len;
                } else {
                    int close = -1;
                    for (int i = body; i + 4 < len; i++)
                        if (html[i] == '<' && html[i + 1] == '/' && ci_starts(html + i + 2, "svg")) { close = i; break; }
                    if (close >= 0) {
                        int ce = find_tag_end(close + 5);
                        markup_end = (ce < len) ? ce + 1 : len;
                    } else {
                        // truncated page (or a chunk boundary in the middle of
                        // the drawing): drop the tail rather than let it leak
                        // into the page as text
                        klogf("browser: unterminated <svg> dropped\n");
                    }
                }
                int mlen = markup_end - tag_start;
                if (mlen > 0 && mlen <= SVG_MARKUP_MAX) {
                    int vbw = 0, vbh = 0;
                    {   // viewBox="minx miny w h"
                        int nums[4] = {0, 0, 0, 0}, ni = 0, i = 0;
                        while (vb[i] && ni < 4) {
                            while (vb[i] && (vb[i] == ' ' || vb[i] == ',' || vb[i] == '\t')) i++;
                            if (!vb[i]) break;
                            nums[ni++] = atoi(vb + i);
                            while (vb[i] && vb[i] != ' ' && vb[i] != ',' && vb[i] != '\t') i++;
                        }
                        if (ni == 4 && nums[2] > 0 && nums[3] > 0) { vbw = nums[2]; vbh = nums[3]; }
                    }
                    int dw = atoi(swv), dh = atoi(shv);
                    if (dw <= 0 && dh <= 0) {
                        // no explicit box: the viewBox carries the intrinsic size
                        dw = vbw > 0 ? vbw : 16;
                        dh = vbh > 0 ? vbh : 16;
                    } else if (dw <= 0) {
                        dw = (vbw > 0 && vbh > 0) ? (dh * vbw) / vbh : dh;
                    } else if (dh <= 0) {
                        dh = (vbw > 0 && vbh > 0) ? (dw * vbh) / vbw : dw;
                    }
                    if (dw < 1) dw = 1;
                    if (dh < 1) dh = 1;
                    if (dw > SVG_MAX_DISP) dw = SVG_MAX_DISP;
                    if (dh > SVG_MAX_DISP) dh = SVG_MAX_DISP;
                    dom.open(Tag::Svg, 0, 0, 0, scls, sid, s_has_inl ? &sinl : 0, 0, tagbuf);
                    DomNode& sn = dom.nodes[dom.top()];
                    sn.svg = String(html + tag_start, mlen);
                    sn.cw = dw;
                    sn.ch = dh;
                    dom.close_top();
                } else {
                    klogf("browser: <svg> markup %d bytes skipped (cap %d)\n",
                          mlen, SVG_MARKUP_MAX);
                }
                pos = markup_end;
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
            // form controls need more than the short attribute buffer above
            char fval[192] = {0}, fph[96] = {0}, fname[48] = {0}, faction[256] = {0};
            char ftype[16] = {0};
            // method="post" / formmethod="post" on a submit button
            char fmeth[8] = {0};
            char relv[32] = {0};
            bool checked_attr = false;
            uint8_t battrs = 0;
            int cw = 300, ch = 150, nfu = -1;
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
                // Long-valued attributes scan straight into their own buffer:
                // an inline image payload (data:image/...) is thousands of
                // bytes, and truncating it at 95 chars makes the image
                // undecodable. Everything else keeps the small scratch field.
                char* avp = aval;
                int avcap = (int)sizeof(aval);
                if (ci_eq(aname, "src")) { avp = src; avcap = (int)sizeof(src); }
                else if (ci_eq(aname, "href")) { avp = href; avcap = (int)sizeof(href); }
                // inline style="" can carry an entire data: URI background
                else if (ci_eq(aname, "style")) { avp = g_inl_style; avcap = (int)sizeof(g_inl_style); }
                int av = 0;
                if (pos < len && html[pos] == '=') {
                    pos++;
                    while (pos < len && (html[pos]==' ' || html[pos]=='\t' || html[pos]=='\n' || html[pos]=='\r')) pos++;
                    if (pos < len && (html[pos]=='"' || html[pos]=='\'')) {
                        char q = html[pos++];
                        while (pos < len && html[pos] != q && av < avcap - 1) avp[av++] = html[pos++];
                        // an over-long value must not swallow the rest of the tag
                        while (pos < len && html[pos] != q) pos++;
                        if (pos < len) pos++;
                    } else {
                        while (pos < len && av < avcap - 1 && html[pos] != ' ' && html[pos] != '>' && html[pos] != '\t') {
                            avp[av++] = html[pos++];
                        }
                    }
                }
                avp[av] = 0;
                if (ci_eq(aname, "href")) clip_str(href, sizeof(href), avp);
                else if (ci_eq(aname, "src")) clip_str(src, sizeof(src), avp);
                else if (ci_eq(aname, "alt")) clip_str(alt, sizeof(alt), avp);
                else if (ci_eq(aname, "class")) clip_str(cls, sizeof(cls), avp);
                else if (ci_eq(aname, "id")) clip_str(id, sizeof(id), avp);
                else if (ci_eq(aname, "onclick")) clip_str(onclick, sizeof(onclick), avp);
                else if (ci_eq(aname, "width")) cw = atoi(avp);
                else if (ci_eq(aname, "height")) ch = atoi(avp);
                else if (ci_eq(aname, "value")) clip_str(fval, sizeof(fval), avp);
                else if (ci_eq(aname, "placeholder")) clip_str(fph, sizeof(fph), avp);
                else if (ci_eq(aname, "name")) clip_str(fname, sizeof(fname), avp);
                else if (ci_eq(aname, "action")) clip_str(faction, sizeof(faction), avp);
                else if (ci_eq(aname, "type")) clip_str(ftype, sizeof(ftype), avp);
                else if (ci_eq(aname, "rel")) clip_str(relv, sizeof(relv), avp);
                // Both spellings land in the same slot: a <form> carries
                // method=, a submit control carries formmethod=, and only one
                // of them can be present on a given node.
                else if (ci_eq(aname, "method") || ci_eq(aname, "formmethod"))
                    clip_str(fmeth, sizeof(fmeth), avp);
                // Boolean attributes: presence is the value (checked / selected)
                else if (ci_eq(aname, "checked") || ci_eq(aname, "selected")) checked_attr = true;
                else if (ci_eq(aname, "hidden")) battrs |= 1;
                else if (ci_eq(aname, "disabled")) battrs |= 2;
                else if (ci_eq(aname, "readonly")) battrs |= 4;
                else if (ci_eq(aname, "required")) battrs |= 8;
                else if (ci_eq(aname, "open")) battrs |= 16;
                else if (ci_eq(aname, "data-nfu")) nfu = atoi(avp);
                else if (ci_eq(aname, "style")) {
                    inl = CSSRule();
                    css_parse_decls(avp, (int)strlen(avp), inl);
                    has_inl = (inl.color || inl.font_size || inl.bold_set ||
                               inl.none || inl.bg || inl.align || inl.bgimg[0] ||
                               inl.bg_hash || inl.width > 0 || inl.height > 0 ||
                               inl.padding >= 0 || inl.margin >= 0 ||
                               inl.border_w > 0 || inl.disp_flex || inl.flex_dir ||
                               inl.flex_wrap || inl.flex_grow > 0 ||
                               inl.flex_shrink >= 0 ||
                               inl.justify || inl.align_i || inl.gap > 0 ||
                               inl.pos_fixed || inl.pos_top != kPosNone ||
                               inl.pos_bottom != kPosNone || inl.pos_left != kPosNone ||
                               inl.z_index >= 0 || inl.line_h > 0 ||
                               inl.width_pct > 0 || inl.height_pct > 0 ||
                               inl.max_w > 0 || inl.max_w_pct > 0 ||
                               inl.min_w > 0 || inl.max_h > 0 ||
                               inl.min_h > 0 ||
                               inl.nowrap || inl.under || inl.clip ||
                               inl.radius || inl.width_off ||
                               inl.disp_grid || inl.grid_cols ||
                               inl.gr_a || inl.gr_b ||
                               inl.tr_pct_x || inl.tr_pct_y || inl.fl);
                    if (!has_inl) inl = CSSRule();
                }
            }
            if (pos < len && html[pos] == '>') pos++;

            // <textarea> holds raw text (RCDATA): markup inside it is inert.
            // baidu parks its search-result stylesheet inside one
            // (<textarea id="s_is_result_css"><style>#lg{display:none}...),
            // which our parser used to read as live CSS and hide the logo.
            if (!closing && ci_eq(tagbuf, "textarea")) {
                dom.open(t, 0, 0, 0, cls, id, has_inl ? &inl : 0, onclick, tagbuf);
                DomNode& tn = dom.nodes[dom.top()];
                if (nfu >= 0) tn.nfu_idx = nfu;
                if (fval[0]) tn.value = fval;
                if (fph[0]) tn.ph = fph;
                if (fname[0]) tn.fname = fname;
                in_textarea_ = true;
                continue;
            }

            if (closing) {
                if (t == Tag::Li) dom.pop_to(Tag::Li);
                else if (t == Tag::P) dom.pop_to(Tag::P);
                else if (t == Tag::Td || t == Tag::Th) dom.pop_to(Tag::Td);
                else if (t == Tag::Tr) dom.pop_to(Tag::Tr);
                else dom.pop_to(t);
                continue;
            }

            // <link rel=stylesheet href=...>: remember the URL; load_worker
            // fetches the sheet synchronously before any script runs (real
            // browsers block the first paint on external CSS).
            if (!closing && ci_eq(tagbuf, "link") && clinks && clink_n &&
                *clink_n < clink_cap && href[0]) {
                const char* rl = relv;
                while (*rl == ' ') rl++;
                // "stylesheet" or space-separated lists containing it
                bool sheet = false;
                for (const char* p = rl; *p; p++) {
                    if (ci_starts(p, "stylesheet")) { sheet = true; break; }
                }
                if (sheet) {
                    clip_str(clinks[(*clink_n)++], 256, href);
                }
            }

            // void elements: br/hr/img/input produce a leaf node, others dropped
            if (is_void_tag(tagbuf)) {
                if (t == Tag::Br || t == Tag::Hr || t == Tag::Img || t == Tag::Input) {
                    dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick, tagbuf);
                    DomNode& vn = dom.nodes[dom.top()];
                    if (nfu >= 0) vn.nfu_idx = nfu;
                    if (fval[0]) vn.value = fval;
                    if (fph[0]) vn.ph = fph;
                    if (fname[0]) vn.fname = fname;
                    if (ftype[0]) clip_str(vn.ftype, sizeof(vn.ftype), ftype);
                    if (fmeth[0]) clip_str(vn.fmethod, sizeof(vn.fmethod), fmeth);
                    vn.checked = checked_attr;
                    vn.battrs = battrs;
                    if (ci_eq(ftype, "hidden")) vn.tag = Tag::Unknown;   // never rendered
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

            // <option> may omit its end tag: a new option closes the previous
            if (t == Tag::Option) dom.pop_to(Tag::Option);
            dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick, tagbuf);
            if (nfu >= 0) dom.nodes[dom.top()].nfu_idx = nfu;
            dom.nodes[dom.top()].battrs = battrs;
            if (checked_attr) dom.nodes[dom.top()].checked = true;
            if (t == Tag::Canvas) {
                DomNode& cn = dom.nodes[dom.top()];
                cn.cw = cw; cn.ch = ch;
            }
            if (t == Tag::Textarea || t == Tag::Form || t == Tag::Select ||
                t == Tag::Option) {
                DomNode& fn = dom.nodes[dom.top()];
                if (fval[0]) fn.value = fval;
                if (fph[0]) fn.ph = fph;
                if (fname[0]) fn.fname = fname;
                if (faction[0]) fn.action = faction;
                if (fmeth[0]) clip_str(fn.fmethod, sizeof(fn.fmethod), fmeth);
            }
            // <button formmethod=post> lives outside the branch above
            if ((t == Tag::Button || t == Tag::Input) && fmeth[0])
                clip_str(dom.nodes[dom.top()].fmethod,
                         sizeof(dom.nodes[dom.top()].fmethod), fmeth);
        }
        flush_text();
        // hand the collected <style> text to the caller as parsed rules
        css_flush();
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
    // Inline payload (data: URI / blob:-as-data:). Kept after decode so a
    // reclaimed slot can be refetched without the original markup, and used
    // instead of an HTTP GET by the worker.
    uint8_t* local;
    uint32_t local_len;
    int w, h;           // decoded size
    int disp_w, disp_h; // display size (capped by MAX_IMG_DISP)
    bool used;          // slot in use
    bool loading;       // fetch in flight
    bool done;          // decoded and cached
    bool fail;          // fetch / decode failed (incl. too large)
    bool fail_logged;   // failure logged once (no per-frame retry spam)
    bool reclaimed;     // was reclaimed once and refetched: box is unchanged,
                        // arriving data must not trigger a full re-layout
    bool as_bg;         // used as a background image (keep full resolution)
    bool is565;         // rgba holds RGB565 (2 bytes/px) instead of BGRA
    bool alpha;         // keep BGRA and blend (inline svg / transparent PNG)
    int want_px;        // decode budget for vector sources (0 = MAX_IMG_DISP)

    Img() : job(0), rgba(0), local(0), local_len(0), w(0), h(0), disp_w(0), disp_h(0),
            used(false), loading(false), done(false), fail(false), fail_logged(false),
            reclaimed(false),
            as_bg(false), is565(false), alpha(false), want_px(0) {
        url[0] = 0;
    }
};

static void start_img_job(Img& im, const char* url);   // defined with the loader
static int img_lookup_or_start(Img* imgs, const char* base_url, const char* src);
static int img_lookup_svg(Img* imgs, const char* markup, int len, int px, uint32_t col);

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
    String ph;     // kind 6 only: placeholder shown while the value is empty
    bool full_page; // kind 4 only: background must fill the viewport (body/html)
    int border;    // border width px (kind 3 only, 0 = none)
    uint32_t bcol; // border color
    bool checked;  // kind 8 only: checkbox / radio state
    bool radio;    // kind 8 only: draw a dot instead of a tick
    bool noframe;  // kind 1 only: inline svg — no placeholder frame
    uint8_t deco;  // text-decoration: 0 auto (links underline), 1 underline, 2 none
    int radius;    // border-radius px (kind 3 only, 0 = square)
    uint32_t gcol2;// kind 3 only: second gradient stop (0 = flat colour)
    uint8_t gdir;  // 0 vertical, 1 horizontal
    int z;         // DOM depth at creation: backgrounds paint shallow-first
    Run() : kind(0), link(false), img_id(-1), node(-1), full_page(false),
           border(0), bcol(0), checked(false), radio(false), noframe(false),
           deco(0), radius(0), gcol2(0), gdir(0), z(0)
        { x = y = w = h = 0; color = 0; cid[0] = 0; }
};

struct PageLayout {
    List<Run> runs;
    int height = 0;
};

// Shift runs [s,e) so each visual line is centered / right-aligned in wrap_w.
// overflow:hidden — blank out the subtree runs that fall outside the box.
// Runs are zero-sized rather than erased so the array indices (flex kid
// ranges, click mapping) stay valid.
static void clip_runs(PageLayout* out, int s, int bx, int by, int bw, int bh) {
    if (bh <= 0 || bw <= 0) return;
    for (int i = s; i < out->runs.size(); i++) {
        Run& r = out->runs[i];
        if (r.kind == 3 || r.kind == 4) continue;    // the box's own backdrops
        if (r.y >= by + bh || r.x >= bx + bw || r.y + r.h <= by || r.x + r.w <= bx) {
            r.w = 0; r.h = 0;
            r.text = String();
            r.ph = String();
        }
    }
}

// Rounded-corner fill: per-row hlines with a circular inset at the four
// corners. The parent's box was painted first (shallow z order), so the
// notched corners show the parent colour — what a real engine composites.
static void fill_round_rect(Surface& s, int x, int y, int w, int h, int rad, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    int rr = rad;
    if (rr < 0) rr = (w < h ? w : h) / 2;        // "50%": pill
    if (rr * 2 > w) rr = w / 2;
    if (rr * 2 > h) rr = h / 2;
    if (rr <= 0) { gfx::fillrect(s, x, y, w, h, col); return; }
    for (int row = 0; row < h; row++) {
        int inset = 0;
        if (row < rr || row >= h - rr) {
            int dy = (row < rr) ? (rr - row) : (row - (h - rr) + 1);
            int dx = 0;
            while ((dx + 1) * (dx + 1) <= rr * rr - dy * dy) dx++;
            inset = rr - dx;
        }
        if (inset < 0) inset = 0;
        if (inset * 2 < w) gfx::fillrect(s, x + inset, y + row, w - 2 * inset, 1, col);
    }
}

// Two-stop gradient fill. Rows for a vertical axis, columns for a horizontal
// one: both are one line per step, so a 1000px hero band is 1000 hlines.
static uint32_t grad_mix(uint32_t a, uint32_t b, int t, int n) {
    if (n <= 1) return a;
    int ar = (int)((a >> 16) & 255), ag = (int)((a >> 8) & 255), ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255), bg = (int)((b >> 8) & 255), bb = (int)(b & 255);
    int r = ar + (br - ar) * t / (n - 1);
    int g = ag + (bg - ag) * t / (n - 1);
    int bl = ab + (bb - ab) * t / (n - 1);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void fill_grad_rect(Surface& s, int x, int y, int w, int h,
                           uint32_t c1, uint32_t c2, int horiz) {
    if (w <= 0 || h <= 0) return;
    if (horiz) {
        int n = (w > 1200) ? 1200 : w;
        for (int i = 0; i < n; i++) {
            int x0 = x + i * w / n, x1 = x + (i + 1) * w / n;
            if (x1 <= x0) continue;
            gfx::fillrect(s, x0, y, x1 - x0, h, grad_mix(c1, c2, i, n));
        }
    } else {
        int n = (h > 1200) ? 1200 : h;
        for (int i = 0; i < n; i++) {
            int y0 = y + i * h / n, y1 = y + (i + 1) * h / n;
            if (y1 <= y0) continue;
            gfx::fillrect(s, x, y0, w, y1 - y0, grad_mix(c1, c2, i, n));
        }
    }
}

static void align_runs(PageLayout* out, int s, int e, int align, int wrap_w) {    if (wrap_w <= 0) return;
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
    // minimal flexbox: the container sets flex/flex_depth before laying out
    // its children; layout_node records each direct child's run range so the
    // container can justify / vertically align after the fact.
    int flex;           // 0 none, 1 row, 2 column
    int flex_gap, flex_justify, flex_align;
    int flex_wrap;      // flex-wrap: wrap (row line breaking)
    int flex_depth;     // depth of direct flex children (-1 = none)
    int flex_x0, flex_y0;
    int flex_h;         // container explicit height (-1 = auto); column justify needs it
    int par_h;          // nearest ancestor's EXPLICIT height (0 = auto): a
                        // height:N% child resolves against it, like real CSS
    int kid_rs[40], kid_re[40], kid_x0[40], kid_x1[40], kid_y0[40], kid_y1[40];
    int kid_g[40];      // flex-grow factor per kid (0 = none)
    int kid_s[40];      // flex-shrink factor per kid (0 = none, 1 = CSS default)
    int kid_n;
    int frk;            // laying out a flex-row kid: suppress wrap checks
    int force_w;        // grid cell width imposed on direct children (0 = none)
    int grid_mode;      // container is a grid: cells keep their own wrapping
    int grid_col;       // cell index within the current grid row
    int grid_ncols;     // cells per row
    int grid_rowb;      // bottom of the tallest cell in the current row
    int nowrap;         // white-space: nowrap — no soft wraps inside this box
    int deco;           // text-decoration: 0 auto (links underline), 1 on, 2 off
    // CSS float tracking. Portals build nav bars and news columns from
    // float:left; each active float occupies [x0,x1] until its bottom edge
    // is above the pen, and normal content wraps around the occupied span.
    struct Flt { int x0, x1, yb; uint8_t side; };   // side 1 left, 2 right
    Flt flts[24];
    int nflts;
    int infl;           // inside a float subtree: suppress <li> bullets
    int fl_meas;        // inside a float's MEASURING pass: nested floats must
                        // lay out single-pass, or N float levels cost 2^N
                        // subtree layouts and real portals hang the tab

    Flow(PageLayout* o, int width, Img* im = 0, const char* base = 0,
         const CSSRule* c = 0, int cn = 0)
        : out(o), x(0), y(4), w(width), indent(0), line_h(16),
          color(C_TEXT_DK), bold(false), in_link(false), imgs(im), base_url(base),
          css(c), css_n(cn), node_now(-1), flex(0), flex_gap(0), flex_justify(0),
          flex_align(0), flex_wrap(0), flex_depth(-1), flex_x0(0), flex_y0(0),
          flex_h(-1), par_h(0),
          kid_n(0), frk(0), force_w(0), grid_mode(0), grid_col(0),
          grid_ncols(0), grid_rowb(0), nowrap(0), deco(0),
          nflts(0), infl(0), fl_meas(0) {}

    void newline() {
        x = indent;
        y += line_h; line_h = 16;
        // wrap-around: content after a float starts right of it, and the
        // constraint disappears once the pen moves below the float's bottom.
        // The push must leave room for the next word to actually emit: a
        // float wider than the line would push x past w-8 on EVERY newline
        // and add_word would loop here forever (runs never grow, so the
        // RUN_MAX guard never fires - the layout thread hangs and the tab
        // freezes on whatever frame was up, i.e. the home page).
        for (int i = 0; i < nflts; i++)
            if (flts[i].side == 1 && flts[i].yb > y && flts[i].x1 > x &&
                flts[i].x1 + 32 <= w)
                x = flts[i].x1;
    }

    void emit(const char* s, int len, int word_w, uint32_t col, bool link, const char* h, int kind) {
        Run r;
        r.x = x; r.y = y; r.w = word_w; r.h = 16;
        r.color = col; r.kind = kind; r.link = link;
        r.img_id = -1; r.node = node_now;
        r.deco = (uint8_t)deco;
        if (link && h) r.href = h;
        for (int i = 0; i < len; i++) r.text += s[i];
        out->runs.push(r);
        x += word_w;
    }

    void add_word(const char* s, int len, uint32_t col, bool link, const char* h) {
        int word_w = disp_w(s, len);
        // A collapsed box (a failed calc() leaving width:1px) used to break
        // after every glyph: 150 lines of 16px is exactly what inflates a
        // header to 2500px. Below a usable line width, let words overflow.
        if (w - 8 >= 12 && x + word_w > w - 8 && !frk && !nowrap) newline();
        emit(s, len, word_w, col, link, h, 0);
    }
};

// ---- minimal flexbox helpers ---------------------------------------------
// Move every run of a laid-out child (or the whole row) by dx/dy. Run
// coordinates are page-absolute; the painter only adds scroll and chrome
// offsets, so shifting after the fact is exactly a re-layout of position.
static void flex_shift(PageLayout& out, int rs, int re, int dx, int dy) {
    for (int i = rs; i < re && i < out.runs.size(); i++) {
        if (dx) out.runs[i].x += dx;
        if (dy) out.runs[i].y += dy;
    }
}
static void flex_kid_measure(PageLayout& out, Flow& f, int i, bool x_skip_bg) {
    int x0 = 1 << 30, x1 = -(1 << 30), y0 = 1 << 30, y1 = -(1 << 30);
    for (int r = f.kid_rs[i]; r < f.kid_re[i] && r < out.runs.size(); r++) {
        const Run& k = out.runs[r];
        // A run whose box lands outside any plausible page is a stale/garbage
        // record: one such entry (y+h == 1180174) used to inflate a whole
        // flex column and push the page 2551px down. Ignore it here.
        if (k.y < -20000 || k.y + k.h > 20000) continue;
        // In a column the child's own background box spans the full container
        // width; the content extent decides the centering.
        if (x_skip_bg && (k.kind == 3 || k.kind == 4)) {
            if (k.y < y0) y0 = k.y;
            if (k.y + k.h > y1) y1 = k.y + k.h;
            continue;
        }
        if (k.x < x0) x0 = k.x;
        if (k.x + k.w > x1) x1 = k.x + k.w;
        if (k.y < y0) y0 = k.y;
        if (k.y + k.h > y1) y1 = k.y + k.h;
    }
    if (x0 > x1) { x0 = f.x; x1 = x0; }
    if (y0 > y1) { y0 = f.y; y1 = y0 + f.line_h; }
    f.kid_x0[i] = x0; f.kid_x1[i] = x1;
    f.kid_y0[i] = y0; f.kid_y1[i] = y1;
}
// Smallest DOM depth among a kid's background boxes = the kid's OWN box
// (descendant boxes are deeper). Flex post-processing must only touch the
// kid's own box, or a nested flex child gets stretched/shrunk/grown twice.
static int flex_kid_z(const PageLayout& out, int rs, int re) {
    int z = 1 << 30;
    for (int r = rs; r < re && r < out.runs.size(); r++) {
        const Run& q = out.runs[r];
        if ((q.kind == 3 || q.kind == 4) && q.z < z) z = q.z;
    }
    return z;
}

// Row/column finished: apply align-items + justify-content, park the pen.
// A row is laid out in lines: with flex-wrap:wrap every kid whose right edge
// overflows the container starts a new line; each line is then aligned,
// justified and grown independently, and later lines shift down.
static void flex_finalize(PageLayout& out, Flow& f, bool row) {
    const int n = f.kid_n;
    // Probe: bloxd's .HomeHeader spans 2551px while all of its visible kids
    // stay on the first line. Print the kid count and the pen so the missing
    // advance is attributed to a container, not guessed.
    if (n <= 0) return;
    if (getenv("NF_CARD_DBG") && f.flex_y0 >= 40 && f.flex_y0 <= 120 && s_flexfin_once < 4000) {
        s_flexfin_once++;
        klogf("flexfin: row=%d n=%d y0=%d y=%d fh=%d x0=%d",
              (int)row, n, f.flex_y0, f.y, f.flex_h, f.flex_x0);
        // Each kid's measured extent: one oversized entry is what inflates the
        // whole header column.
        if (row) {
            for (int i = 0; i < n && i < 12; i++)
                klogf("flexkd: i=%d x0=%d x1=%d y0=%d y1=%d g=%d",
                      i, f.kid_x0[i], f.kid_x1[i], f.kid_y0[i], f.kid_y1[i], f.kid_g[i]);
        }
    }
    for (int i = 0; i < n; i++) flex_kid_measure(out, f, i, !row);
    if (row) {
        // ---- group kids into lines ----------------------------------------
        int ln_start[40], ln_end[40], ln_n = 0;
        int i = 0;
        while (i < n) {
            int j = i;
            while (j + 1 < n &&
                   (!f.flex_wrap ||
                    f.kid_x1[j + 1] - f.kid_x0[i] <= f.w - f.flex_x0 + 2))
                j++;
            ln_start[ln_n] = i; ln_end[ln_n] = j + 1; ln_n++;
            i = j + 1;
        }
        int cum_y = 0;
        for (int L = 0; L < ln_n; L++) {
            const int a = ln_start[L], b = ln_end[L];
            int maxh = 0;
            for (int k = a; k < b; k++) {
                int h = f.kid_y1[k] - f.kid_y0[k];
                if (h > maxh) maxh = h;
            }
            if (L > 0) {
                const int dy = cum_y;   // shift this whole line down
                // ... and back to the container's left edge: wrapped kids
                // keep the x where they overflowed, a real flex line starts
                // at the content edge.
                const int dx = f.flex_x0 - f.kid_x0[a];
                for (int k = a; k < b; k++)
                    flex_shift(out, f.kid_rs[k], f.kid_re[k], dx, dy);
                for (int k = a; k < b; k++) {
                    f.kid_x0[k] += dx; f.kid_x1[k] += dx;
                    f.kid_y0[k] += dy; f.kid_y1[k] += dy;
                }
            } else {
                // First line: the kids were laid out one after another in
                // normal block flow, so each newline() pushed the pen down and
                // the next kid starts BELOW the previous one. A flex row puts
                // them all on one line, so every kid's y must come back to the
                // container's top. Without this a nowrap row (the CSS default)
                // stacked vertically: bloxd's header ate 1135px, its 8 carousel
                // pages 42000px, and the game grid landed 1303px off-screen.
                const int base_y = f.flex_y0;
                for (int k = a; k < b; k++) {
                    const int dy = base_y - f.kid_y0[k];
                    if (dy) {
                        flex_shift(out, f.kid_rs[k], f.kid_re[k], 0, dy);
                        f.kid_y0[k] += dy; f.kid_y1[k] += dy;
                    }
                }
            }
            // ---- vertical alignment within the line ------------------------
            for (int k = a; k < b; k++) {
                const int kz = flex_kid_z(out, f.kid_rs[k], f.kid_re[k]);
                if (f.flex_align == 1) {
                    const int dy = (maxh - (f.kid_y1[k] - f.kid_y0[k])) / 2;
                    if (dy) flex_shift(out, f.kid_rs[k], f.kid_re[k], 0, dy);
                } else if (f.flex_align == 0) {
                    // default stretch: extend the kid's own background box to
                    // the line height (not the nested descendants' boxes)
                    for (int r = f.kid_rs[k]; r < f.kid_re[k] &&
                         r < out.runs.size(); r++) {
                        Run& q = out.runs[r];
                        if ((q.kind == 3 || q.kind == 4) && q.z == kz) q.h = maxh;
                    }
                }
            }
            // ---- grow: expand background boxes, shift later kids right ----
            const int lx0 = f.kid_x0[a];
            const int total = f.kid_x1[b - 1] - lx0;
            const int avail = f.w - lx0;
            const int free = avail - total;
            if (free < 0) {
                // ---- shrink: the line overflows. Reduce kid boxes weighted
                // by shrink * width (the CSS basis-weighted model), shifting
                // later kids left to close the gap. Text inside a shrunk kid
                // keeps its layout: the box narrows around it.
                int sum_w = 0;
                for (int k = a; k < b; k++) {
                    const int bw = f.kid_x1[k] - f.kid_x0[k];
                    if (f.kid_s[k] > 0 && bw > 0) sum_w += f.kid_s[k] * bw;
                }
                if (sum_w > 0) {
                    const int overflow = -free;
                    int acc = 0;
                    for (int k = a; k < b; k++) {
                        if (acc)
                            flex_shift(out, f.kid_rs[k], f.kid_re[k], -acc, 0);
                        const int bw = f.kid_x1[k] - f.kid_x0[k];
                        if (f.kid_s[k] > 0 && bw > 0) {
                            int share = overflow * (f.kid_s[k] * bw) / sum_w;
                            if (share > bw) share = bw;
                            const int kz = flex_kid_z(out, f.kid_rs[k], f.kid_re[k]);
                            for (int r = f.kid_rs[k]; r < f.kid_re[k] &&
                                 r < out.runs.size(); r++) {
                                Run& q = out.runs[r];
                                if ((q.kind == 3 || q.kind == 4) && q.z == kz) {
                                    q.w -= share;
                                    if (q.w < 4) q.w = 4;
                                }
                            }
                            acc += share;
                        }
                    }
                }
            } else if (free > 0) {
                int sum_g = 0;
                for (int k = a; k < b; k++) sum_g += f.kid_g[k];
                if (sum_g > 0) {
                    int acc = 0;
                    for (int k = a; k < b; k++) {
                        if (acc)
                            flex_shift(out, f.kid_rs[k], f.kid_re[k], acc, 0);
                        if (f.kid_g[k] > 0) {
                            const int share = free * f.kid_g[k] / sum_g;
                            const int kz = flex_kid_z(out, f.kid_rs[k], f.kid_re[k]);
                            for (int r = f.kid_rs[k]; r < f.kid_re[k] &&
                                 r < out.runs.size(); r++) {
                                Run& q = out.runs[r];
                                if ((q.kind == 3 || q.kind == 4) && q.z == kz)
                                    q.w += share;
                            }
                            acc += share;
                        }
                    }
                } else if (f.flex_justify == 1) {
                    const int dx = free / 2;
                    for (int k = a; k < b; k++)
                        flex_shift(out, f.kid_rs[k], f.kid_re[k], dx, 0);
                } else if (f.flex_justify == 2) {
                    for (int k = a; k < b; k++)
                        flex_shift(out, f.kid_rs[k], f.kid_re[k], free, 0);
                } else if (f.flex_justify == 3 && b - a >= 2) {
                    const int seg = free / (b - a - 1);
                    int acc = 0;
                    for (int k = a + 1; k < b; k++) {
                        acc += seg;
                        flex_shift(out, f.kid_rs[k], f.kid_re[k], acc, 0);
                    }
                }
            }
            cum_y += maxh + f.flex_gap;
        }
        f.x = f.flex_x0;
        f.y = f.flex_y0 + cum_y - (ln_n ? f.flex_gap : 0);
        // The accumulations above run on measured kid extents. When a kid's
        // runs report a far deeper box than what it actually paints (bloxd's
        // header icons measured 311px tall apiece and pushed the whole page
        // 2551px down), tighten the pen to the lowest painted run instead.
        {
            int painted = f.flex_y0;
            for (int i = 0; i < n; i++) {
                for (int r = f.kid_rs[i]; r < f.kid_re[i] && r < out.runs.size(); r++) {
                    const Run& kk = out.runs[r];
                    if (kk.y < -20000 || kk.y + kk.h > 20000) continue;
                    if (kk.y + kk.h > painted) painted = kk.y + kk.h;
                }
            }
            if (painted > f.flex_y0 && f.y > painted + f.line_h)
                f.y = painted + f.line_h;
            {
                static int s_rt = 0;
                if (getenv("NF_CARD_DBG") && f.flex_y0 >= 40 && f.flex_y0 <= 120 && s_rt < 400) {
                    s_rt++;
                    klogf("rowtight: n=%d y0=%d y=%d painted=%d lh=%d",
                          n, f.flex_y0, f.y, painted, f.line_h);
                }
            }
        }
        f.kid_n = 0;
        return;
    } else {
        if (f.flex_align == 1) {
            // align-items:center in a column: the child's background shrinks to
            // its content and the whole group is centered in the container.
            for (int i = 0; i < n; i++) {
                const int cw = f.kid_x1[i] - f.kid_x0[i];
                int dx = (f.w - cw) / 2 - f.kid_x0[i];
                // an overflowing kid must not be dragged left of the
                // container's content edge (real CSS overflows both sides;
                // that reads as "text band scraped off the page" here)
                if (f.kid_x0[i] + dx < f.flex_x0) dx = f.flex_x0 - f.kid_x0[i];
                for (int r = f.kid_rs[i]; r < f.kid_re[i] && r < out.runs.size(); r++) {
                    Run& k = out.runs[r];
                    if (k.kind == 3 || k.kind == 4) {
                        if (cw > 0) { k.x = f.kid_x0[i] + dx; k.w = cw; }
                    } else if (dx > 0) {
                        k.x += dx;
                    }
                }
            }
        }
        if (f.flex_h > 0) {
            // ---- column shrink: kids overflow the explicit height ----------
            const int total = f.kid_y1[n - 1] - f.flex_y0;
            const int free = f.flex_h - total;
            if (free < 0) {
                int sum_h = 0;
                for (int i = 0; i < n; i++) {
                    const int bh = f.kid_y1[i] - f.kid_y0[i];
                    if (f.kid_s[i] > 0 && bh > 0) sum_h += f.kid_s[i] * bh;
                }
                if (sum_h > 0) {
                    const int overflow = -free;
                    int acc = 0;
                    for (int i = 0; i < n; i++) {
                        if (acc) flex_shift(out, f.kid_rs[i], f.kid_re[i], 0, -acc);
                        const int bh = f.kid_y1[i] - f.kid_y0[i];
                        if (f.kid_s[i] > 0 && bh > 0) {
                            int share = overflow * (f.kid_s[i] * bh) / sum_h;
                            if (share > bh) share = bh;
                            const int kz = flex_kid_z(out, f.kid_rs[i], f.kid_re[i]);
                            for (int r = f.kid_rs[i]; r < f.kid_re[i] &&
                                 r < out.runs.size(); r++) {
                                Run& q = out.runs[r];
                                if ((q.kind == 3 || q.kind == 4) && q.z == kz) {
                                    q.h -= share;
                                    if (q.h < 4) q.h = 4;
                                }
                            }
                            acc += share;
                        }
                    }
                }
            }
        }
        if (f.flex_h > 0) {
            // Shrink compressed the kids into the fixed height: park the pen
            // at the container bottom, so the container's own background box
            // (its height is computed from f.y downstream) does not grow to
            // the unshrunk content extent.
            const int total = f.kid_y1[n - 1] - f.flex_y0;
            f.y = f.flex_y0 + (total > f.flex_h ? f.flex_h : total);
        }
        if (f.flex_h > 0 && f.flex_justify) {
            // justify-content along the column main axis: needs an explicit
            // container height, otherwise there is no free space to hand out.
            const int free = (f.flex_y0 + f.flex_h) - f.kid_y1[n - 1];
            if (free > 0) {                if (f.flex_justify == 1) {
                    const int dy = free / 2;
                    for (int i = 0; i < n; i++)
                        flex_shift(out, f.kid_rs[i], f.kid_re[i], 0, dy);
                } else if (f.flex_justify == 2) {
                    for (int i = 0; i < n; i++)
                        flex_shift(out, f.kid_rs[i], f.kid_re[i], 0, free);
                } else if (f.flex_justify == 3 && n >= 2) {
                    const int seg = free / (n - 1);
                    int acc = 0;
                    for (int i = 1; i < n; i++) {
                        acc += seg;
                        flex_shift(out, f.kid_rs[i], f.kid_re[i], 0, acc);
                    }
                }
            }
        }
    }
    f.kid_n = 0;
}

// word-wrapped text with hard breaks for words wider than the line
static void flow_text(Flow& f, const char* s) {
    int i = 0;
    while (s[i]) {
        int ws = i;
        while (s[ws] && s[ws] != ' ') ws++;
        int wlen = ws - i;
        if (wlen == 0) { i = ws + (s[ws] ? 1 : 0); continue; }
        int word_w = disp_w(s + i, wlen);
        if (word_w > f.w - 8 && wlen > 1 && !f.nowrap) {
            // hard break: emit one UTF-8 character per run (CJK chars
            // are 3 bytes; splitting them would render as '?')
            int j = i;
            while (j < ws) {
                int clen = utf8_char_len(s + j);
                int cw = disp_w(s + j, clen);
                if (f.x + cw > f.w - 8 && !f.frk && !f.nowrap) f.newline();
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
    int mar_l, mar_r, mar_b;   // signed per-side margins
    char bgimg[256];
    uint32_t bg_hash; int bg_len;
    uint8_t disp_flex; uint8_t flex_dir; uint8_t justify; uint8_t align_i;
    int gap;
    uint8_t flex_wrap;   // flex-wrap: wrap (container)
    int flex_grow;       // flex-grow (kid property)
    int flex_shrink;     // flex-shrink (kid property, -1 = unset)
    uint8_t flex_child;   // direct child of a flex row: no own newlines
    int border_w; uint32_t border_col;
    uint8_t pos_fixed;    // position: fixed / absolute
    int pos_top, pos_bottom, pos_left, pos_right;
    int z_index;
    int width_pct, height_pct;   // width/height: N% (0 = not set)
    int width_off;               // px offset riding with width_pct (calc)
    int max_w, max_w_pct, max_w_off, min_w, max_h;
    int min_h;                   // min-height px (0 = unset)
    uint8_t box_border;          // box-sizing: border-box
    uint8_t nowrap;              // white-space: nowrap
    uint8_t under;               // text-decoration (1 underline, 2 none)
    uint8_t clip;                // overflow: hidden
    int radius;                  // border-radius px (-1 = percent pill)
    uint8_t disp_grid;           // display: grid
    uint8_t disp_contents;       // display: contents (no box of its own)
    int grid_cols, grid_min;     // track count / minmax floor
    uint32_t gr_a, gr_b;         // gradient endpoints (0 = none)
    uint8_t gr_dir;              // 0 vertical, 1 horizontal
    int tr_pct_x, tr_pct_y;      // transform: translate(N%,N%)
    int pos_left_pct, pos_top_pct;   // left/top percentages
    uint8_t fl;                  // float: 0 none, 1 left, 2 right
    Eff() : bg(0), align(0), margin(0), padding(0), width(-1), height(-1),
           bg_hash(0), bg_len(0), disp_flex(0), flex_dir(0), justify(0),
           align_i(0), gap(0), flex_wrap(0), flex_grow(0), flex_shrink(-1),
           flex_child(0), border_w(0), border_col(0),
           pos_fixed(0), pos_top(kPosNone), pos_bottom(kPosNone), pos_left(kPosNone), pos_right(kPosNone),
           z_index(-1), width_pct(0), height_pct(0), width_off(0),
           max_w(0), max_w_pct(0), max_w_off(0), min_w(0), max_h(0), min_h(0),
           box_border(0), nowrap(0), under(0), clip(0), radius(0),
           disp_grid(0), disp_contents(0), grid_cols(0), grid_min(0), gr_a(0), gr_b(0),
           gr_dir(0), tr_pct_x(0), tr_pct_y(0), fl(0),
           mar_l(0), mar_r(0), mar_b(0), pos_left_pct(0), pos_top_pct(0)
           { bgimg[0] = 0; }
};

// <select>: the option to show. Options are child nodes, so a JS re-render
// (which re-emits <option selected>) restores the state with no side table.
static int select_sel_kid(const List<DomNode>& nodes, const DomNode& n) {
    int first = -1;
    for (int i = 0; i < n.kids.size(); i++) {
        const DomNode& k = nodes[n.kids[i]];
        if (k.tag != Tag::Option) continue;
        if (first < 0) first = i;
        if (k.checked) return i;
    }
    return first;
}

// Visible text of an <option> (its child text nodes).
static void option_text(const List<DomNode>& nodes, const DomNode& o, String& out) {
    out = String();
    for (int i = 0; i < o.kids.size(); i++) {
        const DomNode& t = nodes[o.kids[i]];
        if (t.tag == Tag::Text) out += t.text;
    }
}

// Value an <option> contributes to the query string: its value="" attribute,
// or its label when there is none (what a browser submits).
static void option_value(const List<DomNode>& nodes, const DomNode& o, String& out) {
    if (o.value.len() > 0) { out = o.value; return; }
    option_text(nodes, o, out);
}

static void select_label(const List<DomNode>& nodes, const DomNode& n, String& out) {
    out = String();
    int k = select_sel_kid(nodes, n);
    if (k < 0) return;
    option_text(nodes, nodes[n.kids[k]], out);
    if (out.len() == 0) option_value(nodes, nodes[n.kids[k]], out);
}

// Apply the per-node effective style (matched rules + inline style) around
// the inner layout pass; the inner pass itself recurses through this wrapper
// so children inherit colors / bold / line height.
// The url() target behind an element's background, following the long-payload
// arena when the sheet inlined the artwork as a data: URI.
static const char* eff_bg_src(const Eff& e) {
    if (e.bgimg[0]) return e.bgimg;
    if (e.bg_hash) return css_url_get(e.bg_hash, e.bg_len);
    return 0;
}
static void layout_node_inner(const List<DomNode>& nodes, int idx, Flow& f,
                              int depth, const Eff& e);

// CSS float:left/right. The box leaves vertical stacking and sits on the
// current line; following content wraps around it. Traditional portals
// (sina / 163 / ifeng / sohu) build their entire nav and news-column layout
// from float:left — without this every column stacks into one left sliver.
//
// Auto-width floats use a two-pass shrink-to-fit: pass 0 lays the subtree
// out with a wide pen to measure the content extent, pass 1 re-lays it out
// into the measured box. The measuring runs are not popped off the array
// (List::clear skips destructors, which would leak every String); each
// run's text/href are cleared first so their heap buffers stay attached to
// the slot and pass 1 reuses the same capacity.
static void layout_float_el(const List<DomNode>& nodes, int idx, Flow& f,
                            int depth, const Eff& e, uint32_t col, bool bset,
                            bool bold, int fs) {
    if (f.out->runs.size() > RUN_MAX || f.nflts >= 24) return;
    // prune floats whose bottom edge is above the current line
    for (int i = 0; i < f.nflts; ) {
        if (f.flts[i].yb <= f.y) { f.flts[i] = f.flts[f.nflts - 1]; f.nflts--; }
        else i++;
    }
    // leftmost free x: right of every still-active left float
    int fx = f.x;
    for (int i = 0; i < f.nflts; i++)
        if (f.flts[i].side == 1 && f.flts[i].x1 > fx) fx = f.flts[i].x1;
    // ...and clear of any right float that still covers this line
    for (int i = 0; i < f.nflts; i++)
        if (f.flts[i].side == 2 && f.flts[i].yb > f.y && fx + 48 > f.flts[i].x0)
            fx = f.flts[i].x0 + 4;
    // no usable room left on this line: clear-float semantics — drop below
    if (fx + 48 > f.w) {
        int maxb = f.y;
        for (int i = 0; i < f.nflts; i++)
            if (f.flts[i].yb > maxb) maxb = f.flts[i].yb;
        if (maxb > f.y) { f.y = maxb; f.nflts = 0; }
        fx = f.x;
    }

    Eff ef = e;
    ef.fl = 0;                       // apply once, not in the subtree
    const uint32_t sv_col = f.color; const bool sv_bold = f.bold;
    const int sv_lh0 = f.line_h, sv_infl = f.infl;
    if (col) f.color = col;
    else if (nodes[idx].tag >= Tag::H1 && nodes[idx].tag <= Tag::H6)
        f.color = C_HEAD;
    if (bset) f.bold = bold;
    if (fs) { int v = fs; if (v < 16) v = 16; if (v > 48) v = 48; f.line_h = v; }
    f.infl = 1;

    // Two-pass shrink-to-fit only at the outermost auto-width float. Inside
    // a measuring pass every nested float lays out single-pass: a portal's
    // 3-4 float levels would otherwise cost 2^N subtree layouts and freeze
    // the paint thread on the home-page frame.
    const int sv_meas = f.fl_meas;
    int passes = (e.width > 0 || f.fl_meas) ? 1 : 2;
    f.fl_meas = 1;

    int cw = 0;
    const size_t s0 = f.out->runs.size();
    const int lh_for_sub = f.line_h;
    for (int pass = 0; pass < passes; pass++) {
        if (pass == 1) {
            for (size_t ri = s0; ri < f.out->runs.size(); ri++) {
                f.out->runs[ri].text.clear();
                f.out->runs[ri].href.clear();
            }
            // truncate to s0 — NOT clear(), which empties the whole array
            // and would wipe every run laid out before this float
            f.out->runs.truncate((int)s0);
        }
        int fw = e.width;
        if (fw <= 0) fw = (pass == 0) ? (f.w - fx - 8) : cw;
        if (fx + fw > f.w - 4) fw = f.w - 4 - fx;
        if (fw < 24) fw = 24;
        const int sv_x = f.x, sv_y = f.y, sv_ind = f.indent, sv_lw = f.w,
                  sv_lh = f.line_h;
        f.x = fx; f.indent = fx; f.w = fx + fw; f.line_h = lh_for_sub;
        layout_node_inner(nodes, idx, f, depth, ef);
        f.x = sv_x; f.y = sv_y; f.indent = sv_ind; f.w = sv_lw; f.line_h = sv_lh;
        if (pass == 0) {
            // content extent from text/img/svg runs only: block backgrounds
            // legitimately span the whole measuring pen
            int mx = 0;
            for (size_t ri = s0; ri < f.out->runs.size(); ri++) {
                const Run& q = f.out->runs[ri];
                if (q.kind == 2 || q.kind == 3 || q.kind == 4) continue;
                if (q.x + q.w > mx) mx = q.x + q.w;
            }
            cw = (mx > fx) ? (mx - fx + 12) : 0;
            // +12: add_word wraps when a word would cross w-8, so the
            // measured extent needs that margin back or the re-layout at
            // exactly the measured width breaks the last word onto a
            // second line (e.g. the home label wraps between its two glyphs)
            if (cw < 24) cw = 24;
        }
    }

    // float occupancy: the bottom of the deepest run in the final range
    int boxb = f.y;
    for (size_t ri = s0; ri < f.out->runs.size(); ri++) {
        const Run& q = f.out->runs[ri];
        if (q.y + q.h > boxb) boxb = q.y + q.h;
    }
    if (e.fl == 2) {
        // right float: against the right edge, left of earlier right floats
        int rx = f.w;
        for (int i = 0; i < f.nflts; i++)
            if (f.flts[i].side == 2 && f.flts[i].x0 < rx) rx = f.flts[i].x0;
        int px = rx - cw;
        // ...and it must not overlap the left floats on this line: ifeng's
        // footer put a 512px left column and a right-anchored column on the
        // same row and they intersected for 11 px (two news lists painted
        // through each other)
        for (int i = 0; i < f.nflts; i++)
            if (f.flts[i].side == 1 && f.flts[i].yb > f.y && f.flts[i].x1 > px)
                px = f.flts[i].x1;
        if (px + cw > f.w || px < f.x) {
            // no room on this line: clear-float semantics — drop below all
            // floats and anchor to the right edge
            int maxb = f.y;
            for (int i = 0; i < f.nflts; i++)
                if (f.flts[i].yb > maxb) maxb = f.flts[i].yb;
            if (maxb > f.y) { f.y = maxb; f.nflts = 0; }
            px = f.w - cw;
            if (px < 0) px = 0;
        }
        if (px < fx) px = fx;
        if (px > fx)
            for (size_t ri = s0; ri < f.out->runs.size(); ri++)
                f.out->runs[ri].x += px - fx;
        f.flts[f.nflts].x0 = px; f.flts[f.nflts].x1 = px + cw;
        f.flts[f.nflts].yb = boxb; f.flts[f.nflts].side = 2; f.nflts++;
        // the pen stays at the left: inline content flows on, wrapping early
    } else {
        f.flts[f.nflts].x0 = fx; f.flts[f.nflts].x1 = fx + cw;
        f.flts[f.nflts].yb = boxb; f.flts[f.nflts].side = 1; f.nflts++;
        f.x = fx + cw + 4;           // inline siblings continue after it
    }
    f.fl_meas = sv_meas;   // siblings of this float inherit the same regime
    f.color = sv_col; f.bold = sv_bold; f.line_h = sv_lh0; f.infl = sv_infl;
}

// Layout cost counters: how many times layout_node actually walked the tree,
// and how many selector matches that cost. Declared here because layout_node
// below bumps them; layout_page reports and resets them.
static long g_lay_calls = 0;
static long g_lay_css = 0;

// Cheap rejection in front of the full selector match. bloxd ships ~1520 rules
// against ~8600 nodes, so the full matcher ran ~13M times per layout and cost
// 2.5-3 s every frame (0.3 fps, which also starved the JS pump: a page's 0 ms
// timeout fired 12.6 s late). Most of those pairs cannot match at all. When the
// selector is a single simple selector - ".name", "#id" or "tag" - one
// comparison decides it, and the full matcher never has to run.
// Returns true only when the pair is provably a miss, so the cascade keeps
// exactly the same result as before.
static bool css_quick_reject(const char* sel, const DomNode& n) {
    if (!sel || !sel[0]) return false;
    // The rightmost compound is the key selector: if it cannot match this node,
    // no ancestor combination can rescue the rule. bloxd's selectors are almost
    // all compound (".a .b", ".a>.b:hover"), so looking at the whole string
    // rejected almost nothing and cost the same as the real matcher.
    const char* key = sel;
    for (const char* q = sel; *q; q++)
        if (*q == ' ' || *q == '>' || *q == '+' || *q == '~') key = q + 1;
    const char c = key[0];
    if (c != '.' && c != '#' && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        return false;   // "*", "[attr]", ":pseudo", "(" ...: let the matcher decide
    int a = 1;
    while (key[a] &&
           (key[a] == '-' || key[a] == '_' ||
            (key[a] >= 'a' && key[a] <= 'z') ||
            (key[a] >= 'A' && key[a] <= 'Z') ||
            (key[a] >= '0' && key[a] <= '9')))
        a++;
    // A trailing pseudo/attribute only narrows the rule further, so the check
    // below is still a valid necessary condition. A trailing class/id starts a
    // different compound and is not decidable here.
    if (key[a] == '.' || key[a] == '#') return false;
    if (c == '.') return !cl_token(n.cls, key + 1, a - 1);
    if (c == '#') {
        const int l = (int)strlen(n.id);
        return !(l == a - 1 && memcmp(n.id, key + 1, (size_t)(a - 1)) == 0);
    }
    if (!n.tag_name[0]) return false;   // let the matcher compare by Tag
    for (int i = 0; i < a; i++) {
        char x = n.tag_name[i], y = key[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return true;
    }
    return false;
}

static void layout_node(const List<DomNode>& nodes, int idx, Flow& f, int depth) {
    if (depth > 32 || f.out->runs.size() > RUN_MAX) return;
    g_lay_calls++;
    const DomNode& n = nodes[idx];

    uint32_t col = 0; int fs = 0; bool bset = false, bold = false;
    bool none = false;
    const char* hid_rule = "";   // which selector hid it (NF_DUMP_HIDE)
    Eff e;
    // Inherited-only properties: a node with no rule of its own must keep the
    // value its parent established (text nodes are matched by no selector).
    e.nowrap = f.nowrap;
    e.under = (uint8_t)f.deco;
    if (f.css) {
        // Skip rules the selector index already rules out for this box: on a
        // bloxd page that is 1520 candidates down to a few dozen, and it is
        // the difference between a 2.3 s layout pass and a usable frame.
        const bool have_idx = css_mark_node(n, f.css, f.css_n);
        for (int i = 0; i < f.css_n; i++) {
            if (have_idx && !s_ck_mark[i]) continue;
            const CSSRule& r = f.css[i];
            g_lay_css++;
            if (!css_match(r.sel, nodes, idx)) continue;
            if (r.color) col = r.color;
            if (r.font_size) fs = r.font_size;
            if (r.bold_set) { bold = r.bold; bset = true; }
            if (r.none) none = true;
            // display follows the cascade: a later rule may bring the element
            // back (display:block after a mobile @media hide)
            if (r.disp) {
                none = (r.disp == 1);
                e.disp_flex = (r.disp == 3) ? 1 : 0;
                e.disp_grid = (r.disp == 4) ? 1 : 0;
                if (none) hid_rule = r.sel;
            }
            if (r.bg) e.bg = r.bg;
            if (r.bgimg[0]) { clip_str(e.bgimg, sizeof(e.bgimg), r.bgimg); e.bg_hash = 0; e.bg_len = 0; }
            else if (r.bg_hash) { e.bgimg[0] = 0; e.bg_hash = r.bg_hash; e.bg_len = r.bg_len; }
            if (r.align) e.align = r.align;
            if (r.margin > 0) e.margin = r.margin;
            if (r.mar_l) e.mar_l = r.mar_l;
            if (r.mar_r) e.mar_r = r.mar_r;
            if (r.mar_b) e.mar_b = r.mar_b;
            if (r.padding > 0) e.padding = r.padding;
            if (r.width > 0) e.width = r.width;
            if (r.height > 0) e.height = r.height;
            if (r.width_pct > 0) e.width_pct = r.width_pct;
            if (r.height_pct > 0) e.height_pct = r.height_pct;
            if (r.border_w > 0) e.border_w = r.border_w;
            if (r.border_col) e.border_col = r.border_col;
            if (r.pos_fixed) e.pos_fixed = 1;
            if (r.pos_top != kPosNone) e.pos_top = r.pos_top;
            if (r.pos_bottom != kPosNone) e.pos_bottom = r.pos_bottom;
            if (r.pos_left != kPosNone) e.pos_left = r.pos_left;
            if (r.pos_left_pct) e.pos_left_pct = r.pos_left_pct;
            if (r.pos_top_pct) e.pos_top_pct = r.pos_top_pct;
            if (r.pos_right != kPosNone) e.pos_right = r.pos_right;
            if (r.z_index >= 0) e.z_index = r.z_index;
            if (r.width_off) e.width_off = r.width_off;
            if (r.max_w > 0) e.max_w = r.max_w;
            if (r.max_w_pct > 0) { e.max_w_pct = r.max_w_pct; e.max_w_off = r.max_w_off; }
            if (r.min_w > 0) e.min_w = r.min_w;
            if (r.max_h > 0) e.max_h = r.max_h;
            if (r.min_h > 0) e.min_h = r.min_h;
            if (r.box_border) e.box_border = 1;
            if (r.nowrap) e.nowrap = 1;
            if (r.under) e.under = r.under;
            if (r.clip) e.clip = 1;
            if (r.radius) e.radius = r.radius;
            if (r.disp_flex && !r.disp) e.disp_flex = 1;
            if (r.disp && r.disp != 3) e.disp_flex = 0;
            if (r.disp && r.disp != 4) e.disp_grid = 0;
            if (r.disp) e.disp_contents = r.disp_contents;
            if (r.disp_contents) e.disp_contents = 1;
            if (r.disp_grid && !r.disp) { e.disp_grid = 1; }
            if (r.disp_grid && r.grid_cols > 0) e.grid_cols = r.grid_cols;
            if (r.disp_grid && r.grid_min > 0) e.grid_min = r.grid_min;
            if (r.gr_a) { e.gr_a = r.gr_a; e.gr_b = r.gr_b; e.gr_dir = r.gr_dir; }
            if (r.tr_pct_x || r.tr_pct_y) { e.tr_pct_x = r.tr_pct_x; e.tr_pct_y = r.tr_pct_y; }
            if (r.fl) e.fl = r.fl;
            if (r.flex_dir) e.flex_dir = 1;
            if (r.justify) e.justify = r.justify;
            if (r.align_i) e.align_i = r.align_i;
            if (r.gap > 0) e.gap = r.gap;
            if (r.flex_wrap) e.flex_wrap = 1;
            if (r.flex_grow > 0) e.flex_grow = r.flex_grow;
            if (r.flex_shrink >= 0) e.flex_shrink = r.flex_shrink;
            if (r.line_h > 0) f.line_h = r.line_h;
        }
    }
    if (n.has_inl) {
        if (n.inl.color) col = n.inl.color;
        if (n.inl.font_size) fs = n.inl.font_size;
        if (n.inl.bold_set) { bold = n.inl.bold; bset = true; }
        if (n.inl.none) none = true;
        if (n.inl.bg) e.bg = n.inl.bg;
        if (n.inl.bgimg[0]) { clip_str(e.bgimg, sizeof(e.bgimg), n.inl.bgimg); e.bg_hash = 0; e.bg_len = 0; }
        else if (n.inl.bg_hash) { e.bgimg[0] = 0; e.bg_hash = n.inl.bg_hash; e.bg_len = n.inl.bg_len; }
        if (n.inl.align) e.align = n.inl.align;
        if (n.inl.margin > 0) e.margin = n.inl.margin;
        if (n.inl.padding > 0) e.padding = n.inl.padding;
        if (n.inl.width > 0) e.width = n.inl.width;
        if (n.inl.height > 0) e.height = n.inl.height;
        if (n.inl.width_pct > 0) e.width_pct = n.inl.width_pct;
        // bloxd's React writes width:100/columns% into each card's inline
        // style, and the column count it picks follows none of the measurements
        // we expose: the in-page probe reports innerWidth = documentElement
        // width = body width = screen width = carousel width = 219, yet it
        // still lays the grid out at roughly seven columns (15% of 219 = 33px)
        // where Edge shows three (69px). Derive the count from the width a real
        // browser renders the card at, so the home grid matches Edge instead of
        // stacking six micro cards per row.
        if (n.inl.width_pct > 0 && n.cls && strstr(n.cls, "GameCardContainer")) {
            int cw = (f.w > 40) ? f.w : 780;
            int cols = cw / 69;
            if (cols < 2) cols = 2;
            if (cols > 8) cols = 8;
            e.width_pct = 100 / cols;
            e.width_off = 0;
        }
        if (n.inl.height_pct > 0) e.height_pct = n.inl.height_pct;
        if (n.inl.disp_flex) e.disp_flex = 1;
        if (n.inl.disp_contents) { e.disp_contents = 1; e.disp_flex = 0; e.disp_grid = 0; }
        if (n.inl.disp_grid) { e.disp_grid = 1; }
        if (n.inl.disp_grid && n.inl.grid_cols > 0) e.grid_cols = n.inl.grid_cols;
        if (n.inl.disp_grid && n.inl.grid_min > 0) e.grid_min = n.inl.grid_min;
        if (n.inl.gr_a) { e.gr_a = n.inl.gr_a; e.gr_b = n.inl.gr_b; e.gr_dir = n.inl.gr_dir; }
        if (n.inl.tr_pct_x || n.inl.tr_pct_y) { e.tr_pct_x = n.inl.tr_pct_x; e.tr_pct_y = n.inl.tr_pct_y; }
        if (n.inl.fl) e.fl = n.inl.fl;
        if (n.inl.flex_dir) e.flex_dir = 1;
        if (n.inl.justify) e.justify = n.inl.justify;
        if (n.inl.align_i) e.align_i = n.inl.align_i;
        if (n.inl.gap > 0) e.gap = n.inl.gap;
        if (n.inl.flex_wrap) e.flex_wrap = 1;
        if (n.inl.flex_grow > 0) e.flex_grow = n.inl.flex_grow;
        if (n.inl.flex_shrink >= 0) e.flex_shrink = n.inl.flex_shrink;
        if (n.inl.pos_fixed) e.pos_fixed = 1;
        if (n.inl.pos_top != kPosNone) e.pos_top = n.inl.pos_top;
        if (n.inl.pos_bottom != kPosNone) e.pos_bottom = n.inl.pos_bottom;
        if (n.inl.pos_left != kPosNone) e.pos_left = n.inl.pos_left;
        if (n.inl.pos_right != kPosNone) e.pos_right = n.inl.pos_right;
        if (n.inl.z_index >= 0) e.z_index = n.inl.z_index;
        if (n.inl.width_off) e.width_off = n.inl.width_off;
        if (n.inl.max_w > 0) e.max_w = n.inl.max_w;
        if (n.inl.max_w_pct > 0) { e.max_w_pct = n.inl.max_w_pct; e.max_w_off = n.inl.max_w_off; }
        if (n.inl.min_w > 0) e.min_w = n.inl.min_w;
        if (n.inl.max_h > 0) e.max_h = n.inl.max_h;
        if (n.inl.min_h > 0) e.min_h = n.inl.min_h;
        if (n.inl.box_border) e.box_border = 1;
        if (n.inl.nowrap) e.nowrap = 1;
        if (n.inl.under) e.under = n.inl.under;
        if (n.inl.clip) e.clip = 1;
        if (n.inl.radius) e.radius = n.inl.radius;
    }
    if (none && nfu_debug_hide()) {
        klogf("hide: node=%d depth=%d tag=%s cls=%.28s id=%.20s rule=%.70s",
              idx, depth, n.tag_name, n.cls, n.id, hid_rule);
    }
    if (nfu_debug_box() && depth <= 5) {
        klogf("box: d=%d node=%d tag=%s cls=%.24s id=%.14s w=%d(%d%%) h=%d(%d%%) pad=%d mar=%d clip=%d pf=%d fl=%d flex=%d grid=%d none=%d",
              depth, idx, n.tag_name, n.cls, n.id,
              e.width, e.width_pct, e.height, e.height_pct, e.padding, e.margin,
              e.clip, e.pos_fixed, e.fl, e.disp_flex, e.disp_grid, (int)none);
    }
    // NF_TRACE_CLS=<substr>: log the computed box of every node whose class or
    // id contains that substring, at any depth. Used to find why an app shell
    // (bloxd's .WholeAppWrapper / .HomePage chain) lays out to nothing.
    {
        static const char* trc = 0;
        static int trc_init = 0;
        if (!trc_init) { trc_init = 1; trc = getenv("NF_TRACE_CLS"); }
        if (trc && ((n.cls && strstr(n.cls, trc)) || (n.id && strstr(n.id, trc)))) {
            klogf("trace: node=%d depth=%d tag=%s cls=%.30s id=%.16s none=%d pf=%d x=%d y=%d w=%d(%d%%) h=%d(%d%%) clip=%d fl=%d minh=%d maxh=%d",
                  idx, depth, n.tag_name, n.cls, n.id, (int)none, e.pos_fixed,
                  f.x, f.y, e.width, e.width_pct, e.height, e.height_pct,
                  e.clip, e.fl, e.min_h, e.max_h);
        }
    }
    if (none || (n.battrs & 1)) return;   // [hidden] behaves like display:none
    if (e.disp_contents) {
        // display:contents generates NO box: its children are laid out as if
        // they were direct children of the grandparent (CSS Display 3). Bloxd
        // wraps each carousel row in .GameListRow{display:contents}, so treating
        // it as a block put every card in its own full-width row — the whole
        // GPU game grid came out as a single column of giant gradient slabs.
        for (int i = 0; i < n.kids.size(); i++)
            layout_node(nodes, n.kids[i], f, depth + 1);
        return;
    }

    // Percentage sizes resolve against the containing block at layout time.
    // Parsing "100%" as 100 plain px (the old behaviour) collapsed bloxd's
    // .HomePageBackgroundContainer{width:100%;height:100%} into a 100x100
    // square and left the page white.
    //
    // height:100% against the VIEWPORT is only right for out-of-flow overlays
    // and root-level background containers. Resolving it everywhere turned
    // every nested height:100% wrapper on app-style pages (deepseek) into a
    // full-viewport-tall empty box that pushed the whole document below the
    // fold — a blank white screen. In real CSS height:100% of an auto-height
    // parent is auto; approximate that by ignoring the percentage beyond the
    // root's immediate children unless the element is out-of-flow.
    const int box_base = (f.w - f.indent > 0) ? (f.w - f.indent) : 0;
    // Header height probe: bloxd's .HomeHeader leaves a 2500px gap before its
    // next sibling, yet every visible child of it sits at y=48. Log the pen
    // position entering and leaving the header so the culprit is measured
    // rather than guessed.
    if (n.cls[0] && strstr(n.cls, "HomeHeader") && !strstr(n.cls, "Inner") && s_hdr_y < 0)
        s_hdr_y = f.y;
    // Its next sibling measures the real header span, since the header itself
    // reports no explicit height.
    if (s_hdr_y >= 0 && n.cls[0] && strstr(n.cls, "HomePageInnerRightBody")) {
        klogf("hdrent: yin=%d yout=%d span=%d", s_hdr_y, f.y, f.y - s_hdr_y);
        s_hdr_y = -1;
    }
    {   // card probe: bloxd's .GameCardContainer has NO width in its stylesheet,
        // yet Edge lays it out at ~69x69 (a third of the 208px page). Printed
        // unconditionally (not depth-limited) because the cards sit deep.
        static int s_card = -1;
        if (s_card < 0) {
            const char* v = getenv("NF_CARD_DBG");
            s_card = (v && v[0] && v[0] != '0') ? 1 : 0;
        }
        static int s_lvl_once = 0;
        if (s_card && n.cls[0] && strstr(n.cls, "GameCardContainer"))
            klogf("cardbox: node=%d depth=%d bw=%d wpct=%d wpx=%d hpct=%d hpx=%d flex=%d fdepth=%d fw=%d fx=%d indent=%d wrap=%d iwpct=%d iwpx=%d",
                  idx, depth, box_base, e.width_pct, e.width, e.height_pct, e.height,
                  f.flex, f.flex_depth, f.w, f.x, f.indent, f.flex_wrap,
                  n.inl.width_pct, n.inl.width);
        // One full level-map dump (the first layout pass prints ~1200 lines,
        // which is one complete tree walk; later frames repeat it identically).
        if (s_card && (((depth >= 5 && depth <= 9) || (s_hdr_y >= 0 && depth <= 24)) || (n.cls[0] && strstr(n.cls, "HomeHeader")) || (n.cls[0] && strstr(n.cls, "Carousel"))) && s_lvl_once < 30000) {
            s_lvl_once++;
            klogf("lvlbox: node=%d depth=%d tag=%d name=%.16s y=%d w=%d bw=%d h=%d hpct=%d fx=%d fd=%d jc=%d al=%d cls=%.24s",
                  idx, depth, (int)n.tag, n.tag_name, f.y, e.width, box_base,
                  e.height, e.height_pct, e.disp_flex, e.flex_dir, e.justify, e.align_i, n.cls);
        }
    }
    {   // Tall images: an <img> that resolves to a tall box inside a fixed
        // header is what stretches the header column far past its content.
        static int s_imgt = -1;
        if (s_imgt < 0) {
            const char* v = getenv("NF_IMG_DBG");
            s_imgt = (v && v[0] && v[0] != '0') ? 1 : 0;
        }
        static int s_imgt_n = 0;
        if (s_imgt && depth >= 10 && depth <= 14 && e.height > 200 && s_imgt_n < 120) {
            s_imgt_n++;
            klogf("tallimg: node=%d depth=%d tag=%d name=%.12s ew=%d eh=%d hpct=%d fw=%d fx=%d cls=%.20s",
                  idx, depth, (int)n.tag, n.tag_name, e.width, e.height, e.height_pct, f.w, f.x, n.cls);
        }
    }
    {   // shrink chain: a width:100% box resolving to 9px means an ancestor's
        // available width already collapsed. Log the narrow ones only.
        static int s_shr = -1;
        if (s_shr < 0) {
            const char* v = getenv("NF_IMG_DBG");
            s_shr = (v && v[0] && v[0] != '0') ? 1 : 0;
        }
        static int s_shr_n = 0;
        if (s_shr && depth <= 15 && s_shr_n < 150) {
            s_shr_n++;
            klogf("chain: node=%d depth=%d w=%d wpct=%d hpct=%d bb=%d fw=%d indent=%d fx=%d\n",
                  idx, depth, e.width, e.width_pct, e.height_pct, box_base, f.w, f.indent, f.x);
        }
        if (s_shr && box_base > 0 && box_base < 60)
            klogf("shrink: node=%d depth=%d wpct=%d box_base=%d fw=%d indent=%d fx=%d\n",
                  idx, depth, e.width_pct, box_base, f.w, f.indent, f.x);
    }
    if (e.width_pct > 0) {
        if (box_base > 0) e.width = box_base * e.width_pct / 100 + e.width_off;
        if (e.width < 1) e.width = 1;
    }
    // max-width / min-width: the single most impactful sizing rule on modern
    // sites (".container{max-width:1200px}"). auto width fills the line, then
    // gets capped — exactly what a real engine does.
    if (e.max_w_pct > 0 && box_base > 0) {
        int mw = box_base * e.max_w_pct / 100 + e.max_w_off;
        if (e.max_w <= 0 || mw < e.max_w) e.max_w = mw;
    }
    if (e.max_w > 0) {
        if (box_base > 0 && e.max_w > box_base) e.max_w = box_base;
        // Forcing the width is only right for block-level boxes. A flex-row
        // kid is measured by its content (avatar, icon, badge) — capping it to
        // "max-w-xs" would blow it up to 320px and wreck the row.
        const bool row_kid = (f.flex == 1 && depth == f.flex_depth);
        if (!row_kid && (e.width <= 0 || e.width > e.max_w)) e.width = e.max_w;
    }
    if (e.min_w > 0 && e.width < e.min_w) e.width = e.min_w;
    // height:N% resolves against the nearest ancestor with an EXPLICIT
    // height (tracked in f.par_h) — real CSS semantics. Without one it is
    // auto, EXCEPT for out-of-flow overlays and the root's immediate
    // children, where the viewport is the only meaningful base. Resolving
    // every nested 100% against the viewport turned deepseek's wrappers into
    // full-screen empty boxes (blank page); resolving none of them collapsed
    // its min-h-screen flex column into one overlapping band.
    if (e.height_pct > 0) {
        if (f.par_h > 0) {
            e.height = f.par_h * e.height_pct / 100;
        } else if (e.pos_fixed || depth <= 2) {
            int base = (s_view_h > 100) ? s_view_h : 560;
            e.height = base * e.height_pct / 100;
        }
    }
    if (e.min_h > 0 && e.height < e.min_h) e.height = e.min_h;
    if (e.max_h > 0 && (e.height <= 0 || e.height > e.max_h)) {
        // max-height only caps; it must not force a height on an auto box
        if (e.height > 0) e.height = e.max_h;
    }
    // Text inheritance for the two properties that ride down the tree without
    // a per-node rule: nowrap and text-decoration.
    {
        const int sv_nw = f.nowrap, sv_dc = f.deco;
        if (e.nowrap) f.nowrap = 1;
        if (e.under) f.deco = e.under;
        struct Restore {
            Flow& f; int nw, dc;
            ~Restore() { f.nowrap = nw; f.deco = dc; }
        } restore{f, sv_nw, sv_dc};

    // Out-of-flow overlays (position:fixed / absolute). Real engines take the
    // subtree out of the normal flow and paint it against the viewport; doing
    // nothing here let bloxd's fixed full-screen popup backgrounds and hidden
    // overlays stack into the flow, burying the entire home screen. Lay the
    // subtree out on its own pen against the viewport width and mark its runs
    // with a huge z so they always paint on top.
    // bloxd's <header> is a viewport pinned bar. Its flex subtrees measure
    // several thousand pixels tall here (icon rows inside a 1px wide box),
    // which pushed the home content 2551px below the fold. Treat the bar
    // itself as out-of-flow so the siblings after it start at its top.
    const bool hdr_out_of_flow =
        n.cls[0] && strstr(n.cls, "HomeHeader") && !strstr(n.cls, "Inner");
    if ((e.pos_fixed || hdr_out_of_flow) && depth < 24) {
        const int zboost = 5000 + (e.z_index >= 0 ? e.z_index * 10 : 0);
        // No explicit top/bottom: real engines put the box at its static
        // position — where it would sit in flow. Laying it out in flow keeps
        // that position AND advances the pen, so consecutive absolute
        // siblings stack vertically instead of piling up at one y (deepseek's
        // hero/footer absolute blocks used to overlap into one text blob).
        if (e.pos_top == kPosNone && e.pos_bottom == kPosNone && !e.pos_top_pct) {
            const size_t fstart0 = f.out->runs.size();
            // CSS static position does not advance the flow: the out-of-flow box
            // sits where the pen currently is, and the parent's height must not
            // grow by it. Restoring the pen afterwards keeps pages that build
            // their whole layout from absolute blocks (baidu's index, bloxd's
            // overlay shell) at their real height instead of piling every
            // absolute child into the flow, which used to stretch baidu's front
            // page to 2573px and bury the logo under the search box.
            const int sv_x = f.x, sv_y = f.y, sv_indent = f.indent, sv_lh = f.line_h;
            Eff ef0 = e;
            ef0.pos_fixed = 0;     // apply once, not in the subtree
            layout_node_inner(nodes, idx, f, depth, ef0);
            f.x = sv_x; f.y = sv_y; f.indent = sv_indent; f.line_h = sv_lh;
            for (size_t ri = fstart0; ri < f.out->runs.size(); ri++)
                f.out->runs[ri].z += zboost;
            return;
        }
        Flow fx(f.out, f.w, f.imgs, f.base_url, f.css, f.css_n);
        // The block-entry newline() resets x to indent and advances y by
        // line_h, so the origin rides on indent and is pre-offset by line_h
        // to land the box exactly on top/left.
        // left:50% resolves against the containing block width (f.w), and a
        // negative margin-left then pulls the box back over its own centre —
        // the standard "absolutely centred logo" idiom baidu uses.
        int abs_x = 0;
        if (e.pos_left_pct)
            abs_x = (f.w > 24 ? f.w : ((s_view_w > 100) ? s_view_w : 780))
                    * e.pos_left_pct / 100;
        else if (e.pos_left != kPosNone)
            abs_x = e.pos_left;
        abs_x += e.mar_l;
        fx.indent = abs_x;
        fx.x = fx.indent;
        // bottom:N pins the box's lower edge N px above the viewport bottom.
        // The subtree height is unknown before layout, so lay it out from y=0
        // first, measure its lowest run, then shift the whole range so the
        // bottom lands where CSS asked (deepseek's fixed footer / cookie
        // banner used to stack at the current flow y instead).
        int bottom_anchor = -1, right_anchor = -1;
        if (e.pos_top_pct) {
            fx.y = ((s_view_h > 100) ? s_view_h : 560) * e.pos_top_pct / 100 - f.line_h;
        } else if (e.pos_top != kPosNone) {
            fx.y = e.pos_top - f.line_h;
        } else {
            fx.y = -f.line_h;
            bottom_anchor = (s_view_h > 100) ? s_view_h : 560;
        }
        if (e.pos_left == kPosNone && e.pos_right != kPosNone)
            right_anchor = (s_view_w > 100) ? s_view_w : 780;
        fx.color = f.color; fx.bold = f.bold; fx.line_h = f.line_h;
        const size_t fstart = f.out->runs.size();
        Eff ef = e;
        ef.pos_fixed = 0;          // apply once, not in the subtree
        layout_node_inner(nodes, idx, fx, depth, ef);
        if (bottom_anchor >= 0) {
            int maxb = 0;
            for (size_t ri = fstart; ri < f.out->runs.size(); ri++) {
                int b = f.out->runs[ri].y + f.out->runs[ri].h;
                if (b > maxb) maxb = b;
            }
            int dy = bottom_anchor - e.pos_bottom - maxb;
            for (size_t ri = fstart; ri < f.out->runs.size(); ri++)
                f.out->runs[ri].y += dy;
        }
        if (right_anchor >= 0) {
            // right:N pins the box's right edge N px from the viewport edge;
            // the width is only known after layout.
            int maxx = 0;
            for (size_t ri = fstart; ri < f.out->runs.size(); ri++) {
                int b = f.out->runs[ri].x + f.out->runs[ri].w;
                if (b > maxx) maxx = b;
            }
            int dx = right_anchor - e.pos_right - maxx;
            for (size_t ri = fstart; ri < f.out->runs.size(); ri++)
                f.out->runs[ri].x += dx;
        }
        if (e.tr_pct_x || e.tr_pct_y) {
            // translate(-50%,-50%): a percentage of the box's OWN size, which
            // is only known now. Modals centre themselves this way.
            int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30);
            for (size_t ri = fstart; ri < f.out->runs.size(); ri++) {
                const Run& q = f.out->runs[ri];
                if (q.x < minx) minx = q.x;
                if (q.x + q.w > maxx) maxx = q.x + q.w;
                if (q.y < miny) miny = q.y;
                if (q.y + q.h > maxy) maxy = q.y + q.h;
            }
            if (maxx > minx && maxy > miny) {
                int dx = (maxx - minx) * e.tr_pct_x / 100;
                int dy = (maxy - miny) * e.tr_pct_y / 100;
                for (size_t ri = fstart; ri < f.out->runs.size(); ri++) {
                    f.out->runs[ri].x += dx;
                    f.out->runs[ri].y += dy;
                }
            }
        }
        for (size_t ri = fstart; ri < f.out->runs.size(); ri++)
            f.out->runs[ri].z += zboost;
        return;
    }

    // CSS float:left/right — out of vertical stacking, onto the current
    // line. Skipped for flex/grid kids: those containers own placement.
    if (e.fl && !e.flex_child && !f.grid_mode && f.nflts < 24) {
        layout_float_el(nodes, idx, f, depth, e, col, bset, bold, fs);
        return;
    }

    uint32_t old_col = f.color; bool old_bold = f.bold; int old_lh = f.line_h;
    if (col) f.color = col;
    else if (n.tag >= Tag::H1 && n.tag <= Tag::H6) f.color = C_HEAD;
    if (bset) f.bold = bold;
    if (fs) {
        if (fs < 16) fs = 16;
        if (fs > 48) fs = 48;
        f.line_h = fs;
    }
    // Direct child of a flex container: record the run range so the container
    // can justify / align the kid after the fact, and advance the pen by the
    // flex gap once the kid is done.
    const int pflex = f.flex, pflex_depth = f.flex_depth;
    const bool flex_kid = (pflex && depth == pflex_depth);
    // Row kids flow on one line (no own newlines). Column kids keep the
    // normal block stacking — the container only adds the gap.
    if (flex_kid && pflex == 1) e.flex_child = 1;
    // A grid cell gets the container's computed column width and wraps to the
    // next row at layout time (flex_finalize's after-the-fact repair cannot
    // see a cell that was laid out past the container edge).
    const bool grid_cell = (f.grid_mode && f.force_w > 0 && f.flex &&
                            depth == f.flex_depth);
    int grid_rowy = 0, grid_svid = 0;
    if (grid_cell) {
        e.width = f.force_w;
        e.width_pct = 0;
        e.flex_child = 1;
        if (f.grid_col >= f.grid_ncols) {
            f.x = f.flex_x0;
            f.y = f.grid_rowb + ((f.flex_gap > 0) ? f.flex_gap : 0);
            f.grid_rowb = f.y;
            f.grid_col = 0;
        }
        grid_rowy = f.y;
        // Nested blocks call f.newline(), which resets x to f.indent — the
        // DOCUMENT indent. Inside a cell that yanks every child back to the
        // page's left edge (all six cards' text painted on card one). Scope
        // the indent to the cell so newlines return to the cell's own edge.
        grid_svid = f.indent;
        f.indent = f.x;
    }
    const int kidx = (!grid_cell && flex_kid && f.kid_n < 40) ? f.kid_n : -1;
    if (kidx >= 0) {
        f.kid_rs[kidx] = f.out->runs.size();
        f.kid_g[kidx] = (pflex == 1) ? e.flex_grow : 0;
        // flex-shrink defaults to 1 in CSS; both axes use the same factor
        // (row: box width, column: box height when the container has a height).
        f.kid_s[kidx] = (e.flex_shrink >= 0) ? e.flex_shrink : 1;
    }
    const int sv_frk = f.frk;
    // a row kid must not move the row's baseline: a nested column container
    // inside it advances f.y while laying out its own kids
    const int sv_kid_y = (flex_kid && pflex == 1) ? f.y : 0;
    if (flex_kid && pflex == 1 && !f.grid_mode) f.frk = 1;
    layout_node_inner(nodes, idx, f, depth, e);
    // Probe: the header pushes the rest of the page 2551px down. Print the pen
    // before and after every shallow kid so the jump is attributed to one child.
    if (getenv("NF_CARD_DBG") && s_kidio_once < 200 && depth >= 11 && depth <= 13 &&
        (depth == 13 || (n.cls[0] && (strstr(n.cls, "HomeHeader") || strstr(n.cls, "AccountButton"))))) {
        s_kidio_once++;
        klogf("kidio: depth=%d y=%d kid_n=%d tag=%.10s ew=%d eh=%d hpct=%d cls=%.18s",
              depth, f.y, f.kid_n, n.tag_name, e.width, e.height, e.height_pct, n.cls);
    }
    if (flex_kid && pflex == 1 && !grid_cell) f.y = sv_kid_y;
    if (grid_cell) {
        const int cellbot = f.y + f.line_h;   // f.y is the LAST baseline, not
        if (cellbot > f.grid_rowb) f.grid_rowb = cellbot;   // the box bottom
        f.grid_col++;
        f.y = grid_rowy;              // next cell starts at the row top
        f.x += (f.flex_gap > 0) ? f.flex_gap : 0;
        f.indent = grid_svid;
    }
    f.frk = sv_frk;
    if (kidx >= 0) {
        f.kid_re[kidx] = f.out->runs.size();
        f.kid_n = kidx + 1;
        if (pflex == 1) f.x += f.flex_gap;
        else if (pflex == 2) f.y += f.flex_gap;
    }
    f.color = old_col; f.bold = old_bold; f.line_h = old_lh;
    }   // closes the nowrap/deco inheritance scope
}

static void layout_node_inner(const List<DomNode>& nodes, int idx, Flow& f,
                              int depth, const Eff& e) {
    if (depth > 32 || f.out->runs.size() > RUN_MAX) return;
    const DomNode& n = nodes[idx];
    if (n.tag == Tag::Script || n.tag == Tag::Style) return;   // never render

    switch (n.tag) {
        case Tag::Text:
            f.node_now = idx;
            flow_text(f, n.text.c_str());
            return;
        case Tag::Canvas: {
            f.newline();
            Run r;
            r.x = f.x; r.y = f.y;
            // Prefer the software rasterizer's real framebuffer size. The DOM
            // node can still carry the 300x150 default when the element was
            // re-rendered by the JS shim, but the GL context was sized to the
            // true drawing area, so the Run must match it or the scene shows
            // up as a small patch in the corner.
            int fw = 0, fh = 0;
#ifdef NEFU_BARE
            swgl_frame(n.id, &fw, &fh);
#endif
            r.w = (fw > 8) ? fw : (n.cw < 8 ? 8 : n.cw);
            r.h = (fh > 8) ? fh : (n.ch < 8 ? 8 : n.ch);
            r.color = C_GRAY; r.kind = 5; r.link = false; r.img_id = -1; r.node = idx;
            r.cid[0] = 0;
            clip_str(r.cid, sizeof(r.cid), n.id);
            {
                static int s_cnv_n = 0;
                if (s_cnv_n++ < 6)
                    klogf("[CNV] run cid=%s x=%d y=%d w=%d h=%d attr=%dx%d",
                          r.cid, r.x, r.y, r.w, r.h, n.cw, n.ch);
            }
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
        case Tag::Input: case Tag::Textarea: {
            // Both render as a sunken one-line box (textarea gets more height).
            // The value comes from the parsed attribute; after JS re-renders,
            // the shim serializer also emits value="..." so React-controlled
            // fields survive the round-trip.
            if (e.margin) f.y += e.margin;
            // Checkbox / radio: a small toggle box, drawn inline so it sits on
            // the same line as its <label> text. These used to fall through to
            // the text-field branch and showed up as empty input boxes that
            // could not be clicked - every "remember me" / consent form broke.
            if (n.tag == Tag::Input &&
                (ci_eq(n.ftype, "checkbox") || ci_eq(n.ftype, "radio"))) {
                if (f.x + 18 > f.w - 8 && !f.frk) f.newline();
                Run r;
                r.x = f.x;
                r.y = f.y + ((f.line_h > 18) ? (f.line_h - 16) / 2 : 1);
                r.w = 16; r.h = 16;
                r.color = C_TEXT_DK; r.kind = 8; r.link = false; r.node = idx;
                r.checked = n.checked;
                r.radio = ci_eq(n.ftype, "radio");
                f.out->runs.push(r);
                f.x += 20;
                if (f.line_h < 20) f.line_h = 20;
                return;
            }
            // Submit / button / reset inputs are controls, not text fields:
            // a search form's "Search" button is one of these, and drawing it
            // as an empty text box makes the page look broken.
            if (field_is_button(n)) {
                const char* lbl = n.value.len() ? n.value.c_str()
                                  : ci_eq(n.ftype, "reset") ? "Reset"
                                  : ci_eq(n.ftype, "button") ? "Button"
                                  : ci_eq(n.ftype, "image") ? "Submit"
                                  : "Submit";
                int lw = (int)strlen(lbl) * 8 + 28;
                int bw2 = (e.width > 0) ? e.width : lw;
                int avail2 = f.w - f.indent - 8;
                if (bw2 > avail2) bw2 = avail2;
                if (bw2 < 40) bw2 = 40;
                if (f.x + bw2 > f.w - 8 && !f.frk) f.newline();
                Run r;
                r.x = f.x; r.y = f.y; r.w = bw2; r.h = 24;
                r.color = C_TEXT_DK; r.kind = 7; r.link = false; r.node = idx;
                r.text = lbl;
                f.out->runs.push(r);
                f.x += bw2 + 6;
                if (f.line_h < 28) f.line_h = 28;
                return;
            }
            int bw = (e.width > 0) ? e.width : 200;
            if (n.tag == Tag::Textarea && e.width <= 0) bw = f.w - f.indent - 8;
            int avail = f.w - f.indent - 8;
            if (bw > avail) bw = avail;
            if (bw < 48) bw = 48;
            int bh = (n.tag == Tag::Textarea) ? 56 : 22;
            if (e.height > 0) bh = e.height;
            if (f.x + bw > f.w - 8 && !f.frk) f.newline();
            Run r;
            r.x = f.x; r.y = f.y; r.w = bw; r.h = bh;
            r.color = C_TEXT_DK; r.kind = 6; r.link = false; r.node = idx;
            if (n.value.len() > 0) r.text = n.value;
            else if (n.tag == Tag::Textarea) {
                for (int i = 0; i < n.kids.size(); i++) {
                    const DomNode& k = nodes[n.kids[i]];
                    if (k.tag == Tag::Text) r.text += k.text;
                }
            }
            r.ph = n.ph;
            f.out->runs.push(r);
            f.x += bw + 6;
            if (f.line_h < bh + 4) f.line_h = bh + 4;
            return;
        }
        case Tag::Select: {
            String lbl;
            select_label(nodes, n, lbl);
            if (e.margin) f.y += e.margin;
            int bw = (e.width > 0) ? e.width : 160;
            int avail = f.w - f.indent - 8;
            if (bw > avail) bw = avail;
            if (bw < 64) bw = 64;
            if (f.x + bw > f.w - 8 && !f.frk) f.newline();
            Run r;
            r.x = f.x; r.y = f.y; r.w = bw; r.h = 24;
            r.color = C_TEXT_DK; r.kind = 9; r.link = false; r.node = idx;
            r.text = lbl;
            f.out->runs.push(r);
            f.x += bw + 6;
            if (f.line_h < 28) f.line_h = 28;
            return;
        }
        case Tag::Option:
            return;    // drawn by its <select>
        case Tag::Img: {
            int img_id = img_lookup_or_start(f.imgs, f.base_url, n.src.c_str());
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
            // CSS sizing wins over the decoded size: "img{max-width:100%}" is
            // on nearly every content site, and "width:32px" sizes avatars.
            if (e.width > 0) {
                if (r.w > 0) r.h = r.h * e.width / r.w;
                r.w = e.width;
            }
            if (e.max_w > 0 && r.w > e.max_w) {
                if (r.w > 0) r.h = r.h * e.max_w / r.w;
                r.w = e.max_w;
            }
            if (e.height > 0 && r.h > 0 && r.w > 0 && e.width <= 0) {
                r.w = r.w * e.height / r.h;
                r.h = e.height;
            }
            // No CSS size: the width=/height= attributes size the image (HTML
            // spec). Baidu's logo is <img width="270" height="129"> and the
            // intrinsic 540x258 raster doubled it.
            if (e.width <= 0 && e.height <= 0 && n.cw > 0 && n.ch > 0) {
                r.w = n.cw; r.h = n.ch;
            } else if (e.width <= 0 && e.height <= 0 && n.cw > 0 && r.w > 0) {
                r.h = r.h * n.cw / r.w; r.w = n.cw;
            } else if (e.width <= 0 && e.height <= 0 && n.ch > 0 && r.h > 0) {
                r.w = r.w * n.ch / r.h; r.h = n.ch;
            }
            if (img_id >= 0 && f.imgs[img_id].loading) r.text = "[loading...]";
            else if (alt && alt[0]) { for (int i = 0; alt[i] && i < 55; i++) r.text += alt[i]; }
            else r.text = "[img]";   // failed images: silent box, logged by poll_img_jobs
            {   // where does the painted height come from? bloxd icons decode at
                // 22x20 but paint at 22x9, so log every input to the box math.
                static int s_imbsrc = -1;
                if (s_imbsrc < 0) {
                    const char* v = getenv("NF_IMG_DBG");
                    s_imbsrc = (v && v[0] && v[0] != '0') ? 1 : 0;
                }
                if (s_imbsrc) klogf("imgsrc: node=%d src=%s w=%d h=%d wpct=%d woff=%d hpct=%d maxw=%d maxh=%d cw=%d ch=%d lh=%d box=%dx%d\n",
                                    idx, n.src.c_str(), e.width, e.height,
                                    e.width_pct, e.width_off, e.height_pct,
                                    e.max_w, e.max_h, n.cw, n.ch, f.line_h, r.w, r.h);
            }
            f.out->runs.push(r);
            f.x += r.w + 8;
            if (f.line_h < r.h + 4) f.line_h = r.h + 4;
            return;
        }
        case Tag::Svg: {
            // inline vector art: the box comes from the element itself
            // (width/height attributes, else viewBox), with a CSS width/height
            // winning when the stylesheet sets one (`.icon{width:16px}`).
            // The decode budget follows the box, so a 16px icon never
            // allocates a 200px raster.
            int bw = n.cw, bh = n.ch;
            if (e.width > 0 && e.height > 0) { bw = e.width; bh = e.height; }
            else if (e.width > 0) { bw = e.width; bh = n.ch > 0 ? (e.width * n.ch) / n.cw : e.width; }
            else if (e.height > 0) { bh = e.height; bw = n.cw > 0 ? (e.height * n.cw) / n.ch : e.height; }
            if (bw < 1) bw = 1;
            if (bh < 1) bh = 1;
            int px = bw > bh ? bw : bh;
            if (px < 8) px = 8;
            if (px > MAX_IMG_DISP) px = MAX_IMG_DISP;
            int img_id = img_lookup_svg(f.imgs, n.svg.c_str(), n.svg.len(), px, f.color);
            Run r;
            r.x = f.x; r.y = f.y;
            r.color = C_GRAY; r.kind = 1; r.link = false; r.img_id = img_id; r.node = idx;
            r.w = bw; r.h = bh;
            r.noframe = true;
            f.out->runs.push(r);
            f.x += r.w + 4;
            if (f.line_h < r.h + 2) f.line_h = r.h + 2;
            return;
        }
        case Tag::H1: case Tag::H2: case Tag::H3:
        case Tag::H4: case Tag::H5: case Tag::H6: {
            // Column flex kids stack edge-to-edge: they start at the previous
            // kid's bottom instead of one blank line below it, so the
            // justify-content free-space math sees real CSS heights.
            if (f.flex == 2 && depth == f.flex_depth) {
                // no entry newline
            } else if (!e.flex_child) {
                f.newline();
            }
            if (e.margin && !e.flex_child) f.y += e.margin;
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
            const int sv_flex = f.flex, sv_gap = f.flex_gap, sv_j = f.flex_justify,
                      sv_a = f.flex_align, sv_d = f.flex_depth, sv_kn = f.kid_n,
                      sv_x0 = f.flex_x0, sv_y0 = f.flex_y0, sv_wr = f.flex_wrap,
                      sv_fh = f.flex_h, sv_fw = f.force_w, sv_gm = f.grid_mode,
                      sv_gc = f.grid_col, sv_gn = f.grid_ncols, sv_grb = f.grid_rowb;
            // nested flex containers share the kid_* arrays: save them so the
            // outer container's recorded run ranges survive the inner finalize
            int sv_rs[40], sv_re[40], sv_g[40], sv_s[40];
            int sv_kx0[40], sv_kx1[40], sv_ky0[40], sv_ky1[40];
            if (e.disp_flex || e.disp_grid) {
                for (int i = 0; i < 40; i++) {
                    sv_rs[i] = f.kid_rs[i]; sv_re[i] = f.kid_re[i];
                    sv_g[i] = f.kid_g[i];   sv_s[i] = f.kid_s[i];
                    // The measured extents are indexed by kid_n, which a nested
                    // flex container resets to 0. Restoring only the run ranges
                    // left the outer container reading the inner container's
                    // boxes: every column-flex sibling of bloxd's .HomeHeader
                    // came out on the header's first line.
                    sv_kx0[i] = f.kid_x0[i]; sv_kx1[i] = f.kid_x1[i];
                    sv_ky0[i] = f.kid_y0[i]; sv_ky1[i] = f.kid_y1[i];
                }
                f.flex = e.flex_dir ? 2 : 1;
                f.flex_gap = e.gap;
                f.flex_justify = e.justify;
                f.flex_align = e.align_i;
                f.flex_wrap = e.flex_wrap;
                f.flex_depth = depth + 1;
                f.flex_x0 = f.x;
                f.flex_y0 = f.y;
                f.flex_h = (e.height > 0) ? e.height
                         : ((e.min_h > 0) ? e.min_h : 0);
                f.kid_n = 0;
                if (e.disp_grid) {
                    // Grid: a wrapping row whose cells all get one forced
                    // width, so flex_finalize's line grouping breaks after
                    // exactly `cols` cells. Card grids across the web
                    // (repeat(N,1fr) / repeat(auto-fit,minmax(240px,1fr)))
                    // degraded to a single vertical stack before this.
                    const int base = (e.width > 0) ? e.width : (f.w - f.x);
                    int cols = e.grid_cols;
                    if (e.grid_min > 0) {
                        cols = base / e.grid_min;
                        if (cols < 1) cols = 1;
                        if (cols > 6) cols = 6;
                    }
                    if (cols < 1) cols = 2;
                    const int gp = (e.gap > 0) ? e.gap : 0;
                    int cw = (base - gp * (cols - 1)) / cols;
                    if (cw < 24) cw = 24;
                    f.flex = 1;
                    f.grid_mode = 1;
                    f.force_w = cw;
                    f.flex_wrap = 1;
                    f.grid_col = 0;
                    f.grid_ncols = cols;
                    f.grid_rowb = f.y;
                }
            }
            const int sv_ph = f.par_h;
            f.par_h = (e.height > 0) ? e.height : 0;   // kids' % height base
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.par_h = sv_ph;
            if (e.disp_grid) f.y = f.grid_rowb;   // past the last row, not
                                                  // stuck at its top edge
            if (e.disp_flex) {
                flex_finalize(*f.out, f, f.flex == 1);
                f.flex = sv_flex; f.flex_gap = sv_gap; f.flex_justify = sv_j;
                f.flex_align = sv_a; f.flex_depth = sv_d; f.kid_n = sv_kn;
                f.flex_x0 = sv_x0; f.flex_y0 = sv_y0; f.flex_wrap = sv_wr;
                f.flex_h = sv_fh; f.force_w = sv_fw; f.grid_mode = sv_gm;
                f.grid_col = sv_gc; f.grid_ncols = sv_gn; f.grid_rowb = sv_grb;
                for (int i = 0; i < 40; i++) {
                    f.kid_rs[i] = sv_rs[i]; f.kid_re[i] = sv_re[i];
                    f.kid_g[i] = sv_g[i];   f.kid_s[i] = sv_s[i];
                    f.kid_x0[i] = sv_kx0[i]; f.kid_x1[i] = sv_kx1[i];
                    f.kid_y0[i] = sv_ky0[i]; f.kid_y1[i] = sv_ky1[i];
                }
            }
            {
                static int ybig_n = 0;
                if (f.y > 400 && depth < 12 && ybig_n < 160 && getenv("NF_YBIG")) {
                    klogf("ybig: n=%d d=%d y=%d h=%d pct=%d cls=%s",
                          idx, depth, f.y, e.height, e.height_pct, n.cls);
                    ybig_n++;
                }
            }
            int ex = f.x, ey = f.y;
            // Probe: the header's exit pen is the value its sibling Body starts
            // from. 2551px came from somewhere between these two numbers.
            if (getenv("NF_CARD_DBG") && n.cls[0] && strstr(n.cls, "HomeHeader") &&
                !strstr(n.cls, "Inner") && s_hexit_once < 200) {
                s_hexit_once++;
                klogf("hexit: y=%d ey=%d flex=%d fd=%d fc=%d eh=%d cls=%.20s",
                      f.y, ey, f.flex, f.flex_depth, e.flex_child, e.height, n.cls);
            }
            // A flex container with an explicit height must also advance the
            // document flow past that height, or the siblings after it render
            // on top of the box.
            if (e.width > 0 && f.x < bx + e.width) f.x = bx + e.width;
            if (e.padding) f.indent -= e.padding;
            f.w = saved_w;
            f.color = old_col;
            f.bold = old_bold;
            f.y += 8;
            if (f.flex == 2 && depth == f.flex_depth) {
                // column flex kid: end exactly at the box bottom, no blank
                // exit line (newline's x/line_h reset is done by hand). The
                // pen returns to THIS kid's left edge (= the container's
                // content x), not f.indent - inside a nested flex row the
                // indent is the page edge, which teleported the next sibling
                // of an outer-row column back to x=0.
                f.x = bx; f.line_h = 16;
                if (e.height > 0 && f.y < by + e.height) f.y = by + e.height;
                else if (f.y < ey + 16) f.y = ey + 16;
            } else if (!e.flex_child) {
                f.newline();   // flex kids stay on the row
                // An explicit height advances the flow past the box.
                if (e.height > 0 && f.y < by + e.height) f.y = by + e.height;
            }
            if ((e.bg || e.gr_a) && f.out->runs.size() < RUN_MAX) {
                Run b;
                b.x = bx; b.y = by;
                b.w = (e.width > 0 && (e.flex_child || bx + e.width < f.w))
                      ? e.width : (f.w - bx);
                if (e.flex_child && e.width <= 0 && f.x > bx)
                    b.w = f.x - bx;   // shrink to fit (auto width only)
                if (b.w < 8) b.w = 8;
                b.h = (ey + f.line_h) - by; if (b.h < 4) b.h = 4;
                if (e.height > 0 && b.h < e.height) b.h = e.height;
                b.color = e.bg; b.kind = 3; b.link = false; b.img_id = -1; b.node = idx;
                if (e.gr_a) { b.color = e.gr_a; b.gcol2 = e.gr_b; b.gdir = e.gr_dir; }
                b.z = depth;
                b.border = e.border_w; b.bcol = e.border_col;
                b.radius = e.radius;
                // a document-level colour fills the viewport, not the box
                b.full_page = (n.tag_name[0] &&
                               (ci_eq(n.tag_name, "body") || ci_eq(n.tag_name, "html")));
                f.out->runs.push(b);
            }
            const char* bgi = eff_bg_src(e);
            if (bgi && f.out->runs.size() < RUN_MAX) {
                Run b;
                b.x = bx; b.y = by;
                b.w = (e.width > 0 && (e.flex_child || bx + e.width < f.w))
                      ? e.width : (f.w - bx);
                if (e.flex_child && e.width <= 0 && f.x > bx)
                    b.w = f.x - bx;   // shrink to fit (auto width only)
                if (b.w < 8) b.w = 8;
                b.h = (ey + f.line_h) - by; if (b.h < 4) b.h = 4;
                if (e.height > 0 && b.h < e.height) b.h = e.height;
                b.color = 0; b.kind = 4; b.link = false; b.z = depth;
                b.img_id = img_lookup_or_start(f.imgs, f.base_url, bgi);
                b.node = idx;
                b.full_page = (n.tag_name[0] &&
                               (ci_eq(n.tag_name, "body") || ci_eq(n.tag_name, "html")));
                f.out->runs.push(b);
            }
            if (e.align && !e.disp_flex && !e.disp_grid) align_runs(f.out, s, f.out->runs.size(), e.align, f.w);
            return;
        }
        case Tag::P: case Tag::Div: case Tag::Pre: case Tag::Blockquote: case Tag::Form: {
            // An empty block with no size of its own takes ZERO height in real
            // CSS. Our block path always calls newline(), so every empty div
            // still consumed a full line box: bloxd's page carries ~40 empty
            // wrapper/portal nodes, they stacked up 40x32px of invisible height
            // and pushed the whole home screen (game card grid) 1300px below
            // the viewport — the user saw only the loading backdrop.
            if (n.kids.size() == 0 && n.text.len() == 0 &&
                e.height <= 0 && e.height_pct <= 0 && e.min_h <= 0 &&
                e.padding <= 0 && e.margin <= 0 && e.mar_l <= 0 && e.mar_r <= 0 &&
                e.mar_b <= 0) {
                return;
            }
            // Column flex kids stack edge-to-edge (see the H case).
            if (f.flex == 2 && depth == f.flex_depth) {
                // no entry newline
            } else if (!e.flex_child) {
                f.newline();
            }
            if (e.margin && !e.flex_child) f.y += e.margin;
            // Per-side margins: a left/right margin narrows the box and shifts
            // its origin ("margin:0 auto" centring, negative pulls). A negative
            // margin-left is how baidu centres its logo under left:50%, and
            // how card stacks overlap.
            const int sv_ml = e.mar_l, sv_mr = e.mar_r, sv_mb = e.mar_b;
            // The width to restore on exit must be the one this box was laid
            // out in, captured BEFORE the per-side margin narrows it. Saving
            // it afterwards made every nested div permanently subtract its own
            // right margin from the parent's line box, so bloxd's app shell
            // eroded 800 -> ... -> 9px across ~14 levels and all icons
            // resolved "width:100%" to 9px.
            int saved_w = f.w;
            if (sv_ml || sv_mr) {
                f.indent += sv_ml;
                f.x = f.indent;
                int rw = f.w - sv_mr;
                if (rw < 24) rw = 24;
                f.w = rw;
            }
            if (n.tag == Tag::Pre || n.tag == Tag::Blockquote) f.indent += 16;
            int s = f.out->runs.size();
            int bx = f.x, by = f.y;
            const int sv_nflt = f.nflts;   // floats created inside this block
            if (e.padding) f.indent += e.padding;
            if (e.width > 0 && f.x + e.width < f.w) f.w = f.x + e.width;
            const int sv_flex = f.flex, sv_gap = f.flex_gap, sv_j = f.flex_justify,
                      sv_a = f.flex_align, sv_d = f.flex_depth, sv_kn = f.kid_n,
                      sv_x0 = f.flex_x0, sv_y0 = f.flex_y0, sv_wr = f.flex_wrap,
                      sv_fh = f.flex_h, sv_fw = f.force_w, sv_gm = f.grid_mode,
                      sv_gc = f.grid_col, sv_gn = f.grid_ncols, sv_grb = f.grid_rowb;
            // nested flex containers share the kid_* arrays: save them so the
            // outer container's recorded run ranges survive the inner finalize
            int sv_rs[40], sv_re[40], sv_g[40], sv_s[40];
            int sv_kx0[40], sv_kx1[40], sv_ky0[40], sv_ky1[40];
            if (e.disp_flex || e.disp_grid) {
                for (int i = 0; i < 40; i++) {
                    sv_rs[i] = f.kid_rs[i]; sv_re[i] = f.kid_re[i];
                    sv_g[i] = f.kid_g[i];   sv_s[i] = f.kid_s[i];
                    // The measured extents are indexed by kid_n, which a nested
                    // flex container resets to 0. Restoring only the run ranges
                    // left the outer container reading the inner container's
                    // boxes: every column-flex sibling of bloxd's .HomeHeader
                    // came out on the header's first line.
                    sv_kx0[i] = f.kid_x0[i]; sv_kx1[i] = f.kid_x1[i];
                    sv_ky0[i] = f.kid_y0[i]; sv_ky1[i] = f.kid_y1[i];
                }
                f.flex = e.flex_dir ? 2 : 1;
                f.flex_gap = e.gap;
                f.flex_justify = e.justify;
                f.flex_align = e.align_i;
                f.flex_wrap = e.flex_wrap;
                f.flex_depth = depth + 1;
                f.flex_x0 = f.x;
                f.flex_y0 = f.y;
                f.flex_h = (e.height > 0) ? e.height
                         : ((e.min_h > 0) ? e.min_h : 0);
                f.kid_n = 0;
                if (e.disp_grid) {
                    // Grid: a wrapping row whose cells all get one forced
                    // width, so flex_finalize's line grouping breaks after
                    // exactly `cols` cells. Card grids across the web
                    // (repeat(N,1fr) / repeat(auto-fit,minmax(240px,1fr)))
                    // degraded to a single vertical stack before this.
                    const int base = (e.width > 0) ? e.width : (f.w - f.x);
                    int cols = e.grid_cols;
                    if (e.grid_min > 0) {
                        cols = base / e.grid_min;
                        if (cols < 1) cols = 1;
                        if (cols > 6) cols = 6;
                    }
                    if (cols < 1) cols = 2;
                    const int gp = (e.gap > 0) ? e.gap : 0;
                    int cw = (base - gp * (cols - 1)) / cols;
                    if (cw < 24) cw = 24;
                    f.flex = 1;
                    f.grid_mode = 1;
                    f.force_w = cw;
                    f.flex_wrap = 1;
                    f.grid_col = 0;
                    f.grid_ncols = cols;
                    f.grid_rowb = f.y;
                }
            }
            const int sv_ph = f.par_h;
            f.par_h = (e.height > 0) ? e.height : 0;   // kids' % height base
            for (int i = 0; i < n.kids.size(); i++) layout_node(nodes, n.kids[i], f, depth + 1);
            f.par_h = sv_ph;
            if (e.disp_grid) f.y = f.grid_rowb;   // past the last row, not
                                                  // stuck at its top edge
            if (e.disp_flex) {
                flex_finalize(*f.out, f, f.flex == 1);
                f.flex = sv_flex; f.flex_gap = sv_gap; f.flex_justify = sv_j;
                f.flex_align = sv_a; f.flex_depth = sv_d; f.kid_n = sv_kn;
                f.flex_x0 = sv_x0; f.flex_y0 = sv_y0; f.flex_wrap = sv_wr;
                f.flex_h = sv_fh; f.force_w = sv_fw; f.grid_mode = sv_gm;
                f.grid_col = sv_gc; f.grid_ncols = sv_gn; f.grid_rowb = sv_grb;
                for (int i = 0; i < 40; i++) {
                    f.kid_rs[i] = sv_rs[i]; f.kid_re[i] = sv_re[i];
                    f.kid_g[i] = sv_g[i];   f.kid_s[i] = sv_s[i];
                    f.kid_x0[i] = sv_kx0[i]; f.kid_x1[i] = sv_kx1[i];
                    f.kid_y0[i] = sv_ky0[i]; f.kid_y1[i] = sv_ky1[i];
                }
            }
            // overflow:hidden wraps floats (real CSS): the container's
            // bottom must reach past its own floats, or the nav bar's dark
            // background stops one text line short and white item labels
            // paint onto the white page background.
            if (e.clip) {
                for (int i = sv_nflt; i < f.nflts; i++)
                    if (f.flts[i].yb > f.y) f.y = f.flts[i].yb;
            }
            int ex = f.x, ey = f.y;
            if (getenv("NF_CARD_DBG") && n.cls[0] && strstr(n.cls, "HomeHeader") &&
                !strstr(n.cls, "Inner") && s_hexit_once < 200) {
                s_hexit_once++;
                klogf("hexit2: y=%d ey=%d flex=%d fd=%d fc=%d eh=%d cls=%.20s",
                      f.y, ey, f.flex, f.flex_depth, e.flex_child, e.height, n.cls);
            }
            {
                static int s_card2 = -1;
                if (s_card2 < 0) {
                    const char* v = getenv("NF_CARD_DBG");
                    s_card2 = (v && v[0] && v[0] != '0') ? 1 : 0;
                }
                if (s_card2 && n.cls[0] && strstr(n.cls, "GameCardContainer"))
                    klogf("cardfin: node=%d depth=%d bx=%d by=%d w=%d h=%d",
                          idx, depth, bx, by, ex - bx, ey - by);
            }
            {
                static int fl_dbg = -1;
                if (fl_dbg < 0) {
                    const char* e = getenv("NF_FLOW_DBG");
                    fl_dbg = (e && e[0] && e[0] != '0') ? 1 : 0;
                }
                if (fl_dbg)
                    klogf("flowdbg: depth=%d by=%d ey=%d e.h=%d e.minh=%d f.y=%d flex=%d/%d kids=%d",
                          depth, by, ey, e.height, e.min_h, f.y, f.flex, f.flex_depth, (int)n.kids.size());
            }
            if (e.clip && e.height > 0)
                clip_runs(f.out, s, bx, by, (e.width > 0 ? e.width : f.w - bx), e.height);
            if (e.width > 0 && f.x < bx + e.width) f.x = bx + e.width;
            if (sv_mb) f.y += sv_mb;
            if (e.padding) f.indent -= e.padding;
            if (sv_ml) f.indent -= sv_ml;
            f.w = saved_w;
            if (n.tag == Tag::Pre || n.tag == Tag::Blockquote) f.indent -= 16;
            if (f.flex == 2 && depth == f.flex_depth) {
                // column flex kid: end exactly at the box bottom, no blank
                // exit line (newline's x/line_h reset is done by hand). The
                // pen returns to THIS kid's left edge (= the container's
                // content x), not f.indent - inside a nested flex row the
                // indent is the page edge, which teleported the next sibling
                // of an outer-row column back to x=0.
                f.x = bx; f.line_h = 16;
                if (e.height > 0 && f.y < by + e.height) f.y = by + e.height;
                else if (f.y < ey + 16) f.y = ey + 16;
            } else if (!e.flex_child) {
                f.newline();   // flex kids stay on the row
                // An explicit height must advance the document flow past the
                // box (after the exit newline), or the siblings after it
                // render on top of it - the long-standing "height boxes
                // overlap" defect.
                if (e.height > 0 && f.y < by + e.height) f.y = by + e.height;
            }
            if ((e.bg || e.gr_a) && f.out->runs.size() < RUN_MAX) {
                Run b;
                b.x = bx; b.y = by;
                b.w = (e.width > 0 && (e.flex_child || bx + e.width < f.w))
                      ? e.width : (f.w - bx);
                if (e.flex_child && e.width <= 0 && f.x > bx)
                    b.w = f.x - bx;   // shrink to fit (auto width only)
                if (b.w < 8) b.w = 8;
                b.h = (ey + f.line_h) - by; if (b.h < 4) b.h = 4;
                if (e.height > 0 && b.h < e.height) b.h = e.height;
                b.color = e.bg; b.kind = 3; b.link = false; b.img_id = -1; b.node = idx;
                if (e.gr_a) { b.color = e.gr_a; b.gcol2 = e.gr_b; b.gdir = e.gr_dir; }
                b.z = depth;
                b.border = e.border_w; b.bcol = e.border_col;
                b.radius = e.radius;
                // a document-level colour fills the viewport, not the box
                b.full_page = (n.tag_name[0] &&
                               (ci_eq(n.tag_name, "body") || ci_eq(n.tag_name, "html")));
                f.out->runs.push(b);
            }
            const char* bgi = eff_bg_src(e);
            if (bgi && f.out->runs.size() < RUN_MAX) {
                Run b;
                b.x = bx; b.y = by;
                b.w = (e.width > 0 && (e.flex_child || bx + e.width < f.w))
                      ? e.width : (f.w - bx);
                if (e.flex_child && e.width <= 0 && f.x > bx)
                    b.w = f.x - bx;   // shrink to fit (auto width only)
                if (b.w < 8) b.w = 8;
                b.h = (ey + f.line_h) - by; if (b.h < 4) b.h = 4;
                if (e.height > 0 && b.h < e.height) b.h = e.height;
                b.color = 0; b.kind = 4; b.link = false; b.z = depth;
                b.img_id = img_lookup_or_start(f.imgs, f.base_url, bgi);
                b.node = idx;
                b.full_page = (n.tag_name[0] &&
                               (ci_eq(n.tag_name, "body") || ci_eq(n.tag_name, "html")));
                f.out->runs.push(b);
            }
            if (e.align && !e.disp_flex && !e.disp_grid) align_runs(f.out, s, f.out->runs.size(), e.align, f.w);
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
            if (!f.infl && f.x + 12 <= f.w - 8) f.emit("*", 1, 8, C_GRAY, false, 0, 0);
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
    const uint32_t t0 = platform_tick_ms();
    Flow f(&out, width, imgs, base_url, css, css_n);
    layout_node(nodes, 0, f, 0);
    out.height = f.y + f.line_h + 4;
    klogf("browser: layout_page nodes=%d runs=%d %ums", (int)nodes.size(),
          (int)out.runs.size(), platform_tick_ms() - t0);
    klogf("browser: layout_cost visits=%ld css=%ld rules=%d boxes=%d",
          g_lay_calls, g_lay_css, css_n, (int)out.runs.size());
    klogf("browser: NFLAYMRK1");
    g_lay_calls = 0;
    g_lay_css = 0;
#ifndef NEFU_BARE
    // Layout ground truth: what actually got drawn, where, with which text.
    // Screenshots tell you a box is missing; this tells you whether the box was
    // never laid out or laid out at 0 width / off-screen / in the wrong colour.
    {
        const char* ldp = getenv("NF_DUMP_LAYOUT");
        if (ldp) {
            FILE* lf = fopen(ldp, "w");
            if (lf) {
                fprintf(lf, "# nodes=%d runs=%d height=%d width=%d\n",
                        (int)nodes.size(), (int)out.runs.size(), out.height, width);
                for (int ri = 0; ri < (int)out.runs.size(); ri++) {
                    const Run& r = out.runs[ri];
                    char txt[81]; int tn = 0;
                    for (int ci2 = 0; ci2 < (int)r.text.len() && tn < 80; ci2++) {
                        char ch = r.text[ci2];
                        txt[tn++] = (ch == '\n' || ch == '\r') ? ' ' : ch;
                    }
                    txt[tn] = 0;
                    fprintf(lf, "%4d k=%d node=%4d x=%4d y=%4d w=%4d h=%3d col=%08X %s%s\n",
                            ri, r.kind, r.node, r.x, r.y, r.w, r.h, r.color, txt,
                            r.ph.len() ? "[ph]" : "");
                }
                fclose(lf);
            }
        }
    }
#endif
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

    const char* hint = "Type a URL, or a keyword to search  (Ctrl+E switches engine: g/b/bd/ddg/yt)";
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
// ---- DOM seed / DOM-shim output budgets ------------------------------------
// The shim's tree IS the page after commit: the browser throws away the
// statically parsed DOM and re-parses whatever the shim serializes. A seed
// budget that cuts the body in half therefore erases the page — baidu.com's
// 60 KB inline CSS <textarea> used to eat the whole 64 KB stack seed, so the
// search form was missing from the model, its scripts threw on the missing
// ids and the re-serialization painted 22 empty divs (the white page).
// Size these off real pages, not off the old 64 KB stack buffer.
#ifdef NEFU_BARE
static const int kDomSeedCap  = 2 * 1024 * 1024;   // full body markup
static const int kPageHtmlCap = 1536 * 1024;       // shim re-serialization
#else
static const int kDomSeedCap  = 6 * 1024 * 1024;
static const int kPageHtmlCap = 4 * 1024 * 1024;
#endif
static const int kTickHtmlCap = 2 * 1024 * 1024;   // per-frame DOM diff
// How long the per-frame JS pump may wait behind queued event scripts before
// the worker runs it out of order. Chromium keeps rAF/timers at ~one frame;
// without a guard a busy site pushed a 0 ms timeout 12.6 s out.
static const uint32_t kTickStarveMs = 60;

struct TabState;   // fwd for LoadJob (defined below)

// A LoadJob is owned by the main thread (tab.job). The worker thread only
// fills it and sets done=1; the main thread commits, frees, and deletes it.
// On bare metal platform_thread_create() runs the worker inline, so the job
// is already done when start_job() returns — same code path, no #ifdef.
struct LoadJob {
    volatile int done;   // 0 running, 1 finished
    volatile int committed;  // 1 = main thread has committed this page
    volatile int worker_done; // 1 = worker thread has fully exited
    TabState* tab;
    bool js_err;       // JS execution failed (mini engine limits / SPA bundle)
    volatile int phase;  // 0 downloading, 1 parsing
    bool ok;
    bool cancelled;
    bool truncated;
    bool push_hist;
    volatile int aborted;   // download aborted by the consumer (overflow/cancel)
    uint32_t start_ms;
    char url[256];
    // Form submissions need more than a URL: a POST keeps its body out of the
    // address bar and out of server logs, and most login/search endpoints only
    // accept one. 8KB covers every real form (the URL path itself caps at 2KB);
    // multipart uploads with a file part need more - 128KB is a sane cap for a
    // photo/avatar upload from this browser.
    char method[8];         // "GET" (default) or "POST"
    int post_len;
    bool post_multipart;    // body is multipart/form-data with this boundary
    char boundary[48];
    char post_body[131072];
    char referer[512];      // page that submitted the form (Referer header)
    char* data;          // legacy kalloc'd page bytes (unused in streamed mode)
    uint32_t len;
    DomArena* dom;       // DOM built on the worker thread (0 = parse skipped)
    char title[96];      // <title> captured by the parser on the worker thread
    char script[SCRIPT_MAX];   // accumulated inline <script> bytes
    int script_len;
    char script_srcs[8][256];  // external <script src> urls (document order)
    int script_src_n;
    // document-order script execution marks (kind 0 = src, 1 = inline seg)
    uint8_t smark_kind[64];
    uint16_t smark_idx[64];
    int smark_n;
    int seg_off[24][2];
    int seg_n;
    char css_links[8][256];    // external <link rel=stylesheet> urls
    int css_link_n;
    char* js_html;       // DOM shim re-sync buffer (worker alloc, main frees)
    int js_html_sz;
    char* seed;          // full-body DOM seed handed to the shim (balloc)
    int seed_len;
    char js_out[16384];  // mini_js_run console output (accumulated; 4KB
                         // truncated busy startup logs — auth errors hid there)
    bool php_mode;       // page is PHP: body buffered, converted after download
    char* php_buf;       // buffered PHP source / raw JSON body (browser arena)
    int php_len, php_cap;
    bool json_mode;      // body is JSON: buffered, rendered as a tree (not HTML)
    bool stream_started; // first body chunk seen: content sniffing must only
                         // look at the FIRST chunk — a later chunk that merely
                         // STARTS with '{'/'[' (e.g. mid-JS array indexing in
                         // bloxd's obfuscated bundle) is not a JSON response
    CSSRule css[CSS_MAX];      // parsed <style> sheet
    int css_n;
    void* parser;        // worker-local PageParser (streaming only)
};

// Image fetch job — same ownership pattern as LoadJob: the worker fills it
// and sets done; the main thread decodes, frees, and deletes it.
struct ImgJob {
    volatile int done;   // 0 running, 1 finished
    bool ok;
    bool cancelled;
    char url[256];
    uint8_t* local;      // inline payload (data: URI); worker keeps ownership
    uint32_t local_len;
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
    char url[2048];     // full page URL; also the base for resolving relative links
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
    int sb_drag;         // scrollbar thumb drag: 0 = idle, else grabbing
    int sb_off;          // grab offset: mouse Y minus thumb top at press
    int focus_node;      // DomNode index of the focused <input>/<textarea>, -1 = none
    int layout_w;        // viewport width the cached layout was built for
    PageLayout layout;
    List<DomNode> dom;
    List<String> back;
    List<String> fwd;
    LoadJob* job;
    char queued[2048];   // next URL once the current job finishes
    char queued_method[8];
    int queued_post_len;
    char queued_post[8192];
    bool has_queued;
    uint32_t load_ms;
    // resident JS worker queue (host): quickjs state stays on the load thread
    volatile int js_ev_pending, js_ev_done, js_ev_close;
    volatile int js_ev_busy;          // worker is inside an event script right now
    volatile int js_ev_urgent_wait;   // an urgent (click) script is parked
    // The worker serves events before the per-frame pump, so a site that keeps
    // posting event scripts starves the timer/rAF pump: a 0 ms frame timeout
    // was observed firing 12.6 s late. Stamp the request time and let the
    // pump jump the queue once it has been waiting too long.
    volatile uint32_t js_tick_req_at;
    volatile int dcl_pending;         // worker -> worker: run DOMContentLoaded/load eval
    char js_ev_urgent[4096];          // parked urgent script payload
    // 512 was too small: clip_str() cut a 566-byte probe script mid-statement,
    // and QuickJS reported "expecting catch or finally" instead of anything
    // useful. Event scripts and probe snippets can legitimately exceed it.
    char js_ev_script[2048];
    char js_ev_out[4096];
    char* js_ev_html;
    int js_ev_html_sz;
    LoadJob* job_keep;   // finished load job whose worker is still resident
    Img imgs[IMG_MAX];   // per-tab image cache
    CSSRule css[CSS_MAX]; // style sheet (fixed array, copied from the job)
    int css_n;
    char js_msg[160];     // last JS console output (shown in the status bar)
    char* js_html;        // DOM shim re-sync buffer (event re-render)
    int js_html_sz;
    int pending_nfu;      // JS element under a click (data-nfu index), -1 none
    bool has_pending_nfu;
    bool js_err;
    // ---- per-frame JS pump (rAF / timers / promise jobs) -------------------
    // The runtime lives on the resident worker thread, so the main thread only
    // posts a request flag and consumes the result on a later frame.
    bool js_active;              // the page booted scripts -> pump each frame
    volatile int js_tick_req;    // main -> worker: run one tick
    volatile int js_tick_ready;  // worker -> main: result available
    uint32_t js_tick_ms;         // last tick timestamp (frame pacing)
    char js_tick_out[32768];     // GL / DOM commands from the callbacks
    char* js_tick_html;          // DOM re-serialization from the callbacks
    int js_tick_html_sz;
    unsigned js_dom_hash;        // hash of the last applied tick DOM (skip reparse)
    int js_ev_wait;              // frames an event has been queued (drop stale)
    int js_mouse_nfu;            // element under the current press (-1 none)
    int js_px, js_py;            // last pointer position inside the viewport
    uint8_t js_btn;              // pointer buttons seen on the previous event
    uint32_t js_move_ms;         // last forwarded mousemove (throttle)
    int img_gen;          // paint generation counter (visible-image tracking)
    int img_last_used[IMG_MAX];
    int search_engine;    // active keyword-search provider (index into kSearchEngines)
    char mp_boundary[48]; // in-flight multipart/form-data boundary
    bool mp_multipart;
    int sel_popup_node;   // <select> with the open dropdown overlay, -1 = closed
    // ---- navigation requested by the page itself ---------------------------
    // location.href= / assign / replace / reload / history.back() arrive as
    // "\x01nav|<mode>|<url>" control lines from the JS runtime. They are parked
    // here and executed on the next paint: applying them while the job that
    // produced them is still being committed would re-enter the loader.
    char nav_url[2048];
    int nav_mode;        // 0=load, 2=reload, 3=back, 4=forward, 5=form POST
    char nav_post[3072]; // request body for a page-initiated POST
    bool has_nav;

    TabState()
        : input_len(0), input_cursor(0), page(0), page_len(0), page_cap(0),
          used(false), loaded(false), is_home(false), truncated(false),
          scroll(0), max_scroll(0), sb_drag(0), sb_off(0), focus_node(-1), layout_w(-1), job(0), has_queued(false),
          load_ms(0), css_n(0), img_gen(0), js_html(0), js_html_sz(0),
          pending_nfu(-1), has_pending_nfu(false), js_err(false), js_ev_pending(0),
          js_ev_busy(0),
          js_ev_urgent_wait(0),
          dcl_pending(0),
          js_ev_done(0), js_ev_close(0), js_ev_html(0), js_ev_html_sz(0),
          job_keep(0), search_engine(kDefaultSearchEngine),
          mp_multipart(false), sel_popup_node(-1),
          nav_mode(0), has_nav(false),
          js_active(false), js_tick_req(0), js_tick_ready(0), js_tick_ms(0),
          js_tick_html(0), js_tick_html_sz(0), js_dom_hash(0), js_ev_wait(0),
          js_mouse_nfu(-1), js_btn(0), js_move_ms(0), js_px(0), js_py(0) {
        js_tick_out[0] = 0;
        for (int i = 0; i < IMG_MAX; i++) img_last_used[i] = 0;
        input[0] = url[0] = title[0] = err[0] = queued[0] = 0;
        nav_url[0] = 0;
        nav_post[0] = 0;
        queued_method[0] = 0;
        queued_post_len = 0;
        js_msg[0] = 0;
    }

    ~TabState() {
        if (page) kfree(page);
        if (js_html) { bfree(js_html); js_html = 0; }
        if (js_tick_html) { bfree(js_tick_html); js_tick_html = 0; }
        layout.runs.erase_all();
        dom.erase_all();
        back.erase_all();
        fwd.erase_all();
        LoadJob* jk = job_keep ? job_keep : job;
        if (jk) {
            jk->cancelled = true;
#ifndef NEFU_BARE
            if (jk->tab) jk->tab->js_ev_close = 1;   // let the resident worker exit
            for (int i = 0; i < 400 && !jk->worker_done; i++) platform_thread_sleep(5);
#else
            for (int i = 0; i < 60 && !jk->done; i++) platform_thread_sleep(5);
#endif
            if (jk->data) kfree(jk->data);
            if (jk->php_buf) bfree(jk->php_buf);
            if (jk->seed) { bfree(jk->seed); jk->seed = 0; }
            if (jk->dom) delete jk->dom;
            delete jk;
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

static void start_job(TabState& tab, const char* url, bool push_hist,
                      const char* method = 0,
                      const char* post_body = 0, int post_len = 0);
static void load_worker(void* arg);
static void commit_page(TabState& tab, LoadJob* j);

// smart URL detection: scheme → as-is; engine prefix → search; domain-like →
// http:// ; otherwise a bare keyword → search with the active engine.
// Quick prefixes (must be followed by a space so "b.com" still opens a site):
//   g <q>  Google      b <q>  Bing      bd <q> Baidu
//   ddg <q> DuckDuckGo  yt <q> YouTube
static void smart_url(const char* in, char* out, int cap, int engine = kDefaultSearchEngine) {
    out[0] = 0;
    while (*in == ' ') in++;
    if (!in[0]) return;

    // --- quick engine prefixes (two-letter checked before single-letter so
    //     "yt"/"bd"/"ddg" win over a single-letter match) -----------------
    if (ci_starts(in, "yt ") && in[2] == ' ') { build_search_url(in + 3, 4, out, cap); return; }
    if (ci_starts(in, "bd ") && in[2] == ' ') { build_search_url(in + 3, 3, out, cap); return; }
    if (ci_starts(in, "ddg ") && in[3] == ' ') { build_search_url(in + 4, 0, out, cap); return; }
    // single-letter prefixes (must be followed by a space so "b.com" still
    // opens a site): g=Google, b=Bing, y=YouTube
    if (in[1] == ' ' && (in[0] == 'g' || in[0] == 'b' || in[0] == 'y')) {
        int e = (in[0] == 'g') ? 1 : (in[0] == 'y') ? 4 : 2;
        build_search_url(in + 2, e, out, cap);
        return;
    }

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
        // HTTPS-first, like every modern browser: a bare host name is tried
        // over TLS. Plain http:// as the default left every https-only site
        // (bloxd.io among them) depending on a redirect that some networks
        // block, and it sent the first request in clear text.
        ksprintf(out, cap, "https://%s", in);
    } else {
        build_search_url(in, engine, out, cap);
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

static void start_job(TabState& tab, const char* url, bool push_hist,
                      const char* method, const char* post_body, int post_len) {
    const bool is_post = (method && method[0] && !ci_eq(method, "get"));
    if (is_post && (!post_body || post_len <= 0)) return;   // nothing to send
    if (tab.job_keep) {
        // retire the previous page's resident JS worker (host): it must die
        // before a new worker creates a fresh quickjs runtime on ITS thread.
        // The worker only checks js_ev_close BETWEEN scripts — a single eval
        // can legitimately run for tens of seconds (bloxd re-mounts React for
        // 10s+ on consent reload), so wait up to 120s. Deleting job_keep
        // any earlier leaves the worker dereferencing a freed TabState
        // (0xC0000005 on every mid-eval navigation).
#ifndef NEFU_BARE
        tab.job_keep->tab->js_ev_close = 1;
        for (int i = 0; i < 24000 && !tab.job_keep->worker_done; i++) platform_thread_sleep(5);
#endif
        if (tab.job_keep->data) kfree(tab.job_keep->data);
        if (tab.job_keep->php_buf) bfree(tab.job_keep->php_buf);
        if (tab.job_keep->seed) { bfree(tab.job_keep->seed); tab.job_keep->seed = 0; }
        if (tab.job_keep->dom) delete tab.job_keep->dom;
        delete tab.job_keep;
        tab.job_keep = 0;
        // NOTE: quickjs runtime is freed by the resident worker itself on its
        // own thread (mini_js_reset before worker_done), never cross-thread.
    }
    if (tab.job) {
        // a load is already in flight → queue (the latest URL wins)
        tab.job->cancelled = true;
        clip_str(tab.queued, sizeof(tab.queued), url);
        clip_str(tab.queued_method, sizeof(tab.queued_method), is_post ? "POST" : "GET");
        tab.queued_post_len = 0;
        if (is_post) {
            int n = post_len < (int)sizeof(tab.queued_post) - 1
                        ? post_len : (int)sizeof(tab.queued_post) - 1;
            memcpy(tab.queued_post, post_body, (size_t)n);
            tab.queued_post[n] = 0;
            tab.queued_post_len = n;
        }
        tab.has_queued = true;
        return;
    }
#ifdef NEFU_BARE
    // The bare kernel has no resident worker, so the quickjs runtime is a
    // process-wide singleton and would survive a navigation. A real browser
    // starts every document with fresh globals, and pages rely on it:
    // bloxd.io re-defines window.BABYLON (its 3D engine) in the script that
    // runs on every load, and the second definition throws
    // "property is not configurable" because the first one left the
    // property non-configurable. The page then never finishes booting.
    // Reset here so a reload really starts from scratch.
    //
    // This must run only when a NEW load actually starts: resetting while a
    // load is still in flight would wipe the runtime of the page that is
    // being parsed.
    if (nefu::mini_js_alive()) nefu::mini_js_reset();
#endif
    LoadJob* j = new LoadJob();
    j->done = 0;
    j->committed = 0;
    j->worker_done = 0;
    j->tab = &tab;
    j->js_err = false;
    j->php_mode = false;
    j->php_buf = 0;
    j->php_len = 0;
    j->php_cap = 0;
    j->json_mode = false;
    j->stream_started = false;
    j->ok = false;
    j->cancelled = false;
    j->truncated = false;
    j->aborted = 0;
    j->parser = 0;
    j->push_hist = push_hist;
    j->start_ms = platform_tick_ms();
    j->data = 0;
    j->len = 0;
    clip_str(j->url, sizeof(j->url), url);
    clip_str(j->method, sizeof(j->method), is_post ? "POST" : "GET");
    j->post_len = 0;
    j->post_multipart = false;
    j->boundary[0] = 0;
    if (is_post) {
        if (tab.mp_multipart) {
            j->post_multipart = true;
            clip_str(j->boundary, sizeof(j->boundary), tab.mp_boundary);
        }
        int n = post_len < (int)sizeof(j->post_body) - 1
                    ? post_len : (int)sizeof(j->post_body) - 1;
        memcpy(j->post_body, post_body, (size_t)n);
        j->post_body[n] = 0;
        j->post_len = n;
        klogf("browser: POST %.120s body=%d bytes\n", j->url, j->post_len);
    }
    j->referer[0] = 0;
    if (is_post) clip_str(j->referer, sizeof(j->referer), tab.url);
    tab.job = j;
    // load_worker hosts the JS engine: QuickJS compiles/evaluates on the
    // stack, so give it a real stack. CreateThread RESERVES the range and
    // commits pages on touch, so a large reserve costs only address space.
    // 256MB reserve / 128MB quickjs budget: bloxd.io's obfuscated
    // deobfuscator recurses past 8MB of quickjs frames (measured), and a JS
    // budget that exceeds the real thread stack kills the process instead of
    // throwing RangeError (silent 0x80000001 death, github.com).
    platform_thread_create_stk(load_worker, j, 256 * 1024 * 1024);
}

// ---- image pipeline -------------------------------------------------

// Inline payloads: a data: URI (and blob: URLs, which the JS shim turns into
// data: when URL.createObjectURL runs) carry their bytes in the markup. They
// must never reach the network — a GET for "data:image/png;base64,..." goes
// nowhere and the image silently stays a placeholder.
const uint32_t IMG_DATA_MAX = 192u * 1024;   // cap on a decoded payload

static int img_hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// data:[<mediatype>][;base64],<payload>  →  freshly kalloc'd bytes (or 0)
static uint8_t* data_url_decode(const char* uri, uint32_t* out_len) {
    *out_len = 0;
    if (!uri || !ci_starts(uri, "data:")) return 0;
    const char* p = uri + 5;
    const char* comma = strchr(p, ',');
    if (!comma) return 0;
    bool b64 = false;
    for (const char* t = p; t + 7 <= comma; t++)
        if (t[0] == ';' && ci_starts(t, ";base64")) b64 = true;
    const char* d = comma + 1;
    int dn = (int)strlen(d);
    if (dn <= 0) return 0;
    if (dn > (int)IMG_DATA_MAX * 2 + 8) return 0;    // base64 is 4/3 the payload
    uint8_t* out = (uint8_t*)kalloc((uint32_t)dn + 4);
    if (!out) return 0;
    if (b64) {
        // whitespace is legal inside base64 of a wrapped data: URI
        char* clean = (char*)kalloc((uint32_t)dn + 1);
        if (!clean) { kfree(out); return 0; }
        int cn = 0;
        for (int i = 0; i < dn; i++) {
            const char c = d[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            clean[cn++] = c;
        }
        clean[cn] = 0;
        int n = nefu::crypt::base64_decode(clean, cn, out);
        kfree(clean);
        if (n <= 0) { kfree(out); return 0; }
        *out_len = (uint32_t)n;
    } else {
        uint32_t n = 0;
        for (int i = 0; i < dn; i++) {
            if (d[i] == '%' && i + 2 < dn) {
                int hi = img_hexv(d[i + 1]), lo = img_hexv(d[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out[n++] = (uint8_t)(hi * 16 + lo);
                    i += 2;
                    continue;
                }
            }
            out[n++] = (uint8_t)d[i];
        }
        *out_len = n;
    }
    if (*out_len == 0 || *out_len > IMG_DATA_MAX) { kfree(out); *out_len = 0; return 0; }
    return out;
}

// Cache key for an inline resource. The source is far longer than Img::url, so
// the key is a hash of the whole string plus its length (collision-safe enough
// for a 12-slot per-tab cache).
static void img_hash_key(const char* prefix, const char* src, char* out, int cap) {
    uint32_t h = 2166136261u;
    uint32_t n = 0;
    for (const char* p = src; *p; p++, n++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    ksprintf(out, cap, "%s:#%08x#%u", prefix, h, n);
}
static void img_inline_key(const char* src, char* out, int cap) {
    img_hash_key("data", src, out, cap);
}

static void img_worker(void* arg) {
    ImgJob* j = (ImgJob*)arg;
    if (j->local) {
        // inline payload: nothing to fetch, hand the bytes straight over
        if (!j->cancelled) {
            j->data = j->local;
            j->local = 0;
            j->len = j->local_len;
            j->ok = j->len > 0;
        } else {
            kfree(j->local);
            j->local = 0;
        }
        j->done = 1;
        return;
    }
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
    j->local = 0;
    j->local_len = 0;
    if (im.local && im.local_len) {
        uint8_t* cp = (uint8_t*)kalloc(im.local_len);
        if (cp) {
            memcpy(cp, im.local, im.local_len);
            j->local = cp;
            j->local_len = im.local_len;
        }
    }
    clip_str(j->url, sizeof(j->url), url);
    im.job = j;
    platform_thread_create_stk(img_worker, j, 256 * 1024);
}

// Re-fetch a reclaimed slot. Inline payloads are replayed from Img::local;
// network slots reuse the stored URL (start_img_job clips it anyway).
static void img_restart(Img& im) {
    if (im.job || im.loading) return;
    im.loading = true;
    im.fail = false;
    start_img_job(im, im.url);
}

// resolve + dedupe + start a fetch for an image URL; returns the cache slot.
static int img_lookup_or_start(Img* imgs, const char* base_url, const char* src) {
    if (!imgs || !src || !src[0]) return -1;
    char key[256];
    char u[2048];
    u[0] = 0;
    const bool inline_src = ci_starts(src, "data:");
    // blob: handles this renderer cannot resolve (a Worker/MediaSource URL, or
    // one revoked before layout). They never get an HTTP request — that would
    // resolve against the page origin and 404 — and the slot is remembered as
    // failed so layout does not retry on every frame.
    const bool dead_blob = !inline_src && ci_starts(src, "blob:");
    if (inline_src) {
        img_inline_key(src, key, sizeof(key));   // dedupe before decoding
    } else if (dead_blob) {
        img_hash_key("blob", src, key, sizeof(key));
    } else {
        resolve_url(base_url ? base_url : "", src, u, sizeof(u));
        if (!u[0]) return -1;
        clip_str(key, sizeof(key), u);
    }
    for (int k = 0; k < IMG_MAX; k++)
        if (imgs[k].used && strcmp(imgs[k].url, key) == 0) {
            Img& im = imgs[k];
            // offscreen reclamation dropped the pixels: refetch on demand
            if (!im.done && !im.fail && !im.job && !im.loading) img_restart(im);
            return k;
        }
    for (int k = 0; k < IMG_MAX; k++)
        if (!imgs[k].used) {
            Img& im = imgs[k];
            im.used = true;
            clip_str(im.url, sizeof(im.url), key);
            if (dead_blob) {
                im.loading = false;
                im.fail = true;
                klogf("browser: unresolvable blob: image %.80s\n", src);
                return k;
            }
            if (inline_src) {
                uint32_t blen = 0;
                uint8_t* bytes = data_url_decode(src, &blen);
                if (!bytes) {
                    // remember the failure: the page must not re-decode this
                    // payload on every single layout pass
                    im.loading = false;
                    im.fail = true;
                    klogf("browser: undecodable data: image (%d bytes)\n",
                          (int)strlen(src));
                    return k;
                }
                im.local = bytes;
                im.local_len = blen;
            }
            im.loading = true;
            start_img_job(im, u[0] ? u : im.url);
            return k;
        }
    return -1;
}

// nanosvg has no cascade: an icon authored with fill="currentColor" would
// rasterise as nothing at all. Substitute the inherited text colour before
// handing the markup over. `col` is 0xRRGGBB.
// nanosvg paints shapes verbatim: it never resolves <use> references, and a
// <symbol> body would be painted at (0,0) right on top of the real content.
// Expand the sprite inline before rasterising: <symbol id="X">body</symbol>
// definitions are lifted out of the markup, and every <use href="#X"/> (plain
// or xlink:href) is replaced by the body it references.
static bool svg_expand_use(const char* src, int len, uint8_t** outp, uint32_t* out_len) {
    *outp = 0; *out_len = 0;
    if (!src || len <= 0 || len > SVG_MARKUP_MAX) return false;
    char* mid = (char*)kalloc((size_t)len + 1);
    if (!mid) return false;
    // stage 1: strip <symbol> definitions, remember their bodies (spans into
    // src - the non-symbol bytes are copied to mid one by one, so src offsets
    // stay valid for the body ranges)
    const int MAXDEF = 12;
    struct Def { char id[36]; int s, e; };
    Def defs[MAXDEF];
    int ndefs = 0;
    int body_total = 0;   // sum of bodies: expanded output can exceed the
                          // stripped markup (a short <use> tag can reference
                          // a long <symbol> body)
    int n = 0, i = 0;
    while (i < len) {
        if (src[i] == '<' && ci_starts(src + i + 1, "symbol")) {
            const char* gt = (const char*)memchr(src + i, '>', (size_t)(len - i));
            if (gt) {
                int tagend = (int)(gt - src);
                char id[36] = {0};
                attr_span_get(src + i, tagend - i, "id", id, sizeof(id));
                int close = -1;
                for (int j = tagend + 1; j + 8 < len; j++)
                    if (src[j] == '<' && src[j + 1] == '/' &&
                        ci_starts(src + j + 2, "symbol")) { close = j; break; }
                if (close >= 0) {
                    const char* cgt = (const char*)memchr(src + close, '>', (size_t)(len - close));
                    int ce = cgt ? (int)(cgt - src) : close + 8;
                    if (id[0] && ndefs < MAXDEF) {
                        ksprintf(defs[ndefs].id, sizeof(defs[ndefs].id), "%s", id);
                        defs[ndefs].s = tagend + 1;   // body: src[tagend+1 .. close)
                        defs[ndefs].e = close;
                        body_total += close - (tagend + 1);
                        ndefs++;
                    }
                    i = ce + 1;   // definition dropped from the paint markup
                    continue;
                }
            }
        }
        mid[n++] = src[i++];
    }
    mid[n] = 0;
    if (ndefs == 0) { kfree(mid); return false; }   // nothing to expand
    // stage 2: replace <use href="#X"/> with the referenced body
    // (a use with x/y adds a <g transform> wrapper: leave headroom)
    const int cap = n + body_total * 2 + 64;
    char* out = (char*)kalloc((size_t)cap + 1);
    if (!out) { kfree(mid); return false; }
    int m = 0;
    i = 0;
    while (i < n) {
        if (mid[i] == '<' && ci_starts(mid + i + 1, "use")) {
            const char* gt = (const char*)memchr(mid + i, '>', (size_t)(n - i));
            if (gt) {
                int tagend = (int)(gt - mid);
                char href[40] = {0};
                attr_span_get(mid + i, tagend - i, "href", href, sizeof(href));
                if (!href[0])
                    attr_span_get(mid + i, tagend - i, "xlink:href", href, sizeof(href));
                int skip_end = tagend + 1;
                if (tagend > i && mid[tagend - 1] != '/') {
                    // not self-closing: swallow children up to </use>
                    for (int j = tagend + 1; j + 5 < n; j++)
                        if (mid[j] == '<' && mid[j + 1] == '/' &&
                            ci_starts(mid + j + 2, "use")) {
                            const char* ugt = (const char*)memchr(mid + j, '>',
                                                                  (size_t)(n - j));
                            skip_end = ugt ? (int)(ugt - mid) + 1 : j + 6;
                            break;
                        }
                }
                const char* nm = (href[0] == '#') ? href + 1 : href;
                for (int d = 0; d < ndefs; d++)
                    if (strcmp(defs[d].id, nm) == 0) {
                        int bl = defs[d].e - defs[d].s;
                        // a <use x= y=> places the referenced content at that
                        // offset; wrap the body in a translated group (the
                        // body coordinates stay untouched)
                        char ux[16] = {0}, uy[16] = {0};
                        attr_span_get(mid + i, tagend - i, "x", ux, sizeof(ux));
                        attr_span_get(mid + i, tagend - i, "y", uy, sizeof(uy));
                        if (bl > 0 && (ux[0] || uy[0])) {
                            char pre[64], post[] = "</g>";
                            int pl = ksprintf(pre, sizeof(pre),
                                              "<g transform=\"translate(%s %s)\">",
                                              ux[0] ? ux : "0", uy[0] ? uy : "0");
                            if (m + bl + pl + 4 <= cap) {
                                memcpy(out + m, pre, pl); m += pl;
                                memcpy(out + m, src + defs[d].s, bl); m += bl;
                                memcpy(out + m, post, 4); m += 4;
                            }
                        } else if (bl > 0 && m + bl <= cap) {
                            memcpy(out + m, src + defs[d].s, bl);
                            m += bl;
                        }
                        break;
                    }
                i = skip_end;
                continue;
            }
        }
        out[m++] = mid[i++];
    }
    kfree(mid);
    if (m <= 0) { kfree(out); return false; }
    out[m] = 0;
    *outp = (uint8_t*)out;
    *out_len = (uint32_t)m;
    return true;
}

static uint8_t* svg_subst_color(const char* src, uint32_t len, uint32_t col,
                                uint32_t* out_len) {
    static const char kNeedle[] = "currentColor";
    const uint32_t nl = 12;   // strlen(kNeedle)
    if (!src || len == 0 || !out_len) return 0;
    if (col == 0) col = C_TEXT_DK;
    uint32_t hits = 0;
    for (uint32_t i = 0; i + nl <= len; i++)
        if (memcmp(src + i, kNeedle, nl) == 0) hits++;
    char hex[8];
    ksprintf(hex, sizeof(hex), "#%02x%02x%02x",
             (unsigned)((col >> 16) & 0xFF), (unsigned)((col >> 8) & 0xFF),
             (unsigned)(col & 0xFF));
    const uint32_t hl = 7;   // strlen(hex)
    size_t cap = (size_t)len + 1;
    if (hits) cap = (size_t)len - (size_t)hits * (nl - hl) + 1;
    uint8_t* out = (uint8_t*)kalloc(cap);
    if (!out) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < len;) {
        if (hits && i + nl <= len && memcmp(src + i, kNeedle, nl) == 0) {
            memcpy(out + n, hex, hl);
            n += hl;
            i += nl;
        } else {
            out[n++] = (uint8_t)src[i++];
        }
    }
    out[n] = 0;
    *out_len = n;
    return out;
}

// Inline <svg>: shares the per-tab image cache with <img>, but the payload
// travels with the page instead of over HTTP. The key covers the markup, the
// inherited colour (fill="currentColor") and the decode budget, because all
// three change the rasterised pixels.
static int img_lookup_svg(Img* imgs, const char* markup, int len, int px, uint32_t col) {
    if (!imgs || !markup || len <= 0) return -1;
    char key[256];
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)markup[i];
        h *= 16777619u;
    }
    ksprintf(key, sizeof(key), "svg|#%08x#%d#%06x#%d", h, len,
             (unsigned)(col & 0xFFFFFF), px);
    for (int k = 0; k < IMG_MAX; k++)
        if (imgs[k].used && strcmp(imgs[k].url, key) == 0) {
            Img& im = imgs[k];
            if (!im.done && !im.fail && !im.job && !im.loading) img_restart(im);
            return k;
        }
    for (int k = 0; k < IMG_MAX; k++)
        if (!imgs[k].used) {
            Img& im = imgs[k];
            im.used = true;
            clip_str(im.url, sizeof(im.url), key);
            im.want_px = px;
            im.alpha = true;   // vector art is mostly transparent
            // sprite expansion first (<use>/<symbol> are opaque to nanosvg)
            uint8_t* expanded = 0;
            uint32_t elen = 0;
            const char* draw_src = markup;
            int draw_len = len;
            if (svg_expand_use(markup, len, &expanded, &elen) && expanded) {
                draw_src = (const char*)expanded;
                draw_len = (int)elen;
            }
            uint32_t olen = 0;
            uint8_t* bytes = svg_subst_color(draw_src, (uint32_t)draw_len, col, &olen);
            if (expanded) kfree(expanded);
            if (!bytes) {
                im.loading = false;
                im.fail = true;
                return k;
            }
            im.local = bytes;
            im.local_len = olen;
            im.loading = true;
            start_img_job(im, key);
            return k;
        }
    return -1;
}

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

// RGB565 <-> BGRA (cached images keep 2 bytes/px; converted per-pixel on draw)
static uint16_t rgb_to_565(const uint8_t* p) {
    return (uint16_t)(((uint16_t)(p[2] >> 3) << 11) |
                      ((uint16_t)(p[1] >> 2) << 5) |
                      (uint16_t)(p[0] >> 3));
}
static uint32_t c565_to_rgb(uint16_t c) {
    // Surface::setpx stores 0xRRGGBB little-endian (memory bytes B,G,R),
    // exactly like the 4-byte blit path: R lives in the high byte.
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
}
// convert a BGRA Surface to a kalloc'd RGB565 buffer of the same size
static uint8_t* surf_to_565(const Surface& s) {
    size_t need = (size_t)s.width * (size_t)s.height * 2;
    uint8_t* buf = (uint8_t*)kalloc(need);
    if (!buf) return 0;
    for (int y = 0; y < s.height; y++) {
        const uint8_t* row = s.addr + (size_t)y * (size_t)s.pitch;
        uint8_t* d = buf + (size_t)y * (size_t)s.width * 2;
        for (int x = 0; x < s.width; x++) {
            const uint8_t* pp = row + (size_t)x * 4;
            uint16_t c = rgb_to_565(pp);
            d[x * 2] = (uint8_t)(c & 0xFF);
            d[x * 2 + 1] = (uint8_t)(c >> 8);
        }
    }
    return buf;
}

// nearest-neighbour downscale that KEEPS the alpha byte of every pixel.
// scale_surface() only carries 0x00RRGGBB, which is right for opaque
// backgrounds but silently zeroes transparency: a downscaled PNG logo then
// scans as "alpha 0 everywhere" and paints as nothing at all (baidu's logo
// box came out as a bare white rectangle). <img> caches use this variant.
static void scale_surface_bgra(Surface& dst, const Surface& src) {
    for (int y = 0; y < dst.height; y++) {
        int sy = (y * src.height) / dst.height;
        for (int x = 0; x < dst.width; x++) {
            int sx = (x * src.width) / dst.width;
            const uint8_t* pp = src.addr + (size_t)sy * (size_t)src.pitch + (size_t)sx * 4;
            uint8_t* dp = dst.addr + (size_t)y * (size_t)dst.pitch + (size_t)x * 4;
            dp[0] = pp[0]; dp[1] = pp[1]; dp[2] = pp[2]; dp[3] = pp[3];
        }
    }
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

// Alpha-blend src over dst (both 0x00RRGGBB), a = 0..255.
static inline uint32_t blend_rgb(uint32_t dst, uint32_t src, uint32_t a) {
    const uint32_t ia = 255 - a;
    const uint32_t r = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia) / 255;
    const uint32_t g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * ia) / 255;
    const uint32_t b = ((src & 0xFF) * a + (dst & 0xFF) * ia) / 255;
    return (r << 16) | (g << 8) | b;
}

// Blit a cached image scaled into (dx,dy,dw,dh). Used by <img> and inline
// <svg> runs: the svg raster never matches the declared CSS box exactly, and
// vector art is mostly transparent, so scaling and alpha are both required.
static void draw_img_box(Surface& s, int dx, int dy, int dw, int dh, const Img& im) {
    if (!im.rgba || im.disp_w <= 0 || im.disp_h <= 0 || dw <= 0 || dh <= 0) return;
    const int sw = im.disp_w, sh = im.disp_h;
    const bool one_to_one = (dw == sw && dh == sh);
    if (im.is565) {
        for (int y = 0; y < dh; y++) {
            int gy = dy + y;
            if (gy < 0 || gy >= s.height) continue;
            int sy = one_to_one ? y : (int)((int64_t)y * sh / dh);
            if (sy < 0 || sy >= sh) continue;
            const uint8_t* row = im.rgba + (size_t)sy * (size_t)sw * 2;
            for (int x = 0; x < dw; x++) {
                int gx = dx + x;
                if (gx < 0 || gx >= s.width) continue;
                int sx = one_to_one ? x : (int)((int64_t)x * sw / dw);
                if (sx < 0 || sx >= sw) continue;
                uint16_t c = (uint16_t)(row[sx * 2] | ((uint16_t)row[sx * 2 + 1] << 8));
                s.setpx(gx, gy, c565_to_rgb(c));
            }
        }
        return;
    }
    for (int y = 0; y < dh; y++) {
        int gy = dy + y;
        if (gy < 0 || gy >= s.height) continue;
        int sy = one_to_one ? y : (int)((int64_t)y * sh / dh);
        if (sy < 0 || sy >= sh) continue;
        const uint8_t* row = im.rgba + (size_t)sy * (size_t)sw * 4;
        for (int x = 0; x < dw; x++) {
            int gx = dx + x;
            if (gx < 0 || gx >= s.width) continue;
            int sx = one_to_one ? x : (int)((int64_t)x * sw / dw);
            if (sx < 0 || sx >= sw) continue;
            const uint8_t* pp = row + (size_t)sx * 4;
            uint32_t c = (uint32_t)pp[0] | ((uint32_t)pp[1] << 8) | ((uint32_t)pp[2] << 16);
            if (im.alpha) {
                const uint32_t a = pp[3];
                if (a == 0) continue;                      // fully transparent
                if (a != 255) c = blend_rgb(s.getpx(gx, gy), c, a);
            }
            s.setpx(gx, gy, c);
        }
    }
}

// cover-scale the cached image into the box (dx,dy,dw,dh), centered, cropping
// overflow 鈥?used for element background boxes (kind 4 runs).
static void draw_img_cover(Surface& s, int dx, int dy, int dw, int dh, const Img& im) {
    // Index the buffer with DISP dimensions, never im.w/im.h: an as_bg image
    // stores a DOWNSCALED buffer (disp_w x disp_h) while im.w/im.h keep the
    // raw size — striding by im.w read past the allocation and painted raw
    // memory as pixels (qq.com's noise band was exactly that).
    if (!im.rgba || im.disp_w <= 0 || im.disp_h <= 0 || dw <= 0 || dh <= 0) return;
    const int bw = im.disp_w, bh = im.disp_h;
    float sc = (float)dw / (float)bw;
    float sc2 = (float)dh / (float)bh;
    if (sc2 > sc) sc = sc2;
    int iw2 = (int)(bw * sc + 0.5f);
    int ih2 = (int)(bh * sc + 0.5f);
    int ox = (dw - iw2) / 2;
    int oy = (dh - ih2) / 2;
    if (im.is565) {
        for (int y = 0; y < dh; y++) {
            int gy = dy + y;
            if (gy < 0 || gy >= s.height) continue;
            int sy = (int)((y - oy) / sc);
            if (sy < 0 || sy >= bh) continue;
            const uint8_t* row = im.rgba + (size_t)sy * (size_t)bw * 2;
            for (int x = 0; x < dw; x++) {
                int gx = dx + x;
                if (gx < 0 || gx >= s.width) continue;
                int sx = (int)((x - ox) / sc);
                if (sx < 0 || sx >= bw) continue;
                uint16_t c = (uint16_t)(row[sx * 2] | ((uint16_t)row[sx * 2 + 1] << 8));
                s.setpx(gx, gy, c565_to_rgb(c));
            }
        }
        return;
    }
    for (int y = 0; y < dh; y++) {
        int gy = dy + y;
        if (gy < 0 || gy >= s.height) continue;
        int sy = (int)((y - oy) / sc);
        if (sy < 0 || sy >= bh) continue;
        const uint8_t* row = im.rgba + (size_t)sy * (size_t)bw * 4;
        for (int x = 0; x < dw; x++) {
            int gx = dx + x;
            if (gx < 0 || gx >= s.width) continue;
            int sx = (int)((x - ox) / sc);
            if (sx < 0 || sx >= bw) continue;
            const uint8_t* pp = row + (size_t)sx * 4;
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
                    // vector: rasterize at (or below) the requested box size.
                    // Keeping BGRA is mandatory here — a 565 cache throws the
                    // alpha channel away and every transparent pixel would
                    // come out as a black hole in the page.
                    int w = 0, h = 0;
                    uint8_t* out = 0;
                    int budget = im.want_px > 0 ? im.want_px : 160;
                    if (nefu::nsvg_decode_mem(j->data, j->len, budget, &w, &h, &out)) {
                        bgra_swap(out, w * h);
                        im.w = w; im.h = h;
                        im.disp_w = w; im.disp_h = h;
                        im.rgba = out;
                        im.is565 = false;
                        im.alpha = true;
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
                            if (im.as_bg) {
                                // background: decode at a display-capped size
                                // (cover needs >= the viewport, never the raw
                                // file resolution)
                                int dw = w, dh = h;
                                if (dw > MAX_BG_DISP) {
                                    dh = (h * MAX_BG_DISP) / dw;
                                    dw = MAX_BG_DISP;
                                }
                                if (dh < 1) dh = 1;
                                uint32_t need = (uint32_t)dw * (uint32_t)dh * 4;
                                uint8_t* buf = (uint8_t*)kalloc(need);
                                if (buf) {
                                    Surface small;
                                    small.addr = buf;
                                    small.width = dw; small.height = dh;
                                    small.pitch = dw * 4;
                                    Surface big;
                                    big.addr = out;
                                    big.width = w; big.height = h; big.pitch = w * 4;
                                    scale_surface(small, big);
                                    uint8_t* b5 = surf_to_565(small);
                                    kfree(buf);
                                    im.rgba = b5;
                                    im.is565 = true;
                                    im.disp_w = dw; im.disp_h = dh;
                                    im.done = b5 != 0;
                                } else {
                                    im.fail = true;
                                }
                            } else {
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
                                    scale_surface_bgra(small, big);
                                    // Transparent PNG/WebP logos used to render
                                    // as black boxes: the 565 cache has no
                                    // alpha. Keep BGRA for those (usually small
                                    // sprites) and blend at paint time.
                                    bool trans = false;
                                    for (uint32_t bi = 3; bi < need; bi += 4)
                                        if (buf[bi] != 0xFF) { trans = true; break; }
                                    if (trans) {
                                        im.rgba = buf;
                                        im.is565 = false;
                                        im.alpha = true;
                                        im.done = true;
                                    } else {
                                        uint8_t* b5 = surf_to_565(small);
                                        kfree(buf);
                                        im.rgba = b5;
                                        im.is565 = true;
                                        im.done = b5 != 0;
                                        if (!b5) im.fail = true;
                                    }
                                } else {
                                    im.fail = true;
                                }
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
            {
                static int img_dbg = -1;
                if (img_dbg < 0) {
                    const char* e = getenv("NF_IMG_DBG");
                    img_dbg = (e && e[0] && e[0] != '0') ? 1 : 0;
                }
                if (img_dbg)
                    klogf("imgdbg: done=%d fail=%d %dx%d alpha=%d url=%.110s",
                          (int)im.done, (int)im.fail, im.disp_w, im.disp_h,
                          (int)im.alpha, im.url);
            }
            if (tab.used && im.done) {
                // A FAILED image must not force a re-layout: its run keeps
                // the same placeholder box, and re-layouting here let a
                // page with a few dead CDN urls (qq.com) re-layout the
                // whole document every frame (3 fps).
                if (im.fail) klogf("browser: image failed: %s", im.url);
                // A RECLAIMED image that refetched keeps its exact box too:
                // skipping the re-layout turns scroll-back refetches from a
                // full-document reflow into a pure data swap.
                if (im.fail || im.reclaimed)
                    ;
                else
                    tab.layout_w = -1;   // image box changed -> re-layout
            } else if (im.fail && !im.fail_logged) {
                im.fail_logged = true;
                klogf("browser: image failed: %s", im.url);
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
        if (im.local) kfree(im.local);
        im.job = 0;
        im.rgba = 0;
        im.local = 0;
        im.local_len = 0;
        im.used = im.loading = im.done = im.fail = false;
        im.reclaimed = false;   // fresh page: first image arrival must re-layout
        im.is565 = im.alpha = im.as_bg = false;
        im.want_px = 0;
        im.w = im.h = im.disp_w = im.disp_h = 0;
        im.url[0] = 0;
    }
}

static bool on_http_chunk(void* ctx, const uint8_t* data, uint32_t len) {
    LoadJob* j = (LoadJob*)ctx;
    if (j->cancelled) { j->aborted = 1; return false; }
    bool first_chunk = !j->stream_started;
    j->stream_started = true;
    if (!j->php_mode && !j->json_mode && j->php_len == 0 && first_chunk && len > 0) {
        // JSON detection (api.github.com, raw gist/rest endpoints): the first
        // non-space byte of the FIRST chunk decides. These bodies carry no
        // markup at all, so they are buffered whole and rendered as a tree
        // instead of being streamed into the HTML parser (which would produce
        // an empty page). Later chunks must never re-sniff: a mid-body chunk
        // can begin with '{' or '[' and is just JS/markup content.
        uint32_t i = 0;
        if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) i = 3;
        while (i < len && (data[i] == ' ' || data[i] == '\t' ||
                           data[i] == '\r' || data[i] == '\n')) i++;
        if (i < len && (data[i] == '{' || data[i] == '[')) j->json_mode = true;
    }
    if (!j->php_mode && !j->json_mode && j->php_len == 0 && first_chunk) {
        // PHP detection: URL carries a .php path, or the very first bytes open
        // a PHP tag. HTML pages keep the normal streaming path untouched.
        if (strstr(j->url, ".php") ||
            (len >= 4 && memcmp(data, "<?ph", 4) == 0) ||
            (len >= 3 && (memcmp(data, "<?=", 3) == 0 || memcmp(data, "<? ", 3) == 0)))
            j->php_mode = true;
    }
    if (j->php_mode || j->json_mode) {
        // buffer the whole body; the transpiler needs the complete document
        // (PHP blocks cross chunk boundaries)
        if (j->php_len + (int)len + 1 > j->php_cap) {
            int ncap = j->php_cap ? j->php_cap * 2 : 65536;
            while (ncap < j->php_len + (int)len + 1) ncap *= 2;
            char* nb = (char*)brealloc(j->php_buf, (size_t)ncap);
            if (!nb) { j->aborted = 1; return false; }
            j->php_buf = nb;
            j->php_cap = ncap;
        }
        memcpy(j->php_buf + j->php_len, data, (size_t)len);
        j->php_len += (int)len;
        j->php_buf[j->php_len] = 0;
        return true;
    }
    PageParser* pp = (PageParser*)j->parser;
    if (!pp) { j->aborted = 1; return false; }
    pp->feed((const char*)data, (int)len, false);
    if (pp->dom.overflow) { j->aborted = 1; return false; }   // DOM capped
    return true;
}

// ===================== JSON responses =====================
// Endpoints such as api.github.com / raw gist URLs answer with JSON. Feeding
// that to the HTML parser produced a blank page (no tags to speak of), so the
// body is buffered instead, parsed with nefu::json, and rendered as an
// indented, colour-grouped line tree. When parsing fails the raw body is shown
// verbatim so nothing is ever lost.
static void json_escape(const char* s, int n, String& out) {
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c == '\t') out += "  ";
        else if (c == '\r') continue;
        else { char b[2] = { c, 0 }; out += b; }
    }
}

static int build_json_page(const char* body, int len, const char* url,
                           char* out, int cap) {
    if (!out || cap < 512) return 0;
    out[0] = 0;
    if (!body || len <= 0) return 0;

    nefu::json::Value v;
    const char* jerr = 0;
    bool ok = nefu::json::parse(body, v, &jerr);

    String pretty;
    if (ok) nefu::json::to_string(v, pretty, true);
    else pretty = body;

    // summary
    const char* kind = "value";
    int members = 0;
    if (ok) {
        if (v.is_object()) { kind = "object"; members = (int)v.obj.size(); }
        else if (v.is_array()) { kind = "array"; members = (int)v.arr.size(); }
        else if (v.is_string()) kind = "string";
        else if (v.is_int()) kind = "number";
        else if (v.is_bool()) kind = "boolean";
        else kind = "null";
    }

    int n = 0;
    auto put = [&](const char* s) {
        while (*s && n < cap - 1) out[n++] = *s++;
        out[n] = 0;
    };
    char head[256];
    ksprintf(head, sizeof(head),
             "<h2>JSON document</h2><div style=\"padding:6px\">"
             "<div class=\"url\">%s</div>", url ? url : "");
    put(head);
    char meta[256];
    if (ok) {
        ksprintf(meta, sizeof(meta),
                 "<div>parsed ok - %s, %d member(s), %d bytes</div>",
                 kind, members, len);
    } else {
        ksprintf(meta, sizeof(meta),
                 "<div>not valid JSON (%s) - showing the raw body, %d bytes</div>",
                 (jerr && jerr[0]) ? jerr : "parse error", len);
    }
    put(meta);
    put("</div>");

    // body: one block element per line so the layout stacks them and the
    // leading spaces (8px per space in this engine) give the nesting
    const int max_lines = 1500;
    int line = 0, i = 0, L = (int)pretty.len();
    const char* p = pretty.c_str();
    put("<div style=\"padding:4px\">");
    while (i < L && line < max_lines) {
        int j = i;
        while (j < L && p[j] != '\n') j++;
        String ln;
        json_escape(p + i, j - i, ln);
        // shade keys and brackets so the structure is readable at a glance
        const char* cls = "";
        int k = 0;
        while (k < j - i && p[i + k] == ' ') k++;
        if (k < j - i) {
            char c0 = p[i + k];
            if (c0 == '{' || c0 == '[' || c0 == '}' || c0 == ']') cls = " style=\"color:#4b8bf5\"";
            else if (c0 == '"') cls = " style=\"color:#2f9e44\"";
        }
        put("<div");
        put(cls);
        put(">");
        if (ln.len() == 0) put(" "); else put(ln.c_str());
        put("</div>");
        line++;
        i = j + 1;
    }
    if (i < L) {
        char tr[128];
        ksprintf(tr, sizeof(tr),
                 "<div>... truncated (%d lines shown of %d bytes)</div>",
                 line, len);
        put(tr);
    }
    put("</div>");
    return n;
}

static void load_worker(void* arg) {
    LoadJob* j = (LoadJob*)arg;
    css_vars_reset();   // custom properties / rem base belong to this document
    // streamed download: received bytes feed the parser chunk by chunk; the
    // raw page is never buffered in full, only an unclosed tag tail is held
    // back until the next chunk completes it. Parsing happens on the worker
    // thread so a huge page never freezes the UI thread.
    PageParser pp(j->script, &j->script_len, j->css, &j->css_n, CSS_MAX,
                  j->script_srcs, &j->script_src_n, 8,
                  j->css_links, &j->css_link_n, 8);
    pp.title_out = j->title;
    pp.title_cap = (int)sizeof(j->title);
    j->parser = &pp;
    bool ok;
    if (ci_starts(j->url, "file://")) {
        // local page (host filesystem): read and feed like a network stream
        uint8_t* fbuf = 0; uint32_t fsz = 0;
        ok = platform_read_file(j->url + 7, &fbuf, &fsz);
        if (ok && fbuf) {
            uint32_t off = 0;
            while (off < fsz) {
                uint32_t take = fsz - off > 32768 ? 32768 : (fsz - off);
                if (!on_http_chunk(j, fbuf + off, take)) { j->aborted = 1; break; }
                off += take;
            }
            kfree(fbuf);
            klogf("browser: local file %u bytes", fsz);
        } else {
            klogf("browser: local file read failed: %s", j->url);
        }
    } else if (j->method[0] && !ci_eq(j->method, "GET")) {
        // Form submission: the collected fields travel as the request body.
        // Origin and Referer are what a real browser sends on a POST, and a
        // server-side CSRF check rejects the request when they are missing.
        char origin[320];
        {
            int on2 = 0;
            const char* se = strstr(j->referer, "://");
            if (se) {
                // scheme plus "://" ...
                for (const char* t = j->referer; t <= se + 2 && on2 < (int)sizeof(origin) - 1; t++)
                    origin[on2++] = *t;
                // ... then host[:port] up to the first path separator
                for (const char* t = se + 3; *t && *t != '/' && *t != '?' && *t != '#' &&
                     on2 < (int)sizeof(origin) - 1; t++) origin[on2++] = *t;
            }
            origin[on2] = 0;
        }
        char hdrs[1024];
        if (j->post_multipart) {
            ksprintf(hdrs, sizeof(hdrs),
                     "Content-Type: multipart/form-data; boundary=%s\r\n"
                     "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                     "Origin: %s\r\n"
                     "Referer: %s\r\n",
                     j->boundary, origin, j->referer[0] ? j->referer : j->url);
        } else {
            ksprintf(hdrs, sizeof(hdrs),
                     "Content-Type: application/x-www-form-urlencoded\r\n"
                     "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                     "Origin: %s\r\n"
                     "Referer: %s\r\n",
                     origin, j->referer[0] ? j->referer : j->url);
        }
        int st = 0;
        ok = platform_http_stream_send(j->method, j->url, hdrs,
                                       (const uint8_t*)j->post_body,
                                       (uint32_t)j->post_len, j, on_http_chunk, &st);
        klogf("browser: POST status=%d ok=%d\n", st, (int)ok);
    } else {
        ok = platform_http_stream(j->url, j, on_http_chunk);
    }
    j->parser = 0;
    if (!j->cancelled && (ok || j->aborted)) {
        if (j->php_mode && j->php_buf) {
            // PHP template: run the mini transpiler, then parse the emitted
            // HTML (scripts inside it still run through the mini JS engine).
            const int conv_cap = 8 * 1024 * 1024;
            char* conv = (char*)balloc((size_t)conv_cap);
            if (conv) {
                int clen = nfuphp::php_convert(j->php_buf, conv, conv_cap);
                klogf("browser: php converted %d -> %d bytes", j->php_len, clen);
                pp.feed(conv, clen, true);
                bfree(conv);
            }
        } else if (j->json_mode && j->php_buf) {
            // JSON payload: parse it and emit a browsable tree instead of
            // handing markup-free bytes to the HTML parser.
            j->php_buf[j->php_len] = 0;
            const int jcap = 1024 * 1024;
            char* jh = (char*)balloc((size_t)jcap);
            if (jh) {
                int jn = build_json_page(j->php_buf, j->php_len, j->url, jh, jcap);
                klogf("browser: json %d bytes -> %d bytes markup", j->php_len, jn);
                if (jn > 0) pp.feed(jh, jn, true);
                bfree(jh);
            }
        } else {
            pp.feed(0, 0, true);   // final flush of any held-back tail
        }
        // external stylesheets (<link rel=stylesheet>): real browsers block
        // the first paint on them, so fetch each one synchronously here on
        // the worker thread and append the parsed rules to the sheet before
        // any script runs. Without this, framework sites whose whole layout
        // lives in /static/css/*.css render with no positioning at all.
        for (int cli = 0; cli < j->css_link_n && cli < 8; cli++) {
            if (j->css_n >= CSS_MAX) { klogf("browser: css sheet full, link %d skipped", cli); break; }
            char cu[1024];
            resolve_url(j->url, j->css_links[cli], cu, sizeof(cu));
            uint8_t* xbody = 0; uint32_t xlen = 0; int xst = 0;
            bool xgot = false;
            if (ci_starts(cu, "file://")) {
                // local stylesheet: platform_http_request has no file handler
                xgot = platform_read_file(cu + 7, &xbody, &xlen);
            } else {
                xgot = platform_http_request("GET", cu, 0, 0, 0, &xst, &xbody, &xlen);
            }
            if (xgot && xbody && xlen > 0) {
                uint32_t clen = xlen < (1u << 20) ? xlen : (1u << 20);
                // definitions first, then the rules that use them (see css_flush)
                css_parse((const char*)xbody, (int)clen, 0, 0, true);
                int added = css_parse((const char*)xbody, (int)clen,
                                      j->css + j->css_n, CSS_MAX - j->css_n);
                if (added > 0) j->css_n += added;
                klogf("browser: external css %u bytes -> %d rules (have=%d max=%d) %.40s",
                      xlen, added, (int)j->css_n, (int)CSS_MAX, (const char*)xbody);
                kfree(xbody);
            } else {
                if (xbody) kfree(xbody);
                klogf("browser: external css fetch failed st=%d: %s", xst, cu);
            }
        }
        j->phase = 1;
        if (j->json_mode) clip_str(j->title, sizeof(j->title), "JSON response");
        DomArena* ar = new DomArena();
        if (ar) {
            ar->nodes = pp.dom.nodes;
            j->dom = ar;
        }
        j->ok = true;
        // copy document-order script marks from the parser into the job
        j->smark_n = pp.smark_n < 64 ? pp.smark_n : 64;
        j->seg_n = pp.seg_n < 24 ? pp.seg_n : 24;
        for (int mi = 0; mi < j->smark_n; mi++) {
            j->smark_kind[mi] = pp.smark_kind[mi];
            j->smark_idx[mi] = pp.smark_idx[mi];
        }
        for (int gi = 0; gi < j->seg_n && gi < 24; gi++) {
            j->seg_off[gi][0] = pp.seg_off[gi][0];
            j->seg_off[gi][1] = pp.seg_off[gi][1];
        }
        klogf("browser: worker parsed %d nodes, srcs=%d script=%d marks=%d",
              (int)pp.dom.nodes.size(), j->script_src_n, j->script_len, j->smark_n);
        // execute scripts through the mini JS engine in document order:
        // external <script src> bundles first, then inline <script> bodies.
        // print/alert output lands in js_out (logged, never on screen); the
        // DOM shim writes its serialized tree into js_html for re-render.
        if (j->script_len > 0 || j->script_src_n > 0) {
            j->script[j->script_len] = 0;
            // seed the DOM shim with the parsed tree (real getElementById).
            // The seed must carry the WHOLE body: see kDomSeedCap above.
            if (!j->seed) j->seed = (char*)balloc((size_t)kDomSeedCap);
            char* seedbuf = j->seed;
            int seedcap = seedbuf ? kDomSeedCap : 1;
            int seedlen = seedbuf ? dom_to_html(pp.dom, seedbuf, seedcap) : 0;
            j->seed_len = seedlen;
            klogf("browser: dom seed %d/%d bytes, %d parsed nodes: %.80s",
                  seedlen, seedcap, (int)pp.dom.nodes.size(), seedbuf ? seedbuf : "");
            // Inline data: images inflate the seed (each one carries its whole
            // payload); a truncated seed loses trailing elements for the page's
            // own scripts, so say so instead of failing silently.
            if (seedlen >= seedcap - 1)
                klogf("browser: WARNING dom seed TRUNCATED at %d bytes (cap %d) — trailing elements missing from the JS model",
                      seedlen, seedcap);
            if (!seedbuf) {
                seedlen = 0;
                klogf("browser: DOM seed allocation failed (%d bytes) — scripts run on an empty model", kDomSeedCap);
            }
            for (int dbi = 0; dbi < (int)pp.dom.nodes.size() && dbi < 10; dbi++) {
                const DomNode& dn2 = pp.dom.nodes[dbi];
                char dbg2[80] = {0};
                if (dn2.tag == Tag::Text) {
                    int cl = dn2.text.len() < 60 ? dn2.text.len() : 60;
                    for (int ci2 = 0; ci2 < cl; ci2++) dbg2[ci2] = dn2.text[ci2];
                    klogf("browser: domnode#%d TEXT '%s'", dbi, dbg2);
                } else {
                    const char* tnm = tag_name_of(dn2.tag);
                    klogf("browser: domnode#%d TAG %s kids=%d", dbi, tnm[0] ? tnm : "(unknown)", (int)dn2.kids.size());
                }
            }
            j->js_html_sz = kPageHtmlCap;
            j->js_html = (char*)balloc((size_t)j->js_html_sz);
            char jsacc[16384]; int jan = 0;
            // Execute scripts in TRUE document order (the parser records a
            // mark for every <script> it meets: inline segment or external
            // src). The old "all inline first, then all external" order broke
            // baidu.com: its head inline scripts call $() while the jQuery
            // bundle they depend on was still queued as src[0].
            nefu::mini_js_set_dom(seedbuf);
            nefu::mini_js_set_env(j->url, j->title, 0, 0);
            nefu::mini_js_set_module_base(j->url);
            // Total worker JS budget: the page must COMMIT while the user
            // watches. The old per-bundle 120s deadline let seven qq.com
            // bundles hold the job open for many minutes with nothing but
            // the home page on screen (the paint thread only re-layouts
            // after commit). An interrupted eval still runs the post-drain
            // DOM flush, so the page commits with whatever mounted.
            // 90s (up from 30s): bloxd.io/play/* ships ~11 MB of game chunks
            // and its WebGL init was killed at 30s, leaving the page stuck
            // on the LogoLoader spinner forever. Fast portals (qq) finish
            // their bundles well inside 30s either way.
            const uint32_t js_budget_end = platform_tick_ms() + 90000;
            nefu::mini_js_set_run_deadline(90000);

            auto run_src_seg = [&](int si) -> void {
                if (si < 0 || si >= j->script_src_n) return;
                char u[2048];
                resolve_url(j->url, j->script_srcs[si], u, sizeof(u));
                // Skip third-party glue that a minimal engine cannot use and
                // that no site depends on for its core UI: captcha widgets,
                // ad/analytics beacons and mobile debug consoles. Skipping them
                // saves seconds of download + parse on JS-heavy pages.
                static const char* kSkipSrcs[] = {
                    "recaptcha", "page-kit.io", "cloudflareinsights",
                    "vconsole", "google-analytics.com", "googletagmanager.com",
                    "googlesyndication.com", "doubleclick.net", "adsbygoogle",
                    "hotjar.com", "sentry-cdn", "browser.sentry-cdn.com",
                    "connect.facebook.net", "static.cloudflareinsights.com",
                    "mc.yandex.ru", "clarity.ms"
                };
                bool skip = false;
                for (size_t ki = 0; ki < sizeof(kSkipSrcs) / sizeof(kSkipSrcs[0]); ki++) {
                    if (strstr(u, kSkipSrcs[ki])) { skip = true; break; }
                }
                if (skip) {
                    fprintf(stderr, "browser: skip src[%d] %s\n", si, u); fflush(stderr);
                    return;
                }
                fprintf(stderr, "browser: fetching src[%d] %s\n", si, u); fflush(stderr);
                uint8_t* body = 0; uint32_t blen = 0;
                bool got = platform_http_get(u, &body, &blen);
                fprintf(stderr, "browser: src[%d] got=%d blen=%u\n", si, got ? 1 : 0, blen); fflush(stderr);
                if (got && body && blen > 0) {
                    // Diagnostics: a short body usually means the server (or a
                    // proxy) answered with an error page instead of the asset.
                    if (blen < 4096) {
                        fprintf(stderr, "browser: src[%d] short body (%u bytes):\n", si, blen);
                        uint32_t show = blen < 600 ? blen : 600;
                        for (uint32_t d = 0; d < show; d++) {
                            unsigned char ch = body[d];
                            fputc((ch >= 32 && ch < 127) ? (int)ch : '.', stderr);
                        }
                        fprintf(stderr, "\n"); fflush(stderr);
                    }
                    char* js = (char*)body;
                    js[blen] = 0;
                    nefu::mini_js_set_dom(seedbuf);
                    nefu::mini_js_set_env(j->url, j->title, 0, 0);
                    nefu::mini_js_set_module_base(u);
                    // Bound the bundle eval: React mounts synchronously inside
                    // it (and bloxd fires social-API fetches from that boot),
                    // so one blackholed endpoint must not hold the job open
                    // forever. The deadline is the REMAINING worker budget,
                    // not a fresh 120s per bundle.
                    {
                        uint32_t now = platform_tick_ms();
                        uint32_t left = (js_budget_end > now) ? (js_budget_end - now) : 1;
                        nefu::mini_js_set_run_deadline(left);
                    }
                    bool _e1 = nefu::mini_js_run(js, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                                      j->js_html, j->js_html_sz) != 0;
                    if (_e1) fprintf(stderr, "browser: JS-FAIL src %s\n", u);
                    j->js_err |= _e1;
                    jan += (int)strlen(jsacc + jan);
                    if (jan > (int)sizeof(jsacc) - 2) jan = (int)sizeof(jsacc) - 2;
                    kfree(body);
                }
            };
            auto run_inline_seg = [&](int gi) -> void {
                if (gi < 0 || gi >= 24 || gi >= j->seg_n) return;
                int s0 = j->seg_off[gi][0], s1 = j->seg_off[gi][1];
                if (s1 <= s0 || s1 > j->script_len) return;
                char saved = j->script[s1];
                j->script[s1] = 0;
                nefu::mini_js_set_dom(seedbuf);
                nefu::mini_js_set_env(j->url, j->title, 0, 0);
                nefu::mini_js_set_module_base(j->url);
                fprintf(stderr, "browser: running inline seg %d (%d bytes)\n", gi, s1 - s0); fflush(stderr);
                if (getenv("NF_DUMP_INLINE")) {
                    // Dump the segment body so a crash inside an inline <script>
                    // can be reproduced and read instead of guessed at.
                    fprintf(stderr, "browser: inline seg %d body <<<%.*s>>>\n", gi, s1 - s0, j->script + s0);
                    fflush(stderr);
                }
                { bool _e2 = nefu::mini_js_run(j->script + s0, jsacc + jan,
                                  (int)sizeof(jsacc) - jan - 1,
                                  j->js_html, j->js_html_sz) != 0;
                  if (_e2) fprintf(stderr, "browser: JS-FAIL inline seg %d\n", gi); fflush(stderr);
                  j->js_err |= _e2; }
                j->script[s1] = saved;
                jan += (int)strlen(jsacc + jan);
                if (jan > (int)sizeof(jsacc) - 2) jan = (int)sizeof(jsacc) - 2;
            };
            if (j->smark_n > 0) {
                for (int mi = 0; mi < j->smark_n; mi++) {
                    if (j->smark_kind[mi] == 0) run_src_seg((int)j->smark_idx[mi]);
                    else run_inline_seg((int)j->smark_idx[mi]);
                }
            } else {
                // legacy fallback (no marks recorded): inline bodies first,
                // then external srcs in order
                fprintf(stderr, "browser: running inline script %d bytes\n", j->script_len); fflush(stderr);
                j->js_err |= (nefu::mini_js_run(j->script, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                                  j->js_html, j->js_html_sz) != 0);
                fprintf(stderr, "browser: inline done\n"); fflush(stderr);
                jan += (int)strlen(jsacc + jan);
                if (jan > (int)sizeof(jsacc) - 2) jan = (int)sizeof(jsacc) - 2;
                for (int si = 0; si < j->script_src_n && si < 8; si++) run_src_seg(si);
            }
            jsacc[jan] = 0;
            // fire DOMContentLoaded/load after ALL sync scripts ran — real
            // browsers dispatch these right after the parser finishes; React
            // apps that mount on DOMContentLoaded/load never start otherwise.
            //
            // Host builds must NOT run it here: load handlers fire the page's
            // social/API fetches and our fetch shim is synchronous, so one
            // slow endpoint (bloxd rotates *.bloxd.io social servers; some
            // fail with TLS errors that stall ~19 s per request, retried
            // forever) would keep this eval running for minutes and delay
            // j->done — the tab would sit on the previous page the whole
            // time. Instead the job is marked done right away (the React DOM
            // from the bundle evals is already in js_html) and the DCL eval
            // runs in the resident loop, bounded to 15 s. On bare there is
            // no resident worker, so the eval stays inline.
#ifndef NEFU_BARE
            j->tab->dcl_pending = 1;
#else
            {
                const char* evsrc =
                    "try { if (document) document.readyState = 'complete'; } catch(e){}"
                    "try { if (document && document.dispatchEvent) {"
                    " document.dispatchEvent({type:'DOMContentLoaded', bubbles:true});"
                    " document.dispatchEvent({type:'readystatechange'}); }"
                    " if (window && window.dispatchEvent) {"
                    " window.dispatchEvent({type:'load'}); } } catch(e){}";
                nefu::mini_js_set_dom(seedbuf);
                nefu::mini_js_set_env(j->url, j->title, 0, 0);
                fprintf(stderr, "browser: firing DOMContentLoaded/load\n"); fflush(stderr);
                j->js_err |= (nefu::mini_js_run(evsrc, jsacc + jan,
                                  (int)sizeof(jsacc) - jan - 1,
                                  j->js_html, j->js_html_sz) != 0);
                jan += (int)strlen(jsacc + jan);
                if (jan > (int)sizeof(jsacc) - 2) jan = (int)sizeof(jsacc) - 2;
                jsacc[jan] = 0;
            }
#endif
            if (jan > 0) { memcpy(j->js_out, jsacc, (size_t)jan); j->js_out[jan] = 0; }
            else j->js_out[0] = 0;
            // The page's print()/alert() output used to be dropped on the
            // floor: it was accumulated here and then never shown anywhere.
            // That made every JS-level problem invisible - on the bare kernel
            // the serial log is the only place to look, so put it there.
            if (jan > 0) {
                char msg[520];
                int n = jan < 500 ? jan : 500;
                memcpy(msg, jsacc, (size_t)n);
                msg[n] = 0;
                for (int i = 0; i < n; i++) if (msg[i] == '\n') msg[i] = ' ';
                klogf("js: out %s\n", msg);
            }
        }
    }
    if (pp.sstream) { bfree(pp.sstream); pp.sstream = 0; }
    j->done = 1;
#ifndef NEFU_BARE
    // resident JS worker: keep quickjs on THIS thread so event dispatch
    // (click listeners in the DOM shim) safely reuses the page's globals.
    while (!j->tab->js_ev_close) {
        TabState* tb = j->tab;
        {   // worker liveness trace: which branch is the loop parked in
            static uint32_t last_wl = 0;
            uint32_t nw = platform_tick_ms();
            if (nw - last_wl > 5000) {
                last_wl = nw;
                fprintf(stderr, "js: WLOOP dcl=%d evp=%d evb=%d evd=%d tickreq=%d tickready=%d active=%d\n",
                        (int)tb->dcl_pending, (int)tb->js_ev_pending, (int)tb->js_ev_busy,
                        (int)tb->js_ev_done, (int)tb->js_tick_req, (int)tb->js_tick_ready,
                        (int)tb->js_active);
                fflush(stderr);
            }
        }
        // Starvation guard: the pump sits behind two higher-priority branches,
        // and a site that posts event scripts continuously keeps it from ever
        // running - a frame's 0 ms timeout was measured firing 12.6 s late.
        // Once the request is older than kTickStarveMs, run it out of order.
        bool tick_starved = tb->js_tick_req && tb->js_tick_req_at &&
                            platform_tick_ms() - tb->js_tick_req_at >= kTickStarveMs;
        if (tick_starved) {
            tb->js_tick_req = 0;
            if (!tb->js_tick_html) {
                tb->js_tick_html_sz = kTickHtmlCap;
                tb->js_tick_html = (char*)balloc((size_t)tb->js_tick_html_sz);
            }
            tb->js_tick_out[0] = 0;
            if (tb->js_tick_html) tb->js_tick_html[0] = 0;
            int n = nefu::mini_js_tick(tb->js_tick_out,
                                       (int)sizeof(tb->js_tick_out),
                                       tb->js_tick_html, tb->js_tick_html_sz);
            tb->js_tick_ready = (n > 0) ? 1 : 0;
        } else if (tb->dcl_pending && !tb->js_ev_pending) {
            // Deferred DOMContentLoaded/load eval (see the load-phase comment
            // above): runs after the page has committed, bounded to 15 s so
            // pages whose load handlers synchronously fetch slow endpoints
            // cannot stall the runtime pump forever. Output flows through the
            // ordinary tick channel (apply_js_commands + DOM re-parse).
            tb->dcl_pending = 0;
            nefu::mini_js_set_env(j->url, j->title, 0, 0);
            if (!tb->js_tick_html) {
                tb->js_tick_html_sz = kTickHtmlCap;
                tb->js_tick_html = (char*)balloc((size_t)tb->js_tick_html_sz);
            }
            tb->js_tick_out[0] = 0;
            if (tb->js_tick_html) tb->js_tick_html[0] = 0;
            fprintf(stderr, "browser: firing DOMContentLoaded/load\n"); fflush(stderr);
            nefu::mini_js_set_run_deadline(15000);
            nefu::mini_js_run(
                "try { if (document) document.readyState = 'complete'; } catch(e){}"
                "try { if (document && document.dispatchEvent) {"
                " document.dispatchEvent({type:'DOMContentLoaded', bubbles:true});"
                " document.dispatchEvent({type:'readystatechange'}); }"
                " if (window && window.dispatchEvent) {"
                " window.dispatchEvent({type:'load'}); } } catch(e){}",
                tb->js_tick_out, (int)sizeof(tb->js_tick_out),
                tb->js_tick_html, tb->js_tick_html_sz);
            tb->js_tick_ready = 1;
        } else if (tb->js_ev_pending) {
            tb->js_ev_busy = 1;
            tb->js_ev_pending = 0;
            char* h = tb->js_ev_html;
            int hsz = tb->js_ev_html_sz;
            if (h && hsz > 0) h[0] = 0;
            nefu::mini_js_run(tb->js_ev_script, tb->js_ev_out,
                              (int)sizeof(tb->js_ev_out), h, hsz);
            tb->js_ev_done = 1;
            tb->js_ev_busy = 0;
        } else if (tb->js_tick_req) {
            // per-frame pump: rAF callbacks, due timers, promise microtasks.
            // Only runs while no event script is queued so the two never
            // interleave inside the shared context.
            tb->js_tick_req = 0;
            if (!tb->js_tick_html) {
                tb->js_tick_html_sz = kTickHtmlCap;
                tb->js_tick_html = (char*)balloc((size_t)tb->js_tick_html_sz);
            }
            tb->js_tick_out[0] = 0;
            if (tb->js_tick_html) tb->js_tick_html[0] = 0;
            int n = nefu::mini_js_tick(tb->js_tick_out,
                                       (int)sizeof(tb->js_tick_out),
                                       tb->js_tick_html, tb->js_tick_html_sz);
            tb->js_tick_ready = (n > 0) ? 1 : 0;
        }
        platform_thread_sleep(2);
    }
    nefu::mini_js_reset();   // quickjs must be freed on the thread that owns it
    j->worker_done = 1;
#endif
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
    // ---- WebGL2 bridge additions ----
    GLint (*glGetUniformLocation)(GLuint, const char*);
    GLint (*glGetAttribLocation)(GLuint, const char*);
    void (*glUniform1f)(GLint, GLfloat);
    void (*glUniform2f)(GLint, GLfloat, GLfloat);
    void (*glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glUniform1i)(GLint, GLint);
    void (*glUniform2i)(GLint, GLint, GLint);
    void (*glUniform3i)(GLint, GLint, GLint, GLint);
    void (*glUniform4i)(GLint, GLint, GLint, GLint, GLint);
    void (*glUniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*glEnable)(GLenum);
    void (*glDisable)(GLenum);
    void (*glBlendFunc)(GLenum, GLenum);
    void (*glDepthFunc)(GLenum);
    void (*glCullFace)(GLenum);
    void (*glDrawElements)(GLenum, GLsizei, GLenum, const void*);
    void (*glGenTextures)(GLsizei, GLuint*);
    void (*glBindTexture)(GLenum, GLuint);
    void (*glTexParameteri)(GLenum, GLenum, GLint);
    void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*glActiveTexture)(GLenum);
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
    GLF(glGetUniformLocation, GLint (*)(GLuint, const char*));
    GLF(glGetAttribLocation, GLint (*)(GLuint, const char*));
    GLF(glUniform1f, void (*)(GLint, GLfloat));
    GLF(glUniform2f, void (*)(GLint, GLfloat, GLfloat));
    GLF(glUniform3f, void (*)(GLint, GLfloat, GLfloat, GLfloat));
    GLF(glUniform4f, void (*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat));
    GLF(glUniform1i, void (*)(GLint, GLint));
    GLF(glUniform2i, void (*)(GLint, GLint, GLint));
    GLF(glUniform3i, void (*)(GLint, GLint, GLint, GLint));
    GLF(glUniform4i, void (*)(GLint, GLint, GLint, GLint, GLint));
    GLF(glUniformMatrix2fv, void (*)(GLint, GLsizei, GLboolean, const GLfloat*));
    GLF(glUniformMatrix3fv, void (*)(GLint, GLsizei, GLboolean, const GLfloat*));
    GLF(glUniformMatrix4fv, void (*)(GLint, GLsizei, GLboolean, const GLfloat*));
    GLF(glEnable, void (*)(GLenum));
    GLF(glDisable, void (*)(GLenum));
    GLF(glBlendFunc, void (*)(GLenum, GLenum));
    GLF(glDepthFunc, void (*)(GLenum));
    GLF(glCullFace, void (*)(GLenum));
    GLF(glDrawElements, void (*)(GLenum, GLsizei, GLenum, const void*));
    GLF(glGenTextures, void (*)(GLsizei, GLuint*));
    GLF(glBindTexture, void (*)(GLenum, GLuint));
    GLF(glTexParameteri, void (*)(GLenum, GLenum, GLint));
    GLF(glTexImage2D, void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*));
    GLF(glActiveTexture, void (*)(GLenum));
    #undef GLF
    g_gl.ready = 1;
}

struct GlShader { unsigned used, sid, id; int type; };   // sid = JS-side id, id = GL object id
struct GlProg { unsigned used, sid, id; };
struct GlBuf { unsigned used, sid, id; int target; };
struct GlVao { unsigned used, sid, id; };
struct GlTex { unsigned used, sid, id, target; };

struct GlCanvas {
    bool used;
    char cid[32];
    int w, h;
    HWND hwnd; HDC hdc; HGLRC rc;
    uint8_t* fb; int fbw, fbh;   // last rendered frame (BGRA, bottom-up)
    uint8_t* tex_px; int tex_len;   // pending direct texture upload (worker -> UI)
    float cr, cg, cb, ca;
    GlShader sh[16]; GlProg pr[8]; GlBuf bu[16];
    GlVao va[8]; GlTex tx[16];
    // the JS bridge hands out proxy ids for locations (the protocol is
    // one-way), so remember what each proxy maps to on the GL side
    int u_loc[256]; unsigned u_set[256];   // proxy uniform id -> real location
    int a_loc[256]; unsigned a_set[256];   // proxy attrib id -> real location
    unsigned cur_prog;
    GLuint vao;
};
// WebGL surfaces live in a small fixed pool. bloxd opens a menu canvas, then
// navigates to /play/survival and asks for "noa-canvas"; every navigation
// leaves the previous slot behind, so a 4-slot pool ran dry after one route
// change and gl_find_or_create returned null ("GL: canvas create failed
// (noa-canvas@300x150)") - the engine then aborted the room join.
static const int kGlCanvasSlots = 8;
static GlCanvas s_glcv[kGlCanvasSlots];

// Pending direct texture uploads: the JS worker thread copies the pixel bytes
// here (the WGL context does not exist yet while the script runs), and the UI
// thread consumes them when the matching texim command applies. All GL calls
// stay on the UI thread.
struct TexPending { char cid[32]; uint8_t* px; int len; };
static TexPending s_texp[4];

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
    // The canvas id may carry its backing-store size: getContext sends
    // "id|<w>x<h>" because the JS-side canvas.width/height are DOM properties
    // and the native DOM has not seen them yet (the tree is serialized only at
    // the end of the turn). Without this the surface was always born at the
    // 300x150 default while the page asked for 600x360.
    char base[64];
    int w0 = 0, h0 = 0;
    const char* bar = strchr(cid, '@');
    if (bar) {
        size_t bl = (size_t)(bar - cid);
        if (bl >= sizeof(base)) bl = sizeof(base) - 1;
        memcpy(base, cid, bl);
        base[bl] = 0;
        sscanf(bar + 1, "%dx%d", &w0, &h0);
    } else {
        clip_str(base, sizeof(base), cid);
    }
    for (int i = 0; i < kGlCanvasSlots; i++)
        if (s_glcv[i].used && ci_eq(s_glcv[i].cid, base)) return &s_glcv[i];
    int w = 300, h = 150;
    for (int i = 0; i < nn; i++)
        if (nodes[i].tag == Tag::Canvas && ci_eq(nodes[i].id, base)) {
            if (nodes[i].cw > 0) w = nodes[i].cw;
            if (nodes[i].ch > 0) h = nodes[i].ch;
            break;
        }
    if (w0 > 0) w = w0;
    if (h0 > 0) h = h0;
    int slot = -1;
    for (int i = 0; i < kGlCanvasSlots; i++) {
        if (s_glcv[i].used) continue;
        slot = i;
        break;
    }
    if (slot < 0) {
        // Pool is full: drop the oldest surface whose canvas is no longer in the
        // live DOM, otherwise fall back to slot 0 (its page is gone anyway).
        slot = 0;
        for (int i = 0; i < kGlCanvasSlots; i++) {
            bool alive = false;
            for (int k = 0; k < nn; k++)
                if (nodes[k].tag == Tag::Canvas && gl_cid_matches(s_glcv[i].cid, nodes[k].id)) { alive = true; break; }
            if (!alive) { slot = i; break; }
        }
        GlCanvas& d = s_glcv[slot];
        klogf("browser: GL: pool full, reusing slot %d (drop %s)", slot, d.cid);
        if (d.rc) g_gl.wglDeleteContext(d.rc);
        if (d.hdc) ReleaseDC(d.hwnd, d.hdc);
        if (d.hwnd) DestroyWindow(d.hwnd);
        if (d.fb) bfree(d.fb);
        memset(&d, 0, sizeof(d));
    }
    {
        GlCanvas& c = s_glcv[slot];
        memset(&c, 0, sizeof(c));
        c.used = true;
        clip_str(c.cid, sizeof(c.cid), base);
        c.w = w; c.h = h;
        c.cr = 0.f; c.cg = 0.f; c.cb = 0.f; c.ca = 1.f;
        c.hwnd = CreateWindowExA(0, "STATIC", "gl", WS_POPUP, 0, 0, w, h, 0, 0, GetModuleHandleA(0), 0);
        if (!c.hwnd) { c.used = false; klogf("browser: GL: B=%d slot %d hwnd failed (%s)", (int)w, slot, base); return 0; }
        c.hdc = GetDC(c.hwnd);
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
        int pf = ChoosePixelFormat(c.hdc, &pfd);
        if (!pf || !SetPixelFormat(c.hdc, pf, &pfd)) { c.used = false; klogf("browser: GL: B=%d slot %d pf failed pf=%d (%s)", (int)w, slot, pf, base); return 0; }
        c.rc = g_gl.wglCreateContext(c.hdc);
        if (!c.rc) { c.used = false; klogf("browser: GL: B=%d slot %d ctx failed (%s)", (int)w, slot, base); return 0; }
        klogf("browser: GL: B=%d slot %d ok %s %dx%d", (int)w, slot, base, w, h);
        return &c;
    }
    return 0;
}

// out-of-band upload pool (defined in swgl.cpp): vertex/shader payloads too
// large for the text command line travel through these.
int nefu_glbin_take(unsigned id, const uint8_t** out);
void nefu_glbin_release(unsigned id);

// Engine-side canvas ids do not always match the DOM id: bloxd renders the
// world through a context created while the canvas was still called "noa"
// (noa-engine names it before wrapping it as "noa-canvas" in the page). Treat
// a prefix as the same canvas so the surface gets the real size and is blitted
// into the visible element instead of drawing into an offscreen 300x150 box.
static int gl_cid_matches(const char* surf, const char* nodeid) {
    if (!surf || !nodeid) return 0;
    if (ci_eq(surf, nodeid)) return 1;
    size_t a = strlen(surf), b = strlen(nodeid);
    if (a >= 3 && a < b && strncmp(surf, nodeid, a) == 0) return 1;
    if (b >= 3 && b < a && strncmp(surf, nodeid, b) == 0) return 1;
    return 0;
}

// The page sets canvas.width/height on every engine resize (Babylon asks
// clientWidth/clientHeight first, which the shim resolves up to the viewport).
// Those values only reach the native DOM on the next serialized turn, so the
// GL surface has to be resized in place when it does - otherwise the game keeps
// drawing into the born-at-300x150 buffer and shows up as a small patch.
static void gl_surface_resize(GlCanvas& c, int w, int h) {
    if (w < 8) w = 8;
    if (h < 8) h = 8;
    if (c.w == w && c.h == h) return;
    klogf("browser: GL: resize %s %dx%d -> %dx%d", c.cid, c.w, c.h, w, h);
    if (c.hwnd)
        SetWindowPos(c.hwnd, 0, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    c.w = w;
    c.h = h;
    if (c.fb) { bfree(c.fb); c.fb = 0; c.fbw = c.fbh = 0; }
}

static void gl_render_readback(GlCanvas& c) {    if (!c.fb || c.fbw != c.w || c.fbh != c.h) {
        if (c.fb) bfree(c.fb);
        c.fb = 0;
        c.fbw = c.fbh = 0;
        uint8_t* nb = (uint8_t*)balloc((size_t)c.w * (size_t)c.h * 4);
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
    // NEFU-DIAG: host-side command inventory. The JS shim counts calls on its
    // side, so without this list an emit that never lands here looks identical
    // to a draw that produced nothing.
    {
        static char s_seen[64][24];
        static int s_seen_n = 0;
        int known = 0;
        for (int i = 0; i < s_seen_n; i++) if (ci_eq(s_seen[i], cmd)) { known = 1; break; }
        if (!known && s_seen_n < 64) {
            clip_str(s_seen[s_seen_n], sizeof(s_seen[0]), cmd);
            s_seen_n++;
            klogf("browser: GLCMD %s cid=%s", cmd, cid ? cid : "");
        }
    }
    gl_load_wgl();
    GlCanvas* c = 0;
    // NEFU-FIX: getContext sends "id@WxH" while the slot stores only the base id, so a
    // lookup on the raw cid never matched. That meant only the creating call found the
    // canvas and every following GL command (clear/draw/uniform) was dropped as
    // "canvas create failed" - a WebGL page rendered black while reporting no errors.
    char gbase[64];
    const char* at = strchr(cid, '@');
    size_t gbLen = at ? (size_t)(at - cid) : strlen(cid);
    if (gbLen >= sizeof(gbase)) gbLen = sizeof(gbase) - 1;
    if (gbLen) memcpy(gbase, cid, gbLen);
    gbase[gbLen] = 0;
    if (!gbLen) clip_str(gbase, sizeof(gbase), cid);
    for (int i = 0; i < kGlCanvasSlots; i++)
        if (s_glcv[i].used && ci_eq(s_glcv[i].cid, gbase)) { c = &s_glcv[i]; break; }
    if (!c) {
        // Create the surface lazily on the first command, whatever it is.
        // The page's getContext emits a "ctx" line, but that line is produced
        // inside an eval whose output buffer can be as small as 64 bytes (event
        // dispatch, a nested script), so the creation order cannot be trusted:
        // bloxd's game canvas lost its "ctx" line that way and then every
        // clear/bufferData/upload below was dropped, leaving a page that
        // reported a live WebGL context and never drew a pixel.
        static int s_lazy_n = 0;
        c = gl_find_or_create(cid, tab.dom.data(), tab.dom.size());
        if (!c) {
            static int s_drop_n = 0;
            s_drop_n++;
            if (s_drop_n < 40 || (s_drop_n % 200) == 0)
                klogf("browser: GL: DROP cmd=%s cid=%s n=%d", cmd, cid, s_drop_n);
            return;
        }
        if (s_lazy_n++ < 12)
            klogf("browser: GL: lazy %s on cmd=%s %dx%d", cid, cmd, c->w, c->h);
    } else if (ci_eq(cmd, "ctx")) {
        for (int i = 0; i < tab.dom.size(); i++)
            if (tab.dom[i].tag == Tag::Canvas && gl_cid_matches(cid, tab.dom[i].id)) {
                if (c->w != tab.dom[i].cw || c->h != tab.dom[i].ch)
                    gl_surface_resize(*c, tab.dom[i].cw, tab.dom[i].ch);
                break;
            }
    }
    // Once per frame (the clear that starts it) pick up any canvas resize the
    // page asked for since the gl surface was created. The frame-start command
    // is "glclr" (the prefixed names are what the shim emits).
    if (ci_eq(cmd, "glclr") || ci_eq(cmd, "clr")) {
        for (int i = 0; i < tab.dom.size(); i++)
            if (tab.dom[i].tag == Tag::Canvas && gl_cid_matches(gbase, tab.dom[i].id)) {
                // every surface that maps onto this canvas follows its size
                for (int k = 0; k < kGlCanvasSlots; k++)
                    if (s_glcv[k].used && gl_cid_matches(s_glcv[k].cid, tab.dom[i].id)) {
                        if (tab.dom[i].cw > 8 && tab.dom[i].ch > 8 &&
                            (s_glcv[k].w != tab.dom[i].cw || s_glcv[k].h != tab.dom[i].ch))
                            gl_surface_resize(s_glcv[k], tab.dom[i].cw, tab.dom[i].ch);
                    }
                break;
            }
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
    if (ci_eq(cmd, "shb")) {   // sid,type,binid,bytes  (full source, out of band)
        int v[4]; parse_ints(val, v, 4);
        int sid = v[0], type = v[1]; unsigned gid = (unsigned)v[2];
        for (int i = 0; i < 16; i++)
            if (c->sh[i].used && c->sh[i].sid == (unsigned)sid) { nefu_glbin_release(gid); return; }
        const uint8_t* src = 0;
        int len = nefu_glbin_take(gid, &src);
        if (!src || len <= 0) return;
        unsigned glid = g_gl.glCreateShader((GLenum)type);
        if (glid) {
            char* txt = (char*)malloc((size_t)len + 1);
            if (txt) {
                memcpy(txt, src, (size_t)len);
                txt[len] = 0;
                const char* s = txt;
                g_gl.glShaderSource(glid, 1, &s, 0);
                free(txt);
                for (int i = 0; i < 16; i++)
                    if (!c->sh[i].used) {
                        c->sh[i].used = 1; c->sh[i].sid = (unsigned)sid;
                        c->sh[i].id = glid; c->sh[i].type = type;
                        break;
                    }
            }
        }
        nefu_glbin_release(gid);
        return;
    }
    if (ci_eq(cmd, "csh")) {        unsigned sid = (unsigned)atoi(val);
        for (int i = 0; i < 16; i++)
            if (c->sh[i].used && c->sh[i].sid == sid) {
                g_gl.glCompileShader(c->sh[i].id);
                // getShaderParameter is a stub on the shim side, so a compile
                // failure is only visible if the host reports it here.
                {
                    GLint ok = 0;
                    g_gl.glGetShaderiv(c->sh[i].id, GL_COMPILE_STATUS, &ok);
                    if (!ok) {
                        char log[600]; log[0] = 0;
                        g_gl.glGetShaderInfoLog(c->sh[i].id, 500, 0, log);
                        static int s_shf = 0;
                        if (s_shf++ < 8)
                            klogf("browser: GL: COMPILE FAIL sid=%u type=%d log=%s",
                                  sid, c->sh[i].type, log);
                    } else {
                        static int s_shok = 0;
                        if (s_shok++ < 20)
                            klogf("browser: GL: compile ok sid=%u type=%d",
                                  sid, c->sh[i].type);
                    }
                }
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
                GLint ok = 0;
                g_gl.glGetProgramiv(c->pr[i].id, GL_LINK_STATUS, &ok);
                if (!ok) {
                    char log[600]; log[0] = 0;
                    g_gl.glGetProgramInfoLog(c->pr[i].id, 500, 0, log);
                    static int s_lkf = 0;
                    if (s_lkf++ < 6)
                        klogf("browser: GL: LINK FAIL pid=%u log=%s", pid, log);
                }
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
    if (ci_eq(cmd, "bufp")) {   // bid,target,binid,bytes  (full vertex payload)
        int v[4]; parse_ints(val, v, 4);
        int bid = v[0], target = v[1]; unsigned gid = (unsigned)v[2];
        const uint8_t* src = 0;
        int len = nefu_glbin_take(gid, &src);
        GlBuf* b = 0;
        for (int i = 0; i < 16; i++)
            if (c->bu[i].used && c->bu[i].sid == (unsigned)bid) { b = &c->bu[i]; break; }
        if (!b) {
            unsigned glid = 0;
            g_gl.glGenBuffers(1, &glid);
            for (int i = 0; i < 16; i++)
                if (!c->bu[i].used) {
                    c->bu[i].used = 1; c->bu[i].sid = (unsigned)bid;
                    c->bu[i].id = glid; c->bu[i].target = target;
                    b = &c->bu[i];
                    break;
                }
        }
        if (b && src && len > 0) {
            b->target = target;
            g_gl.glBindBuffer((GLenum)target, b->id);
            g_gl.glBufferData((GLenum)target, (GLsizeiptr)len, src, GL_STATIC_DRAW);
        }
        nefu_glbin_release(gid);
        return;
    }
    if (ci_eq(cmd, "bufsubp")) {   // bid,target,offset,binid,bytes
        int v[5]; parse_ints(val, v, 5);
        int bid = v[0], target = v[1], off = v[2]; unsigned gid = (unsigned)v[3];
        const uint8_t* src = 0;
        int len = nefu_glbin_take(gid, &src);
        for (int i = 0; i < 16; i++)
            if (c->bu[i].used && c->bu[i].sid == (unsigned)bid) {
                if (src && len > 0) {
                    g_gl.glBindBuffer((GLenum)target, c->bu[i].id);
                    g_gl.glBufferSubData((GLenum)target, (GLintptr)off,
                                         (GLsizeiptr)len, src);
                }
                break;
            }
        nefu_glbin_release(gid);
        return;
    }
    if (ci_eq(cmd, "buf")) {   // bid,target,data
        int bid = atoi(val);
        const char* p = val; while (*p && *p != ',') p++; if (*p) p++;
        int target = atoi(p); while (*p && *p != ',') p++; if (*p) p++;
        float f[512];
        int n = parse_floats(p, f, 512);
        // NEFU-DIAG: a draw with an empty buffer still reports err=0, so the float
        // count is what separates 'uploaded nothing' from 'uploaded but not visible'.
        static int s_bufN = 0;
        if (s_bufN < 4 && getenv("NF_GL_DBG")) {
            s_bufN++;
            klogf("browser: GL: buf bid=%d target=%d floats=%d head=%.40s", bid, target, n, p);
        }
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
        // loc is a getAttribLocation proxy id; resolve it to the real index
        int rl = (v[0] >= 0 && v[0] < 256 && c->a_set[v[0]]) ? c->a_loc[v[0]] : v[0];
        if (rl < 0) return;
        g_gl.glVertexAttribPointer((GLuint)rl, v[1], GL_FLOAT, GL_FALSE, v[2], (const void*)(size_t)v[3]);
        g_gl.glEnableVertexAttribArray((GLuint)rl);
        return;
    }
    if (ci_eq(cmd, "draw")) {   // mode,count
        int v[2]; parse_ints(val, v, 2);
        g_gl.glViewport(0, 0, c->w, c->h);
        g_gl.glDrawArrays((GLenum)v[0], 0, v[1]);
        // NEFU-DIAG: first draws are the only ones worth printing - a WebGL page that
        // renders black can then be split into "command never arrived", "GL rejected
        // the draw", or "draw ran but readback is empty".
        static int s_drawN = 0;
        if (s_drawN < 3 && getenv("NF_GL_DBG")) {
            s_drawN++;
            klogf("browser: GL: draw mode=%d count=%d err=%d canvas=%dx%d", v[0], v[1],
                  (int)(g_gl.glGetError ? g_gl.glGetError() : 0), c->w, c->h);
        }
        gl_render_readback(*c);
        return;
    }
    // ===================== WebGL2 bridge =====================
    // location proxies: JS gets an opaque id, we map it to the real GL
    // location (the command stream is one-way, so this table is the return path)
    if (ci_eq(cmd, "uloc")) {   // pid,uid,name
        int v[2]; if (parse_ints(val, v, 2) < 2) return;
        const char* nm = val;
        for (int i = 0; i < 2; i++) { while (*nm && *nm != ',') nm++; if (*nm) nm++; }
        unsigned gid = 0;
        for (int i = 0; i < 8; i++) if (c->pr[i].used && c->pr[i].sid == (unsigned)v[0]) gid = c->pr[i].id;
        int rl = (gid && g_gl.glGetUniformLocation) ? g_gl.glGetUniformLocation(gid, nm) : -1;
        if (v[1] >= 0 && v[1] < 256) { c->u_loc[v[1]] = rl; c->u_set[v[1]] = 1; }
        return;
    }
    if (ci_eq(cmd, "aloc")) {   // pid,aid,name
        int v[2]; if (parse_ints(val, v, 2) < 2) return;
        const char* nm = val;
        for (int i = 0; i < 2; i++) { while (*nm && *nm != ',') nm++; if (*nm) nm++; }
        unsigned gid = 0;
        for (int i = 0; i < 8; i++) if (c->pr[i].used && c->pr[i].sid == (unsigned)v[0]) gid = c->pr[i].id;
        int rl = (gid && g_gl.glGetAttribLocation) ? g_gl.glGetAttribLocation(gid, nm) : -1;
        if (v[1] >= 0 && v[1] < 256) { c->a_loc[v[1]] = rl; c->a_set[v[1]] = 1; }
        return;
    }
    if (ci_eq(cmd, "uM2") || ci_eq(cmd, "uM3") || ci_eq(cmd, "uM4")) {   // loc,vals...
        int loc = atoi(val);
        const char* p = val;
        while (*p && *p != ',') p++; if (*p) p++;   // step over loc
        float f[16];
        parse_floats(p, f, 16);
        int rl = (loc >= 0 && loc < 256 && c->u_set[loc]) ? c->u_loc[loc] : loc;
        if (rl < 0) return;
        int side = cmd[2] - '0';
        if (side == 2 && g_gl.glUniformMatrix2fv) g_gl.glUniformMatrix2fv(rl, 1, GL_FALSE, f);
        else if (side == 3 && g_gl.glUniformMatrix3fv) g_gl.glUniformMatrix3fv(rl, 1, GL_FALSE, f);
        else if (side == 4 && g_gl.glUniformMatrix4fv) g_gl.glUniformMatrix4fv(rl, 1, GL_FALSE, f);
        return;
    }
    if (cmd[0] == 'u' && cmd[1] >= '1' && cmd[1] <= '4' && (cmd[2] == 'f' || cmd[2] == 'i')) {
        // uniformNf / uniformNi: loc,v0,...,vN-1
        int loc = atoi(val);
        const char* p = val; while (*p && *p != ',') p++; if (*p) p++;
        float f[4];
        parse_floats(p, f, 4);
        int rl = (loc >= 0 && loc < 256 && c->u_set[loc]) ? c->u_loc[loc] : loc;
        if (rl < 0) return;
        int n = cmd[1] - '0';
        if (cmd[2] == 'f') {
            if (n == 1 && g_gl.glUniform1f) g_gl.glUniform1f(rl, f[0]);
            else if (n == 2 && g_gl.glUniform2f) g_gl.glUniform2f(rl, f[0], f[1]);
            else if (n == 3 && g_gl.glUniform3f) g_gl.glUniform3f(rl, f[0], f[1], f[2]);
            else if (n == 4 && g_gl.glUniform4f) g_gl.glUniform4f(rl, f[0], f[1], f[2], f[3]);
        } else {
            if (n == 1 && g_gl.glUniform1i) g_gl.glUniform1i(rl, (GLint)f[0]);
            else if (n == 2 && g_gl.glUniform2i) g_gl.glUniform2i(rl, (GLint)f[0], (GLint)f[1]);
            else if (n == 3 && g_gl.glUniform3i) g_gl.glUniform3i(rl, (GLint)f[0], (GLint)f[1], (GLint)f[2]);
            else if (n == 4 && g_gl.glUniform4i) g_gl.glUniform4i(rl, (GLint)f[0], (GLint)f[1], (GLint)f[2], (GLint)f[3]);
        }
        return;
    }
    if (ci_eq(cmd, "en")) { if (g_gl.glEnable) g_gl.glEnable((GLenum)atoi(val)); return; }
    if (ci_eq(cmd, "dis")) { if (g_gl.glDisable) g_gl.glDisable((GLenum)atoi(val)); return; }
    if (ci_eq(cmd, "blend")) { int v[2]; parse_ints(val, v, 2); if (g_gl.glBlendFunc) g_gl.glBlendFunc((GLenum)v[0], (GLenum)v[1]); return; }
    if (ci_eq(cmd, "dfunc")) { if (g_gl.glDepthFunc) g_gl.glDepthFunc((GLenum)atoi(val)); return; }
    if (ci_eq(cmd, "cull")) { if (g_gl.glCullFace) g_gl.glCullFace((GLenum)atoi(val)); return; }
    if (ci_eq(cmd, "deli")) {   // mode,count,type,offset
        int v[4]; parse_ints(val, v, 4);
        g_gl.glViewport(0, 0, c->w, c->h);
        if (g_gl.glDrawElements) g_gl.glDrawElements((GLenum)v[0], v[1], (GLenum)v[2], (const void*)(size_t)v[3]);
        gl_render_readback(*c);
        return;
    }
    if (ci_eq(cmd, "vao")) {   // sid
        unsigned sid = (unsigned)atoi(val);
        for (int i = 0; i < 8; i++) if (c->va[i].used && c->va[i].sid == sid) return;
        GLuint gid = 0;
        if (g_gl.glGenVertexArrays) g_gl.glGenVertexArrays(1, &gid);
        for (int i = 0; i < 8; i++)
            if (!c->va[i].used) { c->va[i].used = 1; c->va[i].sid = sid; c->va[i].id = gid; return; }
        return;
    }
    if (ci_eq(cmd, "bva")) {   // sid (0 = default VAO)
        unsigned sid = (unsigned)atoi(val);
        if (sid == 0) { if (g_gl.glBindVertexArray) g_gl.glBindVertexArray(0); c->vao = 0; return; }
        for (int i = 0; i < 8; i++)
            if (c->va[i].used && c->va[i].sid == sid) {
                if (g_gl.glBindVertexArray) g_gl.glBindVertexArray(c->va[i].id);
                c->vao = c->va[i].id;
                return;
            }
        return;
    }
    if (ci_eq(cmd, "tx")) {   // sid
        unsigned sid = (unsigned)atoi(val);
        for (int i = 0; i < 16; i++) if (c->tx[i].used && c->tx[i].sid == sid) return;
        GLuint gid = 0;
        if (g_gl.glGenTextures) g_gl.glGenTextures(1, &gid);
        for (int i = 0; i < 16; i++)
            if (!c->tx[i].used) { c->tx[i].used = 1; c->tx[i].sid = sid; c->tx[i].id = gid; return; }
        return;
    }
    if (ci_eq(cmd, "btx")) {   // target,sid
        int v[2]; parse_ints(val, v, 2);
        if (v[1] == 0) { if (g_gl.glBindTexture) g_gl.glBindTexture((GLenum)v[0], 0); return; }
        for (int i = 0; i < 16; i++)
            if (c->tx[i].used && c->tx[i].sid == (unsigned)v[1]) {
                c->tx[i].target = (unsigned)v[0];
                if (g_gl.glBindTexture) g_gl.glBindTexture((GLenum)v[0], c->tx[i].id);
                return;
            }
        return;
    }
    if (ci_eq(cmd, "texp")) {   // target,pname,param (acts on the bound texture)
        int v[3]; parse_ints(val, v, 3);
        if (g_gl.glTexParameteri) g_gl.glTexParameteri((GLenum)v[0], (GLenum)v[1], v[2]);
        return;
    }
    if (ci_eq(cmd, "texim")) {   // target,level,internal,w,h,border,format,type
        int v[8];
        if (parse_ints(val, v, 8) < 8 || !g_gl.glTexImage2D) return;
        const uint8_t* px = 0;
        for (int i = 0; i < 4; i++)
            if (s_texp[i].px && ci_eq(s_texp[i].cid, cid)) { px = s_texp[i].px; break; }
        if (px) {
            g_gl.glTexImage2D((GLenum)v[0], v[1], v[2], v[3], v[4], v[5], (GLenum)v[6], (GLenum)v[7], px);
            for (int i = 0; i < 4; i++)
                if (s_texp[i].px && ci_eq(s_texp[i].cid, cid)) {
                    bfree(s_texp[i].px);
                    s_texp[i].px = 0; s_texp[i].len = 0; s_texp[i].cid[0] = 0;
                    break;
                }
        } else {
            g_gl.glTexImage2D((GLenum)v[0], v[1], v[2], v[3], v[4], v[5], (GLenum)v[6], (GLenum)v[7], 0);
        }
        return;
    }
    if (ci_eq(cmd, "actex")) { if (g_gl.glActiveTexture) g_gl.glActiveTexture((GLenum)atoi(val)); return; }
}

#else
// Bare metal: the software rasterizer (core/apps/swgl.cpp) implements the
// WebGL2 subset the page needs. The JS side emits the same "\x01gl<cmd>|cid|args"
// control lines as on the host, so the only difference is where the commands
// land: swgl instead of a WGL context.

// Pending texture uploads. The JS worker thread fills a slot (no GL may run
// there) and the UI thread applies it when the matching "texim" command is
// dispatched, exactly like the host's s_texp. The bytes never travel through
// the control line: a 1024x1024 RGBA image would be a 5.6 MB base64 argument.
struct SWTexPending { char cid[32]; uint8_t* px; int len; };
static SWTexPending s_swtexp[4];

static int sw_parse_ints(const char* s, int* out, int maxn) {
    int n = 0;
    while (s && *s && n < maxn) {
        out[n++] = atoi(s);
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
    }
    return n;
}

static void gl_cmd(TabState& tab, const char* cmd, const char* cid, const char* val) {
    if (!cid || !cid[0] || !cmd) return;
    // "ctx" creates the canvas (swgl's default size is 300x150). The real
    // size comes from the DOM - a game canvas is usually much larger - so
    // resize it right after the canvas exists, like the host does for its
    // WGL context.
    swgl_cmd(cid, cmd, val);
    // texImage2D: swgl sized and formatted the texture in the command above;
    // the pixel bytes are waiting in the worker-side slot.
    if (ci_eq(cmd, "texim")) {
        int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        sw_parse_ints(val, v, 8);
        for (int i = 0; i < 4; i++) {
            if (s_swtexp[i].px && ci_eq(s_swtexp[i].cid, cid)) {
                swgl_tex_image(cid, v[3], v[4], v[6], v[7],
                               s_swtexp[i].px, s_swtexp[i].len);
                bfree(s_swtexp[i].px);
                s_swtexp[i].px = 0; s_swtexp[i].len = 0; s_swtexp[i].cid[0] = 0;
                break;
            }
        }
    }
    // texSubImage2D: same pending-slot mechanism as texImage2D, but the bytes
    // land in a sub-rectangle of the already-allocated texture instead of
    // (re)allocating it. This was previously a no-op (bound to the delete
    // stub), so engines that stream texture tiles never saw any pixels.
    if (ci_eq(cmd, "tsub")) {
        int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        sw_parse_ints(val, v, 8);
        for (int i = 0; i < 4; i++) {
            if (s_swtexp[i].px && ci_eq(s_swtexp[i].cid, cid)) {
                swgl_tex_sub_image(cid, v[2], v[3], v[4], v[5], v[6], v[7],
                                   s_swtexp[i].px, s_swtexp[i].len);
                bfree(s_swtexp[i].px);
                s_swtexp[i].px = 0; s_swtexp[i].len = 0; s_swtexp[i].cid[0] = 0;
                break;
            }
        }
    }
    if (ci_eq(cmd, "ctx")) {
        int cw = 0, ch = 0;
        for (int i = 0; i < tab.dom.size(); i++) {
            if (tab.dom[i].tag == Tag::Canvas && ci_eq(tab.dom[i].id, cid)) {
                cw = tab.dom[i].cw;
                ch = tab.dom[i].ch;
                break;
            }
        }
        // A game canvas is styled to fill the window, so the HTML width /
        // height attributes are usually absent and the element keeps the
        // 300x150 default. Rasterize at the viewport size instead, or the
        // scene shows up as a small patch in the corner.
        if (cw < s_view_w) cw = s_view_w;
        if (ch < s_view_h) ch = s_view_h;
        swgl_resize(cid, cw, ch);
        // Keep the DOM node in sync with the real framebuffer size and force a
        // relayout. The JS shim re-render may have reset cw/ch to the 300x150
        // default, and without a relayout the canvas Run keeps its old (small)
        // size so the scene is clipped to a corner patch.
        for (int i = 0; i < tab.dom.size(); i++) {
            if (tab.dom[i].tag == Tag::Canvas && ci_eq(tab.dom[i].id, cid)) {
                tab.dom[i].cw = cw;
                tab.dom[i].ch = ch;
                break;
            }
        }
        tab.layout_w = -1;
    }
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

// Hex digit for the percent-decoding of page-requested navigations. Kept
// separate from the host-only hexv(): navigation must work on the bare kernel.
static int nav_hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decode a navigation field. Kept separate from url_decode(): that
// helper lives in the host-only GL block, and navigation has to work on bare.
static void nav_urldec(const char* src, char* out, int cap) {
    int dn = 0;
    for (const char* t = src; *t && dn < cap - 1; t++) {
        if (*t == '%' && t[1] && t[2]) {
            int hi = nav_hexv(t[1]), lo = nav_hexv(t[2]);
            if (hi >= 0 && lo >= 0) {
                out[dn++] = (char)(hi * 16 + lo);
                t += 2;
                continue;
            }
        }
        out[dn++] = *t;
    }
    out[dn] = 0;
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
                if (cbuf[0] == 'n' && cbuf[1] == 'a' && cbuf[2] == 'v') {
                    // Navigation intent from the page (location.href=, assign/
                    // replace, reload, history.back, form submit). The value is
                    // percent-encoded and runs to the end of the line, so it
                    // shares the long-value branch with the GL commands. Mode 5
                    // (form POST) appends the body after a literal '|' - both
                    // halves are encoded, so that '|' is the only separator.
                    // Parked instead of executed: this parser runs while a load
                    // job is being committed (and inside click handlers), so
                    // starting a new load right here would re-enter the loader.
                    char vbig[4096]; int vn3 = 0;
                    for (const char* t = sep2 + 1; *t && *t != '\n' && vn3 < 4095; vn3++) vbig[vn3] = *t++;
                    vbig[vn3] = 0;
                    int mode = atoi(ibuf);
                    if (mode < 0 || mode > 5) mode = 0;
                    tab.nav_post[0] = 0;
                    if (mode == 5) {
                        char* bar = strchr(vbig, '|');
                        if (bar) {
                            *bar = 0;
                            nav_urldec(bar + 1, tab.nav_post, (int)sizeof(tab.nav_post));
                        }
                        if (!tab.nav_post[0]) mode = 0;   // empty body: plain load
                    }
                    char dec[2048];
                    nav_urldec(vbig, dec, (int)sizeof(dec));
                    clip_str(tab.nav_url, sizeof(tab.nav_url), dec);
                    tab.nav_mode = mode;
                    tab.has_nav = true;
                    klogf("browser: page nav mode=%d url=%.120s body=%d\n",
                          mode, tab.nav_url, (int)strlen(tab.nav_post));
                    while (*c && *c != '\n') c++;
                    if (*c) c++;
                    continue;
                }
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

// Bytes of real page text carried by a DOM list. Text that lives inside
// <script>/<style>/<textarea> is markup payload, not content a reader can see,
// so a CSS-heavy shell does not count as a rendered page. Used to refuse a
// DOM shim re-serialization that would WIPE an otherwise good DOM: the shim
// replaces the whole tree, and when its model is incomplete (seed truncation,
// bundle not loaded) it serializes an empty shell that paints a white page.
static int dom_visible_text_len(const List<DomNode>& nodes) {
    int n = 0;
    for (int i = 0; i < (int)nodes.size(); i++) {
        const DomNode& d = nodes[i];
        if (d.tag != Tag::Text || d.text.len() == 0) continue;
        int p = d.parent, hops = 0;
        bool payload = false;
        while (p >= 0 && p < (int)nodes.size() && hops++ < 12) {
            const DomNode& pa = nodes[p];
            if (pa.tag == Tag::Script || pa.tag == Tag::Style || pa.tag == Tag::Textarea) {
                payload = true; break;
            }
            p = pa.parent;
        }
        if (payload) continue;
        n += (int)(d.text.len() > 2048 ? 2048 : d.text.len());
    }
    return n;
}

static void commit_page(TabState& tab, LoadJob* j) {
    clear_imgs(tab);   // previous page's images are gone
    if (tab.js_html) { bfree(tab.js_html); tab.js_html = 0; }   // old JS state
    if (j->data && j->len > 0) {
        // zero-copy: the worker buffer becomes the page buffer (j->data is
        // NULLed here so poll_jobs does not free it again)
        if (tab.page) kfree(tab.page);
        tab.page = 0;   // raw page bytes are freed by the worker after parsing
        tab.page_cap = (int)j->len + 1;
        tab.page_len = (int)j->len;
        j->data = 0;
    }
    bool ok_page = j->dom != 0;   // parsed DOM is the success marker now

    // history: remember the page we are leaving (only on successful navigation)
    if (j->push_hist) {
        if (ok_page && tab.url[0] && tab.loaded && !tab.is_home && !tab.err[0]) {
            if (tab.back.size() >= HIST_MAX) tab.back.remove(0);
            tab.back.push(tab.url);
        }
        tab.fwd.erase_all();
    }

    clip_str(tab.url, sizeof(tab.url), j->url);
    klogf("browser: commit %s nodes=%d css=%d js_err=%d seed=%d html=%d",
          j->url, j->dom ? (int)j->dom->nodes.size() : 0, j->css_n, (int)j->js_err,
          j->seed_len, j->js_html ? (int)strlen(j->js_html) : 0);
    if (ok_page) {
        tab.loaded = true;
        tab.is_home = false;
        tab.truncated = j->truncated;
        tab.err[0] = 0;
        // the DOM was built by the worker thread; the layout is rebuilt lazily
        // in paint (tab.dom = shallow copy of the node list, same as before)
        tab.dom.erase_all();
        tab.focus_node = -1;    // node indices from the old page are meaningless
        tab.sel_popup_node = -1;
        if (j->dom) {
            if (j->dom->overflow) klogf("browser: fatal: DOM overflow, page truncated (%s)", j->url);
            tab.dom = j->dom->nodes;
            clip_str(tab.title, sizeof(tab.title), j->title);
            tab.css_n = j->css_n;
            for (int k = 0; k < j->css_n && k < CSS_MAX; k++) tab.css[k] = j->css[k];
            tab.js_err = j->js_err;
            // the page booted scripts: keep the runtime pumping every frame
            // (rAF game loop / React scheduler / fetch continuations).
            // js_err (one mid-page script threw) no longer kills the pump:
            // real browsers keep timers alive after a script error, and the
            // committed DOM still needs its async updates + paint.
            tab.js_active = (j->script_len > 0 || j->script_src_n > 0);
            tab.js_tick_ms = platform_tick_ms();
            tab.js_tick_req = 0;
            tab.js_tick_ready = 0;
            tab.js_ev_done = 0;
            if (j->js_out[0]) {
                // \x01 DOM commands mutate the committed DOM; plain console
                // text from print/alert goes to the log, never on screen.
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
                if (vis[0]) klogf("browser: js: %s", vis);
            }
            if (j->js_html && j->js_html[0]) {
                // the DOM shim serialized a JS-built tree: re-parse it and
                // replace the committed DOM (styles stay from the <style>)
                // heap scratch: CSS_MAX rules are ~1.4 MB — too big for the
                // stack, and a static copy would bloat the bare kernel BSS
                CSSRule* css2 = (CSSRule*)kalloc(sizeof(CSSRule) * CSS_MAX);
                int css2_n = 0;
                PageParser pp2(0, 0, css2, &css2_n, CSS_MAX);
                pp2.feed(j->js_html, (int)strlen(j->js_html), true);
                if (!pp2.dom.overflow) {
                    const int old_txt = dom_visible_text_len(tab.dom);
                    const int new_txt = dom_visible_text_len(pp2.dom.nodes);
#ifndef NEFU_BARE
                    // full serialized DOM to disk for layout debugging
                    {
                        const char* ddp = getenv("NF_DUMP_DOM");
                        if (ddp) { FILE* df = fopen(ddp, "wb");
                            if (df) { fwrite(j->js_html, 1, strlen(j->js_html), df); fclose(df); } }
                    }
#endif
                    if (new_txt == 0 && old_txt >= 32) {
                        // The shim's model lost everything the static page had (incomplete
                        // seed / killed bundle): keep the parsed DOM instead of painting
                        // its empty shell — that shell is the baidu.com white page.
                        klogf("browser: re-render REJECTED — shim output has no text (old=%d bytes, %d nodes vs %d new) len=%d head=%.200s",
                              old_txt, (int)tab.dom.size(), (int)pp2.dom.nodes.size(),
                              (int)strlen(j->js_html), j->js_html);
                    } else {
                    tab.dom = pp2.dom.nodes;
                    klogf("browser: js dom re-render (%d nodes, text %d -> %d bytes) len=%d html=%.400s",
                          (int)tab.dom.size(), old_txt, new_txt, (int)strlen(j->js_html), j->js_html);
                    const unsigned char* hp = (const unsigned char*)j->js_html;
                    char hb[256]; int hn = 0;
                    for (int i = 0; i < 48 && hp[i]; i++) hn += snprintf(hb + hn, (int)sizeof(hb) - hn, "%02x ", hp[i]);
                    klogf("browser: js_html hex: %s", hb);
                    for (int dbi = 0; dbi < (int)tab.dom.size() && dbi < 10; dbi++) {
                        const DomNode& dn2 = tab.dom[dbi];
                        char dbg2[40] = {0};
                        if (dn2.tag == Tag::Text) {
                            int cl = dn2.text.len() < 30 ? (int)dn2.text.len() : 30;
                            for (int ci2 = 0; ci2 < cl; ci2++) dbg2[ci2] = dn2.text[ci2];
                            klogf("browser: rr node#%d TEXT '%s'", dbi, dbg2);
                        } else {
                            const char* tnm = tag_name_of(dn2.tag);
                            klogf("browser: rr node#%d TAG %s kids=%d", dbi, tnm[0] ? tnm : "(unknown)", (int)dn2.kids.size());
                        }
                    }
                    }
                } else {
                    klogf("browser: js dom re-render overflow src=%d need=%d scap=%d MAX_PAGE=%d (nodes=%d depth-cap=%d, html=%d head=%.120s), kept original DOM",
                          pp2.dom.ovf_src, pp2.ovf_need, pp2.scap, MAX_PAGE, (int)pp2.dom.nodes.size(), DOM_DEPTH, (int)strlen(j->js_html), j->js_html);
                }
                kfree(css2);
            }
            tab.js_msg[0] = 0;
        }
        if (!tab.title[0]) url_host(j->url, tab.title, sizeof(tab.title));
    } else {
        tab.loaded = true;
        tab.is_home = false;
        tab.truncated = false;
        tab.page_len = 0;
        tab.err[0] = 0;
        ksprintf(tab.err, sizeof(tab.err), "Cannot reach %s", j->url);
        klogf("browser: load failed: %s", j->url);
        clip_str(tab.title, sizeof(tab.title), "Error");
    }
    clip_str(tab.input, sizeof(tab.input), tab.url);
    tab.input_len = (int)strlen(tab.input);
    tab.input_cursor = tab.input_len;
    tab.layout_w = -1;   // force a re-layout
    tab.scroll = 0;
    tab.js_dom_hash = 0; // new page: the next tick must reparse regardless
    tab.load_ms = platform_tick_ms() - j->start_ms;
}

static void poll_jobs(BrowserState* st) {
    poll_img_jobs(st);
    for (int t = 0; t < MAX_TABS; t++) {
        TabState& tab = st->tabs[t];
        LoadJob* j = tab.job;
        if (!j || !j->done || j->committed) continue;
        tab.job = 0;
        j->committed = 1;
        // j->data is freed by the worker as soon as parsing is done, so the
        // success marker is the parsed DOM, not the raw page buffer
        if (tab.used && !j->cancelled && j->ok && j->dom) {
            commit_page(tab, j);   // consumes j->js_html (DOM shim re-render)
            if (j->js_html) { bfree(j->js_html); j->js_html = 0; }
        } else if (tab.used && !j->cancelled) {
            if (j->js_html) { bfree(j->js_html); j->js_html = 0; }
            // load failed
            tab.loaded = true;
            tab.is_home = false;
            tab.page_len = 0;
            tab.err[0] = 0;
            ksprintf(tab.err, sizeof(tab.err), "Cannot reach %s", j->url);
            klogf("browser: load failed: %s", j->url);
            clip_str(tab.title, sizeof(tab.title), "Error");
            clip_str(tab.url, sizeof(tab.url), j->url);
            tab.layout_w = -1;
            tab.scroll = 0;
        }
        if (j->data) kfree(j->data);
        if (j->php_buf) bfree(j->php_buf);
        if (j->dom) delete j->dom;
        // start_job's retire path and ~TabState free these too — null them so
        // the buffers are never released twice (silent double-free crash on
        // every second navigation).
        j->data = 0;
        j->php_buf = 0;
        j->dom = 0;
#ifndef NEFU_BARE
        tab.job_keep = j;   // worker stays resident for event dispatch
#else
        if (j->seed) { bfree(j->seed); j->seed = 0; }
        delete j;
#endif
        if (tab.used && tab.has_queued) {
            tab.has_queued = false;
            char u[2048];
            clip_str(u, sizeof(u), tab.queued);
            const bool post = ci_eq(tab.queued_method, "POST");
            start_job(tab, u, true,
                      post ? "POST" : 0,
                      post ? tab.queued_post : 0, tab.queued_post_len);
        }
    }
}

static void navigate_url(TabState& tab, const char* url, bool push_hist) {
    if (!url || !url[0]) return;
    klogf("browser: navigate %s\n", url);
    clip_str(tab.input, sizeof(tab.input), url);
    tab.input_len = (int)strlen(tab.input);
    tab.input_cursor = tab.input_len;
    tab.has_queued = false;
    tab.err[0] = 0;
    start_job(tab, url, push_hist);
}

// Submit a form: same bookkeeping as navigate_url(), but the request carries a
// body and the method stays POST. The address bar shows the action URL only -
// that is exactly what a browser does when a login form is submitted.
static void navigate_post(TabState& tab, const char* url, const char* body,
                          int body_len) {
    if (!url || !url[0]) return;
    if (!body || body_len <= 0) { navigate_url(tab, url, true); return; }
    klogf("browser: post %s (%d bytes)\n", url, body_len);
    clip_str(tab.input, sizeof(tab.input), url);
    tab.input_len = (int)strlen(tab.input);
    tab.input_cursor = tab.input_len;
    tab.has_queued = false;
    tab.err[0] = 0;
    start_job(tab, url, true, "POST", body, body_len);
}

static void navigate_input(TabState& tab) {
    char u[2048];
    smart_url(tab.input, u, sizeof(u), tab.search_engine);
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
        // placeholder (dimmed while focused) so the bar is never blank; the
        // active search engine name is shown so the user knows where a bare
        // keyword will be sent.
        char ph[96];
        int e = tab.search_engine;
        if (e < 0 || e >= kSearchEngineCount) e = kDefaultSearchEngine;
        ksprintf(ph, sizeof(ph), "Search (%s) or enter address",
                 kSearchEngines[e].name);
        gfx::text(s, text_x, rc.y + 4, ph,
                  focus ? 0xCBD5E1 : C_ADDR_BD, C_ADDR_BG);
    }
    if (focus) {
        int cx = text_x + tab.input_cursor * 8 - disp;
        if (cx >= rc.x + 4 && cx < rc.x + rc.w - 4)
            gfx::char8x16(s, cx, rc.y + 4, '|', C_TAB_ACT, C_ADDR_BG);
    }
}

// Geometry of an open <select> dropdown: locate the kind9 run of the node in
// the cached layout and derive the panel rect (page coords) + option count.
static void post_js_script(TabState& tab, const char* script, bool urgent);
// Geometry of an open <select> dropdown: locate the kind9 run of the node in
// the cached layout and derive the panel rect (page coords) + option count.
static bool select_popup_rect(const TabState& tab, int& px0, int& py0, int& pw, int& pn) {
    px0 = py0 = pw = pn = 0;
    if (tab.sel_popup_node < 0 || tab.sel_popup_node >= tab.dom.size()) return false;
    const DomNode& sel = tab.dom[tab.sel_popup_node];
    if (sel.tag != Tag::Select) return false;
    for (int i = 0; i < (int)tab.layout.runs.size(); i++) {
        const Run& r = tab.layout.runs[i];
        if (r.kind != 9 || r.node != tab.sel_popup_node) continue;
        px0 = r.x; py0 = r.y + r.h; pw = r.w;
        for (int k = 0; k < sel.kids.size(); k++)
            if (tab.dom[sel.kids[k]].tag == Tag::Option) pn++;
        return pn > 0;
    }
    return false;
}

// Pick an option in the open dropdown (the popup variant of select_cycle):
// mark it selected, tell the page through __nfuSelect, invalidate layout.
static void select_pick(TabState& tab, int idx) {
    const int node = tab.sel_popup_node;
    if (node < 0 || node >= tab.dom.size()) return;
    DomNode& sel = tab.dom[node];
    if (sel.tag != Tag::Select) return;
    int k = -1;
    String val;
    bool ok = false;
    for (int i = 0; i < sel.kids.size(); i++) {
        DomNode& o = tab.dom[sel.kids[i]];
        if (o.tag != Tag::Option) continue;
        k++;
        o.checked = (k == idx);
        if (k == idx) { ok = true; option_value(tab.dom, o, val); }
    }
    if (ok && sel.nfu_idx >= 0) {
        char js[192];
        ksprintf(js, sizeof(js), "try{__nfuSelect(%d,%d);}catch(e){}", sel.nfu_idx, idx);
        post_js_script(tab, js, true);
    }
    klogf("browser: select popup node=%d -> option %d value=%.24s", node, idx, val.c_str());
}

static void paint_page(Surface& s, const PageLayout& lay, int scroll,
                       int vx, int vy, int vw, int vh, Img* imgs = 0,
                       int* last_used = 0, int gen = 0, const char* base_url = 0,
                       int focus_node = -1, const TabState* tabp = 0) {
    // per-frame run listing is a layout diagnostic (NF_RUN_DBG=1); printing it
    // unconditionally produced 26 MB logs on JS-heavy pages
    static int run_dbg = -1;
    if (run_dbg < 0) {
        const char* rd = getenv("NF_RUN_DBG");
        run_dbg = (rd && rd[0] && rd[0] != '0') ? 1 : 0;
    }
    if (run_dbg) {
        const char* rt = getenv("NF_RUN_TXT");
        for (int i = 0; i < lay.runs.size(); i++) {
            const Run& r = lay.runs[i];
            if (r.kind == 3 || r.kind == 4)
                klogf("run %d: kind=%d z=%d x=%d y=%d w=%d h=%d col=%06x",
                      i, r.kind, r.z, r.x, r.y, r.w, r.h, r.color & 0xFFFFFF);
            else if (rt && rt[0] && r.kind == 0)
                klogf("run %d: txt y=%d x=%d w=%d z=%d col=%06x '%.44s'",
                      i, r.y, r.x, r.w, r.z, r.color & 0xFFFFFF, r.text.c_str());
        }
    }
    // pass 0: page-level (body/html) backgrounds at the very bottom, in run
    // order (colour first, then image — the image sits on the colour). They
    // are pinned to the viewport: a document background does not scroll away
    // with the content, and a short run would otherwise be culled as soon as
    // the page scrolled.
    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        if (!r.full_page) continue;
        if (r.kind == 3) {
            gfx::fillrect(s, vx + r.x, vy, r.w, vh, r.color);
            continue;
        }
        if (r.kind != 4) continue;
        if (r.img_id >= 0 && imgs && imgs[r.img_id].done && imgs[r.img_id].rgba) {
            if (last_used) last_used[r.img_id] = gen;
            draw_img_cover(s, vx + r.x, vy, r.w, vh, imgs[r.img_id]);
        } else if (r.img_id >= 0 && imgs && imgs[r.img_id].done && base_url &&
                   imgs[r.img_id].url[0]) {
            if (last_used) last_used[r.img_id] = gen;
            img_restart(imgs[r.img_id]);
        }
    }
    // pass 1: block backgrounds (kind 3) underneath everything else, painted
    // shallow DOM depth first so a nested child's box sits on its parent's.
    for (int zz = 0; zz <= Z_PAINT_MAX; zz++) {
      for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        if (r.kind != 3 || r.full_page || r.z != zz) continue;
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;   // plain skip: runs are NOT
        // y-sorted anymore (fixed overlays interleave, and a break here
        // silently dropped every background run after one off-screen box)
        if (r.gcol2) {
            static int gdbg = -1;
            if (gdbg < 0) { const char* e = getenv("NF_IMG_DBG"); gdbg = (e && e[0] && e[0] != '0') ? 1 : 0; }
            if (gdbg) klogf("gradbox: node=%d x=%d y=%d w=%d h=%d c=%06x c2=%06x",
                            r.node, r.x, r.y, r.w, r.h, r.color & 0xFFFFFF, r.gcol2 & 0xFFFFFF);
            fill_grad_rect(s, vx + r.x, vy + sy, r.w, r.h, r.color, r.gcol2, r.gdir);
        }
        else if (r.radius != 0)
            fill_round_rect(s, vx + r.x, vy + sy, r.w, r.h, r.radius, r.color);
        else
            gfx::fillrect(s, vx + r.x, vy + sy, r.w, r.h, r.color);
        if (r.border > 0 && r.bcol) {
            int bw = r.border; if (bw > 8) bw = 8;
            gfx::fillrect(s, vx + r.x, vy + sy, r.w, bw, r.bcol);                     // top
            gfx::fillrect(s, vx + r.x, vy + sy + r.h - bw, r.w, bw, r.bcol);          // bottom
            gfx::fillrect(s, vx + r.x, vy + sy, bw, r.h, r.bcol);                     // left
            gfx::fillrect(s, vx + r.x + r.w - bw, vy + sy, bw, r.h, r.bcol);          // right
        }
      }
    }
    // pass 1.5: element background images (kind 4), above colors, below content
    for (int zz = 0; zz <= Z_PAINT_MAX; zz++) {
      for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        if (r.kind != 4 || r.full_page || r.z != zz) continue;
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;   // plain skip: runs are NOT
        // y-sorted anymore (fixed overlays interleave, and a break here
        // silently dropped every background run after one off-screen box)
        if (r.img_id >= 0 && imgs && imgs[r.img_id].done && imgs[r.img_id].rgba) {
            if (last_used) last_used[r.img_id] = gen;
            draw_img_cover(s, vx + r.x, vy + sy, r.w, r.h, imgs[r.img_id]);
        } else if (r.img_id >= 0 && imgs && imgs[r.img_id].done && base_url &&
                   imgs[r.img_id].url[0]) {
            if (last_used) last_used[r.img_id] = gen;   // visible again: shield from reclaim
            img_restart(imgs[r.img_id]);                // refetch (network or inline)
        }
      }
    }
    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;   // plain skip: runs are NOT
        // y-sorted anymore (fixed overlays interleave, and a break here
        // silently dropped every background run after one off-screen box)   // vertical culling

        if (r.kind == 2) {   // horizontal rule
            int x0 = vx + r.x, x1 = vx + r.x + r.w;
            if (x0 < vx) x0 = vx;
            if (x1 > vx + vw) x1 = vx + vw;
            if (x1 > x0) gfx::hline(s, x0, x1 - 1, vy + sy, r.color);
            continue;
        }
        if (r.kind == 1) {   // image
            {
                static int pdbg = -1;
                if (pdbg < 0) {
                    const char* e = getenv("NF_IMG_DBG");
                    pdbg = (e && e[0] && e[0] != '0') ? 1 : 0;
                }
                if (pdbg && r.img_id >= 0 && imgs)
                    klogf("paintimg: node=%d id=%d done=%d rgba=%d %dx%d box=%d,%d %dx%d",
                          r.node, r.img_id, (int)imgs[r.img_id].done,
                          imgs[r.img_id].rgba != 0, imgs[r.img_id].disp_w,
                          imgs[r.img_id].disp_h, r.x, r.y - scroll, r.w, r.h);
            }
            if (r.img_id >= 0 && imgs && imgs[r.img_id].done && imgs[r.img_id].rgba) {
                if (last_used) last_used[r.img_id] = gen;
                draw_img_box(s, vx + r.x, vy + sy, r.w, r.h, imgs[r.img_id]);
                if (r.noframe) { /* no border: real browsers do not outline images */ }
            } else {
                if (r.img_id >= 0 && imgs && imgs[r.img_id].done && base_url &&
                    imgs[r.img_id].url[0]) {
                    if (last_used) last_used[r.img_id] = gen;   // visible again: shield from reclaim
                    img_restart(imgs[r.img_id]);
                }
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
        if (r.kind == 6) {   // text field (<input> / <textarea>)
            int bx = vx + r.x, by = vy + sy;
            bool foc = (r.node >= 0 && r.node == focus_node);
            gfx::fillrect(s, bx, by, r.w, r.h, C_INPUT_BG);
            gfx::rect(s, bx, by, r.w, r.h, foc ? C_LINK : C_INPUT_EDGE);
            if (foc) gfx::rect(s, bx + 1, by + 1, r.w - 2, r.h - 2, C_LINK);
            const bool has_val = (r.text.len() > 0);
            const char* shown = has_val ? r.text.c_str() : r.ph.c_str();
            uint32_t tcol = has_val ? C_TEXT_DK : C_GRAY;
            int len = (int)strlen(shown);
            int maxc = (r.w - 8) / 8;
            if (maxc < 1) maxc = 1;
            int start = 0;
            if (len > maxc) start = len - maxc;    // keep the caret end visible
            int cx = bx + 4;
            for (int k = start; k < len && k - start < maxc; k++) {
                unsigned char ch = (unsigned char)shown[k];
                if (ch < 128) gfx::char8x16(s, cx, by + r.h / 2 - 6, (char)ch, tcol, C_INPUT_BG);
                cx += 8;
            }
            if (foc) gfx::fillrect(s, cx, by + 5, 1, r.h - 10, C_TEXT_DK);
            continue;
        }
        if (r.kind == 7) {   // button-style <input> (submit / button / reset)
            int bx = vx + r.x, by = vy + sy;
            gfx::fillrect(s, bx, by, r.w, r.h, C_IMG);
            gfx::rect(s, bx, by, r.w, r.h, C_INPUT_EDGE);
            // raised look: a light top edge, a darker bottom one
            gfx::fillrect(s, bx + 1, by + 1, r.w - 2, 1, 0xFFFFFFFF);
            gfx::fillrect(s, bx + 1, by + r.h - 2, r.w - 2, 1, C_INPUT_EDGE);
            const char* lbl = r.text.c_str();
            int len = (int)strlen(lbl);
            int maxc = (r.w - 12) / 8;
            if (maxc < 1) maxc = 1;
            if (len > maxc) len = maxc;
            int x0 = bx + (r.w - len * 8) / 2;
            if (x0 < bx + 4) x0 = bx + 4;
            for (int k = 0; k < len; k++) {
                unsigned char ch = (unsigned char)lbl[k];
                if (ch < 128) gfx::char8x16(s, x0 + k * 8, by + r.h / 2 - 6, (char)ch, C_TEXT_DK, C_IMG);
            }
            continue;
        }
        if (r.kind == 8) {   // checkbox / radio
            int bx = vx + r.x, by = vy + sy;
            int bs = (r.w < r.h) ? r.w : r.h;
            gfx::fillrect(s, bx, by, bs, bs, C_INPUT_BG);
            gfx::rect(s, bx, by, bs, bs, C_INPUT_EDGE);
            if (r.checked) {
                if (r.radio) {
                    int d = bs / 2;
                    gfx::fillrect(s, bx + (bs - d) / 2, by + (bs - d) / 2, d, d, C_LINK);
                } else {
                    // tick: two strokes, the short leg down-left, long leg up-right
                    gfx::line(s, bx + 3, by + bs / 2, bx + bs / 2 - 1, by + bs - 4, C_TEXT_DK);
                    gfx::line(s, bx + bs / 2 - 1, by + bs - 4, bx + bs - 3, by + 4, C_TEXT_DK);
                }
            }
            continue;
        }
        if (r.kind == 9) {   // <select>: closed dropdown showing the selection
            int bx = vx + r.x, by = vy + sy;
            gfx::fillrect(s, bx, by, r.w, r.h, C_INPUT_BG);
            gfx::rect(s, bx, by, r.w, r.h, C_INPUT_EDGE);
            const char* lbl = r.text.c_str();
            int len = (int)strlen(lbl);
            int maxc = (r.w - 28) / 8;            // leave room for the arrow
            if (maxc < 1) maxc = 1;
            if (len > maxc) len = maxc;
            for (int k = 0; k < len; k++) {
                unsigned char ch = (unsigned char)lbl[k];
                if (ch < 128) gfx::char8x16(s, bx + 6 + k * 8, by + r.h / 2 - 6, (char)ch, C_TEXT_DK, C_INPUT_BG);
            }
            // disclosure triangle
            int ax = bx + r.w - 16, ay = by + r.h / 2 - 2;
            for (int k = 0; k < 5; k++) gfx::hline(s, ax + k, ax + 8 - k, ay + k, C_GRAY);
            continue;
        }
        if (r.kind == 5) {   // canvas (WebGL frame or placeholder)
            bool cv_matched = false;
#ifdef NEFU_BARE
            // Bare metal: the software rasterizer keeps the rendered frame.
            // It is top-down, so the rows are copied in order.
            if (r.cid[0]) {
                int fw = 0, fh = 0;
                const uint8_t* fb = swgl_frame(r.cid, &fw, &fh);
                if (fb && fw > 0 && fh > 0) {
                    int bw = fw < r.w ? fw : r.w;
                    int bh = fh < r.h ? fh : r.h;
                    for (int yy = 0; yy < bh; yy++) {
                        int py = vy + sy + yy;
                        if (py < 0 || py >= s.height) continue;
                        const uint8_t* row = fb + (size_t)yy * (size_t)fw * 4;
                        for (int xx = 0; xx < bw; xx++) {
                            int px = vx + r.x + xx;
                            if (px < 0 || px >= s.width) continue;
                            const uint8_t* pp = row + (size_t)xx * 4;
                            uint32_t cc = (uint32_t)pp[0] | ((uint32_t)pp[1] << 8) |
                                          ((uint32_t)pp[2] << 16);
                            s.setpx(px, py, cc);
                        }
                    }
                    cv_matched = true;
                }
            }
#else
            if (r.cid[0]) {
                const char* cid = r.cid;
                for (int i = 0; i < kGlCanvasSlots; i++) {
                    const GlCanvas& gc = s_glcv[i];

                    if (!gc.used || !gc.fb || !gl_cid_matches(gc.cid, cid)) continue;
                    int bw = gc.w < r.w ? gc.w : r.w;
                    int bh = gc.h < r.h ? gc.h : r.h;
                    {
                        static int s_blit_n = 0;
                        if (s_blit_n++ < 6)
                            klogf("[CNV] blit cid=%s fb=%dx%d run=%dx%d vx=%d vy=%d sy=%d",
                                  cid, gc.w, gc.h, r.w, r.h, vx, vy, sy);
                    }
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
            gfx::text(s, vx + r.x, vy + sy, r.text.c_str(), r.color, C_PAGE_BG | 0x80000000);
            // text-decoration: links underline by default, but a reset
            // ("a{text-decoration:none}", every Tailwind preflight) must win.
            const bool ul = (r.deco == 1) || (r.deco == 0 && r.link);
            if (ul) {
                int x0 = vx + r.x, x1 = vx + r.x + r.w - 1;
                if (x0 < vx) x0 = vx;
                if (x1 > vx + vw - 1) x1 = vx + vw - 1;
                if (x1 > x0) gfx::hline(s, x0, x1, vy + sy + 15, r.color);
            }
        }
    }
    // <select> dropdown overlay: floats above all page content, clamped to
    // the viewport bottom. The selected option gets a highlight bar.
    if (tabp && tabp->sel_popup_node >= 0) {
        int qx, qy, qw, qn;
        if (select_popup_rect(*tabp, qx, qy, qw, qn)) {
            int ph = qn * 24;
            if (qy - scroll + ph > vh) ph = vh - (qy - scroll);   // clamp
            if (ph > 0) {
                const int sx0 = vx + qx, sy0 = vy + qy - scroll;
                gfx::fillrect(s, sx0, sy0, qw, ph, C_INPUT_BG);
                gfx::rect(s, sx0, sy0, qw, ph, C_INPUT_EDGE);
                const DomNode& sel = tabp->dom[tabp->sel_popup_node];
                const int cur = select_sel_kid(tabp->dom, sel);
                int k = 0;
                for (int i = 0; i < sel.kids.size() && (k + 1) * 24 <= ph; i++) {
                    const DomNode& o = tabp->dom[sel.kids[i]];
                    if (o.tag != Tag::Option) continue;
                    if (k == cur)
                        gfx::fillrect(s, sx0 + 1, sy0 + k * 24 + 1, qw - 2, 22, C_TAB_ACT);
                    String txt;
                    option_text(tabp->dom, o, txt);
                    if (txt.len() == 0) option_value(tabp->dom, o, txt);
                    const char* lbl = txt.c_str();
                    int len = (int)strlen(lbl);
                    int maxc = (qw - 12) / 8;
                    if (maxc < 1) maxc = 1;
                    if (len > maxc) len = maxc;
                    const uint32_t tc = (k == cur) ? 0xFFFFFF : C_TEXT_DK;
                    const uint32_t bc = (k == cur) ? C_TAB_ACT : C_INPUT_BG;
                    for (int c = 0; c < len; c++) {
                        unsigned char ch = (unsigned char)lbl[c];
                        if (ch < 128)
                            gfx::char8x16(s, sx0 + 6 + c * 8, sy0 + k * 24 + 4,
                                          (char)ch, tc, bc);
                    }
                    k++;
                }
            }
        }
    }
}

// Scrollbar geometry shared by the painter and the mouse handler so the thumb
// you can grab is exactly the thumb you see. Thumb height is proportional to
// the visible fraction (clamped so a very long page still leaves a target);
// thumb_y is the offset inside the track.
static void sb_geom(int vh, int scroll, int content_h, int& thumb_h, int& thumb_y) {
    thumb_h = vh;
    thumb_y = 0;
    if (vh <= 0 || content_h <= vh) return;
    thumb_h = vh * vh / content_h;
    if (thumb_h < 28) thumb_h = 28;
    if (thumb_h > vh) thumb_h = vh;
    const int max_scroll = content_h - vh;
    if (max_scroll <= 0) return;
    thumb_y = (vh - thumb_h) * scroll / max_scroll;
    if (thumb_y < 0) thumb_y = 0;
    if (thumb_y > vh - thumb_h) thumb_y = vh - thumb_h;
}

// Real scrollbar gutter: track + proportional thumb + grip lines. `active`
// darkens the thumb while it is being dragged. When the page fits, only the
// empty track is drawn so the gutter still reads as chrome.
static void paint_scrollbar(Surface& s, int sbx, int vy, int vh, int scroll,
                            int content_h, bool active) {
    if (vh <= 0) return;
    gfx::fillrect(s, sbx, vy, SB_W, vh, C_SB_TRACK);
    gfx::fillrect(s, sbx, vy, 1, vh, 0xFFD5DAE2);   // separator vs page content
    int thumb_h, thumb_y;
    sb_geom(vh, scroll, content_h, thumb_h, thumb_y);
    if (thumb_h >= vh) return;                      // nothing to scroll
    const uint32_t col = active ? 0xFF6B7280 : 0xFF9CA3AF;
    gfx::fillrect(s, sbx + 2, vy + thumb_y, SB_W - 4, thumb_h, col);
    if (thumb_h >= 26) {                            // grip lines (3 dashes)
        const uint32_t g = active ? 0xFFE5E7EB : 0xFFF3F4F6;
        const int cx = sbx + SB_W / 2 - 2;
        const int cy = vy + thumb_y + thumb_h / 2;
        gfx::fillrect(s, cx, cy - 6, 4, 1, g);
        gfx::fillrect(s, cx, cy - 1, 4, 1, g);
        gfx::fillrect(s, cx, cy + 4, 4, 1, g);
    }
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

// Queue a DOM script for the page runtime. The host build hands it to the
// resident worker (the thread that owns QuickJS) and consumes the result on a
// later paint; the bare build runs it inline. Never blocks the UI thread.
// Re-map the focused text field after a DOM swap (page load / JS re-render):
// node indices are per-render, the data-nfu index survives.
static void refocus_by_nfu(TabState& tab, int nfu_idx) {
    tab.focus_node = -1;
    if (nfu_idx < 0) return;
    for (int i = 0; i < tab.dom.size(); i++) {
        const DomNode& d = tab.dom[i];
        if (d.nfu_idx == nfu_idx && field_is_text(d)) {
            tab.focus_node = i;
            return;
        }
    }
}

static void post_js_script(TabState& tab, const char* script, bool urgent = false) {
    if (!script || !script[0]) return;
    if (!tab.js_html) {
        tab.js_html_sz = kPageHtmlCap;
        tab.js_html = (char*)balloc((size_t)tab.js_html_sz);
    }
#ifndef NEFU_BARE
    if (tab.job_keep && !tab.job_keep->worker_done) {
        // busy MUST be checked too: while the worker is inside mini_js_run
        // both pending and done are 0, so an act-driver post landed straight
        // in the slot, armed pending=1, and the worker cleared js_html[0] at
        // the top of the next run before the main thread consumed the
        // previous run's done — losing the DOM update (e.g. the click's
        // re-render) most frames.
        if (tab.js_ev_pending || tab.js_ev_done || tab.js_ev_busy) {
            // single event slot. Low-priority traffic (mousemove streams)
            // must be droppable, but urgent events (clicks on buttons) get a
            // hold slot and are posted the moment the worker frees up —
            // dropping a click silently breaks every page interaction.
            if (!urgent) return;
            klogf("browser: ev slot busy, parking urgent script (%d bytes)", (int)strlen(script));
            clip_str(tab.js_ev_urgent, sizeof(tab.js_ev_urgent), script);
            tab.js_ev_urgent_wait = 1;
            // Ask the JS runtime to cut the script that owns the slot if it
            // keeps holding it. Without this a page bootstrap (bloxd's bundle)
            // keeps the slot for tens of seconds and every click lands here
            // forever - the UI looks frozen while the log says "stepped".
            // Only after the first commit: cutting a bootstrap mid-way leaves
            // a half-built engine and the page never paints at all.
            if (tab.job_keep && tab.job_keep->committed) mini_js_request_preempt(6000);
            return;
        }
        clip_str(tab.js_ev_script, sizeof(tab.js_ev_script), script);
        tab.js_ev_html = tab.js_html;
        tab.js_ev_html_sz = tab.js_html_sz;
        tab.js_ev_out[0] = 0;
        tab.js_ev_done = 0;
        tab.js_ev_pending = 1;
        tab.js_ev_wait = 0;
        return;
    }
#endif
    char out[JS_OUT_MAX];
    out[0] = 0;
    nefu::mini_js_set_env(tab.url, tab.title, 0, 0);
    nefu::mini_js_run(script, out, (int)sizeof(out), tab.js_html, tab.js_html_sz);
    if (out[0]) {
        // The output channel carries two kinds of traffic: the "\x01gl..."
        // control lines the GL bridge uses, and plain text the page printed.
        // The plain text used to be dropped, which made every page-side
        // problem invisible; on the bare kernel the serial log is the only
        // place to look.
        //
        // Both kinds share one buffer, so the split has to be done per line:
        // a control line starts with \x01, a printed line does not. Looking
        // only at the first byte of the whole buffer threw away every
        // printed line as soon as a control command came first.
        const char* p = out;
        while (*p) {
            const char* nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > 0 && p[0] != '\x01') {
                char msg[520];
                int n = (int)len;
                if (n > 500) n = 500;
                memcpy(msg, p, (size_t)n);
                msg[n] = 0;
                for (int i = 0; i < n; i++) if (msg[i] == '\r') msg[i] = ' ';
                klogf("js: out %s\n", msg);
            }
            if (!nl) break;
            p = nl + 1;
        }
        apply_js_commands(tab, out);
    }
    tab.layout_w = -1;
}

// Build and queue one DOM event for the page: element-scoped when idx >= 0,
// document/window-scoped otherwise (idx -1). key is a character or keysym.
static void post_js_event(TabState& tab, const char* type, int idx,
                          int x, int y, const char* key, int mods) {
    if (!tab.js_active) return;
    char k[40];
    k[0] = 0;
    if (key && key[0]) {
        int n = 0;
        k[n++] = '\'';
        for (const char* p = key; *p && n < (int)sizeof(k) - 4; p++) {
            if (*p == '\'' || *p == '\\') k[n++] = '\\';
            k[n++] = *p;
        }
        k[n++] = '\'';
        k[n] = 0;
    } else {
        clip_str(k, sizeof(k), "0");
    }
    char ev[192];
    ksprintf(ev, sizeof(ev), "try{__nfuEvent('%s',%d,%d,%d,%s,%d,0);}catch(e){}",
             type, idx, x, y, k, mods);
    post_js_script(tab, ev);
}

// Mouse variant: carries the pointer delta so a pointer-locked game can turn
// the camera (MouseEvent.movementX/Y).
static void post_js_mouse(TabState& tab, const char* type, int idx,
                          int x, int y, int mvx, int mvy) {
    if (!tab.js_active) return;
    char ev[192];
    ksprintf(ev, sizeof(ev),
             "try{__nfuMvX=%d;__nfuMvY=%d;__nfuEvent('%s',%d,%d,%d,0,0,0);}catch(e){}",
             mvx, mvy, type, idx, x, y);
    // clicks / mousedowns drive app logic and must never be dropped for the
    // mousemove stream; moves are droppable low-priority traffic
    post_js_script(tab, ev, strcmp(type, "mousemove") != 0);
}

static void on_key(Window* w, const KeyEvent* e);   // defined below, used by the type feeder
static void post_js_focus(TabState& tab, int nfu_idx, bool on);   // ditto

static void on_paint(Window* w) {
    Surface* s = &w->back;
    BrowserState* st = (BrowserState*)w->userdata;
    if (!st) return;

    poll_jobs(st);   // commit finished loads (also starts queued ones)

    TabState& tab = st->tabs[st->active];

    // A navigation the page asked for itself (location.href=, assign/replace,
    // reload, history.back, form submit) is applied here - one frame after the
    // script that requested it - so the loader is never re-entered mid-eval.
    if (tab.has_nav) {
        tab.has_nav = false;
        const int mode = tab.nav_mode;
        char u[2048];
        clip_str(u, sizeof(u), tab.nav_url);
        if (mode == 2) { reload(tab); }
        else if (mode == 3) { go_back(tab); }
        else if (mode == 4) { go_fwd(tab); }
        else if (mode == 5) {
            // A form the page submitted itself: replay it as a real POST.
            char b[3072];
            clip_str(b, sizeof(b), tab.nav_post);
            tab.nav_post[0] = 0;
            char abs[2048];
            resolve_url(tab.url, u, abs, sizeof(abs));
            if (abs[0] && b[0]) navigate_post(tab, abs, b, (int)strlen(b));
        }
        else if (u[0]) {
            // javascript:/data:/mailto: in location.href = ... must not become a
            // page load (real browsers execute or hand them off; loading them
            // would only produce a bogus request).
            if (ci_starts(u, "javascript:") || ci_starts(u, "data:") ||
                ci_starts(u, "mailto:") || ci_starts(u, "tel:") ||
                ci_starts(u, "about:")) {
                klogf("browser: page nav ignored (%.24s)", u);
            } else {
                char abs[2048];
                resolve_url(tab.url, u, abs, sizeof(abs));
                if (abs[0] && nav_host_is(abs, "page-kit.io")) {
                    // Ad-recovery interstitial (Venatus): refuse and stay.
                    klogf("browser: page nav refused (sdk vendor interstitial)");
                } else if (abs[0]) {
                    if (mode == 1) clip_str(tab.url, sizeof(tab.url), abs);   // address only
                    else navigate_url(tab, abs, true);
                }
            }
        }
    }

    // ---- scripted typing --------------------------------------------------
    // Each character is delivered as a real key event, so it travels the same
    // path as the keyboard (native field editing + JS mirror). Typing waits
    // until a text field actually has focus, which makes "click the box, then
    // type" work even though both actions are queued up front.
    if (s_type_len > 0 || s_type_enter) {
        // Nothing focused yet: take the first real text field of the page, so
        // "open <url> type:hello key:Enter" works without a click action.
        if (tab.focus_node < 0 && tab.loaded) {
            for (int i = 0; i < tab.dom.size(); i++) {
                const DomNode& d = tab.dom[i];
                if (!field_is_text(d)) continue;
                tab.focus_node = i;
                post_js_focus(tab, d.nfu_idx, true);
                tab.layout_w = -1;
                klogf("browser: typing auto-focused field node=%d name=%.20s\n", i, d.fname.c_str());
                break;
            }
        }
        if (tab.focus_node >= 0) {
            st->input_focus = false;   // the page field owns the keyboard now
            uint32_t now = platform_tick_ms();
            if (now - s_type_at > 90) {
                KeyEvent ev;
                ev.down = true;
                ev.mods = 0;
                for (int i = 0; i < 8; i++) ev.utf8[i] = 0;
                if (s_type_len > 0) {
                    ev.keycode = KEY_NONE;
                    ev.ascii = s_type_buf[0];
                    on_key(w, &ev);
                    memmove(s_type_buf, s_type_buf + 1, (size_t)s_type_len);
                    s_type_len--;
                } else {
                    // text finished: an explicit submit lands here as Enter,
                    // which is what triggers the form navigation natively
                    ev.keycode = KEY_ENTER;
                    ev.ascii = 0;
                    s_type_enter = 0;
                    on_key(w, &ev);
                }
                s_type_at = now;
            }
        }
    }

    // ---- JS results coming back from the resident runtime ------------------
    // 1) a dispatched event script finished (click / key / mouse / nav)
        if (tab.js_ev_done) {
        tab.js_ev_done = 0;
        // NOTE: consume the finished run's output FIRST. Re-posting the
        // parked urgent script before reading js_html raced the resident
        // worker (it wakes within 2 ms, clears js_html[0] at the top of the
        // next run) so the click's re-render was lost most frames and the
        // screen kept showing the pre-click DOM.
        if (tab.js_ev_out[0]) {
            apply_js_commands(tab, tab.js_ev_out);
            char vis[JS_OUT_MAX]; int vn = 0;
            for (const char* c = tab.js_ev_out; *c && vn < JS_OUT_MAX - 1; ) {
                if (*c == '\x01') { while (*c && *c != '\n') c++; if (*c) c++; continue; }
                vis[vn++] = *c++;
            }
            vis[vn] = 0;
            if (vis[0]) klogf("browser: js: %s", vis);
        }
        if (tab.js_html && tab.js_html[0]) {
            int keep_nfu = (tab.focus_node >= 0 && tab.focus_node < tab.dom.size())
                               ? tab.dom[tab.focus_node].nfu_idx : -1;
            CSSRule* css2 = (CSSRule*)kalloc(sizeof(CSSRule) * CSS_MAX);
            int css2_n = 0;
            PageParser pp2(0, 0, css2, &css2_n, CSS_MAX);
            pp2.feed(tab.js_html, (int)strlen(tab.js_html), true);
            if (!pp2.dom.overflow) {
                tab.dom = pp2.dom.nodes;
                if (keep_nfu >= 0) refocus_by_nfu(tab, keep_nfu);
                klogf("browser: js dom re-render (%d nodes) via event (html_len=%d)",
                      tab.dom.size(), (int)strlen(tab.js_html));
                // NF_DUMP_EV: dump the exact markup an event run handed back.
                // A click that collapses bloxd's 387-node shell into the bare
                // preroll page can only be diagnosed by looking at this text.
                {
                    const char* ev_dump = getenv("NF_DUMP_EV");
                    if (ev_dump && ev_dump[0] && ev_dump[0] != '0') {
                        char pth[300];
                        ksprintf(pth, sizeof(pth), "%s\\ev_dom_%u.html",
                                 getenv("TEMP") ? getenv("TEMP") : ".", (unsigned)platform_tick_ms());
                        FILE* df = fopen(pth, "wb");
                        if (df) { fwrite(tab.js_html, 1, strlen(tab.js_html), df); fclose(df);
                                  klogf("browser: ev dom dumped: %s", pth); }
                    }
                }
                if (tab.dom.size() < 50) {
                    char pth[300];
                    ksprintf(pth, sizeof(pth), "%s\\ev_small_%u.html",
                             getenv("TEMP") ? getenv("TEMP") : ".", (unsigned)platform_tick_ms());
                    FILE* df = fopen(pth, "wb");
                    if (df) { fwrite(tab.js_html, 1, strlen(tab.js_html), df); fclose(df);
                              klogf("browser: small reparse dumped: %s head=%.200s", pth, tab.js_html); }
                }
            } else {
                klogf("browser: ev reparse OVERFLOW src=%d need=%d nodes=%d html_len=%d",
                      pp2.dom.ovf_src, pp2.ovf_need, (int)pp2.dom.nodes.size(),
                      (int)strlen(tab.js_html));
            }
            kfree(css2);
            tab.layout_w = -1;
        }
        if (tab.js_ev_urgent_wait) {
            // worker just freed up: post the parked urgent (click) script
            tab.js_ev_urgent_wait = 0;
            klogf("browser: flushing parked urgent script (%d bytes)", (int)strlen(tab.js_ev_urgent));
            post_js_script(tab, tab.js_ev_urgent, false);
        }
        tab.js_ev_wait = 0;
    }
    // 2) a frame of the rAF / timer pump produced new GL commands or markup
    if (tab.js_tick_ready) {
        tab.js_tick_ready = 0;
        if (tab.js_tick_out[0]) apply_js_commands(tab, tab.js_tick_out);
        if (tab.js_tick_html && tab.js_tick_html[0]) {
            // Hash first: a big page re-serializes to the same 600 KB body every
            // frame, and re-parsing that per frame burns the frame budget for
            // nothing. Only a CHANGED tree needs the reparse.
            unsigned hsh = 2166136261u;
            for (const char* hc = tab.js_tick_html; *hc; hc++) hsh = (hsh ^ (unsigned char)*hc) * 16777619u;
#ifndef NEFU_BARE
            {
                static int s_tick_seq = 0, s_tick_last = -1;
                const char* tdp = getenv("NF_DUMP_TICK");
                int tlen = (int)strlen(tab.js_tick_html);
                if (tdp && tlen != s_tick_last && s_tick_seq < 40) {
                    s_tick_last = tlen;
                    char tpath[600];
                    snprintf(tpath, sizeof(tpath), "%s_%02d_%d.html", tdp, s_tick_seq++, tlen);
                    FILE* tf = fopen(tpath, "wb");
                    if (tf) { fwrite(tab.js_tick_html, 1, tlen, tf); fclose(tf); }
                    klogf("browser: NF_DUMP_TICK #%d -> %s (%d bytes, %d nodes now)",
                          s_tick_seq - 1, tpath, tlen, (int)tab.dom.size());
                }
            }
#endif
            if (hsh != tab.js_dom_hash) {
            tab.js_dom_hash = hsh;
            int keep_nfu = (tab.focus_node >= 0 && tab.focus_node < tab.dom.size())
                               ? tab.dom[tab.focus_node].nfu_idx : -1;
            CSSRule* css3 = (CSSRule*)kalloc(sizeof(CSSRule) * CSS_MAX);
            int css3_n = 0;
            PageParser pp3(0, 0, css3, &css3_n, CSS_MAX);
            pp3.feed(tab.js_tick_html, (int)strlen(tab.js_tick_html), true);
            if (!pp3.dom.overflow) {
                const int old_txt = dom_visible_text_len(tab.dom);
                const int new_txt = dom_visible_text_len(pp3.dom.nodes);
                if (new_txt == 0 && old_txt >= 32) {
                    // Same anti-wipe rule as the commit path: a timer that
                    // serializes an empty model must not erase the page.
                    klogf("browser: tick re-render REJECTED (no text; old=%d bytes) len=%d", old_txt,
                          (int)strlen(tab.js_tick_html));
                } else {
                    tab.dom = pp3.dom.nodes;
                    if (keep_nfu >= 0) refocus_by_nfu(tab, keep_nfu);
                    tab.layout_w = -1;
                }
            }
            kfree(css3);
            }
        }
    }
    // 2b) a parked urgent script whose slot never frees through js_ev_done
    // (the slot can stay busy because the rAF pump holds it): once the worker
    // is actually idle on the event channel — not even mid-execution — flush
    // it. The busy flag closes the pending=0/done=0 window while the worker
    // is inside mini_js_run, during which posting would clobber its buffers.
    if (tab.js_ev_urgent_wait && !tab.js_ev_pending && !tab.js_ev_done &&
        !tab.js_ev_busy) {
        tab.js_ev_urgent_wait = 0;
        char js[4096];
        clip_str(js, sizeof(js), tab.js_ev_urgent);
        tab.js_ev_urgent[0] = 0;
        post_js_script(tab, js, false);
    }
    // 3) click hit-test result from the previous mouse event
    if (s_debug_nfu_click >= 0) {
        // test-harness hook (--nfuclick): dispatch a click on a DOM element
        // by its data-nfu index without depending on viewport geometry
        klogf("browser: nfuclick consumed idx=%d js_active=%d", s_debug_nfu_click, tab.js_active ? 1 : 0);
        tab.pending_nfu = s_debug_nfu_click;
        tab.has_pending_nfu = true;
        s_debug_nfu_click = -1;
    }
    if (tab.has_pending_nfu) {
        tab.has_pending_nfu = false;
        char ev[96];
        ksprintf(ev, sizeof(ev), "__nfuDispatch(%d,0,0);", tab.pending_nfu);
        post_js_script(tab, ev, true);   // urgent: never dropped for mousemoves
    }
    // 3b) run the queued page actions ("key:Escape", "text:Play", ...).
    // The page has to be live first, and a freshly loaded React tree needs a
    // moment before its dialogs exist, so each action is retried until it
    // actually hits. A one-shot attempt would skip the consent dialog for
    // good: it only renders once the dialog in front of it was dismissed.
    //
    // The whole sequence runs on the JS side (g.__nfuActRun): the page
    // reports whether an action hit, retries on a miss and advances on a
    // hit. C only hands over the list, so no state has to be mirrored.
    // The page executes the queued actions and keeps its own cursor: it
    // performs one step per call, advances on a hit and retries on a miss.
    // The host drives the rhythm (one step every 1.5 s), because the page
    // runtime flushes its timers after every eval - a retry scheduled inside
    // the page would burn through all its attempts in a single call.
    if (s_action_count > 0 && tab.js_active) {
        uint32_t now = platform_tick_ms();
        if (now - s_action_at > 1500) {
            char ev[512];
            int n = 0;      // characters written
            int cnt = 0;    // actions written
            ev[0] = 0;
            for (int i = 0; i < s_action_count && cnt < 6; i++) {
                const char* a = s_actions[i];
                int l = (int)strlen(a);
                if (n + l + 4 >= (int)sizeof(ev)) break;
                if (n) ev[n++] = ',';
                ev[n++] = '\'';
                for (int j = 0; j < l && n < (int)sizeof(ev) - 3; j++) {
                    if (a[j] == '\'' || a[j] == '\\') ev[n++] = '\\';
                    ev[n++] = a[j];
                }
                ev[n++] = '\'';
                cnt++;
            }
            ev[n] = 0;
            char full[560];
            ksprintf(full, sizeof(full), "try{__nfuActRun([%s]);}catch(e){}", ev);
            klogf("browser: act script: %s\n", full);
            post_js_script(tab, full, true);
            klogf("browser: page actions stepped (%d queued)\n", s_action_count);
            s_action_at = now;
        }
    }
    // Periodic JS probe: NF_PROBE_JS holds a snippet whose return value (or
    // thrown message) is printed as [PROBE] every NF_PROBE_MS milliseconds.
    // Debugging aid for pages whose internal state is not observable from the
    // DOM, e.g. a stalled async bootstrap chain.
    if (tab.js_active) {
        static const char* s_probe = getenv("NF_PROBE_JS");
        static int s_probe_ms = 0;
        static int s_probe_logged = 0;
        if (!s_probe_logged) {
            s_probe_logged = 1;
            klogf("browser: probe env %s (len=%d)\n", s_probe ? "set" : "unset", s_probe ? (int)strlen(s_probe) : 0);
        }
        if (s_probe && s_probe[0] && !s_probe_ms) {
            const char* v = getenv("NF_PROBE_MS");
            s_probe_ms = v ? atoi(v) : 3000;
            if (s_probe_ms < 500) s_probe_ms = 500;
        }
        if (s_probe && s_probe[0]) {
            uint32_t now = platform_tick_ms();
            static uint32_t s_probe_at = 0;
            if (now - s_probe_at > (uint32_t)s_probe_ms) {
                s_probe_at = now;
                // 1400 clipped a 1295-char probe right at the wrapper boundary
                // and QuickJS reported "expecting ')'" at <script>:1:1400.
                char full[4096];
                // The page context has no `print` (only the shim's console), so
                // the original print()-based form threw inside both try blocks
                // and produced nothing at all - the probe looked like it never
                // ran. console.log is routed to stderr by the shim.
                ksprintf(full, sizeof(full),
                         "try{console.log('[PROBE] '+String((function(){%s})()));}catch(e){try{console.log('[PROBE-ERR] '+e)}catch(e2){}}",
                         s_probe);
                post_js_script(tab, full, true);
            }
        }
    }
    // 4) drive the page's animation loop: one tick per frame while idle
    if (tab.js_active) {
        uint32_t now = platform_tick_ms();
        bool runtime_live = (tab.job_keep && !tab.job_keep->worker_done);
#ifdef NEFU_BARE
        runtime_live = true;   // no threads: the runtime lives on this thread
#endif
        {   // why is the tick request stalled? (5 s cadence)
            static uint32_t last_tl = 0;
            if (now - last_tl > 5000) {
                last_tl = now;
                fprintf(stderr, "js: TREQ live=%d req=%d evp=%d evd=%d busy=%d dt=%u wdone=%d\n",
                        (int)runtime_live, (int)tab.js_tick_req, (int)tab.js_ev_pending,
                        (int)tab.js_ev_done, (int)tab.js_ev_busy,
                        now - tab.js_tick_ms,
                        tab.job_keep ? (int)tab.job_keep->worker_done : -1);
                fflush(stderr);
            }
        }
        if (runtime_live && !tab.js_tick_req && !tab.js_ev_pending &&
            !tab.js_ev_done && now - tab.js_tick_ms >= 16) {
            if (!tab.js_tick_html) {
                tab.js_tick_html_sz = 256 * 1024;
                tab.js_tick_html = (char*)balloc((size_t)tab.js_tick_html_sz);
            }
#ifdef NEFU_BARE
            // single-threaded: pump right here, render on the next frame
            tab.js_tick_out[0] = 0;
            if (tab.js_tick_html) tab.js_tick_html[0] = 0;
            int n = nefu::mini_js_tick(tab.js_tick_out, (int)sizeof(tab.js_tick_out),
                                       tab.js_tick_html, tab.js_tick_html_sz);
            tab.js_tick_ready = (n > 0) ? 1 : 0;
#else
            tab.js_tick_req = 1;   // the resident worker runs it
            tab.js_tick_req_at = now;
#endif
            tab.js_tick_ms = now;
        }
    }
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
    // NF_VIEW_W / NF_VIEW_H override the layout viewport without resizing the
    // window. Used to reproduce a phone-shaped viewport (bloxd's 219x763
    // reference) so portrait-only CSS branches can be compared like for like.
    {
        static int ovw = -2, ovh = -2;
        if (ovw == -2) {
            const char* a = getenv("NF_VIEW_W"); const char* b = getenv("NF_VIEW_H");
            ovw = (a && a[0]) ? atoi(a) : 0;
            ovh = (b && b[0]) ? atoi(b) : 0;
        }
        if (ovw > 50 && ovw < 4096) vw = ovw;
        if (ovh > 50 && ovh < 4096) vh = ovh;
    }
    // Remember the viewport for the GL bridge: a WebGL canvas is rasterized
    // at this size (a game canvas has no width/height attributes).
    s_view_w = vw;
    s_view_h = vh;
    gfx::fillrect(*s, 0, BAR_H, cw, ch - STAT_H - BAR_H, C_PAGE_BG);

    // build / rebuild the layout lazily (once per navigation or resize)
    if (tab.layout_w != vw) {
        tab.layout.runs.erase_all();
        if (!tab.loaded || tab.is_home) {
            build_home(tab.layout, vw);
        } else if (tab.dom.size() > 0) {
            layout_page(tab.dom, vw, tab.layout, tab.imgs, tab.url,
                        tab.css_n > 0 ? tab.css : 0, tab.css_n);
            if (tab.truncated) {
                klogf("browser: warning: page truncated at %d MB (%s)",
                      (tab.page_cap + 524288) / 1048576, tab.url);
            }
        }
        tab.layout_w = vw;
    }

    tab.max_scroll = maxi(0, tab.layout.height - vh);
    if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll;

    // visibility tracking + offscreen image reclamation: decoded images that
    // have not been on screen for several frames release their pixels (cache
    // keeps the URL/size, so scrolling back simply refetches one at a time)
    tab.img_gen++;
    if (tab.js_err && tab.loaded && !tab.is_home) {
        bool has_text = false;
        for (int i = 0; i < tab.layout.runs.size() && !has_text; i++) {
            const Run& r = tab.layout.runs[i];
            if (r.kind == 0 && r.text.len() > 0) has_text = true;
        }
        if (!has_text) {
            // SPA / heavy-JS site that the mini engine cannot run: show a
            // friendly panel with the site name instead of a blank viewport.
            int pw = 440, ph = 96;
            int px = vx + (vw - pw) / 2, py = vy + vh / 2 - ph / 2;
            gfx::fillrect(*s, px, py, pw, ph, 0xFF111827);
            gfx::rect(*s, px, py, pw, ph, C_HRLINE);
            const char* l1 = "This site needs a full JavaScript engine";
            char l2[64];
            url_host(tab.url, l2, sizeof(l2));
            char l3[64];
            if (tab.title[0]) {
                int k = 0;
                for (; k < (int)sizeof(l3) - 1 && tab.title[k]; k++)
                    l3[k] = ((unsigned char)tab.title[k] < 128) ? tab.title[k] : '?';
                l3[k] = 0;
            } else l3[0] = 0;
            int ty = py + 26;
            int x0 = px + pw / 2 - (int)strlen(l1) * 4;
            if (x0 < px + 6) x0 = px + 6;
            for (int c = 0; l1[c]; c++)
                gfx::char8x16(*s, x0 + c * 8, ty, l1[c], 0xFFFFFFFF, 0xFF111827);
            int l2n = (int)strlen(l2);
            if (l2n > 50) l2n = 50;
            x0 = px + pw / 2 - l2n * 4;
            if (x0 < px + 6) x0 = px + 6;
            for (int c = 0; c < l2n; c++)
                gfx::char8x16(*s, x0 + c * 8, ty + 20, l2[c], 0xFF60A5FA, 0xFF111827);
            if (l3[0]) {
                int l3n = (int)strlen(l3);
                if (l3n > 50) l3n = 50;
                x0 = px + pw / 2 - l3n * 4;
                if (x0 < px + 6) x0 = px + 6;
                for (int c = 0; c < l3n; c++)
                    gfx::char8x16(*s, x0 + c * 8, ty + 40, l3[c], C_GRAY, 0xFF111827);
            }
        } else {
            paint_page(*s, tab.layout, tab.scroll, vx, vy, vw, vh, tab.imgs,
                       tab.img_last_used, tab.img_gen, tab.url, tab.focus_node, &tab);
        }
    } else {
        paint_page(*s, tab.layout, tab.scroll, vx, vy, vw, vh, tab.imgs,
                   tab.img_last_used, tab.img_gen, tab.url, tab.focus_node, &tab);
    }
    // ---- scrollbar --------------------------------------------------------
    // The browser owns its gutter (Window::own_scrollbar) so the drawn thumb
    // and the grabbable thumb are the same rectangle, and the WM does not
    // paint a second thin bar on top of it.
    paint_scrollbar(*s, cw - SB_W - 2, vy, vh, tab.scroll, vh + tab.max_scroll,
                    tab.sb_drag != 0);
    w->own_scrollbar = true;
    int done_cnt = 0;
    for (int k = 0; k < IMG_MAX; k++)
        if (tab.imgs[k].done && tab.imgs[k].rgba) done_cnt++;
    if (done_cnt > 4) {
        for (int k = 0; k < IMG_MAX; k++) {
            Img& im = tab.imgs[k];
            // reclaim any decoded image that was not painted for a while (the
            // threshold is a grace window in frames: some pages keep two
            // variant logos (light/dark) at the same spot; with a 1-frame
            // window they ping-ponged reclaim->refetch every frame). Scrolling
            // back triggers an automatic refetch. One per frame keeps redraws
            // smooth. Static pages: the first paint marks visible images and
            // reclaims the off-screen ones after the window expires.
            if (im.done && im.rgba && tab.img_gen - tab.img_last_used[k] >= 300) {
                kfree(im.rgba);
                im.rgba = 0;
                im.done = false;
                im.is565 = false;
                im.reclaimed = true;   // refetch keeps the existing layout box
                klogf("browser: reclaimed offscreen image %s", im.url);
                break;   // one per frame keeps re-draws smooth
            }
        }
    }
    // mirror the total content height so the WM draws the unified scrollbar
    // (with draggable thumb); browser keeps the SB_W gutter in its layout
    w->content_scroll_h = vh + tab.max_scroll;
    w->scroll_y = tab.scroll;

    // ---- status ----
    paint_status(*s, tab, cw, ch);
}

// ===================== input handlers =====================
// Map an OS key event to the DOM `key` value pages expect (printable char, or
// the spec name for the non-printing keys a game/SPA actually reads).
static void key_name(const KeyEvent* e, char* out, int cap) {
    out[0] = 0;
    if (e->utf8[0]) { clip_str(out, cap, e->utf8); return; }
    switch (e->keycode) {
        case KEY_ENTER: clip_str(out, cap, "Enter"); return;
        case KEY_BACKSPACE: clip_str(out, cap, "Backspace"); return;
        case KEY_DEL: clip_str(out, cap, "Delete"); return;
        case KEY_TAB: clip_str(out, cap, "Tab"); return;
        case KEY_ESC: clip_str(out, cap, "Escape"); return;
        case KEY_LEFT: clip_str(out, cap, "ArrowLeft"); return;
        case KEY_UP: clip_str(out, cap, "ArrowUp"); return;
        case KEY_RIGHT: clip_str(out, cap, "ArrowRight"); return;
        case KEY_DOWN: clip_str(out, cap, "ArrowDown"); return;
        case KEY_HOME: clip_str(out, cap, "Home"); return;
        case KEY_END: clip_str(out, cap, "End"); return;
        case KEY_PGUP: clip_str(out, cap, "PageUp"); return;
        case KEY_PGDN: clip_str(out, cap, "PageDown"); return;
        case KEY_SPACE: clip_str(out, cap, " "); return;
        case KEY_SHIFT: clip_str(out, cap, "Shift"); return;
        case KEY_CTRL: clip_str(out, cap, "Control"); return;
        case KEY_ALT: clip_str(out, cap, "Alt"); return;
        default: break;
    }
    unsigned char c = (unsigned char)e->ascii;
    if (c >= 32 && c < 127) { out[0] = (char)c; out[1] = 0; }
}

// ---- focused form field editing -----------------------------------------
// <input>/<textarea> are edited natively: the C++ DOM is what gets painted, so
// the value has to live there. Every edit is mirrored into the JS DOM through
// __nfuSetInput so the page's own handlers (React's onChange above all) see the
// keystrokes; the serializer then sends the resulting value back on re-render.

static void js_escape_single(const char* in, char* out, int cap) {
    int n = 0;
    for (const char* p = in; *p && n < cap - 8; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\r') continue;
        if (c == '\\' || c == '\'') out[n++] = '\\';
        if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; continue; }
        out[n++] = (char)c;
    }
    out[n] = 0;
}

static void post_js_input(TabState& tab, int nfu_idx, const char* val) {
    if (nfu_idx < 0) return;
    char esc[400];
    js_escape_single(val ? val : "", esc, sizeof(esc));
    char ev[460];
    ksprintf(ev, sizeof(ev), "try{__nfuSetInput(%d,'%s');}catch(e){}", nfu_idx, esc);
    post_js_script(tab, ev, true);
}

static void post_js_focus(TabState& tab, int nfu_idx, bool on) {
    if (nfu_idx < 0) return;
    char ev[96];
    ksprintf(ev, sizeof(ev), "try{__nfuFocusEl(%d,%d);}catch(e){}", nfu_idx, on ? 1 : 0);
    post_js_script(tab, ev);
}

// Push a checkbox/radio state into the page (change event -> React onChange).
static void post_js_checked(TabState& tab, int nfu_idx, bool v) {
    if (nfu_idx < 0) return;
    char ev[96];
    ksprintf(ev, sizeof(ev), "try{__nfuSetChecked(%d,%d);}catch(e){}", nfu_idx, v ? 1 : 0);
    post_js_script(tab, ev);
}

// ---- checkboxes / radios -------------------------------------------------
// A click flips a checkbox and *selects* a radio. Radios that share a name
// inside the same form are one group: picking one clears the others, which is
// what makes a plan/gender/shipping picker behave.
static void toggle_check(TabState& tab, int node) {
    if (node < 0 || node >= tab.dom.size()) return;
    if (tab.dom[node].tag != Tag::Input) return;
    const bool radio = ci_eq(tab.dom[node].ftype, "radio");
    const bool now = radio ? true : !tab.dom[node].checked;
    int form = node;
    while (form > 0 && tab.dom[form].tag != Tag::Form) form = tab.dom[form].parent;
    if (form <= 0) form = -1;
    if (radio) {
        const char* gname = tab.dom[node].fname.c_str();
        for (int i = 0; i < tab.dom.size(); i++) {
            DomNode& d = tab.dom[i];
            if (i == node || d.tag != Tag::Input || !ci_eq(d.ftype, "radio")) continue;
            if (strcmp(d.fname.c_str(), gname) != 0) continue;
            if (form > 0) {
                int p = i;
                while (p > 0 && tab.dom[p].tag != Tag::Form) p = tab.dom[p].parent;
                if (p != form) continue;
            }
            if (d.checked) { d.checked = false; post_js_checked(tab, d.nfu_idx, false); }
        }
    }
    tab.dom[node].checked = now;
    post_js_checked(tab, node, now);
    tab.layout_w = -1;             // the run carries a copy of the state
    klogf("browser: %s %s node=%d name=%.16s",
          radio ? "radio" : "checkbox", now ? "on" : "off", node,
          tab.dom[node].fname.c_str());
}

// A click on a <label> activates the control it wraps - that is how most sites
// let users hit the words instead of the 16px box.
static int label_control(TabState& tab, int node) {
    int p = node;
    for (int up = 0; p > 0 && p < tab.dom.size() && up < 6; up++) {
        const DomNode& d = tab.dom[p];
        if (ci_eq(d.tag_name, "label")) {
            for (int q = 0; q < tab.dom.size(); q++) {
                if (tab.dom[q].tag != Tag::Input) continue;
                if (!field_is_text(tab.dom[q]) && !ci_eq(tab.dom[q].ftype, "checkbox") &&
                    !ci_eq(tab.dom[q].ftype, "radio")) continue;
                int pp = q;
                while (pp > 0 && pp != p) pp = tab.dom[pp].parent;
                if (pp == p) return q;
            }
            return -1;
        }
        p = d.parent;
    }
    return -1;
}

// Click on a closed <select>: advance to the next option. A real popup list
// would need a floating overlay; cycling keeps the control usable and, more
// importantly, lets the form submit the value the user actually picked.
static void select_cycle(TabState& tab, int node) {
    if (node < 0 || node >= tab.dom.size()) return;
    DomNode& sel = tab.dom[node];
    if (sel.tag != Tag::Select) return;
    int nopt = 0, cur = -1;
    for (int i = 0; i < sel.kids.size(); i++) {
        const DomNode& o = tab.dom[sel.kids[i]];
        if (o.tag != Tag::Option) continue;
        if (o.checked) cur = nopt;
        nopt++;
    }
    if (nopt == 0) return;
    const int next = (cur < 0) ? 0 : ((cur + 1) % nopt);
    int k = -1;
    String val;
    for (int i = 0; i < sel.kids.size(); i++) {
        DomNode& o = tab.dom[sel.kids[i]];
        if (o.tag != Tag::Option) continue;
        k++;
        o.checked = (k == next);
        if (k == next) option_value(tab.dom, o, val);
    }
    if (sel.nfu_idx >= 0) {
        char js[192];
        ksprintf(js, sizeof(js), "try{__nfuSelect(%d,%d);}catch(e){}", sel.nfu_idx, next);
        post_js_script(tab, js, true);
    }
    tab.layout_w = -1;
    klogf("browser: select node=%d -> option %d value=%.24s", node, next, val.c_str());
}

static void field_set(TabState& tab, int node, const char* text, bool append) {
    if (node < 0 || node >= tab.dom.size()) return;
    DomNode& n = tab.dom[node];
    if (append) n.value += text; else n.value = text;
    post_js_input(tab, n.nfu_idx, n.value.c_str());
    tab.layout_w = -1;              // the run carries a copy of the text
}

static void field_backspace(TabState& tab, int node) {
    if (node < 0 || node >= tab.dom.size()) return;
    DomNode& n = tab.dom[node];
    int l = n.value.len();
    if (l <= 0) return;
    int i = l - 1;                                   // never split a UTF-8 char
    while (i > 0 && ((unsigned char)n.value[i] & 0xC0) == 0x80) i--;
    String v;
    for (int k = 0; k < i; k++) v += n.value[k];
    field_set(tab, node, v.c_str(), false);
}

// Does this node submit a form when clicked? Returns the control's index, or -1.
// Only controls that really sit inside a <form> count: a bare <button> is
// usually a JS button ("Play"), and treating it as a submit would reload the
// page with a bogus query string.
static int submit_control_at(TabState& tab, int node) {
    int n = node;
    for (int guard = 0; n > 0 && n < tab.dom.size() && guard < 64; guard++) {
        const DomNode& d = tab.dom[n];
        // <button> defaults to type=submit; explicit type=button / type=reset
        // must not submit (a game's "Play" button is usually the former).
        const bool ctl = (d.tag == Tag::Button && !ci_eq(d.ftype, "button") &&
                          !ci_eq(d.ftype, "reset")) ||
                         (d.tag == Tag::Input &&
                          (ci_eq(d.ftype, "submit") || ci_eq(d.ftype, "image")));
        if (ctl) {
            int p = d.parent;
            for (int g2 = 0; p > 0 && p < tab.dom.size() && g2 < 64; g2++) {
                if (tab.dom[p].tag == Tag::Form) return n;
                p = tab.dom[p].parent;
            }
            return -1;
        }
        n = d.parent;
    }
    return -1;
}

// Submit: collect name=value from the fields of the enclosing form (or the
// whole document when the field is not in one) and navigate to the action.
static void submit_form(TabState& tab, int node) {
    int form = node;
    while (form > 0 && tab.dom[form].tag != Tag::Form) form = tab.dom[form].parent;
    if (form <= 0) form = -1;
    char action[512]; action[0] = 0;
    if (form > 0) clip_str(action, sizeof(action), tab.dom[form].action.c_str());
    if (!action[0]) clip_str(action, sizeof(action), tab.url);

    char q[1024]; int qn = 0; q[0] = 0;
    // A form with a non-empty type="file" field submits as multipart/form-data
    // with the file bytes inlined (the field's value is the filesystem path -
    // this browser has no file-picker dialog, so the user types a path).
    bool has_file = false;
    for (int i = 0; i < tab.dom.size(); i++) {
        const DomNode& d = tab.dom[i];
        if (d.tag == Tag::Input && ci_eq(d.ftype, "file") &&
            d.fname.len() > 0 && d.value.len() > 0) has_file = true;
    }
    const bool mp = has_file;   // only POST can carry multipart (meth parsed below)
    char bb[48]; bb[0] = 0;
    String mpbody;
    if (mp) ksprintf(bb, sizeof(bb), "----nefuForm%u", platform_tick_ms());
    for (int i = 0; i < tab.dom.size() && qn < (int)sizeof(q) - 80; i++) {
        const DomNode& d = tab.dom[i];
        // Text fields plus the hidden inputs a real form posts along with them
        // (csrf tokens, ie=UTF-8, ...). Hidden fields keep tag Unknown so they
        // never take layout space, but they are still part of the submission.
        const bool hidden = (d.tag == Tag::Unknown && ci_eq(d.ftype, "hidden"));
        const bool check = (d.tag == Tag::Input &&
                            (ci_eq(d.ftype, "checkbox") || ci_eq(d.ftype, "radio")));
        const bool dsel = (d.tag == Tag::Select);
        if (!field_is_text(d) && !hidden && !check && !dsel) continue;
        if (check && !d.checked) continue;      // unchecked boxes are never sent
        if (d.fname.len() == 0) continue;
        if (form > 0) {
            int p = i;
            while (p > 0 && tab.dom[p].tag != Tag::Form) p = tab.dom[p].parent;
            if (p != form) continue;
        }
        String selval;
        const char* vsrc = d.value.c_str();
        if (dsel) {
            // the chosen <option>: its value="" attribute, else its label
            int k = select_sel_kid(tab.dom, d);
            if (k >= 0) option_value(tab.dom, tab.dom[d.kids[k]], selval);
            vsrc = selval.c_str();
        } else if (check && !d.value.len()) {
            vsrc = "on";                        // browser default for a bare box
        }
        char k[96], v[512];
        url_encode(d.fname.c_str(), k, sizeof(k));
        url_encode(vsrc, v, sizeof(v));
        qn += ksprintf(q + qn, (int)sizeof(q) - qn, "%s%s=%s", qn ? "&" : "", k, v);
        if (mp) {
            // multipart twin of the same field (raw value, no url encoding)
            klogf("mp: field %s", d.fname.c_str());
            mpbody += String("--", 2) + bb + "\r\nContent-Disposition: form-data; name=\"" +
                      d.fname.c_str() + "\"";
            if (d.tag == Tag::Input && ci_eq(d.ftype, "file")) {
                // file part: read the bytes named by the path in the value.
                // Real browsers send the base filename only, never the path.
                const char* base = strrchr(d.value.c_str(), '/');
                const char* base2 = strrchr(d.value.c_str(), '\\');
                if (base2 && base2 > base) base = base2;
                if (base) base++; else base = d.value.c_str();
                mpbody += String("; filename=\"") + base + "\"\r\n"
                           "Content-Type: application/octet-stream\r\n\r\n";
                uint8_t* fb = 0; uint32_t fl = 0;
                if (platform_read_file(d.value.c_str(), &fb, &fl) && fb) {
                    const uint32_t cap = 96u * 1024;
                    if (fl > cap) fl = cap;   // keep the submission bounded
                    mpbody += String((const char*)fb, (int)fl);
                    kfree(fb);
                    klogf("browser: file part %s (%u bytes)", d.value.c_str(), fl);
                } else {
                    klogf("browser: file part read FAILED: %s", d.value.c_str());
                }
                mpbody += String("\r\n", 2);
            } else {
                mpbody += String("\r\n\r\n") + vsrc + "\r\n";
            }
        }
    }
    // The control that submitted the form adds its own name=value, exactly as
    // a browser does for <button name="go" value="1">.
    if (node > 0 && node < tab.dom.size() && qn < (int)sizeof(q) - 80) {
        const DomNode& bd = tab.dom[node];
        const bool sub = (bd.tag == Tag::Button) ||
                         (bd.tag == Tag::Input && (ci_eq(bd.ftype, "submit") ||
                                                   ci_eq(bd.ftype, "image")));
        if (sub && bd.fname.len() > 0 && !ci_eq(bd.ftype, "button") &&
            !ci_eq(bd.ftype, "reset")) {
            char bk[96], bv[512];
            url_encode(bd.fname.c_str(), bk, sizeof(bk));
            url_encode(bd.value.len() ? bd.value.c_str() : "Submit", bv, sizeof(bv));
            qn += ksprintf(q + qn, (int)sizeof(q) - qn, "%s%s=%s", qn ? "&" : "", bk, bv);
        }
    }
    char base[512];
    clip_str(base, sizeof(base), action);
    for (int i = 0; base[i]; i++) if (base[i] == '#' || base[i] == '?') { base[i] = 0; break; }
    // Submission method: the form's method="" unless the button that was
    // pressed overrides it with formmethod="".
    char meth[8];
    clip_str(meth, sizeof(meth), "GET");
    if (form > 0 && tab.dom[form].fmethod[0])
        clip_str(meth, sizeof(meth), tab.dom[form].fmethod);
    if (node > 0 && node < tab.dom.size() && tab.dom[node].fmethod[0])
        clip_str(meth, sizeof(meth), tab.dom[node].fmethod);
    const bool post = ci_eq(meth, "post");
    char full[1600];
    if (post) clip_str(full, sizeof(full), base);        // body carries the fields
    else if (q[0]) ksprintf(full, sizeof(full), "%s?%s", base, q);
    else clip_str(full, sizeof(full), base);
    char u[2048];
    resolve_url(tab.url, full, u, sizeof(u));
    klogf("browser: submit form (%s) -> %s\n", post ? "POST" : "GET", u);
    tab.focus_node = -1;
    if (!u[0]) return;
    // Let the page decide. When the form lives in the scripted DOM, dispatch
    // `submit` there and let the shim start the load - unless a handler called
    // preventDefault(), which is exactly how SPA search boxes do their own
    // fetch instead of a full page load. Static (JS-less) forms navigate here.
    // A file upload cannot travel through the JS dispatch path (the shim body
    // argument is a string; file bytes are binary) - go straight to the native
    // multipart submission.
    if (mp && post) {
        klogf("mp: assembling tail (len=%d)", mpbody.len());
        mpbody += String("--", 2) + bb + "--\r\n";
        klogf("mp: tail done (len=%d)", mpbody.len());
        clip_str(tab.mp_boundary, sizeof(tab.mp_boundary), bb);
        tab.mp_multipart = true;
        klogf("mp: navigating (%d bytes, boundary %s)", mpbody.len(), bb);
        navigate_post(tab, u, mpbody.c_str(), mpbody.len());
        klogf("mp: navigate returned");
        tab.mp_multipart = false;
        return;
    }
    if (tab.js_active && form > 0 && form < tab.dom.size()) {
        const DomNode& fn = tab.dom[form];
        // Room for the escaped URL (<=2KB) plus a form body (<=1.2KB) plus the
        // wrapper; a truncated call would be a JS syntax error, not a partial one.
        char js[4096];
        js[0] = 0;
        char esc[2200]; int en = 0;
        for (const char* p = u; *p && en < (int)sizeof(esc) - 3; p++) {
            if (*p == '"' || *p == '\\') esc[en++] = '\\';
            esc[en++] = *p;
        }
        esc[en] = 0;
        // The body is handed over as a third argument so the shim can replay
        // the POST when nobody cancelled the submit (a JS-less handler path
        // would otherwise degrade the submission to a GET).
        char besc[1200]; besc[0] = 0;
        if (post) {
            int bn = 0;
            for (const char* p = q; *p && bn < (int)sizeof(besc) - 3; p++) {
                if (*p == '"' || *p == '\\') besc[bn++] = '\\';
                besc[bn++] = *p;
            }
            besc[bn] = 0;
        }
        const char* body_arg = post ? besc : "null";
        if (fn.nfu_idx >= 0) {
            ksprintf(js, sizeof(js), "try{__nfuSubmit(%d,\"%s\",%s%s%s);}catch(e){}",
                     fn.nfu_idx, esc, post ? "\"" : "", body_arg, post ? "\"" : "");
            klogf("browser: submit dispatched to page (form=%d)", fn.nfu_idx);
        } else if (fn.id[0]) {
            // server-rendered form: address it by id (no data-nfu index)
            char sid[40]; int sn = 0;
            for (const char* p = fn.id; *p && sn < (int)sizeof(sid) - 1; p++) {
                const char c = *p;
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == '.' || c == ':';
                if (!ok) break;
                sid[sn++] = c;
            }
            sid[sn] = 0;
            if (sid[0]) {
                ksprintf(js, sizeof(js), "try{__nfuSubmitById('%s',\"%s\",%s%s%s);}catch(e){}",
                         sid, esc, post ? "\"" : "", body_arg, post ? "\"" : "");
                klogf("browser: submit dispatched to page (form id=%s)", sid);
            }
        }
        if (js[0]) { post_js_script(tab, js, true); return; }
    }
    if (post) navigate_post(tab, u, q, qn);
    else navigate_url(tab, u, true);
}

// True when the focused field consumed the key.
static bool field_key(TabState& tab, const KeyEvent* e) {    int node = tab.focus_node;
    if (node < 0 || node >= tab.dom.size()) { tab.focus_node = -1; return false; }
    DomNode& n = tab.dom[node];
    if (!field_is_text(n)) { tab.focus_node = -1; return false; }
    if (e->keycode == KEY_ESC) {
        tab.focus_node = -1;
        post_js_focus(tab, n.nfu_idx, false);
        tab.layout_w = -1;
        return true;
    }
    if (e->keycode == KEY_ENTER) {
        if (n.tag == Tag::Textarea) { field_set(tab, node, "\n", true); return true; }
        submit_form(tab, node);
        return true;
    }
    if (e->keycode == KEY_BACKSPACE) { field_backspace(tab, node); return true; }
    if (e->keycode == KEY_TAB) {
        // move to the next text field in document order
        for (int k = 1; k <= tab.dom.size(); k++) {
            int j = (node + k) % tab.dom.size();
            const DomNode& d = tab.dom[j];
            if (field_is_text(d)) {
                post_js_focus(tab, n.nfu_idx, false);
                tab.focus_node = j;
                post_js_focus(tab, d.nfu_idx, true);
                tab.layout_w = -1;
                return true;
            }
        }
        return true;
    }
    if (e->utf8[0]) { field_set(tab, node, e->utf8, true); return true; }
    unsigned char a = (unsigned char)e->ascii;
    if (a >= 32 && a < 127) { char ch[2]; ch[0] = (char)a; ch[1] = 0; field_set(tab, node, ch, true); return true; }
    return false;
}

static void on_key(Window* w, const KeyEvent* e) {
    if (!e) return;
    BrowserState* st = (BrowserState*)w->userdata;
    if (!st) return;
    TabState& tab = st->tabs[st->active];
    // The page owns the keyboard unless the address bar has focus.
    bool page_keys = tab.js_active && !st->input_focus;
    char kname[16];
    key_name(e, kname, sizeof(kname));
    int mods = (int)e->mods;

    // A focused text field owns the keyboard - on static pages just as much as
    // on scripted ones, so this must not depend on page_keys.
    const bool typing = (tab.focus_node >= 0 && !st->input_focus);
    if (typing && e->down) {
        if (field_key(tab, e)) return;
    }

    if (st->input_focus) {
        if (!e->down) return;
        // ---- address bar editing ----
        if (e->keycode == KEY_ENTER) {
            st->input_focus = false;
            navigate_input(tab);
            return;
        }
        if (e->keycode == KEY_ESC) {
            // ESC leaves the address bar and hands the keyboard back to the
            // page (every browser does this). The window manager no longer
            // closes the browser on ESC (esc_close = false), so this is the
            // only way back to the page from the URL bar.
            st->input_focus = false;
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

    if (!e->down) {
        if (page_keys) post_js_event(tab, "keyup", -1, 0, 0, kname, mods);
        return;
    }

    // ---- page shortcuts ----
    int vh = w->content_h - STAT_H - BAR_H - 2;

    // While the page runs scripts, movement / editing keys belong to it: a game
    // loop reads WASD + arrows + space, an SPA reads Enter/Backspace/Tab/Esc.
    if (page_keys) {
        bool pagekey = (e->keycode == KEY_UP || e->keycode == KEY_DOWN ||
                        e->keycode == KEY_LEFT || e->keycode == KEY_RIGHT ||
                        e->keycode == KEY_SPACE || e->keycode == KEY_ENTER ||
                        e->keycode == KEY_BACKSPACE || e->keycode == KEY_TAB ||
                        e->keycode == KEY_ESC || e->keycode == KEY_DEL);
        if (pagekey) {
            post_js_event(tab, "keydown", -1, 0, 0, kname, mods);
            return;
        }
    }

    if (e->keycode == KEY_UP) { tab.scroll -= 24; if (tab.scroll < 0) tab.scroll = 0; return; }
    if (e->keycode == KEY_DOWN) { tab.scroll += 24; if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll; return; }
    if (e->keycode == KEY_PGUP) { tab.scroll -= vh; if (tab.scroll < 0) tab.scroll = 0; return; }
    if (e->keycode == KEY_PGDN) { tab.scroll += vh; if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll; return; }
    if (e->keycode == KEY_HOME) { tab.scroll = 0; return; }
    if (e->keycode == KEY_END) { tab.scroll = tab.max_scroll; return; }
    if (e->keycode == KEY_F5) { reload(tab); return; }

    // Ctrl+letter shortcuts; the bare letter also works while no text field is
    // focused (the platform used to deliver Ctrl+letter as a plain letter).
    const bool sc = (mods & 0x02) || !typing;
    if (sc && e->ascii == 'l') { st->input_focus = true; tab.input_cursor = tab.input_len; tab.focus_node = -1; return; }
    if (sc && e->ascii == 't') { new_tab(st); return; }
    if (sc && e->ascii == 'w') { close_tab(st); return; }
    if (sc && e->ascii == 'r') { reload(tab); return; }
    if (sc && e->ascii == 'h') { go_home(st, tab); return; }
    if (sc && e->ascii == 'e') { // cycle the active keyword-search engine
        tab.search_engine = (tab.search_engine + 1) % kSearchEngineCount;
        return;
    }

    // printable character not claimed by a browser shortcut: deliver it to the
    // page so text fields and hotkey handlers in SPAs keep working
    if (page_keys && kname[0]) post_js_event(tab, "keydown", -1, 0, 0, kname, mods);
}

static void on_mouse(Window* w, int mx, int my, uint8_t buttons) {
    BrowserState* st = (BrowserState*)w->userdata;
    if (!st) return;
    TabState& tab = st->tabs[st->active];
    int cw = w->content_w;

    // ---- scrollbar: a thumb drag owns the pointer until the button is up ---
    // The browser paints its own gutter (Window::own_scrollbar), so this is
    // both the hit test and the drag state machine: press on the thumb grabs
    // it, press on the track pages once and then keeps following the mouse
    // (desktop-browser behaviour), move maps mouse Y back to a page offset,
    // release drops the grab. Page JS never sees these presses.
    {
        const int vy0 = BAR_H + 2;
        const int vh0 = w->content_h - STAT_H - vy0 - 2;
        const int sbx = cw - SB_W - 2;
        const bool in_sb = (mx >= sbx && mx < cw && my >= vy0 && my < vy0 + vh0);

        if (tab.sb_drag) {
            if (!(buttons & 1)) {
                tab.sb_drag = 0;                 // released: drop the grab
            } else if (tab.max_scroll > 0 && vh0 > 0) {
                const int content_h = vh0 + tab.max_scroll;
                int thumb_h, thumb_y;
                sb_geom(vh0, tab.scroll, content_h, thumb_h, thumb_y);
                const int track = vh0 - thumb_h;
                int ns = 0;
                if (track > 0)
                    ns = (my - vy0 - tab.sb_off) * tab.max_scroll / track;
                if (ns < 0) ns = 0;
                if (ns > tab.max_scroll) ns = tab.max_scroll;
                tab.scroll = ns;
                w->scroll_y = ns;
            }
            tab.js_btn = 0;
            return;
        }

        if ((buttons & 1) && in_sb) {
            if (tab.max_scroll > 0) {
                const int content_h = vh0 + tab.max_scroll;
                int thumb_h, thumb_y;
                sb_geom(vh0, tab.scroll, content_h, thumb_h, thumb_y);
                if (my >= vy0 + thumb_y && my < vy0 + thumb_y + thumb_h) {
                    tab.sb_drag = 1;             // grabbed the thumb
                    tab.sb_off = my - (vy0 + thumb_y);
                } else {
                    // track click: page towards the click, then keep dragging
                    if (my < vy0 + thumb_y) tab.scroll -= vh0;
                    else tab.scroll += vh0;
                    if (tab.scroll < 0) tab.scroll = 0;
                    if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll;
                    w->scroll_y = tab.scroll;
                    sb_geom(vh0, tab.scroll, content_h, thumb_h, thumb_y);
                    tab.sb_drag = 1;
                    tab.sb_off = thumb_h / 2;
                }
            }
            tab.js_btn = 0;
            return;
        }
    }

    // ---- pointer events for the page --------------------------------------
    // Games read mousedown/mousemove/mouseup to aim and click inside the 3D
    // view; SPAs read them for hover and drag. Moves are throttled so a mere
    // cursor pass does not flood the JS worker with script evaluations.
    {
        const int vy0 = BAR_H + 2;
        bool in_view = (my >= vy0 && my < w->content_h - STAT_H - 2 &&
                        mx >= 2 && mx < cw - SB_W - 4);
        int px = mx - 2;
        int py = my - vy0 + tab.scroll;
        int nfu = -1;
        if (in_view) {
            for (int i = tab.layout.runs.size() - 1; i >= 0; i--) {
                const Run& r = tab.layout.runs[i];
                if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
                    if (r.node >= 0 && r.node < tab.dom.size()) nfu = tab.dom[r.node].nfu_idx;
                    break;
                }
            }
        }
        bool was_down = (tab.js_btn & 1) != 0;
        bool is_down = (buttons & 1) != 0;
        if (tab.js_active && in_view && is_down && !was_down) {
            tab.js_mouse_nfu = nfu;
            tab.js_px = px;
            tab.js_py = py;
            post_js_event(tab, "mousedown", nfu, px, py, 0, 0);
        } else if (tab.js_active && in_view && is_down && was_down) {
            uint32_t now = platform_tick_ms();
            if (now - tab.js_move_ms >= 16) {
                tab.js_move_ms = now;
                post_js_mouse(tab, "mousemove", nfu, px, py, px - tab.js_px, py - tab.js_py);
                tab.js_px = px;
                tab.js_py = py;
            }
        } else if (!is_down && was_down && tab.js_active) {
            post_js_event(tab, "mouseup", tab.js_mouse_nfu, px, py, 0, 0);
        }
        tab.js_btn = buttons;
    }

    if (!(buttons & 1)) return;

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

    // (the scrollbar gutter is handled by the drag state machine at the top of
    // this handler; presses there never reach the page)

    st->input_focus = false;

    // run an inline onclick handler: mini JS executes, DOM commands from the
    // output stream mutate the DOM, then the layout is invalidated
    auto exec_onclick = [&](const char* script) {
        if (!script || !script[0]) return;
        char out[JS_OUT_MAX];
        out[0] = 0;
        nefu::mini_js_set_env(tab.url, tab.title, 0, 0);
        mini_js_run(script, out, (int)sizeof(out), 0, 0);
        if (out[0]) {
            apply_js_commands(tab, out);
            char vis[JS_OUT_MAX];
            int vn = 0;
            for (const char* c = out; *c && vn < JS_OUT_MAX - 1; ) {
                if (*c == '\x01') { while (*c && *c != '\n') c++; if (*c) c++; continue; }
                vis[vn++] = *c++;
            }
            vis[vn] = 0;
            if (vis[0]) klogf("browser: js: %s", vis);
        }
        tab.layout_w = -1;   // re-layout on next paint
    };

    // hit-testing (reverse order: topmost run wins); onclick wins over href
    int px = mx - vx;
    int py = my - vy + tab.scroll;
    // An open <select> dropdown swallows the click first: picking an option
    // selects it and closes, clicking anywhere else just closes the popup.
    if (tab.sel_popup_node >= 0) {
        int qx, qy, qw, qn;
        if (select_popup_rect(tab, qx, qy, qw, qn)) {
            int ph = qn * 24;
            const int view_bottom = tab.scroll + vh;
            if (qy + ph > view_bottom) ph = view_bottom - qy;   // clamp to viewport
            if (py >= qy && py < qy + ph && px >= qx && px < qx + qw) {
                const int idx = (py - qy) / 24;
                if (idx >= 0 && idx < qn) select_pick(tab, idx);
                tab.layout_w = -1;
            }
        }
        tab.sel_popup_node = -1;
        return;
    }
    // Any click first drops the text-field focus (a click into the field below
    // takes it right back); this mirrors what a browser does on blur.
    if (tab.focus_node >= 0 && tab.focus_node < tab.dom.size()) {
        post_js_focus(tab, tab.dom[tab.focus_node].nfu_idx, false);
        tab.focus_node = -1;
        tab.layout_w = -1;
    }
    klogf("browser: click at screen=(%d,%d) page=(%d,%d) scroll=%d runs=%d",
          mx, my, px, py, tab.scroll, (int)tab.layout.runs.size());
    // locate the Agree button (privacy popup) among the runs: logs its rect
    for (int i = 0; i < (int)tab.layout.runs.size(); i++) {
        const Run& r = tab.layout.runs[i];
        if (r.node >= 0 && r.node < tab.dom.size() &&
            tab.dom[r.node].text == "Agree")
            klogf("browser: Agree run rect page=(%d,%d) size=(%d,%d) screen_y=%d",
                  r.x, r.y, r.w, r.h, r.y + vy - tab.scroll);
    }
    for (int i = tab.layout.runs.size() - 1; i >= 0; i--) {
        const Run& r = tab.layout.runs[i];
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
            klogf("browser: click hit run node=%d nfu=%d link=%d href=%.40s text=%.30s",
                  r.node, (r.node >= 0 && r.node < tab.dom.size()) ? tab.dom[r.node].nfu_idx : -2,
                  r.link ? 1 : 0, r.link ? r.href.c_str() : "",
                  (r.node >= 0 && r.node < tab.dom.size()) ? tab.dom[r.node].text.c_str() : "");
            if (r.node >= 0 && r.node < tab.dom.size()) {
                DomNode& nd = tab.dom[r.node];
                // Checkbox / radio / select: the control handles the click
                // itself (and tells the page through a change event).
                if (nd.tag == Tag::Input &&
                    (ci_eq(nd.ftype, "checkbox") || ci_eq(nd.ftype, "radio"))) {
                    toggle_check(tab, r.node);
                    return;
                }
                if (nd.tag == Tag::Select) {
                    // open / close the dropdown overlay (select_cycle remains
                    // for keyboard-driven use)
                    tab.sel_popup_node = (tab.sel_popup_node == r.node) ? -1 : r.node;
                    return;
                }
                // Clicking a <label> is a click on the control it wraps.
                {
                    int lc = label_control(tab, r.node);
                    if (lc >= 0) {
                        const DomNode& lcd = tab.dom[lc];
                        if (ci_eq(lcd.ftype, "checkbox") || ci_eq(lcd.ftype, "radio")) {
                            toggle_check(tab, lc);
                            return;
                        }
                        tab.focus_node = lc;
                        post_js_focus(tab, lcd.nfu_idx, true);
                        tab.layout_w = -1;
                        klogf("browser: focus field via label node=%d", lc);
                        return;
                    }
                }
                if (field_is_text(nd)) {
                    // Focus the field and own the keyboard from here on.
                    tab.focus_node = r.node;
                    post_js_focus(tab, nd.nfu_idx, true);
                    tab.layout_w = -1;
                    klogf("browser: focus field node=%d name=%.20s value=%.30s",
                          r.node, nd.fname.c_str(), nd.value.c_str());
                    return;
                }
                // A submit button inside a form: the click's default action is
                // the form submission (the run may be the button's label, so the
                // lookup walks up the parent chain). A scripted button keeps
                // priority - its own handler decides what happens.
                {
                    int sctl = submit_control_at(tab, r.node);
                    if (sctl > 0) {
                        const DomNode& sc = tab.dom[sctl];
                        if (sc.onclick[0]) { exec_onclick(sc.onclick); return; }
                        if (sc.nfu_idx >= 0) {
                            tab.pending_nfu = sc.nfu_idx;
                            tab.has_pending_nfu = true;
                            return;
                        }
                        klogf("browser: click submit control node=%d (%.16s)",
                              sctl, sc.tag_name);
                        submit_form(tab, sctl);
                        return;
                    }
                }
                const char* oc = nd.onclick;
                if (oc[0]) { exec_onclick(oc); return; }
                if (nd.nfu_idx >= 0) {
                    // JS-built element: dispatch its click listeners via the
                    // DOM shim, which re-serializes -> re-render next frame
                    tab.pending_nfu = nd.nfu_idx;
                    tab.has_pending_nfu = true;
                    return;
                }
                // Server-rendered controls carry no data-nfu index, so a page
                // that attached its listener with addEventListener() would
                // never hear the click. Buttons and inputs are addressed by id
                // (links are excluded on purpose: their default action is the
                // navigation, which the host already performs below). The hit
                // run is usually the button's label, so walk up to the control.
                if (tab.js_active) {
                    int ctl = r.node;
                    for (int up = 0; ctl > 0 && ctl < tab.dom.size() && up < 8; up++) {
                        const DomNode& cd = tab.dom[ctl];
                        if (cd.id[0] && (cd.tag == Tag::Button || cd.tag == Tag::Input)) {
                            // Sanitize: the id ends up inside a JS string literal,
                            // so a quote in it must not be able to break out.
                            char sid[40]; int sn = 0;
                            for (const char* p = cd.id; *p && sn < (int)sizeof(sid) - 1; p++) {
                                const char c = *p;
                                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                                c == '.' || c == ':';
                                if (!ok) break;
                                sid[sn++] = c;
                            }
                            sid[sn] = 0;
                            if (sid[0]) {
                                char js[256];
                                ksprintf(js, sizeof(js),
                                         "try{__nfuClickById('%s',%d,%d);}catch(e){}",
                                         sid, px, py);
                                klogf("browser: click -> page listener by id=%s", sid);
                                post_js_script(tab, js, true);
                                return;
                            }
                        }
                        ctl = cd.parent;
                    }
                }
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
            char u[2048];
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
    // app_scroll window: the WM scrollbar mirrors our offset (thumb dragging
    // arrives as delta==0 with w->scroll_y already set)
    if (delta == 0) tab.scroll = w->scroll_y;
    if (tab.scroll < 0) tab.scroll = 0;
    if (tab.scroll > tab.max_scroll) tab.scroll = tab.max_scroll;
    w->scroll_y = tab.scroll;
}

static void on_close(Window* w) {
    BrowserState* st = (BrowserState*)w->userdata;
    if (st) {
        delete st;   // TabState destructors free pages / DOM / history / jobs
        w->userdata = 0;
    }
}

} // namespace

// Called directly by the JS engine while a script is running, so the WGL
// context exists before any direct bridge calls (texture upload) happen.
// Idempotent: the later "ctx" command in the command stream re-uses it.
#ifndef NEFU_BARE
void gl_ctx_create(const char* cid, int w, int h) {
    if (!cid || !cid[0]) return;
    gl_load_wgl();
    GlCanvas* c = gl_find_or_create(cid, 0, 0);
    if (!c) return;
    if (w > 0 && h > 0 && (c->w != w || c->h != h)) { c->w = w; c->h = h; }
    g_gl.wglMakeCurrent(c->hdc, c->rc);
    if (!g_gl.ready) gl_load_ext();
}

// Direct texture upload (bypasses the text protocol): the JS engine already
// holds the pixel bytes in memory (Uint8Array / ArrayBuffer from an image or a
// procedural texture), so it hands them straight to the GL context instead of
// base64-encoding them into the command stream. WebGL games (bloxd.io etc.)
// upload every texture through this path. Defined at nefu scope so the JS
// bridge (minijs.cpp) can link to it across translation units.
void gl_tex_upload(const char* cid, int target, int level, int internal,
                   int w, int h, int format, int type,
                   const uint8_t* px, int len) {
    (void)target; (void)level; (void)internal; (void)w; (void)h;
    (void)format; (void)type;
    if (!px || len <= 0) return;
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (s_texp[i].px && s_texp[i].cid[0] && ci_eq(s_texp[i].cid, cid)) { slot = i; break; }
        if (slot < 0 && !s_texp[i].px) slot = i;
    }
    if (slot < 0) return;
    if (s_texp[slot].px) bfree(s_texp[slot].px);
    s_texp[slot].px = (uint8_t*)balloc(len);
    if (!s_texp[slot].px) { s_texp[slot].len = 0; return; }
    memcpy(s_texp[slot].px, px, len);
    s_texp[slot].len = len;
    clip_str(s_texp[slot].cid, sizeof(s_texp[slot].cid), cid ? cid : "");
}
#else
void gl_ctx_create(const char* cid, int w, int h) {
    (void)cid; (void)w; (void)h;
}
void gl_tex_upload(const char* cid, int target, int level, int internal,
                   int w, int h, int format, int type,
                   const uint8_t* px, int len) {
    // Bare metal: the software rasterizer needs the bytes, so keep them in a
    // worker-side slot. swgl_tex_image() applies them when the "texim" command
    // that carries the size/format is dispatched on the UI thread.
    (void)target; (void)level; (void)internal; (void)w; (void)h;
    (void)format; (void)type;
    if (!cid || !cid[0] || !px || len <= 0) return;
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (s_swtexp[i].px && ci_eq(s_swtexp[i].cid, cid)) { slot = i; break; }
        if (slot < 0 && !s_swtexp[i].px) slot = i;
    }
    if (slot < 0) slot = 0;           // all slots busy: recycle the first
    if (s_swtexp[slot].px) bfree(s_swtexp[slot].px);
    s_swtexp[slot].px = (uint8_t*)balloc((size_t)len);
    if (!s_swtexp[slot].px) { s_swtexp[slot].len = 0; return; }
    memcpy(s_swtexp[slot].px, px, (size_t)len);
    s_swtexp[slot].len = len;
    clip_str(s_swtexp[slot].cid, sizeof(s_swtexp[slot].cid), cid);
}
#endif

// ===================== entry points =====================

void browser_debug_nfu_click(int nfu_idx) {
    s_debug_nfu_click = nfu_idx;
}

// Queue one page action. The action string is one of:
//   "key:Escape"  press a key
//   "text:Play"   click the element showing that text
//   "class:Foo"   click the element whose class contains "Foo"
// A bare string is treated as visible text.
//
// The queue is executed by the page itself (see __nfuActRun), which retries
// an action until it hits - a dialog may only render once the dialog in front
// of it was dismissed.
void browser_page_action(const char* action) {
    if (!action || !action[0]) return;
    if (s_action_count >= ACT_MAX) {
        klogf("browser: action queue full, dropping '%s'\n", action);
        return;
    }
    clip_str(s_actions[s_action_count], sizeof(s_actions[0]), action);
    s_action_count++;
    s_action_at = platform_tick_ms();
    klogf("browser: page action queued '%s' (%d total)\n", action, s_action_count);
}

// Ask the page to click one or more elements by their visible text
// ("Agree", "Play", ...). A comma separated list is pressed in order, with a
// pause in between so that React can render the next button.
void browser_click_text(const char* text) {
    if (!text || !text[0]) return;
    // A fresh request replaces the queue.
    s_action_count = 0;
    s_action_next = 0;
    s_action_at = platform_tick_ms();
    const char* p = text;
    while (*p && s_action_count < ACT_MAX) {
        const char* comma = strchr(p, ',');
        size_t n = comma ? (size_t)(comma - p) : strlen(p);
        if (n > 0 && n < sizeof(s_actions[0])) {
            char one[72];
            if (n >= sizeof(one)) n = sizeof(one) - 1;
            memcpy(one, p, n);
            one[n] = 0;
            // A bare label is a visible-text action.
            if (strncmp(one, "key:", 4) != 0 && strncmp(one, "text:", 5) != 0 &&
                strncmp(one, "class:", 6) != 0) {
                char tagged[80];
                ksprintf(tagged, sizeof(tagged), "text:%s", one);
                clip_str(s_actions[s_action_count], sizeof(s_actions[0]), tagged);
            } else {
                clip_str(s_actions[s_action_count], sizeof(s_actions[0]), one);
            }
            s_action_count++;
        }
        if (!comma) break;
        p = comma + 1;
    }
    klogf("browser: click text requested '%s' (%d label(s))\n", text, s_action_count);
}

// Send a key press straight to the page. The page only receives keys while it
// owns the keyboard, so this deliberately bypasses the address bar: a dialog
// that listens on document.documentElement can only be closed this way.
void browser_send_key(const char* key) {
    if (!key || !key[0]) return;
    char act[72];
    ksprintf(act, sizeof(act), "key:%s", key);
    browser_page_action(act);
}

// Type a string into the focused text field (scripted driver: the terminal's
// "open <url> class:... type:hello key:Enter" sequence). Feeding starts as soon
// as a field is focused and runs one character per ~90 ms, so the page and the
// re-render can keep up.
void browser_send_text(const char* text) {
    if (!text) return;
    s_type_len = 0;
    for (const char* p = text; *p && s_type_len < (int)sizeof(s_type_buf) - 1; p++)
        s_type_buf[s_type_len++] = *p;
    s_type_buf[s_type_len] = 0;
    s_type_enter = 0;
    s_type_at = platform_tick_ms() - 1000;   // first character right away
    klogf("browser: typing '%s' (%d chars)\n", s_type_buf, s_type_len);
}

// Press Enter in the page's focused field once the pending text is typed -
// that is what actually submits the form (the JS-side key event cannot).
void browser_send_enter() {
    s_type_enter = 1;
    s_type_at = platform_tick_ms() - 1000;
    klogf("browser: submit requested (pending text %d chars)\n", s_type_len);
}

// forward declaration for the shared launcher
static void browser_launch_ex(const char* url, bool fullscreen);

void browser_launch_url(const char* url) {
    browser_launch_ex(url, false);
}

// Open the browser full screen. A game site wants the whole screen, and on
// the bare kernel the page must not be covered by another window.
void browser_launch_fullscreen(const char* url) {
    browser_launch_ex(url, true);
}

static void browser_launch_ex(const char* url, bool fullscreen) {
    BrowserState* st = new BrowserState();
    st->tab_count = 1;
    st->tabs[0].used = true;
    st->input_focus = true;
    go_home(st, st->tabs[0]);

    if (url && *url) {
        char u[2048];
        smart_url(url, u, sizeof(u), st->tabs[0].search_engine);
        if (u[0]) navigate_url(st->tabs[0], u, false);
    }

    Window* w = 0;
    if (fullscreen) {
        Screen* sc = platform_screen();
        int sw = sc ? sc->width : 1024;
        int sh = sc ? sc->height : 768;
        w = g_wm->create_window(T("nefuOS 浏览器", "nefuOS Browser"), 0, 0, sw, sh, true);
    }
    if (!w) w = g_wm->create_window(T("nefuOS 浏览器", "nefuOS Browser"), 20, 20, 760, 560);
    if (!w) { delete st; return; }

    w->userdata = st;
    w->on_paint = on_paint;
    w->on_key = on_key;
    w->on_mouse = on_mouse;
    w->on_scroll = on_scroll;
    w->on_close = on_close;
    w->app_scroll = true;   // browser draws its own scrolled viewport; WM scrollbar mirrors it
    // ESC belongs to the page (a dialog closes itself with it), so the window
    // manager must not also close the browser on ESC.
    w->esc_close = false;

    g_wm->raise(w);
}

void browser_launch() {
    browser_launch_url(0);
}

} // namespace nefu

