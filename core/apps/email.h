// nefuOS Email Client - Professional Mail Application
// ---------------------------------------------------------------------------
// Views : list / read / compose / account
// Folders: Inbox, Sent, Drafts, Starred (virtual), Trash
// Features: compose (To/Cc/Subject/body + drafts), reply, forward, delete &
//           restore, star, live search, read/unread, SMTP send + POP3 receive
//           on the host build (winsock, real network), and persistence of
//           mailboxes + account to /home/user/Mail via the VFS.
// Build model: email.h is compiled inside apps.cpp for BOTH the bare kernel
// and the Win32 host build. The winsock SMTP/POP3 path is compiled only on
// the host (no TLS; suitable for local/dev mail servers); on the bare kernel
// compose/send still work and mail is stored locally.
#pragma once
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../net/net.h"

#if defined(_WIN32) && !defined(NEFU_BARE)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace nefu { namespace apps {

// ============================== data model ==============================

enum { EF_INBOX = 0, EF_SENT, EF_DRAFTS, EF_STARRED, EF_TRASH, EF_COUNT };
enum { EV_LIST = 0, EV_READ, EV_COMPOSE, EV_ACCOUNT };

enum {
    SW = 150,       // sidebar width
    TOOL_H = 32,    // toolbar height
    STATUS_H = 20,  // status bar height
    ROW_H = 30,     // message list row height
    MAX_MSGS = 48,  // messages per folder
    MAX_ATT = 4     // attachments per message / per compose
};

// one attachment: metadata only; decoded bytes live in the VFS
struct EmailAttachment {
    char name[64];   // file name
    char path[96];   // VFS path of the data (received) or source (compose)
    char type[32];   // MIME content type
    int  size;       // decoded byte size
};

struct EmailMessage {
    char from[128];
    char to[128];
    char cc[128];
    char bcc[128];
    char subject[256];
    char body[4096];
    char date[32];      // normalized "YYYY-MM-DD HH:MM" (sortable)
    char msgid[96];     // Message-ID used for deduplication
    EmailAttachment att[MAX_ATT];
    int att_cnt;
    bool read;
    bool starred;
    bool has_html;      // body was produced by stripping text/html
};

struct EmailFolder {
    char name[24];
    EmailMessage messages[MAX_MSGS];
    int msg_count;
    int unread_count;
};

struct EmailAccount {
    char display[48];
    char email[64];
    char smtp_host[64];
    int  smtp_port;
    char pop_host[64];
    int  pop_port;
    char username[64];
    char password[64];
    char signature[160];    // auto-appended to new compose messages
    bool configured;
};

struct EmailState {
    EmailFolder folders[EF_COUNT];
    EmailAccount acct;
    int view;
    int cur_folder;
    int cur_msg;
    int list_scroll;
    int read_scroll;
    // virtual "Starred" folder mapping (source folder + message index)
    int star_src[MAX_MSGS];
    int star_idx[MAX_MSGS];
    int star_cnt;
    int star_unread;
    // sort mode: 0 = date desc, 1 = date asc, 2 = sender, 3 = subject
    int sort_mode;
    // visible-order index for the current folder (search filter + sort)
    int view_ord[MAX_MSGS];
    int view_cnt;
    // dirty-check signature: view is rebuilt only when any of these change
    int view_sig_folder;
    int view_sig_sort;
    int view_sig_msgs;
    int view_sig_star;
    char view_sig_search[64];
    // compose state
    int  comp_field;            // 0=To 1=Cc 2=Bcc 3=Subject 4=Body
    char comp_to[256];
    char comp_cc[256];
    char comp_bcc[256];
    char comp_subject[256];
    char comp_body[4096];
    int  comp_cursor[5];
    int  comp_body_scroll;
    int  comp_draft_slot;       // -1 = new message, else draft index being edited
    // compose attachments (paths picked from the VFS)
    char comp_att[MAX_ATT][96];
    int  comp_att_cnt;
    int  comp_att_sel;          // selected attachment row (-1 = none)
    // attachment file picker (modal overlay)
    bool picker_open;
    char picker_dir[128];       // directory being browsed
    char picker_ents[64][96];   // entry names
    bool picker_isdir[64];
    int  picker_cnt;
    int  picker_scroll;
    int  picker_sel;
    // search
    char search[64];
    bool search_focus;
    // account editor
    int  acc_field;
    char acc_buf[9][128];       // 0..7 network fields, 8 = signature
    // read view
    bool view_source;           // show raw header/source instead of body
    // status
    char status[128];
    bool net_up;
    bool dirty;

    EmailState() {
        view = EV_LIST;
        cur_folder = EF_INBOX;
        cur_msg = 0;
        list_scroll = 0;
        read_scroll = 0;
        sort_mode = 0;
        view_cnt = 0;
        view_sig_folder = -1;   // force the first ensure_view() to rebuild
        comp_field = 0;
        comp_draft_slot = -1;
        comp_body_scroll = 0;
        comp_cursor[0] = comp_cursor[1] = comp_cursor[2] = comp_cursor[3] = comp_cursor[4] = 0;
        comp_att_cnt = 0;
        comp_att_sel = -1;
        search[0] = 0;
        search_focus = false;
        acc_field = 0;
        status[0] = 0;
        net_up = false;
        dirty = false;
        view_source = false;
        picker_open = false;
        picker_dir[0] = 0;
        picker_cnt = 0;
        picker_scroll = 0;
        picker_sel = 0;
        memset(&acct, 0, sizeof(acct));
        acct.smtp_port = 587;
        acct.pop_port = 110;
        for (int i = 0; i < EF_COUNT; i++) {
            folders[i].msg_count = 0;
            folders[i].unread_count = 0;
            memset(folders[i].messages, 0, sizeof(folders[i].messages));
        }
        const char* names[EF_COUNT] = { "Inbox", "Sent", "Drafts", "Starred", "Trash" };
        for (int i = 0; i < EF_COUNT; i++) strcpy(folders[i].name, names[i]);
        for (int i = 0; i < 9; i++) acc_buf[i][0] = 0;
    }
};

// ============================== text helpers ==============================

// bytes of the UTF-8 codepoint starting at s[0] (1 for ASCII / invalid)
static int mail_step(const char* s) {
    uint8_t c = (uint8_t)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// rendered width of one codepoint: 8px ASCII, 16px CJK (matches gfx 16x16 font)
static int mail_cw(const char* s) { return ((uint8_t)s[0] < 0x80) ? 8 : 16; }

static int mail_prefix_px(const char* t, int start, int end) {
    int w = 0;
    for (int i = start; i < end;) {
        int stp = mail_step(t + i);
        w += mail_cw(t + i);
        i += stp;
    }
    return w;
}

// byte offset of the char whose midpoint is nearest xpx, within [start,end)
static int mail_char_at(const char* t, int start, int end, int xpx) {
    int w = 0, i = start;
    while (i < end) {
        int stp = mail_step(t + i);
        int cw = mail_cw(t + i);
        if (w + cw / 2 > xpx) break;
        w += cw;
        i += stp;
    }
    return i;
}

// visual line-wrap: fills starts[] with the byte offset of each line start,
// returns the number of lines (line height is 16px everywhere).
static int mail_wrap(const char* text, int maxw, int* starts, int max_lines) {
    if (!text || !text[0]) { starts[0] = 0; return 1; }
    int tlen = (int)strlen(text);
    int lines = 1, pos = 0;
    starts[0] = 0;
    while (pos < tlen && lines < max_lines) {
        int w = 0, j = pos, last_space = -1;
        while (j < tlen && text[j] != '\n') {
            int stp = mail_step(text + j);
            int cw = mail_cw(text + j);
            if (w + cw > maxw) break;
            w += cw;
            if (text[j] == ' ' && j > pos) last_space = j;
            j += stp;
        }
        if (j < tlen && text[j] == '\n') {              // hard break
            if (j + 1 >= tlen) break;                    // trailing newline: no empty line
            starts[lines++] = j + 1;
            pos = j + 1;
        } else if (j == pos) {                          // single char wider than line
            int stp = mail_step(text + pos);
            if (pos + stp >= tlen) break;
            starts[lines++] = pos + stp;
            pos += stp;
        } else if (last_space > pos && j > pos) {       // wrap at last space
            if (last_space + 1 >= tlen) break;
            starts[lines++] = last_space + 1;
            pos = last_space + 1;
        } else {                                        // hard wrap mid-word
            if (j >= tlen) break;                        // reached the end
            starts[lines++] = j;
            pos = j;
        }
    }
    return lines;
}

// draw text truncated to maxw px (single line)
static void mail_text_clip(Surface& s, int x, int y, const char* t, int maxw, uint32_t fg, uint32_t bg) {
    int w = 0, i = 0;
    while (t[i]) {
        int stp = mail_step(t + i);
        int cw = mail_cw(t + i);
        if (w + cw > maxw) break;
        w += cw;
        i += stp;
    }
    if (i > 0) {
        char tmp[256];
        if (i > 255) i = 255;
        memcpy(tmp, t, (size_t)i);
        tmp[i] = 0;
        gfx::text(s, x, y, tmp, fg, bg);
    }
}

// draw a bounded substring [start,end) at a single line position
static void mail_text_seg(Surface& s, int x, int y, const char* t, int start, int end, uint32_t fg, uint32_t bg) {
    if (end <= start) return;
    char tmp[300];
    int n = end - start;
    if (n > 299) n = 299;
    memcpy(tmp, t + start, (size_t)n);
    tmp[n] = 0;
    gfx::text(s, x, y, tmp, fg, bg);
}

// single-line text input with a visible cursor and horizontal auto-scroll
static void mail_draw_input(Surface& s, int x, int y, int w, int h,
                            const char* text, int cursor, bool focus, bool mask) {
    const char* disp = mask ? "********" : text;
    int len = (int)strlen(disp);
    int vieww = w - 12;
    int start = 0;
    if (focus) {
        int curpx = mail_prefix_px(disp, 0, cursor > len ? len : cursor);
        if (curpx > vieww) {                       // shift so the cursor stays visible
            start = cursor;
            int back = 0;
            while (start > 0) {
                int stp = mail_step(disp + start - 1);
                int st = start - stp;
                int cw = mail_cw(disp + st);
                if (back + cw > vieww) break;
                back += cw;
                start = st;
            }
        }
    }
    gfx::fillrect(s, x, y, w, h, 0xFFFFFF);
    gfx::rect(s, x, y, w, h, focus ? 0x3498DB : 0xB8C2CC);
    mail_text_clip(s, x + 4, y + (h - 16) / 2, disp + start, w - 8,
                   text[0] ? 0x2C3E50 : 0x95A5A6, 0xFFFFFF);
    if (focus) {
        int cx = x + 4 + mail_prefix_px(disp, start, cursor > len ? len : cursor);
        if (cx < x + w - 2) gfx::char8x16(s, cx, y + (h - 16) / 2, '|', 0x3498DB, 0xFFFFFF);
    }
}

// multi-line body editor / viewer with word-wrap and vertical scroll
static void mail_draw_body(Surface& s, int x, int y, int w, int h,
                           const char* text, int cursor, int scroll, bool editable) {
    gfx::fillrect(s, x, y, w, h, 0xFFFFFF);
    gfx::rect(s, x, y, w, h, 0xB8C2CC);
    if (!text) return;
    int starts[96];
    int lines = mail_wrap(text, w - 10, starts, 96);
    int visible = h / 16;
    if (scroll > lines - visible) scroll = lines - visible;
    if (scroll < 0) scroll = 0;
    int tlen = (int)strlen(text);
    for (int i = scroll; i < lines && i < scroll + visible; i++) {
        int end = (i + 1 < lines) ? starts[i + 1] - 1 : tlen;
        int ly = y + 4 + (i - scroll) * 16;
        mail_text_seg(s, x + 5, ly, text, starts[i], end, 0x2C3E50, 0xFFFFFF);
        if (editable && cursor >= starts[i] && (i + 1 >= lines || cursor < starts[i + 1])) {
            int cx = x + 5 + mail_prefix_px(text, starts[i], cursor);
            gfx::char8x16(s, cx, ly, '|', 0x3498DB, 0xFFFFFF);
        }
    }
}

static void mail_btn(Surface& s, int x, int y, int w, int h, const char* label, uint32_t bg) {
    gfx::fillrect(s, x, y, w, h, bg);
    int tw = gfx::text_width(label);
    gfx::text(s, x + (w - tw) / 2, y + (h - 16) / 2, label, 0xFFFFFF, bg);
}

static bool mail_in(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

// "2026-09-24 10:05" with zero-padding (nefuOS ksprintf has no %0Nd)
static void mail_now(char* out, int sz) {
    DateInfo d;
    if (!platform_rtc_date(&d)) { strcpy(out, "1970-01-01 00:00"); return; }
    int n = ksprintf(out, (size_t)sz, "%04d-", d.year);
    if (n < 0) n = 0;
    if (n + 1 < sz) { out[n++] = (char)('0' + d.month / 10); out[n++] = (char)('0' + d.month % 10); out[n++] = '-'; }
    if (n + 1 < sz) { out[n++] = (char)('0' + d.day / 10); out[n++] = (char)('0' + d.day % 10); out[n++] = ' '; }
    if (n + 1 < sz) { out[n++] = (char)('0' + d.hour / 10); out[n++] = (char)('0' + d.hour % 10); out[n++] = ':'; }
    if (n + 1 < sz) { out[n++] = (char)('0' + d.min / 10); out[n++] = (char)('0' + d.min % 10); }
    out[n] = 0;
}

// ============================== MIME text helpers ==============================

static int mail_b64_decode(const char* in, char* out, int outsz);   // fwd (defined below)
static int mail_hexv(char c);                                            // fwd (defined below)
// decode one RFC 2047 encoded word: =?charset?B?base64?= or =?charset?Q?qp?=
// returns bytes written into out (already decoded to UTF-8 bytes)
static int mail_decode_word(const char* tok, int tlen, char* out, int outsz) {
    if (tlen < 8 || tok[0] != '=' || tok[1] != '?') return 0;
    const char* q1 = 0, *q2 = 0, *q3 = 0;
    for (int i = 2; i < tlen; i++) {
        if (tok[i] == '?' && !q1) { q1 = tok + i; continue; }
        if (tok[i] == '?' && q1 && !q2) { q2 = tok + i; continue; }
        if (tok[i] == '?' && q2 && !q3) { q3 = tok + i; }
    }
    if (!q1 || !q2 || !q3) return 0;
    // token ends with ?= ; q3+1 must be '='
    if (q3 + 1 >= tok + tlen || q3[1] != '=') return 0;
    char enc = q2[1];
    const char* payload = q2 + 2;
    int plen = (int)(q3 - payload);
    char tmp[512];
    if (plen > 511) plen = 511;
    memcpy(tmp, payload, (size_t)plen);
    tmp[plen] = 0;
    if (enc == 'B' || enc == 'b') {
        return mail_b64_decode(tmp, out, outsz);
    } else if (enc == 'Q' || enc == 'q') {
        // Q-encoding: '_' means space, =XX hex escapes
        int o = 0;
        for (int i = 0; i < plen && o < outsz - 1; i++) {
            if (tmp[i] == '_') out[o++] = ' ';
            else if (tmp[i] == '=' && i + 2 < plen) {
                int h = mail_hexv(tmp[i + 1]), l = mail_hexv(tmp[i + 2]);
                if (h >= 0 && l >= 0) { out[o++] = (char)((h << 4) | l); i += 2; }
                else out[o++] = tmp[i];
            } else out[o++] = tmp[i];
        }
        out[o] = 0;
        return o;
    }
    return 0;
}

// decode RFC 2047 encoded words scattered in a header into one plain string
static void mail_decode_header(const char* in, char* out, int outsz) {
    int o = 0;
    const char* p = in;
    char tmp[600];
    while (*p && o < outsz - 1) {
        if (p[0] == '=' && p[1] == '?') {
            // find end marker "?="
            const char* end = strstr(p + 2, "?=");
            if (end) {
                int tlen = (int)(end + 2 - p);
                int n = mail_decode_word(p, tlen, tmp, (int)sizeof(tmp));
                if (n > 0) {
                    for (int i = 0; i < n && o < outsz - 1; i++) out[o++] = tmp[i];
                    p = end + 2;
                    // skip one separating whitespace between adjacent words
                    if (*p == ' ' && (p[1] == '=' && p[2] == '?')) p++;
                    continue;
                }
            }
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

// crude HTML -> text: drop tags, decode a few entities, keep <br>/</p> as newlines
static void mail_strip_html(const char* in, char* out, int outsz) {
    int o = 0;
    bool pre = false;   // preserve whitespace inside <pre>
    for (const char* p = in; *p && o < outsz - 2; p++) {
        if (*p == '<') {
            if (strncmp(p, "<pre", 4) == 0 || strncmp(p, "<PRE", 4) == 0) pre = true;
            else if (strncmp(p, "</pre", 5) == 0 || strncmp(p, "</PRE", 5) == 0) pre = false;
            const char* e = strchr(p, '>');
            if (!e) break;
            if (strncmp(p, "<br", 3) == 0 || strncmp(p, "<BR", 3) == 0 ||
                strncmp(p, "</p", 3) == 0 || strncmp(p, "</P", 3) == 0 ||
                strncmp(p, "</div", 5) == 0 || strncmp(p, "</DIV", 5) == 0)
                out[o++] = '\n';
            p = e;
        } else if (*p == '&') {
            if (strncmp(p, "&amp;", 5) == 0) { out[o++] = '&'; p += 4; }
            else if (strncmp(p, "&lt;", 4) == 0) { out[o++] = '<'; p += 3; }
            else if (strncmp(p, "&gt;", 4) == 0) { out[o++] = '>'; p += 3; }
            else if (strncmp(p, "&quot;", 6) == 0) { out[o++] = '"'; p += 5; }
            else if (strncmp(p, "&#39;", 5) == 0) { out[o++] = '\''; p += 4; }
            else if (strncmp(p, "&nbsp;", 6) == 0) { out[o++] = ' '; p += 5; }
            else if (strncmp(p, "&copy;", 6) == 0) { out[o++] = '('; out[o++] = 'c'; out[o++] = ')'; p += 5; }
            else out[o++] = *p;
        } else if (*p == '\n' && !pre) {
            out[o++] = ' ';
        } else {
            out[o++] = *p;
        }
    }
    // collapse runs of blank lines / trailing spaces for readability
    int w = 0;
    for (int i = 0; i < o; i++) {
        if (out[i] == '\n' && w > 0 && out[w - 1] == '\n') continue;
        if (out[i] == ' ' && w > 0 && out[w - 1] == ' ' && i > 0) continue;
        out[w++] = out[i];
    }
    while (w > 0 && (out[w - 1] == '\n' || out[w - 1] == ' ')) w--;
    out[w] = 0;
}

// parse "Tue, 24 Sep 2026 10:05:00 +0800" -> "2026-09-24 10:05"
static const char* MAIL_MONTHS[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
static int mail_month_index(const char* m) {
    for (int i = 0; i < 12; i++) if (strncmp(m, MAIL_MONTHS[i], 3) == 0) return i + 1;
    return 0;
}

static void mail_parse_rfc822_date(const char* in, char* out, int outsz) {
    // Accept "Mon, 24 Sep 2026 10:05:xx ...", "24 Sep 2026 10:05", "2026-09-24T10:05:xx"
    if (!in || !in[0]) { out[0] = 0; return; }
    if (in[0] >= '0' && in[0] <= '9' && in[1] >= '0' && in[1] <= '9' &&
        in[2] >= '0' && in[2] <= '9' && in[3] >= '0' && in[3] <= '9' && in[4] == '-') {
        // already ISO-ish: 2026-09-24T10:05:00Z
        int year = atoi(in);
        int mon = atoi(in + 5), day = atoi(in + 8);
        int hh = atoi(in + 11), mm = atoi(in + 14);
        ksprintf(out, (size_t)outsz, "%04d-%02d-%02d %02d:%02d", year, mon, day, hh, mm);
        return;
    }
    const char* p = in;
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    while (*p == ' ') p++;
    int day = atoi(p);
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    int mon = mail_month_index(p);
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    int year = atoi(p);
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    int hh = atoi(p), mm = 0;
    while (*p && *p != ':' ) p++;
    if (*p == ':') { p++; mm = atoi(p); }
    if (year < 1970) year = 2026;
    if (mon == 0) mon = 1;
    ksprintf(out, (size_t)outsz, "%04d-%02d-%02d %02d:%02d", year, mon, day, hh, mm);
}

// make a safe file name from a raw attachment name
static void mail_sanitize_name(const char* in, char* out, int outsz) {
    int o = 0;
    for (int i = 0; in[i] && o < outsz - 1; i++) {
        char c = in[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '\r' || c == '\n') c = '_';
        out[o++] = c;
    }
    out[o] = 0;
}

// strip MIME encoded-word quotes from an address: "Name <a@b>" -> "Name"
static void mail_disp_name(const char* addr, char* out, int outsz) {
    const char* lt = strchr(addr, '<');
    if (lt) {
        int n = (int)(lt - addr);
        if (n > outsz - 1) n = outsz - 1;
        memcpy(out, addr, (size_t)n);
        out[n] = 0;
        // trim trailing spaces
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) out[--n] = 0;
        // strip surrounding quotes
        if (n >= 2 && out[0] == '"' && out[n - 1] == '"') { out[n - 1] = 0; memmove(out, out + 1, (size_t)n); }
        return;
    }
    strncpy(out, addr, outsz - 1);
    out[outsz - 1] = 0;
}

// extract the bare email address from "Name <a@b>" or "a@b"
static void mail_addr_only(const char* addr, char* out, int outsz) {
    const char* lt = strchr(addr, '<');
    const char* rt = lt ? strchr(lt + 1, '>') : 0;
    if (lt && rt) {
        int n = (int)(rt - lt - 1);
        if (n > outsz - 1) n = outsz - 1;
        memcpy(out, lt + 1, (size_t)n);
        out[n] = 0;
        return;
    }
    strncpy(out, addr, outsz - 1);
    out[outsz - 1] = 0;
}

// RFC 2047 encode a header (Subject) into an ASCII-safe form.
// ASCII-only input is passed through unchanged; otherwise the whole string is
// wrapped as =?UTF-8?B?...?= chunks folded at 75 columns.
static void mail_encode_header(const char* in, char* out, int outsz) {
    if (!in || !in[0]) { out[0] = 0; return; }
    bool need = false;
    for (const char* p = in; *p; p++)
        if ((unsigned char)*p >= 0x80 || *p == '=' || *p == '?' || *p == '_') { need = true; break; }
    if (!need) { strncpy(out, in, outsz - 1); out[outsz - 1] = 0; return; }
    static const char* TB = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0, col = 0;
    const char* p = in;
    while (*p && o < outsz - 8) {
        // collect a UTF-8 chunk of at most 42 bytes -> 56 base64 chars
        char chunk[48];
        int cn = 0;
        while (*p && cn < 42) {
            unsigned char c = (unsigned char)*p;
            int adv = 1;
            if (c >= 0x80) {
                if ((c & 0xE0) == 0xC0) adv = 2;
                else if ((c & 0xF0) == 0xE0) adv = 3;
                else if ((c & 0xF8) == 0xF0) adv = 4;
            }
            if (cn + adv > 42) break;
            for (int k = 0; k < adv && *p; k++) chunk[cn++] = *p++;
        }
        if (cn == 0) break;
        // base64 the chunk
        char b64[64];
        int bo = 0;
        for (int i = 0; i < cn; i += 3) {
            unsigned char c1 = (unsigned char)chunk[i];
            unsigned char c2 = (i + 1 < cn) ? (unsigned char)chunk[i + 1] : 0;
            unsigned char c3 = (i + 2 < cn) ? (unsigned char)chunk[i + 2] : 0;
            b64[bo++] = TB[c1 >> 2];
            b64[bo++] = TB[((c1 & 3) << 4) | (c2 >> 4)];
            b64[bo++] = (i + 1 < cn) ? TB[((c2 & 15) << 2) | (c3 >> 6)] : '=';
            b64[bo++] = (i + 2 < cn) ? TB[c3 & 63] : '=';
        }
        b64[bo] = 0;
        // emit one encoded word, folding with CRLF + space when needed
        int wordlen = 10 + bo;                 // =?UTF-8?B? + body + ?=
        if (o > 0 && col + wordlen > 74 && o < outsz - 4) { out[o++] = '\r'; out[o++] = '\n'; out[o++] = ' '; col = 1; }
        if (o + wordlen >= outsz - 1) break;
        out[o++] = '='; out[o++] = '?'; out[o++] = 'U'; out[o++] = 'T'; out[o++] = 'F';
        out[o++] = '-'; out[o++] = '8'; out[o++] = '?'; out[o++] = 'B'; out[o++] = '?';
        memcpy(out + o, b64, (size_t)bo); o += bo;
        out[o++] = '?'; out[o++] = '=';
        col += wordlen;
    }
    out[o] = 0;
}

// ---- base64 / hex / quoted-printable codecs (shared by bare + host) ----

static int mail_hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int mail_b64_decode(const char* in, char* out, int outsz) {
    int o = 0, val = 0, bits = 0;
    for (const char* p = in; *p && o < outsz - 1; p++) {
        char c = *p;
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (char)((val >> bits) & 0xFF); }
    }
    out[o] = 0;
    return o;
}

static void mail_qp_decode(const char* in, char* out, int outsz) {
    int o = 0;
    for (int i = 0; in[i] && o < outsz - 1; i++) {
        char c = in[i];
        if (c == '=' && in[i + 1] == '\n') { i += 1; continue; }
        if (c == '=' && in[i + 1] == '\r' && in[i + 2] == '\n') { i += 2; continue; }
        int h = mail_hexv(in[i + 1]), l = mail_hexv(in[i + 2]);
        if (c == '=' && h >= 0 && l >= 0) {
            out[o++] = (char)((h << 4) | l);
            i += 2;
            continue;
        }
        out[o++] = c;
    }
    out[o] = 0;
}



// ============================== persistence ==============================

static const char* mail_path(int folder) {
    switch (folder) {
        case EF_SENT:   return "/home/user/Mail/sent.mail";
        case EF_DRAFTS: return "/home/user/Mail/drafts.mail";
        case EF_TRASH:  return "/home/user/Mail/trash.mail";
        default:        return "/home/user/Mail/inbox.mail";
    }
}

static void mail_save(EmailState* st) {
    if (!g_vfs) return;
    g_vfs->mkdir("/home/user/Mail");
    for (int f = 0; f < EF_COUNT; f++) {
        if (f == EF_STARRED) continue;
        String out;
        out.reserve(8192);
        EmailFolder& fo = st->folders[f];
        for (int i = 0; i < fo.msg_count; i++) {
            EmailMessage& m = fo.messages[i];
            char nb[16];
            ksprintf(nb, sizeof(nb), "%d", (int)strlen(m.body));
            out += "M\n";
            out += "F="; out += m.from;  out += "\n";
            out += "T="; out += m.to;    out += "\n";
            out += "C="; out += m.cc;    out += "\n";
            out += "S="; out += m.subject; out += "\n";
            out += "D="; out += m.date;  out += "\n";
            out += "R="; out += m.read ? "1" : "0"; out += "\n";
            out += "K="; out += m.starred ? "1" : "0"; out += "\n";
            out += "I="; out += m.msgid[0] ? m.msgid : "-"; out += "\n";
            out += "H="; out += m.has_html ? "1" : "0"; out += "\n";
            char ac[8];
            ksprintf(ac, sizeof(ac), "%d", m.att_cnt);
            out += "G="; out += ac; out += "\n";
            for (int ai = 0; ai < m.att_cnt && ai < MAX_ATT; ai++) {
                char as[16];
                ksprintf(as, sizeof(as), "%d", m.att[ai].size);
                out += "A="; out += m.att[ai].name; out += "\t";
                out += m.att[ai].path; out += "\t";
                out += m.att[ai].type; out += "\t";
                out += as; out += "\n";
            }
            out += "B="; out += nb;      out += "\n";
            out += m.body; out += "\nE\n";
        }
        FSNode* fn = g_vfs->resolve(mail_path(f));
        if (!fn) fn = g_vfs->create_file(mail_path(f));
        if (fn) g_vfs->write_file(fn, (const uint8_t*)out.c_str(), (uint32_t)out.len());
    }
    String a;
    a += "name=";    a += st->acct.display;   a += "\n";
    a += "email=";   a += st->acct.email;     a += "\n";
    a += "smtp_host="; a += st->acct.smtp_host; a += "\n";
    char pbuf[16];
    ksprintf(pbuf, sizeof(pbuf), "%d", st->acct.smtp_port);
    a += "smtp_port="; a += pbuf; a += "\n";
    a += "pop_host="; a += st->acct.pop_host; a += "\n";
    ksprintf(pbuf, sizeof(pbuf), "%d", st->acct.pop_port);
    a += "pop_port="; a += pbuf; a += "\n";
    a += "user=";  a += st->acct.username; a += "\n";
    a += "pass=";  a += st->acct.password; a += "\n";
    a += "sig=";   a += st->acct.signature; a += "\n";
    FSNode* af = g_vfs->resolve("/home/user/Mail/account.conf");
    if (!af) af = g_vfs->create_file("/home/user/Mail/account.conf");
    if (af) g_vfs->write_file(af, (const uint8_t*)a.c_str(), (uint32_t)a.len());
    st->dirty = false;
}

// read a \n-terminated line from a raw buffer; returns length (0 at end)
static int mail_line(const char*& p, const char* end, char* out, int max) {
    int n = 0;
    while (p < end && *p != '\n' && n < max - 1) { out[n++] = *p; p++; }
    if (p < end && *p == '\n') p++;
    if (n > 0 && out[n - 1] == '\r') n--;
    out[n] = 0;
    return n;
}

static void mail_load(EmailState* st) {
    if (!g_vfs) return;
    for (int f = 0; f < EF_COUNT; f++) {
        if (f == EF_STARRED) continue;
        FSNode* fn = g_vfs->resolve(mail_path(f));
        if (!fn || fn->is_dir || fn->size == 0) continue;
        const char* p = (const char*)fn->data;
        const char* end = p + fn->size;
        EmailFolder& fo = st->folders[f];
        fo.msg_count = 0;
        fo.unread_count = 0;
        char line[512];
        while (p < end && fo.msg_count < MAX_MSGS) {
            if (mail_line(p, end, line, sizeof(line)) <= 0) break;
            if (strcmp(line, "M") != 0) continue;
            EmailMessage m;
            memset(&m, 0, sizeof(m));
            m.read = false;
            m.starred = false;
            int bodylen = 0;
            while (p < end) {
                if (mail_line(p, end, line, sizeof(line)) <= 0) break;
                if (strcmp(line, "E") == 0) break;
                if (strncmp(line, "F=", 2) == 0) strncpy(m.from, line + 2, sizeof(m.from) - 1);
                else if (strncmp(line, "T=", 2) == 0) strncpy(m.to, line + 2, sizeof(m.to) - 1);
                else if (strncmp(line, "C=", 2) == 0) strncpy(m.cc, line + 2, sizeof(m.cc) - 1);
                else if (strncmp(line, "S=", 2) == 0) strncpy(m.subject, line + 2, sizeof(m.subject) - 1);
                else if (strncmp(line, "D=", 2) == 0) strncpy(m.date, line + 2, sizeof(m.date) - 1);
                else if (strcmp(line, "R=1") == 0) m.read = true;
                else if (strcmp(line, "K=1") == 0) m.starred = true;
                else if (strncmp(line, "I=", 2) == 0 && line[2] != '-') strncpy(m.msgid, line + 2, sizeof(m.msgid) - 1);
                else if (strcmp(line, "H=1") == 0) m.has_html = true;
                else if (strncmp(line, "G=", 2) == 0) m.att_cnt = atoi(line + 2);
                else if (strncmp(line, "A=", 2) == 0 && m.att_cnt > 0) {
                    int slot = m.att_cnt - 1;
                    if (slot >= 0 && slot < MAX_ATT) {
                        char* t1 = strchr(line + 2, '\t');
                        char* t2 = t1 ? strchr(t1 + 1, '\t') : 0;
                        char* t3 = t2 ? strchr(t2 + 1, '\t') : 0;
                        if (t1) { *t1 = 0; strncpy(m.att[slot].name, line + 2, sizeof(m.att[slot].name) - 1); }
                        if (t2) { *t2 = 0; strncpy(m.att[slot].path, t1 + 1, sizeof(m.att[slot].path) - 1); }
                        if (t3) { *t3 = 0; strncpy(m.att[slot].type, t2 + 1, sizeof(m.att[slot].type) - 1); m.att[slot].size = atoi(t3 + 1); }
                    }
                }
                else if (strncmp(line, "B=", 2) == 0) { bodylen = atoi(line + 2); break; }
            }
            if (bodylen > 0) {
                if (bodylen > (int)sizeof(m.body) - 1) bodylen = (int)sizeof(m.body) - 1;
                int got = 0;
                while (p < end && got < bodylen) m.body[got++] = *p++;
                // skip the "\nE" trailer
                while (p < end && *p != '\n') p++;
                if (p < end) p++;
                m.body[got] = 0;
            }
            fo.messages[fo.msg_count++] = m;
            // strncpy does not NUL-terminate when the source fills the buffer
            fo.messages[fo.msg_count - 1].from[sizeof(EmailMessage::from) - 1] = 0;
            fo.messages[fo.msg_count - 1].to[sizeof(EmailMessage::to) - 1] = 0;
            fo.messages[fo.msg_count - 1].cc[sizeof(EmailMessage::cc) - 1] = 0;
            fo.messages[fo.msg_count - 1].subject[sizeof(EmailMessage::subject) - 1] = 0;
            fo.messages[fo.msg_count - 1].date[sizeof(EmailMessage::date) - 1] = 0;
            if (!m.read) fo.unread_count++;
        }
    }
    // account
    FSNode* af = g_vfs->resolve("/home/user/Mail/account.conf");
    if (af && !af->is_dir && af->size > 0) {
        const char* p = (const char*)af->data;
        const char* end = p + af->size;
        char line[192];
        while (p < end) {
            if (mail_line(p, end, line, sizeof(line)) <= 0) break;
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            const char* v = eq + 1;
            if (strcmp(line, "name") == 0) strncpy(st->acct.display, v, sizeof(st->acct.display) - 1);
            else if (strcmp(line, "email") == 0) { strncpy(st->acct.email, v, sizeof(st->acct.email) - 1); }
            else if (strcmp(line, "smtp_host") == 0) strncpy(st->acct.smtp_host, v, sizeof(st->acct.smtp_host) - 1);
            else if (strcmp(line, "smtp_port") == 0) st->acct.smtp_port = atoi(v);
            else if (strcmp(line, "pop_host") == 0) strncpy(st->acct.pop_host, v, sizeof(st->acct.pop_host) - 1);
            else if (strcmp(line, "pop_port") == 0) st->acct.pop_port = atoi(v);
            else if (strcmp(line, "user") == 0) strncpy(st->acct.username, v, sizeof(st->acct.username) - 1);
            else if (strcmp(line, "pass") == 0) strncpy(st->acct.password, v, sizeof(st->acct.password) - 1);
            else if (strcmp(line, "sig") == 0) strncpy(st->acct.signature, v, sizeof(st->acct.signature) - 1);
        }
        st->acct.display[sizeof(st->acct.display) - 1] = 0;
        st->acct.email[sizeof(st->acct.email) - 1] = 0;
        st->acct.smtp_host[sizeof(st->acct.smtp_host) - 1] = 0;
        st->acct.pop_host[sizeof(st->acct.pop_host) - 1] = 0;
        st->acct.username[sizeof(st->acct.username) - 1] = 0;
        st->acct.password[sizeof(st->acct.password) - 1] = 0;
        st->acct.configured = st->acct.email[0] != 0;
    }
}

// first-run sample mail (only used when nothing has been persisted yet)
static void mail_seed(EmailState* st) {
    EmailFolder& in = st->folders[EF_INBOX];
    EmailMessage m;
    memset(&m, 0, sizeof(m));
    m.read = false; m.starred = false;
    strcpy(m.from, "support@nefuos.dev");
    strcpy(m.to, "user@nefuos.dev");
    strcpy(m.subject, "Welcome to nefuOS!");
    strcpy(m.body, "Welcome to nefuOS! This is your first email.\n\nThe new mail app supports folders, compose, reply, search and persistent storage across reboots.");
    strcpy(m.date, "2026-09-20 09:12");
    in.messages[in.msg_count++] = m;

    memset(&m, 0, sizeof(m));
    m.read = false; m.starred = true;
    strcpy(m.from, "updates@nefuos.dev");
    strcpy(m.to, "user@nefuos.dev");
    strcpy(m.subject, "nefuOS 2.0 update is available");
    strcpy(m.body, "A new update brings the professional mail client, faster boot and more stable networking.\n\nInstall it from the Software Store.");
    strcpy(m.date, "2026-09-22 18:40");
    in.messages[in.msg_count++] = m;

    memset(&m, 0, sizeof(m));
    m.read = false; m.starred = false;
    strcpy(m.from, "community@nefuos.dev");
    strcpy(m.to, "user@nefuos.dev");
    strcpy(m.subject, "nefuOS user meetup - next Friday");
    strcpy(m.body, "Join us for the monthly nefuOS meetup. We will demo the new mail app and take feature requests.");
    strcpy(m.date, "2026-09-23 21:05");
    in.messages[in.msg_count++] = m;
    in.unread_count = 3;

    EmailFolder& sn = st->folders[EF_SENT];
    memset(&m, 0, sizeof(m));
    m.read = true;
    strcpy(m.from, "user@nefuos.dev");
    strcpy(m.to, "support@nefuos.dev");
    strcpy(m.subject, "Re: Welcome to nefuOS!");
    strcpy(m.body, "Thanks! Looking forward to the new mail features.");
    strcpy(m.date, "2026-09-20 10:02");
    sn.messages[sn.msg_count++] = m;

    EmailFolder& dr = st->folders[EF_DRAFTS];
    memset(&m, 0, sizeof(m));
    m.read = true;
    strcpy(m.from, "user@nefuos.dev");
    strcpy(m.to, "team@nefuos.dev");
    strcpy(m.subject, "Ideas for the mail app");
    strcpy(m.body, "Next steps: IMAP support, attachments, and offline drafts.");
    strcpy(m.date, "2026-09-24 08:30");
    dr.messages[dr.msg_count++] = m;
}

// ============================== visible list mapping ==============================

// search matches subject / sender / recipient / body (case-insensitive-lite)
static bool mail_visible(const EmailMessage& m, const char* q) {
    if (!q || !q[0]) return true;
    return strstr(m.subject, q) != 0 || strstr(m.from, q) != 0 ||
           strstr(m.to, q) != 0 || strstr(m.cc, q) != 0 || strstr(m.body, q) != 0;
}

static void mail_rebuild_star(EmailState* st) {
    st->star_cnt = 0;
    st->star_unread = 0;
    int srcs[4] = { EF_INBOX, EF_SENT, EF_DRAFTS, EF_TRASH };
    for (int f = 0; f < 4; f++) {
        EmailFolder& fo = st->folders[srcs[f]];
        for (int i = 0; i < fo.msg_count && st->star_cnt < MAX_MSGS; i++) {
            if (!fo.messages[i].starred) continue;
            st->star_src[st->star_cnt] = srcs[f];
            st->star_idx[st->star_cnt] = i;
            if (!fo.messages[i].read) st->star_unread++;
            st->star_cnt++;
        }
    }
}

// ---- visible order index (search filter + sort) ----
// view_ord[n] stores a star-array index for the Starred folder, or a plain
// message index for any real folder.  Built once per folder/search change.

// comparator used by mail_build_view (returns <0 when a sorts before b)
static int mail_cmp(const EmailMessage& a, const EmailMessage& b, int mode) {
    int r;
    switch (mode) {
        case 1:  r = strcmp(a.date, b.date); return r;              // date asc
        case 2:  r = strcmp(a.from, b.from); if (r) return r; break; // sender
        case 3:  r = strcmp(a.subject, b.subject); if (r) return r; break; // subject
        default: break;
    }
    r = strcmp(b.date, a.date);      // mode 0 / tie-break: date desc
    if (r) return r;
    return strcmp(a.from, b.from);
}

static void mail_build_view(EmailState* st) {
    int n = 0;
    if (st->cur_folder == EF_STARRED) {
        for (int i = 0; i < st->star_cnt; i++) {
            EmailFolder& fo = st->folders[st->star_src[i]];
            int idx = st->star_idx[i];
            if (idx < 0 || idx >= fo.msg_count) continue;
            if (!mail_visible(fo.messages[idx], st->search)) continue;
            st->view_ord[n++] = i;
        }
    } else {
        EmailFolder& fo = st->folders[st->cur_folder];
        for (int i = 0; i < fo.msg_count; i++)
            if (mail_visible(fo.messages[i], st->search)) st->view_ord[n++] = i;
    }
    st->view_cnt = n;
    // simple insertion sort by the active sort mode
    for (int i = 1; i < n; i++) {
        int key = st->view_ord[i];
        int j = i - 1;
        while (j >= 0) {
            EmailMessage *pa = 0, *pb = 0;
            if (st->cur_folder == EF_STARRED) {
                EmailFolder& fa = st->folders[st->star_src[st->view_ord[j]]];
                EmailFolder& fb = st->folders[st->star_src[key]];
                pa = &fa.messages[st->star_idx[st->view_ord[j]]];
                pb = &fb.messages[st->star_idx[key]];
            } else {
                pa = &st->folders[st->cur_folder].messages[st->view_ord[j]];
                pb = &st->folders[st->cur_folder].messages[key];
            }
            if (mail_cmp(*pa, *pb, st->sort_mode) <= 0) break;
            st->view_ord[j + 1] = st->view_ord[j];
            j--;
        }
        st->view_ord[j + 1] = key;
    }
    // record the state this view was built from
    st->view_sig_folder = st->cur_folder;
    st->view_sig_sort = st->sort_mode;
    st->view_sig_msgs = (st->cur_folder == EF_STARRED) ? st->star_cnt : st->folders[st->cur_folder].msg_count;
    st->view_sig_star = st->star_cnt;
    strncpy(st->view_sig_search, st->search, sizeof(st->view_sig_search) - 1);
}

// rebuild the visible view only when folder / sort / search / data changed
static void mail_ensure_view(EmailState* st) {
    int m = (st->cur_folder == EF_STARRED) ? st->star_cnt : st->folders[st->cur_folder].msg_count;
    if (st->view_cnt == 0 || st->view_sig_folder != st->cur_folder ||
        st->view_sig_sort != st->sort_mode || st->view_sig_msgs != m ||
        st->view_sig_star != st->star_cnt ||
        strcmp(st->view_sig_search, st->search) != 0)
        mail_build_view(st);
}

static int vis_total(EmailState* st) {
    mail_ensure_view(st);
    return st->view_cnt;
}

static EmailMessage* vis_msg(EmailState* st, int n) {
    mail_ensure_view(st);
    if (n < 0 || n >= st->view_cnt) return 0;
    int v = st->view_ord[n];
    if (st->cur_folder == EF_STARRED) {
        EmailFolder& fo = st->folders[st->star_src[v]];
        int idx = st->star_idx[v];
        if (idx < 0 || idx >= fo.msg_count) return 0;
        return &fo.messages[idx];
    }
    EmailFolder& fo = st->folders[st->cur_folder];
    if (v < 0 || v >= fo.msg_count) return 0;
    return &fo.messages[v];
}

// resolve visible index n to (folder, message index) - real location
static void vis_src(EmailState* st, int n, int& folder, int& idx) {
    mail_ensure_view(st);
    folder = st->cur_folder;
    idx = -1;
    if (n < 0 || n >= st->view_cnt) return;
    int v = st->view_ord[n];
    if (st->cur_folder == EF_STARRED) {
        folder = st->star_src[v];
        idx = st->star_idx[v];
        return;
    }
    idx = v;
}

static void mail_clamp_list(EmailState* st) {
    mail_build_view(st);
    int n = vis_total(st);
    if (st->cur_msg >= n) st->cur_msg = n - 1;
    if (st->cur_msg < 0) st->cur_msg = 0;
}

// ============================== message operations ==============================

static void mail_move_msg(EmailState* st, int srcf, int idx, int dstf) {
    if (srcf == dstf || srcf == EF_STARRED || dstf == EF_STARRED) return;
    EmailFolder& src = st->folders[srcf];
    EmailFolder& dst = st->folders[dstf];
    if (idx < 0 || idx >= src.msg_count || dst.msg_count >= MAX_MSGS) return;
    EmailMessage m = src.messages[idx];
    dst.messages[dst.msg_count++] = m;
    if (!m.read && srcf == EF_INBOX && st->folders[EF_INBOX].unread_count > 0)
        st->folders[EF_INBOX].unread_count--;
    for (int j = idx; j < src.msg_count - 1; j++) src.messages[j] = src.messages[j + 1];
    src.msg_count--;
}

static void mail_remove_msg(EmailState* st, int f, int idx) {
    EmailFolder& fo = st->folders[f];
    if (idx < 0 || idx >= fo.msg_count) return;
    if (!fo.messages[idx].read && fo.unread_count > 0) fo.unread_count--;
    for (int j = idx; j < fo.msg_count - 1; j++) fo.messages[j] = fo.messages[j + 1];
    fo.msg_count--;
}

static void mail_toggle_star(EmailState* st) {
    int f, i;
    vis_src(st, st->cur_msg, f, i);
    if (i < 0) return;
    EmailMessage& m = st->folders[f].messages[i];
    m.starred = !m.starred;
    mail_rebuild_star(st);
    mail_build_view(st);
    mail_clamp_list(st);
    mail_save(st);
}

static void mail_toggle_read(EmailState* st) {
    int f, i;
    vis_src(st, st->cur_msg, f, i);
    if (i < 0) return;
    EmailMessage& m = st->folders[f].messages[i];
    m.read = !m.read;
    if (m.read) { if (f == EF_INBOX && st->folders[EF_INBOX].unread_count > 0) st->folders[EF_INBOX].unread_count--; }
    else { if (f == EF_INBOX) st->folders[EF_INBOX].unread_count++; }
    mail_rebuild_star(st);
    mail_build_view(st);
    mail_clamp_list(st);
    mail_save(st);
}

static void mail_delete_current(EmailState* st) {
    int f, i;
    vis_src(st, st->cur_msg, f, i);
    if (i < 0) return;
    if (st->cur_folder == EF_TRASH) {
        mail_remove_msg(st, EF_TRASH, i);
        strcpy(st->status, "Message permanently deleted");
    } else {
        mail_move_msg(st, f, i, EF_TRASH);
        strcpy(st->status, "Message moved to Trash");
    }
    mail_rebuild_star(st);
    mail_build_view(st);
    st->view = EV_LIST;
    mail_clamp_list(st);
    mail_save(st);
}

static void mail_open_msg(EmailState* st) {
    int f, i;
    vis_src(st, st->cur_msg, f, i);
    if (i < 0) return;
    EmailMessage& m = st->folders[f].messages[i];
    if (!m.read) {
        m.read = true;
        if (f == EF_INBOX && st->folders[EF_INBOX].unread_count > 0) st->folders[EF_INBOX].unread_count--;
    }
    mail_rebuild_star(st);
    mail_build_view(st);
    st->view = EV_READ;
    st->read_scroll = 0;
    mail_save(st);
}

// ============================== compose operations ==============================

static void compose_start(EmailState* st, bool reset_all) {
    if (reset_all) {
        st->comp_to[0] = 0;
        st->comp_cc[0] = 0;
        st->comp_bcc[0] = 0;
        st->comp_subject[0] = 0;
        st->comp_body[0] = 0;
        st->comp_field = 0;
        st->comp_cursor[0] = st->comp_cursor[1] = st->comp_cursor[2] = st->comp_cursor[3] = st->comp_cursor[4] = 0;
        st->comp_body_scroll = 0;
        st->comp_draft_slot = -1;
        st->comp_att_cnt = 0;
        st->comp_att_sel = -1;
        // append the configured signature to a fresh message
        if (st->acct.signature[0]) {
            int bl = (int)strlen(st->comp_body);
            const char* sig = st->acct.signature;
            while (*sig && bl < (int)sizeof(st->comp_body) - 2) st->comp_body[bl++] = *sig++;
            st->comp_body[bl++] = '\n';
            st->comp_body[bl] = 0;
        }
    }
    st->view = EV_COMPOSE;
}

static void compose_from_draft(EmailState* st, int slot) {
    EmailFolder& d = st->folders[EF_DRAFTS];
    if (slot < 0 || slot >= d.msg_count) { compose_start(st, true); return; }
    EmailMessage& m = d.messages[slot];
    compose_start(st, true);
    strncpy(st->comp_to, m.to, sizeof(st->comp_to) - 1);
    strncpy(st->comp_cc, m.cc, sizeof(st->comp_cc) - 1);
    strncpy(st->comp_bcc, m.bcc, sizeof(st->comp_bcc) - 1);
    strncpy(st->comp_subject, m.subject, sizeof(st->comp_subject) - 1);
    strncpy(st->comp_body, m.body, sizeof(st->comp_body) - 1);
    st->comp_att_cnt = 0;
    for (int ai = 0; ai < m.att_cnt && ai < MAX_ATT; ai++)
        strncpy(st->comp_att[ai], m.att[ai].path, 95);
    st->comp_att_cnt = m.att_cnt > MAX_ATT ? MAX_ATT : m.att_cnt;
    st->comp_draft_slot = slot;
    st->comp_field = 4;
    st->comp_cursor[4] = (int)strlen(st->comp_body);
}

// "Re: subject" style prefix (klib has no strncat)
static void mail_subject_prefix(char* out, int outsz, const char* prefix, const char* subj) {
    strcpy(out, prefix);
    int p = (int)strlen(prefix);
    int sl = (int)strlen(subj);
    if (p + sl >= outsz) sl = outsz - p - 1;
    if (sl > 0) memcpy(out + p, subj, (size_t)sl);
    out[p + sl] = 0;
}

// quote an existing message into the compose body (for reply / forward)
static void compose_quote(EmailState* st, const EmailMessage& m, bool fwd) {
    String q;
    if (!fwd) {
        q += "On "; q += m.date; q += ", "; q += m.from; q += " wrote:\n";
    } else {
        q += "---------- Forwarded message ----------\n";
        q += "From: "; q += m.from; q += "\n";
        q += "Date: "; q += m.date; q += "\n";
        q += "Subject: "; q += m.subject; q += "\n\n";
    }
    const char* b = m.body;
    while (*b && q.len() < 1500) {
        const char* nl = strchr(b, '\n');
        int ln = nl ? (int)(nl - b) : (int)strlen(b);
        if (!fwd) { q += "> "; if (ln > 0) q += String(b, ln); q += "\n"; }
        else { if (ln > 0) q += String(b, ln); q += "\n"; }
        if (!nl) break;
        b = nl + 1;
    }
    strncpy(st->comp_body, q.c_str(), sizeof(st->comp_body) - 1);
    st->comp_cursor[4] = (int)strlen(st->comp_body);
}

static void compose_reply(EmailState* st) {
    EmailMessage* m = vis_msg(st, st->cur_msg);
    if (!m) return;
    compose_start(st, true);
    strncpy(st->comp_to, m->from, sizeof(st->comp_to) - 1);
    if (strncmp(m->subject, "Re:", 3) != 0) mail_subject_prefix(st->comp_subject, (int)sizeof(st->comp_subject), "Re: ", m->subject);
    else strncpy(st->comp_subject, m->subject, sizeof(st->comp_subject) - 1);
    compose_quote(st, *m, false);
    st->comp_field = 4;
    st->comp_cursor[4] = (int)strlen(st->comp_body);
}

// reply to sender + everyone in To/Cc (excluding our own address)
static void compose_reply_all(EmailState* st) {
    EmailMessage* m = vis_msg(st, st->cur_msg);
    if (!m) return;
    compose_start(st, true);
    strncpy(st->comp_to, m->from, sizeof(st->comp_to) - 1);
    char cc[256];
    cc[0] = 0;
    const char* parts[2] = { m->to, m->cc };
    for (int p = 0; p < 2; p++) {
        const char* s = parts[p];
        while (*s && (int)strlen(cc) < 250) {
            while (*s == ' ' || *s == ',') s++;
            const char* e = s;
            while (*e && *e != ',') e++;
            if (e == s) break;
            char one[160];
            int n = (int)(e - s);
            if (n > 159) n = 159;
            memcpy(one, s, (size_t)n);
            one[n] = 0;
            // skip our own address
            char only[96];
            mail_addr_only(one, only, sizeof(only));
            if (st->acct.email[0] && strcmp(only, st->acct.email) == 0) { s = *e ? e + 1 : e; continue; }
            if (cc[0]) strcat(cc, ", ");
            strcat(cc, one);
            s = *e ? e + 1 : e;
        }
    }
    strncpy(st->comp_cc, cc, sizeof(st->comp_cc) - 1);
    if (strncmp(m->subject, "Re:", 3) != 0) mail_subject_prefix(st->comp_subject, (int)sizeof(st->comp_subject), "Re: ", m->subject);
    else strncpy(st->comp_subject, m->subject, sizeof(st->comp_subject) - 1);
    compose_quote(st, *m, false);
    st->comp_field = 4;
    st->comp_cursor[4] = (int)strlen(st->comp_body);
}

static void compose_forward(EmailState* st) {
    EmailMessage* m = vis_msg(st, st->cur_msg);
    if (!m) return;
    compose_start(st, true);
    if (strncmp(m->subject, "Fwd:", 4) != 0) mail_subject_prefix(st->comp_subject, (int)sizeof(st->comp_subject), "Fwd: ", m->subject);
    else strncpy(st->comp_subject, m->subject, sizeof(st->comp_subject) - 1);
    compose_quote(st, *m, true);
    st->comp_field = 4;
    st->comp_cursor[4] = (int)strlen(st->comp_body);
}

static void compose_save_draft(EmailState* st) {
    EmailFolder& d = st->folders[EF_DRAFTS];
    EmailMessage m;
    memset(&m, 0, sizeof(m));
    strncpy(m.from, st->acct.email[0] ? st->acct.email : "user@nefuos.dev", sizeof(m.from) - 1);
    strncpy(m.to, st->comp_to, sizeof(m.to) - 1);
    strncpy(m.cc, st->comp_cc, sizeof(m.cc) - 1);
    strncpy(m.bcc, st->comp_bcc, sizeof(m.bcc) - 1);
    strncpy(m.subject, st->comp_subject, sizeof(m.subject) - 1);
    strncpy(m.body, st->comp_body, sizeof(m.body) - 1);
    m.att_cnt = st->comp_att_cnt > MAX_ATT ? MAX_ATT : st->comp_att_cnt;
    for (int ai = 0; ai < m.att_cnt; ai++) {
        strncpy(m.att[ai].path, st->comp_att[ai], sizeof(m.att[ai].path) - 1);
        mail_sanitize_name(st->comp_att[ai], m.att[ai].name, sizeof(m.att[ai].name));
        // guess a content type from the file extension
        const char* ext = strrchr(m.att[ai].name, '.');
        strcpy(m.att[ai].type, "application/octet-stream");
        if (ext) {
            if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0 || strcmp(ext, ".log") == 0) strcpy(m.att[ai].type, "text/plain");
            else if (strcmp(ext, ".png") == 0) strcpy(m.att[ai].type, "image/png");
            else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) strcpy(m.att[ai].type, "image/jpeg");
            else if (strcmp(ext, ".gif") == 0) strcpy(m.att[ai].type, "image/gif");
            else if (strcmp(ext, ".pdf") == 0) strcpy(m.att[ai].type, "application/pdf");
            else if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) strcpy(m.att[ai].type, "text/html");
        }
    }
    mail_now(m.date, sizeof(m.date));
    m.read = true;
    if (st->comp_draft_slot >= 0 && st->comp_draft_slot < d.msg_count) {
        d.messages[st->comp_draft_slot] = m;
    } else if (d.msg_count < MAX_MSGS) {
        d.messages[d.msg_count++] = m;
        st->comp_draft_slot = d.msg_count - 1;
    }
    mail_save(st);
    st->view = EV_LIST;
    st->cur_folder = EF_DRAFTS;
    mail_build_view(st);
    // locate the saved draft inside the (possibly re-sorted) visible order
    int vi = 0;
    for (; vi < st->view_cnt; vi++)
        if (st->view_ord[vi] == st->comp_draft_slot) break;
    st->cur_msg = (vi < st->view_cnt) ? vi : 0;
    strcpy(st->status, "Draft saved");
}

static void compose_discard(EmailState* st) {
    if (st->comp_draft_slot >= 0 && st->comp_draft_slot < st->folders[EF_DRAFTS].msg_count)
        mail_remove_msg(st, EF_DRAFTS, st->comp_draft_slot);
    compose_start(st, true);
    st->view = EV_LIST;
    st->status[0] = 0;
}

// ============================== live SMTP / POP3 (host build) ==============================

#if defined(_WIN32) && !defined(NEFU_BARE)

static bool mail_net_started() {
    static bool started = false;
    if (!started) {
        WSADATA wsa;
        started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }
    return started;
}

static SOCKET mail_net_connect(const char* host, int port) {
    if (!mail_net_started()) return INVALID_SOCKET;
    struct addrinfo hints, *res = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char pbuf[16];
    ksprintf(pbuf, sizeof(pbuf), "%d", port);
    if (getaddrinfo(host, pbuf, &hints, &res) != 0) return INVALID_SOCKET;
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); return INVALID_SOCKET; }
    DWORD tmo = 8000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof(tmo));
    if (::connect(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        closesocket(s);
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}

// read one response line (\r\n terminated, \r stripped)
static bool mail_net_line(SOCKET s, char* out, int max) {
    int n = 0;
    while (n < max - 1) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) break;
        if (c == '\n') break;
        if (c != '\r') out[n++] = c;
    }
    out[n] = 0;
    return n > 0;
}

// send a command and read a single-line response (POP3 style, "+OK"/"-ERR")
static bool mail_net_expect_pop(SOCKET s, const char* cmd, char* resp, int rsz) {
    int cl = (int)strlen(cmd);
    if (send(s, cmd, cl, 0) != cl) return false;
    if (send(s, "\r\n", 2, 0) != 2) return false;
    return mail_net_line(s, resp, rsz);
}

// send a command and read the (possibly multi-line) response; returns final code
static bool mail_net_cmd_code(SOCKET s, const char* cmd, int& code) {
    if (cmd) {
        int cl = (int)strlen(cmd);
        if (send(s, cmd, cl, 0) != cl) return false;
        if (send(s, "\r\n", 2, 0) != 2) return false;
    }
    char line[1024];
    for (;;) {
        if (!mail_net_line(s, line, sizeof(line))) return false;
        if ((int)strlen(line) < 4) { code = atoi(line); return true; }
        if (line[3] == '-') continue;      // multi-line continuation
        code = atoi(line);
        return true;
    }
}

static bool mail_net_expect(SOCKET s, const char* cmd, int want) {
    int code;
    if (!mail_net_cmd_code(s, cmd, code)) return false;
    return code == want || (want == 250 && (code == 250 || code == 251));
}

static bool mail_b64(const char* in, char* out, int outsz) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int len = (int)strlen(in), o = 0;
    for (int i = 0; i < len && o + 8 < outsz; i += 3) {
        unsigned char c1 = (unsigned char)in[i];
        unsigned char c2 = (i + 1 < len) ? (unsigned char)in[i + 1] : 0;
        unsigned char c3 = (i + 2 < len) ? (unsigned char)in[i + 2] : 0;
        out[o++] = T[c1 >> 2];
        out[o++] = T[((c1 & 3) << 4) | (c2 >> 4)];
        out[o++] = (i + 1 < len) ? T[((c2 & 15) << 2) | (c3 >> 6)] : '=';
        out[o++] = (i + 2 < len) ? T[c3 & 63] : '=';
    }
    out[o] = 0;
    return o > 0;
}



static bool mail_istarts(const char* line, const char* key) {
    while (*key) {
        char a = *line, b = *key;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
        line++;
        key++;
    }
    return true;
}

// copy the header value of "Key: value" into out
static void mail_hdrval(const char* line, int ln, char* out, int outsz) {
    const char* v = line;
    while (v < line + ln && *v != ':') v++;
    if (v >= line + ln) { out[0] = 0; return; }
    v++;
    while (v < line + ln && (*v == ' ' || *v == '\t')) v++;
    int n = (int)(line + ln - v);
    if (n > outsz - 1) n = outsz - 1;
    if (n > 0) memcpy(out, v, (size_t)n);
    out[n] = 0;
}

// read a POP3 message body (until \r\n.\r\n / \n.\n terminator)
static bool mail_net_data(SOCKET s, char* buf, int max) {
    int n = 0;
    while (n < max - 1) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) break;
        buf[n++] = c;
        if (n >= 5 && buf[n - 5] == '\r' && buf[n - 4] == '\n' &&
            buf[n - 3] == '.' && buf[n - 2] == '\r' && buf[n - 1] == '\n') {
            n -= 5;
            break;
        }
        if (n >= 3 && buf[n - 3] == '\n' && buf[n - 2] == '.' && buf[n - 1] == '\n') {
            n -= 3;
            break;
        }
    }
    buf[n] = 0;
    return n > 0;
}

// extract the boundary= value from a Content-Type header line
static void mail_boundary(const char* ct, int ln, char* out, int outsz) {
    out[0] = 0;
    const char* b = strstr(ct, "boundary=");
    if (!b || b >= ct + ln) return;
    b += 9;
    if (*b == '"') { b++; }
    int o = 0;
    while (*b && *b != '"' && *b != ';' && *b != '\r' && *b != '\n' && o < outsz - 1) out[o++] = *b++;
    out[o] = 0;
}

// find a filename= / name= parameter inside a header value
static void mail_param_name(const char* hv, int ln, char* out, int outsz) {
    out[0] = 0;
    const char* p = strstr(hv, "name=");
    if (!p || p >= hv + ln) p = strstr(hv, "filename=");
    if (!p || p >= hv + ln) return;
    p = strchr(p, '=') + 1;
    if (*p == '"') p++;
    int o = 0;
    while (*p && *p != '"' && *p != ';' && *p != '\r' && *p != '\n' && o < outsz - 1) out[o++] = *p++;
    out[o] = 0;
    // RFC 2047 encoded filename
    char dec[120];
    if (strstr(out, "=?")) { mail_decode_header(out, dec, sizeof(dec)); strncpy(out, dec, outsz - 1); }
}

// decode one MIME body part (base64 / quoted-printable / 8bit / 7bit)
static void mail_part_body(const char* body, int blen, const char* cte,
                           char* out, int outsz) {
    char tmp[8192];
    int n = blen;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    memcpy(tmp, body, (size_t)n);
    tmp[n] = 0;
    if (strstr(cte, "base64")) {
        // strip CR/LF before decoding
        char b64[8192];
        int bo = 0;
        for (int i = 0; i < n && bo < (int)sizeof(b64) - 1; i++)
            if (tmp[i] != '\r' && tmp[i] != '\n') b64[bo++] = tmp[i];
        b64[bo] = 0;
        mail_b64_decode(b64, out, outsz);
    } else if (strstr(cte, "quoted-printable")) {
        mail_qp_decode(tmp, out, outsz);
    } else {
        strncpy(out, tmp, outsz - 1);
        out[outsz - 1] = 0;
    }
}

// parse raw RFC822 message into our model: headers (RFC2047-decoded),
// multipart handling, HTML fallback, attachment detection + VFS save.
static void mail_parse_pop(EmailMessage& m, const char* raw) {
    memset(&m, 0, sizeof(m));
    char from[512] = { 0 }, subj[1024] = { 0 }, to[512] = { 0 }, cc[512] = { 0 };
    char date[128] = { 0 }, mid[128] = { 0 };
    char cte[32] = { 0 }, ctype[128] = { 0 }, bound[96] = { 0 }, dispo[96] = { 0 };
    const char* p = raw;
    bool s_subj = false;
    // ---- header block (with folding) ----
    while (*p && !(*p == '\r' && p[1] == '\n') && !(*p == '\n')) {
        const char* e = p;
        while (*e && *e != '\r' && *e != '\n') e++;
        int ln = (int)(e - p);
        if (ln > 0) {
            if (p[0] != ' ' && p[0] != '\t') {
                if (mail_istarts(p, "From:")) { mail_hdrval(p, ln, from, sizeof(from)); s_subj = false; }
                else if (mail_istarts(p, "Subject:")) { mail_hdrval(p, ln, subj, sizeof(subj)); s_subj = true; }
                else if (mail_istarts(p, "To:")) { mail_hdrval(p, ln, to, sizeof(to)); s_subj = false; }
                else if (mail_istarts(p, "Cc:")) { mail_hdrval(p, ln, cc, sizeof(cc)); s_subj = false; }
                else if (mail_istarts(p, "Date:")) { mail_hdrval(p, ln, date, sizeof(date)); s_subj = false; }
                else if (mail_istarts(p, "Message-ID:")) { mail_hdrval(p, ln, mid, sizeof(mid)); s_subj = false; }
                else if (mail_istarts(p, "Content-Type:")) { mail_hdrval(p, ln, ctype, sizeof(ctype)); }
                else if (mail_istarts(p, "Content-Transfer-Encoding:")) mail_hdrval(p, ln, cte, sizeof(cte));
            } else if (s_subj && strlen(subj) < sizeof(subj) - 80) {
                int cl = (int)strlen(subj);
                if (cl < (int)sizeof(subj) - 1) { subj[cl] = ' '; cl++; }
                if (ln > 1 && cl + ln - 1 < (int)sizeof(subj)) {
                    memcpy(subj + cl, p + 1, (size_t)(ln - 1));
                    subj[cl + ln - 1] = 0;
                }
            }
        }
        p = e;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    if (*p == '\r') p += 2; else if (*p == '\n') p++;
    // decode RFC 2047 words in From / Subject / To / Cc
    char from_d[512], subj_d[1024], to_d[512], cc_d[512];
    mail_decode_header(from, from_d, sizeof(from_d));
    mail_decode_header(subj, subj_d, sizeof(subj_d));
    mail_decode_header(to, to_d, sizeof(to_d));
    mail_decode_header(cc, cc_d, sizeof(cc_d));
    strncpy(m.from, from_d, sizeof(m.from) - 1);
    strncpy(m.subject, subj_d, sizeof(m.subject) - 1);
    strncpy(m.to, to_d, sizeof(m.to) - 1);
    strncpy(m.cc, cc_d, sizeof(m.cc) - 1);
    mail_parse_rfc822_date(date, m.date, sizeof(m.date));
    if (mid[0]) { char t[96]; mail_sanitize_name(mid, t, sizeof(t)); strncpy(m.msgid, t, sizeof(m.msgid) - 1); }
    // ---- body ----
    mail_boundary(ctype, (int)strlen(ctype), bound, sizeof(bound));
    char body[8192];
    int bo = 0;
    if (bound[0] && strstr(ctype, "multipart/")) {
        // walk the raw body for each "--boundary" part
        const char* q = p;
        while (*q && bo < (int)sizeof(body) - 1) {
            const char* nl = strstr(q, "\n");
            if (!nl) break;
            int lln = (int)(nl - q);
            bool isb = false;
            if (lln >= 2 && q[0] == '-' && q[1] == '-') {
                const char* bv = q + 2;
                int bl = lln - 2;
                if (bl == (int)strlen(bound) && strncmp(bv, bound, (size_t)bl) == 0) isb = true;
            }
            if (isb) {
                // check for closing "--boundary--"
                const char* eol = q + 2 + strlen(bound);
                if (eol + 2 <= nl && eol[0] == '-' && eol[1] == '-') break;
                // parse this part's headers
                const char* ph = nl + 1;
                const char* pe = ph;
                while (*pe && !(*pe == '\r' && pe[1] == '\n') && !(*pe == '\n')) {
                    const char* e2 = pe;
                    while (*e2 && *e2 != '\r' && *e2 != '\n') e2++;
                    int p2 = (int)(e2 - pe);
                    if (p2 > 0) {
                        if (mail_istarts(pe, "Content-Type:")) mail_hdrval(pe, p2, ctype, sizeof(ctype));
                        else if (mail_istarts(pe, "Content-Transfer-Encoding:")) mail_hdrval(pe, p2, cte, sizeof(cte));
                        else if (mail_istarts(pe, "Content-Disposition:")) mail_hdrval(pe, p2, dispo, sizeof(dispo));
                    }
                    pe = e2;
                    if (*pe == '\r') pe++;
                    if (*pe == '\n') pe++;
                    if (*pe == '\r' && pe[1] == '\n') break;
                    if (*pe == '\n') break;
                }
                if (*pe == '\r') pe += 2; else if (*pe == '\n') pe++;
                dispo[0] = 0;
                // find where this part ends (next boundary)
                const char* bend = strstr(pe, "\n--");
                if (!bend) bend = pe + strlen(pe);
                int partlen = (int)(bend - pe);
                if (partlen > 16000) partlen = 16000;
                // decide: text part vs attachment
                bool is_att = strstr(dispo, "attachment") != 0 ||
                              (strstr(ctype, "application/") != 0 && !strstr(ctype, "text/")) ||
                              strstr(ctype, "image/") != 0 || strstr(ctype, "video/") != 0 ||
                              strstr(ctype, "audio/") != 0;
                char fname[96] = { 0 };
                if (is_att) mail_param_name(ctype, (int)strlen(ctype), fname, sizeof(fname));
                if (is_att && fname[0] && m.att_cnt < MAX_ATT) {
                    char decoded[20000];
                    mail_part_body(pe, partlen, cte, decoded, sizeof(decoded));
                    EmailAttachment& at = m.att[m.att_cnt];
                    mail_sanitize_name(fname, at.name, sizeof(at.name));
                    at.size = (int)strlen(decoded);
                    // save decoded bytes into /home/user/Mail/attachments/
                    if (g_vfs && at.size > 0) {
                        g_vfs->mkdir("/home/user/Mail/attachments");
                        char ap[160];
                        ksprintf(ap, sizeof(ap), "/home/user/Mail/attachments/%s", at.name);
                        FSNode* fn = g_vfs->resolve(ap);
                        if (!fn) fn = g_vfs->create_file(ap);
                        if (fn) g_vfs->write_file(fn, (const uint8_t*)decoded, (uint32_t)at.size);
                        strncpy(at.path, ap, sizeof(at.path) - 1);
                    }
                    strncpy(at.type, ctype, sizeof(at.type) - 1);
                    m.att_cnt++;
                } else if (strstr(ctype, "text/html") && !m.body[0]) {
                    char plain[7000];
                    mail_part_body(pe, partlen, cte, plain, sizeof(plain));
                    mail_strip_html(plain, m.body, (int)sizeof(m.body));
                    m.has_html = true;
                } else if (strstr(ctype, "text/") && !strstr(ctype, "html") && !m.body[0]) {
                    mail_part_body(pe, partlen, cte, m.body, (int)sizeof(m.body));
                }
                q = bend;
                continue;
            }
            // plain body line (non-multipart fallback handled below)
            q = nl + 1;
        }
    } else {
        // simple body: raw until end, un-dot-stuff
        while (*p && bo < (int)sizeof(body) - 1) {
            if (p[0] == '.' && p[1] == '.') p++;            // un-dot-stuff
            while (*p && *p != '\r' && *p != '\n' && bo < (int)sizeof(body) - 1) body[bo++] = *p++;
            if (*p == '\r' && p[1] == '\n') p += 2; else if (*p == '\n') p++;
            if (bo < (int)sizeof(body) - 1) body[bo++] = '\n';
        }
        if (bo > 0 && body[bo - 1] == '\n') bo--;
        body[bo] = 0;
        char cte2[32];
        strncpy(cte2, cte, sizeof(cte2));
        mail_part_body(body, bo, cte2, m.body, (int)sizeof(m.body));
    }
    m.read = false;
    m.starred = false;
    if (!m.date[0]) mail_now(m.date, sizeof(m.date));
}

// POP3 receive: returns number fetched, -1 on connection error
static int mail_pop3_fetch(EmailState* st, char* err, int errsz) {
    const EmailAccount& a = st->acct;
    SOCKET s = mail_net_connect(a.pop_host, a.pop_port);
    if (s == INVALID_SOCKET) {
        ksprintf(err, errsz, "POP3: cannot reach %s:%d", a.pop_host, a.pop_port);
        return -1;
    }
    char line[512];
    if (!mail_net_line(s, line, sizeof(line)) || strncmp(line, "+OK", 3) != 0) {
        closesocket(s);
        strcpy(err, "POP3: server rejected connection");
        return -1;
    }
    char cmd[256];
    ksprintf(cmd, sizeof(cmd), "USER %s", a.username);
    if (!mail_net_expect_pop(s, cmd, line, sizeof(line)) || strncmp(line, "-ERR", 4) == 0) { closesocket(s); strcpy(err, "POP3: USER rejected"); return -1; }
    ksprintf(cmd, sizeof(cmd), "PASS %s", a.password);
    if (!mail_net_expect_pop(s, cmd, line, sizeof(line)) || strncmp(line, "-ERR", 4) == 0) { closesocket(s); strcpy(err, "POP3: authentication failed"); return -1; }
    if (!mail_net_expect_pop(s, "STAT", line, sizeof(line)) || strncmp(line, "-ERR", 4) == 0) { closesocket(s); strcpy(err, "POP3: STAT failed"); return -1; }
    const char* sp = strchr(line, ' ');
    int total = sp ? atoi(sp + 1) : 0;
    if (total > 99) total = 99;
    int fetched = 0;
    for (int n = 1; n <= total && fetched < 20; n++) {
        char raw[24576];
        ksprintf(cmd, sizeof(cmd), "RETR %d", n);
        if (!mail_net_expect_pop(s, cmd, line, sizeof(line))) continue;
        if (strncmp(line, "-ERR", 4) == 0) continue;
        if (!mail_net_data(s, raw, sizeof(raw))) continue;
        EmailMessage m;
        mail_parse_pop(m, raw);
        if (!m.subject[0] && !m.from[0]) continue;
        bool dup = false;
        EmailFolder& in = st->folders[EF_INBOX];
        for (int i = 0; i < in.msg_count; i++) {
            if (m.msgid[0] && in.messages[i].msgid[0]) {
                if (strcmp(in.messages[i].msgid, m.msgid) == 0) { dup = true; break; }
            } else if (strcmp(in.messages[i].subject, m.subject) == 0 && strcmp(in.messages[i].from, m.from) == 0) {
                dup = true;
                break;
            }
        }
        if (dup || in.msg_count >= MAX_MSGS) continue;
        if (!m.date[0]) mail_now(m.date, sizeof(m.date));
        in.messages[in.msg_count++] = m;
        in.unread_count++;
        fetched++;
    }
    mail_net_expect_pop(s, "QUIT", line, sizeof(line));
    closesocket(s);
    return fetched;
}

// SMTP send: full AUTH LOGIN + DATA exchange with response-code checks.
// Supports To/Cc/Bcc recipient expansion, RFC 2047 subject encoding and
// MIME multipart/mixed attachments read from the VFS.
static bool mail_smtp_send(EmailState* st, char* err, int errsz) {
    const EmailAccount& a = st->acct;
    SOCKET s = mail_net_connect(a.smtp_host, a.smtp_port);
    if (s == INVALID_SOCKET) {
        ksprintf(err, errsz, "SMTP: cannot reach %s:%d", a.smtp_host, a.smtp_port);
        return false;
    }
    char line[1024];
    if (!mail_net_line(s, line, sizeof(line))) { closesocket(s); strcpy(err, "SMTP: no greeting"); return false; }
    int code = atoi(line);
    if (code != 220) { closesocket(s); strcpy(err, "SMTP: server rejected greeting"); return false; }
    if (!mail_net_expect(s, "EHLO nefuos.local", 250)) { closesocket(s); strcpy(err, "SMTP: EHLO failed"); return false; }
    if (!mail_net_expect(s, "AUTH LOGIN", 334)) { closesocket(s); strcpy(err, "SMTP: server does not allow AUTH LOGIN"); return false; }
    char b1[160], b2[160];
    if (!mail_b64(a.username, b1, sizeof(b1))) { closesocket(s); strcpy(err, "SMTP: auth error"); return false; }
    if (!mail_net_expect(s, b1, 334)) { closesocket(s); strcpy(err, "SMTP: username rejected"); return false; }
    if (!mail_b64(a.password, b2, sizeof(b2))) { closesocket(s); strcpy(err, "SMTP: auth error"); return false; }
    if (!mail_net_expect(s, b2, 235)) { closesocket(s); strcpy(err, "SMTP: password rejected"); return false; }

    // ---- expand To/Cc/Bcc into individual RCPT commands ----
    char cmd[320];
    ksprintf(cmd, sizeof(cmd), "MAIL FROM:<%s>", a.email);
    if (!mail_net_expect(s, cmd, 250)) { closesocket(s); strcpy(err, "SMTP: MAIL FROM rejected"); return false; }
    int rcpt_total = 0;
    const char* rcpt_groups[3] = { st->comp_to, st->comp_cc, st->comp_bcc };
    for (int g = 0; g < 3; g++) {
        const char* gs = rcpt_groups[g];
        while (*gs) {
            while (*gs == ' ' || *gs == ',') gs++;
            if (!*gs) break;
            const char* e = gs;
            while (*e && *e != ',') e++;
            int n = (int)(e - gs);
            char one[160];
            if (n > 159) n = 159;
            memcpy(one, gs, (size_t)n);
            one[n] = 0;
            char only[96];
            mail_addr_only(one, only, sizeof(only));
            if (only[0]) {
                ksprintf(cmd, sizeof(cmd), "RCPT TO:<%s>", only);
                if (!mail_net_expect(s, cmd, 250)) {
                    ksprintf(err, errsz, "SMTP: recipient rejected <%s>", only);
                    closesocket(s);
                    return false;
                }
                rcpt_total++;
            }
            gs = *e ? e + 1 : e;
        }
    }
    if (rcpt_total == 0) { closesocket(s); strcpy(err, "SMTP: no recipients"); return false; }
    if (!mail_net_expect(s, "DATA", 354)) { closesocket(s); strcpy(err, "SMTP: DATA rejected"); return false; }

    // ---- build the MIME message ----
    char date[32];
    mail_now(date, sizeof(date));
    // RFC 2047 encode the subject only when it needs it
    char subj_enc[768];
    mail_encode_header(st->comp_subject, subj_enc, sizeof(subj_enc));
    bool has_att = st->comp_att_cnt > 0;
    String msg;
    msg += "From: ";
    if (a.display[0]) { msg += a.display; msg += " <"; msg += a.email; msg += ">"; }
    else msg += a.email;
    msg += "\r\n";
    msg += "To: "; msg += st->comp_to; msg += "\r\n";
    if (st->comp_cc[0]) { msg += "Cc: "; msg += st->comp_cc; msg += "\r\n"; }
    if (st->comp_bcc[0]) { msg += "Bcc: "; msg += st->comp_bcc; msg += "\r\n"; }
    msg += "Subject: "; msg += subj_enc; msg += "\r\n";
    msg += "Date: "; msg += date; msg += "\r\n";
    msg += "MIME-Version: 1.0\r\n";
    if (has_att) {
        msg += "Content-Type: multipart/mixed; boundary=\"nefu_boundary_42\"\r\n\r\n";
        msg += "--nefu_boundary_42\r\n";
    }
    msg += "Content-Type: text/plain; charset=utf-8\r\n";
    msg += "Content-Transfer-Encoding: 8bit\r\n\r\n";
    msg += st->comp_body;
    msg += "\r\n";
    if (has_att) {
        // attach each file from the VFS, base64 encoded
        for (int ai = 0; ai < st->comp_att_cnt && ai < MAX_ATT; ai++) {
            if (!st->comp_att[ai][0]) continue;
            FSNode* fn = g_vfs ? g_vfs->resolve(st->comp_att[ai]) : 0;
            if (!fn || fn->is_dir || fn->size == 0) continue;
            char fname[64];
            mail_sanitize_name(st->comp_att[ai], fname, sizeof(fname));
            // content type guess
            const char* ext = strrchr(fname, '.');
            const char* ctype = "application/octet-stream";
            if (ext) {
                if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0 || strcmp(ext, ".log") == 0) ctype = "text/plain";
                else if (strcmp(ext, ".png") == 0) ctype = "image/png";
                else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) ctype = "image/jpeg";
                else if (strcmp(ext, ".gif") == 0) ctype = "image/gif";
                else if (strcmp(ext, ".pdf") == 0) ctype = "application/pdf";
                else if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) ctype = "text/html";
            }
            msg += "--nefu_boundary_42\r\n";
            msg += "Content-Type: "; msg += ctype; msg += "; name=\""; msg += fname; msg += "\"\r\n";
            msg += "Content-Transfer-Encoding: base64\r\n";
            msg += "Content-Disposition: attachment; filename=\""; msg += fname; msg += "\"\r\n\r\n";
            // base64 with 76-char line wrapping
            const uint8_t* src = (const uint8_t*)fn->data;
            int slen = (int)fn->size;
            int col = 0;
            char b64line[80];
            for (int i = 0; i < slen; i += 3) {
                unsigned char c1 = src[i];
                unsigned char c2 = (i + 1 < slen) ? src[i + 1] : 0;
                unsigned char c3 = (i + 2 < slen) ? src[i + 2] : 0;
                b64line[col++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[c1 >> 2];
                b64line[col++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((c1 & 3) << 4) | (c2 >> 4)];
                b64line[col++] = (i + 1 < slen) ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((c2 & 15) << 2) | (c3 >> 6)] : '=';
                b64line[col++] = (i + 2 < slen) ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[c3 & 63] : '=';
                if (col >= 76) {
                    b64line[col] = 0;
                    msg += b64line;
                    msg += "\r\n";
                    col = 0;
                }
            }
            if (col > 0) {
                b64line[col] = 0;
                msg += b64line;
                msg += "\r\n";
            }
        }
        msg += "--nefu_boundary_42--\r\n";
    }
    // normalize line endings to CRLF + dot-stuff leading '.' lines
    String out;
    out.reserve(msg.len() + 64);
    const char* q = msg.c_str();
    while (*q) {
        const char* nl = strchr(q, '\n');
        int ln = nl ? (int)(nl - q) : (int)strlen(q);
        if (ln > 0 && q[ln - 1] == '\r') ln--;
        if (ln > 0 && q[0] == '.') out += ".";
        if (ln > 0) out += String(q, ln);
        out += "\r\n";
        if (!nl) break;
        q = nl + 1;
    }
    out += ".\r\n";
    if (send(s, out.c_str(), (int)out.len(), 0) != (int)out.len()) {
        closesocket(s); strcpy(err, "SMTP: data send failed"); return false;
    }
    if (!mail_net_line(s, line, sizeof(line))) { closesocket(s); strcpy(err, "SMTP: no data reply"); return false; }
    code = atoi(line);
    if (code != 250) { closesocket(s); strcpy(err, "SMTP: message rejected by server"); return false; }
    mail_net_cmd_code(s, "QUIT", code);
    closesocket(s);
    return true;
}

#endif // host build

// ============================== send / refresh entry points ==============================

static void mail_net_status(EmailState* st) {
#if defined(_WIN32) && !defined(NEFU_BARE)
    NetAdapterInfo ai;
    st->net_up = platform_net_get(&ai) && ai.up;
#else
    st->net_up = g_net.up;
#endif
}

static void mail_send_current(EmailState* st) {
    if (!st->comp_to[0]) { strcpy(st->status, "Add a recipient first"); return; }
#if defined(_WIN32) && !defined(NEFU_BARE)
    char err[96];
    err[0] = 0;
    if (st->acct.configured) {
        if (!mail_smtp_send(st, err, sizeof(err))) {
            strncpy(st->status, err, sizeof(st->status) - 1);
            return;
        }
        strcpy(st->status, "Sent via SMTP");
    } else {
        strcpy(st->status, "No SMTP account - saved to Sent locally");
    }
#else
    strcpy(st->status, "Bare build: live SMTP unavailable - saved to Sent");
#endif
    EmailFolder& sn = st->folders[EF_SENT];
    if (sn.msg_count < MAX_MSGS) {
        EmailMessage m;
        memset(&m, 0, sizeof(m));
        strncpy(m.from, st->acct.email[0] ? st->acct.email : "user@nefuos.dev", sizeof(m.from) - 1);
        strncpy(m.to, st->comp_to, sizeof(m.to) - 1);
        strncpy(m.cc, st->comp_cc, sizeof(m.cc) - 1);
        strncpy(m.bcc, st->comp_bcc, sizeof(m.bcc) - 1);
        strncpy(m.subject, st->comp_subject, sizeof(m.subject) - 1);
        strncpy(m.body, st->comp_body, sizeof(m.body) - 1);
        m.att_cnt = st->comp_att_cnt > MAX_ATT ? MAX_ATT : st->comp_att_cnt;
        for (int ai = 0; ai < m.att_cnt; ai++) {
            strncpy(m.att[ai].path, st->comp_att[ai], sizeof(m.att[ai].path) - 1);
            mail_sanitize_name(st->comp_att[ai], m.att[ai].name, sizeof(m.att[ai].name));
            strcpy(m.att[ai].type, "application/octet-stream");
            const char* ext = strrchr(m.att[ai].name, '.');
            if (ext) {
                if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0 || strcmp(ext, ".log") == 0) strcpy(m.att[ai].type, "text/plain");
                else if (strcmp(ext, ".png") == 0) strcpy(m.att[ai].type, "image/png");
                else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) strcpy(m.att[ai].type, "image/jpeg");
                else if (strcmp(ext, ".pdf") == 0) strcpy(m.att[ai].type, "application/pdf");
            }
            FSNode* fn = g_vfs ? g_vfs->resolve(st->comp_att[ai]) : 0;
            m.att[ai].size = (fn && !fn->is_dir) ? (int)fn->size : 0;
        }
        mail_now(m.date, sizeof(m.date));
        m.read = true;
        sn.messages[sn.msg_count++] = m;
    }
    if (st->comp_draft_slot >= 0 && st->comp_draft_slot < st->folders[EF_DRAFTS].msg_count)
        mail_remove_msg(st, EF_DRAFTS, st->comp_draft_slot);
    compose_start(st, true);
    st->view = EV_LIST;
    st->cur_folder = EF_SENT;
    mail_build_view(st);
    // the just-sent message sorts to the top (newest first)
    st->cur_msg = st->view_cnt > 0 ? 0 : 0;
    mail_rebuild_star(st);
    mail_build_view(st);
    mail_save(st);
}

static void mail_refresh(EmailState* st) {
#if defined(_WIN32) && !defined(NEFU_BARE)
    if (st->acct.configured && st->acct.pop_host[0]) {
        char err[96];
        int n = mail_pop3_fetch(st, err, sizeof(err));
        if (n >= 0) {
            char buf[64];
            ksprintf(buf, sizeof(buf), "POP3: %d new message(s)", n);
            strcpy(st->status, buf);
            mail_rebuild_star(st);
            mail_save(st);
        } else strncpy(st->status, err, sizeof(st->status) - 1);
    } else strcpy(st->status, "No POP3 account - configure one in Account");
#else
    strcpy(st->status, "POP3 live receive is host-build only");
#endif
}

// ============================== painting ==============================

static void mail_paint_sidebar(EmailState* st, Surface& s, int H) {
    gfx::fillrect(s, 0, 0, SW, H, 0x2C3E50);
    gfx::text(s, 10, 8, "nefu Mail", 0xFFFFFF, 0x2C3E50);
    const char* email = st->acct.configured ? st->acct.email : "no account";
    gfx::text(s, 10, 26, email, 0x8FAABC, 0x2C3E50);
    for (int i = 0; i < EF_COUNT; i++) {
        int y = 52 + i * 32;
        bool act = (st->view == EV_ACCOUNT) ? false : (st->cur_folder == i);
        uint32_t bg = act ? 0x34495E : 0x2C3E50;
        gfx::fillrect(s, 6, y, SW - 12, 28, bg);
        if (act) gfx::fillrect(s, 6, y, 3, 28, 0x3498DB);
        gfx::text(s, 16, y + 6, st->folders[i].name, 0xECF0F1, bg);
        int u = (i == EF_STARRED) ? st->star_unread : st->folders[i].unread_count;
        if (u > 0) {
            char ub[8];
            ksprintf(ub, sizeof(ub), "%d", u);
            int bw = gfx::text_width(ub) + 10;
            int bx = SW - 12 - bw;
            gfx::fillrect(s, bx, y + 7, bw, 14, 0xE74C3C);
            gfx::text(s, bx + 5, y + 9, ub, 0xFFFFFF, 0xE74C3C);
        }
    }
    // Account row (opens the account editor)
    int ay = 52 + EF_COUNT * 32;
    uint32_t abg = (st->view == EV_ACCOUNT) ? 0x34495E : 0x2C3E50;
    gfx::fillrect(s, 6, ay, SW - 12, 28, abg);
    if (st->view == EV_ACCOUNT) gfx::fillrect(s, 6, ay, 3, 28, 0x3498DB);
    gfx::text(s, 16, ay + 6, "Account", 0xECF0F1, abg);
    // network state at the bottom of the sidebar
    gfx::text(s, 10, H - STATUS_H - 2, st->net_up ? "Online" : "Offline",
              st->net_up ? 0x2ECC71 : 0xE74C3C, 0x2C3E50);
}

static void mail_paint_toolbar(Surface& s, int W) {
    gfx::fillrect(s, SW, 0, W - SW, TOOL_H, 0xECF0F1);
}

static const char* mail_sort_label(int mode) {
    switch (mode) {
        case 1:  return "Date/up";
        case 2:  return "From";
        case 3:  return "Subj";
        default: return "Date/dn";
    }
}

static void mail_toggle_sort(EmailState* st) {
    st->sort_mode = (st->sort_mode + 1) % 4;
    mail_build_view(st);
    mail_clamp_list(st);
}

static void mail_mark_all_read(EmailState* st) {
    EmailFolder& fo = st->folders[st->cur_folder];
    for (int i = 0; i < fo.msg_count; i++) fo.messages[i].read = true;
    fo.unread_count = 0;
    mail_rebuild_star(st);
    mail_build_view(st);
    mail_save(st);
    strcpy(st->status, "Folder marked as read");
}

static void mail_empty_trash(EmailState* st) {
    st->folders[EF_TRASH].msg_count = 0;
    st->folders[EF_TRASH].unread_count = 0;
    mail_rebuild_star(st);
    mail_build_view(st);
    mail_save(st);
    st->cur_msg = 0;
    strcpy(st->status, "Trash emptied");
}

static void mail_paint_list(EmailState* st, Surface& s, int W, int H) {
    mail_paint_toolbar(s, W);
    int x = SW + 6;
    mail_btn(s, x, 5, 62, 22, "Compose", 0x3498DB); x += 68;
    mail_btn(s, x, 5, 62, 22, "Refresh", 0x27AE60); x += 68;
    mail_draw_input(s, x, 5, 118, 22, st->search, (int)strlen(st->search), st->search_focus, false);
    x += 124;
    mail_btn(s, x, 5, 62, 22, mail_sort_label(st->sort_mode), 0x8E44AD);
    char cnt[48];
    int n = vis_total(st);
    ksprintf(cnt, sizeof(cnt), "%d", n);
    gfx::text(s, W - gfx::text_width(cnt) - 10, 9, cnt, 0x2C3E50, 0xECF0F1);

    // second toolbar row: folder-scoped bulk actions
    int ly = TOOL_H;
    gfx::fillrect(s, SW, ly, W - SW, 24, 0xF4F6F7);
    gfx::fillrect(s, SW, ly + 23, W - SW, 1, 0xE0E3E5);
    if (st->cur_folder == EF_INBOX || st->cur_folder == EF_STARRED) {
        mail_btn(s, SW + 6, ly + 1, 92, 22, "Mark All Read", 0x2980B9);
    } else if (st->cur_folder == EF_TRASH) {
        mail_btn(s, SW + 6, ly + 1, 92, 22, "Empty Trash", 0xE74C3C);
    } else {
        gfx::text(s, SW + 10, ly + 5, "Sort: click the toolbar button to cycle Date / From / Subject", 0x95A5A6, 0xF4F6F7);
    }

    int y = ly + 26;
    int listh = H - y - STATUS_H;
    int maxrows = listh / ROW_H;
    int from = st->list_scroll;
    if (from > n - maxrows) from = n - maxrows;
    if (from < 0) from = 0;
    for (int i = from; i < n && i < from + maxrows; i++) {
        EmailMessage* m = vis_msg(st, i);
        if (!m) continue;
        int ry = y + (i - from) * ROW_H;
        bool sel = (i == st->cur_msg);
        uint32_t bg = sel ? 0xDBE9F4 : 0xFFFFFF;
        gfx::fillrect(s, SW, ry, W - SW, ROW_H, bg);
        if (sel) gfx::fillrect(s, SW, ry, 3, ROW_H, 0x3498DB);
        // star
        if (m->starred) gfx::fillcircle(s, SW + 14, ry + ROW_H / 2, 5, 0xF39C12);
        else gfx::circle(s, SW + 14, ry + ROW_H / 2, 5, 0x95A5A6);
        // unread dot
        if (!m->read) gfx::fillcircle(s, SW + 32, ry + ROW_H / 2, 4, 0xE74C3C);
        // paperclip icon when the message carries attachments
        int sx = SW + 44;
        if (m->att_cnt > 0) {
            gfx::fillcircle(s, sx + 3, ry + 7, 2, 0x2980B9);
            gfx::fillrect(s, sx + 2, ry + 9, 2, 5, 0x2980B9);
            sx += 10;
        }
        mail_text_clip(s, sx, ry + 7, m->from, 128 - (sx - SW - 44), m->read ? 0x7F8C8D : 0x2C3E50, bg);
        mail_text_clip(s, SW + 178, ry + 7, m->subject, W - SW - 178 - 84, m->read ? 0x7F8C8D : 0x2C3E50, bg);
        gfx::text(s, W - gfx::text_width(m->date) - 12, ry + 7, m->date, 0x95A5A6, bg);
    }
    // thin scrollbar when the list overflows
    if (n > maxrows) {
        int bh = (maxrows * listh) / n;
        if (bh < 14) bh = 14;
        int by = y + from * (listh - bh) / (n - maxrows > 0 ? n - maxrows : 1);
        gfx::fillrect(s, W - 5, by, 3, bh, 0xBDC3C7);
    }
}

static void mail_paint_read(EmailState* st, Surface& s, int W, int H) {
    mail_paint_toolbar(s, W);
    int x = SW + 6;
    mail_btn(s, x, 5, 44, 22, "Back", 0x95A5A6); x += 50;
    mail_btn(s, x, 5, 52, 22, "Reply", 0x3498DB); x += 58;
    mail_btn(s, x, 5, 64, 22, "Reply All", 0x3498DB); x += 70;
    mail_btn(s, x, 5, 52, 22, "Fwd", 0x3498DB); x += 58;
    mail_btn(s, x, 5, 56, 22, "Delete", 0xE74C3C); x += 62;
    EmailMessage* m = vis_msg(st, st->cur_msg);
    if (m) {
        mail_btn(s, x, 5, 56, 22, m->starred ? "Unstar" : "Star", 0xF39C12); x += 62;
        mail_btn(s, x, 5, 60, 22, m->read ? "Unread" : "Read", 0x8E44AD); x += 66;
        mail_btn(s, x, 5, 58, 22, st->view_source ? "Plain" : "Source", 0x7F8C8D);
        int yy = TOOL_H + 10;
        gfx::text(s, SW + 10, yy, m->subject, 0x2C3E50, 0xFFFFFF); yy += 20;
        if (st->view_source) {
            // full header block reconstructed from the model, one line each
            char h1[320], h2[320], h3[320], h4[320];
            ksprintf(h1, sizeof(h1), "From: %s", m->from);
            ksprintf(h2, sizeof(h2), "To: %s", m->to);
            if (m->cc[0]) ksprintf(h3, sizeof(h3), "Cc: %s", m->cc);
            else h3[0] = 0;
            ksprintf(h4, sizeof(h4), "Date: %s", m->date);
            mail_text_clip(s, SW + 10, yy, h1, W - SW - 20, 0x34495E, 0xFFFFFF); yy += 16;
            mail_text_clip(s, SW + 10, yy, h2, W - SW - 20, 0x34495E, 0xFFFFFF); yy += 16;
            if (h3[0]) { mail_text_clip(s, SW + 10, yy, h3, W - SW - 20, 0x34495E, 0xFFFFFF); yy += 16; }
            mail_text_clip(s, SW + 10, yy, h4, W - SW - 20, 0x34495E, 0xFFFFFF); yy += 16;
            if (m->msgid[0]) {
                char h5[320];
                ksprintf(h5, sizeof(h5), "Message-ID: %s", m->msgid);
                mail_text_clip(s, SW + 10, yy, h5, W - SW - 20, 0x34495E, 0xFFFFFF); yy += 16;
            }
            {
                char h6[128];
                ksprintf(h6, sizeof(h6), "Content-Type: %s", m->has_html ? "text/html" : "text/plain");
                mail_text_clip(s, SW + 10, yy, h6, W - SW - 20, 0x34495E, 0xFFFFFF); yy += 16;
            }
            if (m->att_cnt > 0) {
                char h7[128];
                ksprintf(h7, sizeof(h7), "Attachments: %d", m->att_cnt);
                mail_text_clip(s, SW + 10, yy, h7, W - SW - 20, 0x34495E, 0xFFFFFF); yy += 16;
            }
            yy += 6;
        } else {
            gfx::text(s, SW + 10, yy, m->from, 0x34495E, 0xFFFFFF); yy += 16;
            gfx::text(s, SW + 10, yy, m->to, 0x7F8C8D, 0xFFFFFF); yy += 16;
            if (m->cc[0]) { gfx::text(s, SW + 10, yy, m->cc, 0x7F8C8D, 0xFFFFFF); yy += 16; }
            gfx::text(s, SW + 10, yy, m->date, 0x95A5A6, 0xFFFFFF); yy += 22;
        }
        gfx::fillrect(s, SW + 8, yy, W - SW - 16, 1, 0xDDDDDD);
        // attachment strip
        if (m->att_cnt > 0 && !st->view_source) {
            int ay = yy + 6;
            for (int ai = 0; ai < m->att_cnt && ai < MAX_ATT; ai++) {
                char al[192];
                ksprintf(al, sizeof(al), "  [%s]  %s  (%d bytes)",
                          m->att[ai].type[0] ? m->att[ai].type : "file",
                          m->att[ai].name, m->att[ai].size);
                gfx::text(s, SW + 10, ay, al, 0x2980B9, 0xFFFFFF);
                ay += 16;
            }
            yy = ay + 4;
            gfx::fillrect(s, SW + 8, yy, W - SW - 16, 1, 0xDDDDDD);
        }
        mail_draw_body(s, SW + 8, yy + 8, W - SW - 16, H - yy - 8 - STATUS_H, m->body, -1, st->read_scroll, false);
    }
}

// ---- attachment file picker (modal) ----
// lists /home/user (and subdirectories) so the user can attach VFS files.

static void mail_open_picker(EmailState* st, const char* dir) {
    strncpy(st->picker_dir, dir, sizeof(st->picker_dir) - 1);
    st->picker_cnt = 0;
    st->picker_scroll = 0;
    st->picker_sel = 0;
    FSNode* n = g_vfs ? g_vfs->resolve(dir) : 0;
    if (n && n->is_dir) {
        for (int i = 0; i < (int)n->children.size() && st->picker_cnt < 64; i++) {
            FSNode* c = n->children[i];
            if (c && c->is_dir) {
                strncpy(st->picker_ents[st->picker_cnt], c->name.c_str(), 95);
                st->picker_ents[st->picker_cnt][95] = 0;
                st->picker_isdir[st->picker_cnt] = 1;
                st->picker_cnt++;
            }
        }
        for (int i = 0; i < (int)n->children.size() && st->picker_cnt < 64; i++) {
            FSNode* c = n->children[i];
            if (c && !c->is_dir) {
                strncpy(st->picker_ents[st->picker_cnt], c->name.c_str(), 95);
                st->picker_ents[st->picker_cnt][95] = 0;
                st->picker_isdir[st->picker_cnt] = 0;
                st->picker_cnt++;
            }
        }
    }
    st->picker_open = true;
}

// attach the file currently selected in the picker (if any)
static void mail_picker_attach(EmailState* st) {
    if (st->picker_sel < 0 || st->picker_sel >= st->picker_cnt) return;
    if (st->picker_isdir[st->picker_sel]) {
        // descend into the selected directory
        char nd[160];
        if (st->picker_dir[1] == 0 && st->picker_dir[0] == '/')
            ksprintf(nd, sizeof(nd), "/%s", st->picker_ents[st->picker_sel]);
        else
            ksprintf(nd, sizeof(nd), "%s/%s", st->picker_dir, st->picker_ents[st->picker_sel]);
        mail_open_picker(st, nd);
        return;
    }
    if (st->comp_att_cnt >= MAX_ATT) {
        strcpy(st->status, "Attachment limit reached (4)");
        return;
    }
    char fp[160];
    if (st->picker_dir[1] == 0 && st->picker_dir[0] == '/')
        ksprintf(fp, sizeof(fp), "/%s", st->picker_ents[st->picker_sel]);
    else
        ksprintf(fp, sizeof(fp), "%s/%s", st->picker_dir, st->picker_ents[st->picker_sel]);
    // refuse double-adding the same path
    for (int i = 0; i < st->comp_att_cnt; i++)
        if (strcmp(st->comp_att[i], fp) == 0) { strcpy(st->status, "Already attached"); return; }
    strncpy(st->comp_att[st->comp_att_cnt], fp, 95);
    st->comp_att[st->comp_att_cnt][95] = 0;
    st->comp_att_cnt++;
    st->picker_open = false;
    char msg[192];
    ksprintf(msg, sizeof(msg), "Attached %s", st->picker_ents[st->picker_sel]);
    strncpy(st->status, msg, sizeof(st->status) - 1);
}

static void mail_paint_picker(EmailState* st, Surface& s, int W, int H) {
    // dim the background
    gfx::fillrect(s, SW, TOOL_H, W - SW, H - TOOL_H - STATUS_H, 0xFFFFFF);
    int px = SW + 40, py = 44, pw = W - SW - 80, ph = H - 44 - STATUS_H - 60;
    if (pw < 120) pw = 120;
    if (ph < 120) ph = 120;
    gfx::fillrect(s, px, py, pw, ph, 0xF4F6F7);
    gfx::rect(s, px, py, pw, ph, 0x95A5A6);
    char title[160];
    ksprintf(title, sizeof(title), "Attach file - %s", st->picker_dir);
    gfx::text(s, px + 10, py + 6, title, 0x2C3E50, 0xF4F6F7);
    mail_btn(s, px + pw - 66, py + 3, 56, 20, "Cancel", 0xE74C3C);
    // up one level row
    int y = py + 30;
    gfx::fillrect(s, px + 4, y, pw - 8, 20, 0xE8ECEF);
    gfx::text(s, px + 12, y + 2, "[..] up one level", 0x2980B9, 0xE8ECEF);
    // entries
    int maxrows = (ph - 34) / 20;
    int from = st->picker_scroll;
    if (from > st->picker_cnt - maxrows) from = st->picker_cnt - maxrows;
    if (from < 0) from = 0;
    for (int i = from; i < st->picker_cnt && i < from + maxrows; i++) {
        int ry = y + 22 + (i - from) * 20;
        uint32_t bg = (i == st->picker_sel) ? 0xDBE9F4 : 0xF4F6F7;
        gfx::fillrect(s, px + 4, ry, pw - 8, 20, bg);
        char label[110];
        if (st->picker_isdir[i]) ksprintf(label, sizeof(label), "[dir] %s", st->picker_ents[i]);
        else ksprintf(label, sizeof(label), "      %s", st->picker_ents[i]);
        gfx::text(s, px + 10, ry + 2, label, st->picker_isdir[i] ? 0x2980B9 : 0x34495E, bg);
    }
    gfx::text(s, px + 10, py + ph - 18, "Click a file to attach, or a folder to open it.", 0x7F8C8D, 0xF4F6F7);
}

static void mail_paint_compose(EmailState* st, Surface& s, int W, int H) {
    mail_paint_toolbar(s, W);
    int x = SW + 6;
    mail_btn(s, x, 5, 62, 22, "Send", 0x27AE60); x += 68;
    mail_btn(s, x, 5, 78, 22, "Save Draft", 0xE67E22); x += 84;
    mail_btn(s, x, 5, 62, 22, "Discard", 0x95A5A6); x += 68;
    mail_btn(s, x, 5, 62, 22, "Attach", 0x8E44AD);
    gfx::text(s, W - gfx::text_width("Compose") - 10, 9, "Compose", 0x2C3E50, 0xECF0F1);

    int fx = SW + 64;
    int fw = W - SW - 64 - 10;
    gfx::text(s, SW + 10, 12, "To:", 0x34495E, 0xECF0F1);
    mail_draw_input(s, fx, 8, fw, 24, st->comp_to, st->comp_cursor[0], st->comp_field == 0, false);
    gfx::text(s, SW + 10, 40, "Cc:", 0x34495E, 0xECF0F1);
    mail_draw_input(s, fx, 36, fw, 24, st->comp_cc, st->comp_cursor[1], st->comp_field == 1, false);
    gfx::text(s, SW + 10, 68, "Bcc:", 0x34495E, 0xECF0F1);
    mail_draw_input(s, fx, 64, fw, 24, st->comp_bcc, st->comp_cursor[2], st->comp_field == 2, false);
    gfx::text(s, SW + 10, 96, "Subj:", 0x34495E, 0xECF0F1);
    mail_draw_input(s, fx, 92, fw, 24, st->comp_subject, st->comp_cursor[3], st->comp_field == 3, false);
    int by = 120;
    int bh = H - by - STATUS_H - 8;
    if (st->comp_att_cnt > 0) bh -= 18;
    mail_draw_body(s, SW + 10, by, W - SW - 20, bh,
                   st->comp_body, st->comp_field == 4 ? st->comp_cursor[4] : -1, st->comp_body_scroll, true);
    // attachment strip under the body
    if (st->comp_att_cnt > 0) {
        int ay = by + bh + 2;
        gfx::fillrect(s, SW, ay, W - SW, 18, 0xF4F6F7);
        String al;
        al += "Att: ";
        for (int i = 0; i < st->comp_att_cnt; i++) {
            char nm[64];
            mail_sanitize_name(st->comp_att[i], nm, sizeof(nm));
            if (i > 0) al += ", ";
            al += nm;
        }
        mail_text_clip(s, SW + 8, ay + 1, al.c_str(), W - SW - 16, 0x8E44AD, 0xF4F6F7);
    }
    // modal attachment picker overlays the compose area
    if (st->picker_open) mail_paint_picker(st, s, W, H);
}

static const char* ACC_LABELS[9] = {
    "Display name", "Email address", "SMTP server", "SMTP port",
    "POP3 server", "POP3 port", "Username", "Password", "Signature"
};

static void mail_paint_account(EmailState* st, Surface& s, int W) {
    mail_paint_toolbar(s, W);
    int x = SW + 6;
    mail_btn(s, x, 5, 62, 22, "Save", 0x27AE60); x += 68;
    mail_btn(s, x, 5, 62, 22, "Back", 0x95A5A6); x += 68;
    mail_btn(s, x, 5, 108, 22, "Test Conn", 0x8E44AD);
    gfx::text(s, W - gfx::text_width("Account") - 10, 9, "Account", 0x2C3E50, 0xECF0F1);
    int y = 44;
    for (int i = 0; i < 9; i++) {
        gfx::text(s, SW + 12, y + 5, ACC_LABELS[i], 0x34495E, 0xFFFFFF);
        bool mask = (i == 7);
        int fw = W - SW - 150 - 12;
        if (i == 8) {   // signature gets a taller, multi-purpose input
            mail_draw_input(s, SW + 150, y, fw, 24,
                            st->acc_buf[i],
                            (int)strlen(st->acc_buf[i]),
                            st->acc_field == i, false);
        } else {
            mail_draw_input(s, SW + 150, y, fw, 24,
                            mask && st->acc_field != 7 ? "********" : st->acc_buf[i],
                            mask && st->acc_field != 7 ? 0 : (int)strlen(st->acc_buf[i]),
                            st->acc_field == i, mask);
        }
        y += 30;
    }
    y += 6;
    gfx::text(s, SW + 12, y, "Live SMTP/POP3 works on the host build (no TLS).", 0x7F8C8D, 0xFFFFFF);
    y += 16;
    gfx::text(s, SW + 12, y, "Mail is stored in /home/user/Mail. Signature is appended to new mail.", 0x7F8C8D, 0xFFFFFF);
}

static void email_paint(Window* w) {
    EmailState* st = (EmailState*)w->userdata;
    if (!st) return;
    Surface& s = w->back;
    int W = w->content_w, H = w->content_h;
    mail_paint_sidebar(st, s, H);
    switch (st->view) {
        case EV_LIST:     mail_paint_list(st, s, W, H); break;
        case EV_READ:     mail_paint_read(st, s, W, H); break;
        case EV_COMPOSE:  mail_paint_compose(st, s, W, H); break;
        case EV_ACCOUNT:  mail_paint_account(st, s, W); break;
        default: break;
    }
    gfx::fillrect(s, SW, H - STATUS_H, W - SW, STATUS_H, 0x34495E);
    gfx::text(s, SW + 8, H - STATUS_H + 2, st->status, 0xECF0F1, 0x34495E);
}

// ============================== mouse ==============================

static void email_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    EmailState* st = (EmailState*)w->userdata;
    if (!st) return;
    int W = w->content_w, H = w->content_h;

    // sidebar
    if (mx < SW) {
        for (int i = 0; i < EF_COUNT; i++) {
            int y = 52 + i * 32;
            if (my >= y && my < y + 28) {
                st->cur_folder = i;
                st->cur_msg = 0;
                st->list_scroll = 0;
                st->search[0] = 0;
                if (i == EF_STARRED) mail_rebuild_star(st);
                mail_build_view(st);
                st->view = EV_LIST;
                st->status[0] = 0;
                email_paint(w);
                return;
            }
        }
        int ay = 52 + EF_COUNT * 32;
        if (my >= ay && my < ay + 28) {
            // copy current account into the edit buffers
            strncpy(st->acc_buf[0], st->acct.display, sizeof(st->acc_buf[0]) - 1);
            strncpy(st->acc_buf[1], st->acct.email, sizeof(st->acc_buf[1]) - 1);
            strncpy(st->acc_buf[2], st->acct.smtp_host, sizeof(st->acc_buf[2]) - 1);
            ksprintf(st->acc_buf[3], sizeof(st->acc_buf[3]), "%d", st->acct.smtp_port);
            strncpy(st->acc_buf[4], st->acct.pop_host, sizeof(st->acc_buf[4]) - 1);
            ksprintf(st->acc_buf[5], sizeof(st->acc_buf[5]), "%d", st->acct.pop_port);
            strncpy(st->acc_buf[6], st->acct.username, sizeof(st->acc_buf[6]) - 1);
            strncpy(st->acc_buf[7], st->acct.password, sizeof(st->acc_buf[7]) - 1);
            strncpy(st->acc_buf[8], st->acct.signature, sizeof(st->acc_buf[8]) - 1);
            st->acc_field = 0;
            st->view = EV_ACCOUNT;
            email_paint(w);
        }
        return;
    }

    switch (st->view) {
        case EV_LIST: {
            int x = SW + 6;
            if (mail_in(mx, my, x, 5, 62, 22)) { compose_start(st, true); break; }
            x += 68;
            if (mail_in(mx, my, x, 5, 62, 22)) { mail_refresh(st); break; }
            x += 68;
            if (mail_in(mx, my, x, 5, 118, 22)) { st->search_focus = true; break; }
            x += 124;
            if (mail_in(mx, my, x, 5, 62, 22)) { mail_toggle_sort(st); break; }
            st->search_focus = false;
            // second toolbar row
            int ly = TOOL_H;
            if ((st->cur_folder == EF_INBOX || st->cur_folder == EF_STARRED) &&
                mail_in(mx, my, SW + 6, ly + 1, 92, 22)) { mail_mark_all_read(st); break; }
            if (st->cur_folder == EF_TRASH &&
                mail_in(mx, my, SW + 6, ly + 1, 92, 22)) { mail_empty_trash(st); break; }
            int y = ly + 26;
            int listh = H - y - STATUS_H;
            int maxrows = listh / ROW_H;
            int from = st->list_scroll;
            int n = vis_total(st);
            if (from > n - maxrows) from = n - maxrows;
            if (from < 0) from = 0;
            for (int i = from; i < n && i < from + maxrows; i++) {
                int ry = y + (i - from) * ROW_H;
                if (my >= ry && my < ry + ROW_H) {
                    if (mx >= SW && mx < SW + 26) {       // star column
                        st->cur_msg = i;
                        mail_toggle_star(st);
                    } else {
                        st->cur_msg = i;
                        if (st->cur_folder == EF_DRAFTS) {
                            int f, idx;
                            vis_src(st, i, f, idx);
                            compose_from_draft(st, idx);
                        } else mail_open_msg(st);
                    }
                    break;
                }
            }
            break;
        }
        case EV_READ: {
            int x = SW + 6;
            if (mail_in(mx, my, x, 5, 44, 22)) { st->view = EV_LIST; break; }
            x += 50;
            if (mail_in(mx, my, x, 5, 52, 22)) { compose_reply(st); break; }
            x += 58;
            if (mail_in(mx, my, x, 5, 64, 22)) { compose_reply_all(st); break; }
            x += 70;
            if (mail_in(mx, my, x, 5, 52, 22)) { compose_forward(st); break; }
            x += 58;
            if (mail_in(mx, my, x, 5, 56, 22)) { mail_delete_current(st); break; }
            x += 62;
            EmailMessage* m = vis_msg(st, st->cur_msg);
            if (m) {
                if (mail_in(mx, my, x, 5, 56, 22)) { mail_toggle_star(st); break; }
                x += 62;
                if (mail_in(mx, my, x, 5, 60, 22)) { mail_toggle_read(st); break; }
                x += 66;
                if (mail_in(mx, my, x, 5, 58, 22)) {
                    st->view_source = !st->view_source;
                    st->read_scroll = 0;
                    break;
                }
                // attachment rows: report where the file lives
                int yy = TOOL_H + 10;
                yy += 20;
                yy += 16;              // from
                yy += 16;              // to
                if (m->cc[0]) yy += 16;
                yy += 22;              // date
                yy += 1;               // divider
                if (m->att_cnt > 0) {
                    int ay = yy + 6;
                    for (int ai = 0; ai < m->att_cnt && ai < MAX_ATT; ai++) {
                        if (my >= ay && my < ay + 16) {
                            char msg[256];
                            if (m->att[ai].path[0])
                                ksprintf(msg, sizeof(msg), "Saved: %s", m->att[ai].path);
                            else
                                ksprintf(msg, sizeof(msg), "Attachment %s (not persisted)", m->att[ai].name);
                            strncpy(st->status, msg, sizeof(st->status) - 1);
                            email_paint(w);
                            return;
                        }
                        ay += 16;
                    }
                }
            }
            break;
        }
        case EV_COMPOSE: {
            if (st->picker_open) {
                // picker modal: Cancel, up-one-level, entries
                int px = SW + 40, py = 44, pw = W - SW - 80, ph = H - 44 - STATUS_H - 60;
                if (pw < 120) pw = 120;
                if (ph < 120) ph = 120;
                if (mail_in(mx, my, px + pw - 66, py + 3, 56, 20)) { st->picker_open = false; break; }
                int y = py + 30;
                if (mail_in(mx, my, px + 4, y, pw - 8, 20)) {
                    // go up one level
                    char parent[160];
                    const char* slash = strrchr(st->picker_dir, '/');
                    if (slash && slash != st->picker_dir) {
                        int n = (int)(slash - st->picker_dir);
                        if (n > 158) n = 158;
                        memcpy(parent, st->picker_dir, (size_t)n);
                        parent[n] = 0;
                    } else strcpy(parent, "/");
                    mail_open_picker(st, parent);
                    break;
                }
                int maxrows = (ph - 34) / 20;
                int from = st->picker_scroll;
                if (from > st->picker_cnt - maxrows) from = st->picker_cnt - maxrows;
                if (from < 0) from = 0;
                for (int i = from; i < st->picker_cnt && i < from + maxrows; i++) {
                    int ry = y + 22 + (i - from) * 20;
                    if (mail_in(mx, my, px + 4, ry, pw - 8, 20)) {
                        st->picker_sel = i;
                        mail_picker_attach(st);
                        break;
                    }
                }
                break;
            }
            int x = SW + 6;
            if (mail_in(mx, my, x, 5, 62, 22)) { mail_send_current(st); break; }
            x += 68;
            if (mail_in(mx, my, x, 5, 78, 22)) { compose_save_draft(st); break; }
            x += 84;
            if (mail_in(mx, my, x, 5, 62, 22)) { compose_discard(st); break; }
            x += 68;
            if (mail_in(mx, my, x, 5, 62, 22)) { mail_open_picker(st, "/home/user"); break; }
            int fx = SW + 64;
            int fw = W - SW - 64 - 10;
            if (my >= 8 && my < 32 && mx >= fx) {
                st->comp_field = 0;
                st->comp_cursor[0] = mail_char_at(st->comp_to, 0, (int)strlen(st->comp_to), mx - fx - 4);
            } else if (my >= 36 && my < 60 && mx >= fx) {
                st->comp_field = 1;
                st->comp_cursor[1] = mail_char_at(st->comp_cc, 0, (int)strlen(st->comp_cc), mx - fx - 4);
            } else if (my >= 64 && my < 88 && mx >= fx) {
                st->comp_field = 2;
                st->comp_cursor[2] = mail_char_at(st->comp_bcc, 0, (int)strlen(st->comp_bcc), mx - fx - 4);
            } else if (my >= 92 && my < 116 && mx >= fx) {
                st->comp_field = 3;
                st->comp_cursor[3] = mail_char_at(st->comp_subject, 0, (int)strlen(st->comp_subject), mx - fx - 4);
            } else if (my >= 120 && my < H - STATUS_H - 8) {
                st->comp_field = 4;
                // map click to cursor position in wrapped body
                int starts[96];
                int lines = mail_wrap(st->comp_body, fw - 10, starts, 96);
                int row = (my - 120 - 4) / 16 + st->comp_body_scroll;
                if (row >= 0 && row < lines) {
                    int end = (row + 1 < lines) ? starts[row + 1] - 1 : (int)strlen(st->comp_body);
                    st->comp_cursor[4] = mail_char_at(st->comp_body, starts[row], end, mx - SW - 10 - 5);
                }
            }
            break;
        }
        case EV_ACCOUNT: {
            int x = SW + 6;
            if (mail_in(mx, my, x, 5, 62, 22)) {
                // commit account
                strncpy(st->acct.display, st->acc_buf[0], sizeof(st->acct.display) - 1);
                strncpy(st->acct.email, st->acc_buf[1], sizeof(st->acct.email) - 1);
                strncpy(st->acct.smtp_host, st->acc_buf[2], sizeof(st->acct.smtp_host) - 1);
                strncpy(st->acct.pop_host, st->acc_buf[4], sizeof(st->acct.pop_host) - 1);
                strncpy(st->acct.username, st->acc_buf[6], sizeof(st->acct.username) - 1);
                strncpy(st->acct.password, st->acc_buf[7], sizeof(st->acct.password) - 1);
                strncpy(st->acct.signature, st->acc_buf[8], sizeof(st->acct.signature) - 1);
                st->acct.smtp_port = atoi(st->acc_buf[3]);
                st->acct.pop_port = atoi(st->acc_buf[5]);
                if (st->acct.smtp_port <= 0) st->acct.smtp_port = 587;
                if (st->acct.pop_port <= 0) st->acct.pop_port = 110;
                st->acct.configured = st->acct.email[0] != 0;
                mail_save(st);
                strcpy(st->status, st->acct.configured ? "Account saved" : "Enter an email address to enable live mail");
                st->view = EV_LIST;
                break;
            }
            x += 68;
            if (mail_in(mx, my, x, 5, 62, 22)) { st->view = EV_LIST; break; }
            x += 68;
            if (mail_in(mx, my, x, 5, 108, 22)) {
#if defined(_WIN32) && !defined(NEFU_BARE)
                if (!st->acct.pop_host[0]) { strcpy(st->status, "Enter a POP3 host first"); break; }
                SOCKET t = mail_net_connect(st->acct.pop_host, st->acct.pop_port);
                if (t == INVALID_SOCKET) { strcpy(st->status, "Connection failed"); break; }
                char cl[512];
                bool ok = mail_net_line(t, cl, sizeof(cl)) && strncmp(cl, "+OK", 3) == 0;
                closesocket(t);
                strcpy(st->status, ok ? "POP3 server reachable" : "Reached, but no POP3 greeting");
#else
                strcpy(st->status, "Live test is host-build only");
#endif
                break;
            }
            int y = 44;
            for (int i = 0; i < 9; i++) {
                if (my >= y && my < y + 24 && mx >= SW + 150) {
                    st->acc_field = i;
                    break;
                }
                y += 30;
            }
            break;
        }
        default: break;
    }
    email_paint(w);
}

// ============================== keyboard ==============================

// insert a UTF-8 string at cursor
static void mail_buf_insert(char* buf, int cap, int& cursor, const char* s) {
    int sl = (int)strlen(s);
    int len = (int)strlen(buf);
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    if (len + sl >= cap) sl = cap - 1 - len;
    if (sl <= 0) return;
    memmove(buf + cursor + sl, buf + cursor, (size_t)(len - cursor + 1));
    memcpy(buf + cursor, s, (size_t)sl);
    cursor += sl;
}

static void mail_buf_backspace(char* buf, int& cursor) {
    int len = (int)strlen(buf);
    if (cursor > len) cursor = len;
    if (cursor <= 0) return;
    int stp = mail_step(buf + cursor - 1);
    memmove(buf + cursor - stp, buf + cursor, (size_t)(len - cursor + 1));
    cursor -= stp;
}

static void mail_buf_del(char* buf, int& cursor) {
    int len = (int)strlen(buf);
    if (cursor >= len) return;
    int stp = mail_step(buf + cursor);
    memmove(buf + cursor, buf + cursor + stp, (size_t)(len - cursor - stp + 1));
}

// buffer currently being edited in the compose view
static char* comp_field_buf(EmailState* st) {
    if (st->comp_field == 0) return st->comp_to;
    if (st->comp_field == 1) return st->comp_cc;
    if (st->comp_field == 2) return st->comp_bcc;
    if (st->comp_field == 3) return st->comp_subject;
    return st->comp_body;
}

static void email_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    EmailState* st = (EmailState*)w->userdata;
    if (!st) return;

    switch (st->view) {
        case EV_LIST: {
            if (st->search_focus) {
                if (e->keycode == KEY_ESC) st->search_focus = false;
                else if (e->keycode == KEY_BACKSPACE) {
                    int cur = (int)strlen(st->search);
                    mail_buf_backspace(st->search, cur);
                }
                else if (e->utf8[0]) {
                    int cur = (int)strlen(st->search);
                    mail_buf_insert(st->search, 63, cur, e->utf8);
                }
                else if (e->ascii >= 32 && e->ascii < 127) {
                    int cur = (int)strlen(st->search);
                    if (cur < 62) { st->search[cur] = e->ascii; st->search[cur + 1] = 0; }
                } else if (e->keycode == KEY_ENTER) { if (vis_total(st) > 0) mail_open_msg(st); }
                mail_build_view(st);
                mail_clamp_list(st);
                break;
            }
            if (e->keycode == KEY_ESC) break;
            if (e->keycode == KEY_UP) { if (st->cur_msg > 0) st->cur_msg--; }
            else if (e->keycode == KEY_DOWN) { if (st->cur_msg < vis_total(st) - 1) st->cur_msg++; }
            else if (e->keycode == KEY_ENTER || e->keycode == KEY_SPACE) {
                if (vis_total(st) > 0) {
                    if (st->cur_folder == EF_DRAFTS) {
                        int f, idx;
                        vis_src(st, st->cur_msg, f, idx);
                        compose_from_draft(st, idx);
                    } else mail_open_msg(st);
                }
            }
            else if (e->ascii == '/' || e->ascii == 's') st->search_focus = true;
            else if (e->ascii == 'c' || e->ascii == 'C') { compose_start(st, true); }
            break;
        }
        case EV_READ: {
            if (e->keycode == KEY_ESC || e->keycode == KEY_BACKSPACE) st->view = EV_LIST;
            else if (e->ascii == 'r' || e->ascii == 'R') compose_reply(st);
            else if (e->ascii == 'a' || e->ascii == 'A') compose_reply_all(st);
            else if (e->ascii == 'f' || e->ascii == 'F') compose_forward(st);
            else if (e->keycode == KEY_DEL) mail_delete_current(st);
            else if (e->ascii == 's' || e->ascii == 'S') mail_toggle_star(st);
            else if (e->keycode == KEY_UP) { if (st->read_scroll > 0) st->read_scroll--; }
            else if (e->keycode == KEY_DOWN) st->read_scroll++;
            else if (e->keycode == KEY_PGUP) { st->read_scroll -= 12; if (st->read_scroll < 0) st->read_scroll = 0; }
            else if (e->keycode == KEY_PGDN) st->read_scroll += 12;
            break;
        }
        case EV_COMPOSE: {
            if (st->picker_open) {
                if (e->keycode == KEY_ESC) { st->picker_open = false; break; }
                else if (e->keycode == KEY_UP) { if (st->picker_sel > 0) st->picker_sel--; }
                else if (e->keycode == KEY_DOWN) { if (st->picker_sel < st->picker_cnt - 1) st->picker_sel++; }
                else if (e->keycode == KEY_ENTER || e->keycode == KEY_SPACE) mail_picker_attach(st);
                // keep the selection visible
                int maxrows = (w->content_h - 44 - STATUS_H - 60 - 34) / 20;
                if (st->picker_sel < st->picker_scroll) st->picker_scroll = st->picker_sel;
                if (st->picker_sel >= st->picker_scroll + maxrows) st->picker_scroll = st->picker_sel - maxrows + 1;
                if (st->picker_scroll < 0) st->picker_scroll = 0;
                break;
            }
            int& cur = st->comp_cursor[st->comp_field];
            char* buf = comp_field_buf(st);
            if (e->keycode == KEY_ESC) { compose_save_draft(st); return; }
            else if (e->keycode == KEY_TAB) { st->comp_field = (st->comp_field + 1) % 5; }
            else if (e->keycode == KEY_ENTER) {
                if (st->comp_field < 4) st->comp_field++;
                else mail_buf_insert(st->comp_body, (int)sizeof(st->comp_body), cur, "\n");
            }
            else if (e->keycode == KEY_BACKSPACE) mail_buf_backspace(buf, cur);
            else if (e->keycode == KEY_DEL) mail_buf_del(buf, cur);
            else if (e->keycode == KEY_LEFT) { if (cur > 0) cur -= mail_step(buf + cur - 1); }
            else if (e->keycode == KEY_RIGHT) { int len = (int)strlen(buf); if (cur < len) cur += mail_step(buf + cur); }
            else if (e->utf8[0]) mail_buf_insert(buf, (int)(st->comp_field == 4 ? sizeof(st->comp_body) : 256), cur, e->utf8);
            else if (e->ascii >= 32 && e->ascii < 127) {
                char s2[2] = { e->ascii, 0 };
                mail_buf_insert(buf, (int)(st->comp_field == 4 ? sizeof(st->comp_body) : 256), cur, s2);
            }
            break;
        }
        case EV_ACCOUNT: {
            if (e->keycode == KEY_ESC) st->view = EV_LIST;
            else if (e->keycode == KEY_TAB || e->keycode == KEY_ENTER) st->acc_field = (st->acc_field + 1) % 9;
            else if (e->keycode == KEY_BACKSPACE) {
                int cur = (int)strlen(st->acc_buf[st->acc_field]);
                mail_buf_backspace(st->acc_buf[st->acc_field], cur);
            }
            else if (e->utf8[0]) {
                int cur = (int)strlen(st->acc_buf[st->acc_field]);
                mail_buf_insert(st->acc_buf[st->acc_field], 95, cur, e->utf8);
            }
            else if (e->ascii >= 32 && e->ascii < 127) {
                if ((st->acc_field == 3 || st->acc_field == 5) && (e->ascii < '0' || e->ascii > '9')) break;
                char s2[2] = { e->ascii, 0 };
                int cur = (int)strlen(st->acc_buf[st->acc_field]);
                mail_buf_insert(st->acc_buf[st->acc_field], 95, cur, s2);
            }
            break;
        }
        default: break;
    }
    email_paint(w);
}

// ============================== scroll / close / open ==============================

static void email_scroll(Window* w, int delta) {
    EmailState* st = (EmailState*)w->userdata;
    if (!st) return;
    if (st->view == EV_LIST) {
        int n = vis_total(st);
        int listh = w->content_h - TOOL_H - 26 - STATUS_H;
        int maxrows = listh / ROW_H;
        st->list_scroll -= delta;
        if (st->list_scroll < 0) st->list_scroll = 0;
        if (st->list_scroll > n - maxrows) st->list_scroll = n - maxrows;
        if (st->list_scroll < 0) st->list_scroll = 0;
    } else if (st->view == EV_READ) {
        st->read_scroll -= delta;
        if (st->read_scroll < 0) st->read_scroll = 0;
    } else if (st->view == EV_COMPOSE && st->comp_field == 4) {
        st->comp_body_scroll -= delta;
        if (st->comp_body_scroll < 0) st->comp_body_scroll = 0;
    }
    email_paint(w);
}

static void email_close(Window* w) {
    EmailState* st = (EmailState*)w->userdata;
    if (!st) return;
    if (st->dirty || st->acct.configured) mail_save(st);
    delete st;
    w->userdata = 0;
}

Window* open_email() {
    Window* w = g_wm->create_window("Email", 80, 80, 640, 460);
    if (!w) return 0;
    EmailState* st = new EmailState();
    w->userdata = st;
    w->on_paint = email_paint;
    w->on_mouse = email_mouse;
    w->on_key = email_key;
    w->on_scroll = email_scroll;
    w->on_close = email_close;
    w->esc_close = false;
    mail_load(st);
    if (!st->acct.configured && st->folders[EF_INBOX].msg_count == 0) mail_seed(st);
    mail_rebuild_star(st);
    mail_build_view(st);
    mail_net_status(st);
    strcpy(st->status, st->acct.configured ? "Ready - live SMTP/POP3 on host build" : "Ready - configure an account for live mail");
    return w;
}

}} // namespace nefu::apps
