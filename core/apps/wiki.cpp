// nefuOS Wiki - database-backed encyclopedia.
// Data lives in /var/lib/nefuos/db/wiki.db (one entry per line,
// "title<TAB>body", '~' in body renders as a newline).
// Guests get read-only access; editing requires admin login, which is
// verified against a salted SHA-256 hash (never a plaintext password).
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"
#include "../sys/settings.h"   // T() UI-language macro

namespace nefu {

// exported by terminal.cpp (admin auth shared with `su`)
bool admin_is_admin();
bool admin_login(const char* pw);
void admin_logout();

static const char* WIKI_DB = "/var/lib/nefuos/db/wiki.db";
static const char* WIKI_ONLINE_URL = "https://miaoda.feishu.cn/app/app_17eecj9ct6k/";

struct WikiEntry {
    String title;
    String body;
};

struct WikiState {
    Window* win;
    List<WikiEntry> entries;
    int sel;             // selected entry (-1 = none)
    int list_scroll;
    int body_scroll;
    String search;
    bool search_focus;
    bool edit_mode;
    String edit_buf;
    int edit_row, edit_col, edit_scroll;
    bool edit_dirty;
    bool login_mode;     // password prompt active
    String pw_buf;       // password buffer (never echoed)
    Button btns[6];
    uint8_t last_buttons;
    uint32_t last_key_t;
};

// ---------- persistence ----------

static void wiki_seed() {
    const char* pages[] = {
        "nefuOS\tnefuOS 0.2 is a tiny hobby operating system written in C++ and C~"
        "It ships two backends: a native Windows desktop app and a bare-metal x86_64 "
        "kernel that boots from an El Torito ISO.~Everything is real: window manager, "
        "Unix-like VFS, networking, and apps.",
        "File System\tThe VFS follows the Unix hierarchy: /bin /etc /home /usr /tmp "
        "/var and friends.~Downloads land in /usr/downloads, temporary files in /tmp.~"
        "Deleting a file moves it to /home/user/.Trash unless emptied with "
        "'empty-trash'.",
        "Browser\tA self-contained browser with its own HTML parser, forms, cookies, "
        "history, URL resolution and Bing search.~Pages can be saved to "
        "/usr/downloads. On bare metal it talks to a real HTTP server over the e1000 NIC.",
        "Store\tThe Software Store ships the catalog in /etc/store.conf. Installed apps "
        "appear in the Start menu.~Scroll with PgDn/PgUp or the scrollbar.",
        "Terminal\t~60 Unix-style commands: ls cd cat mkdir rm echo ping ifconfig su "
        "db recovery honeypot selftest ...~Run ./x.bin for a NEFBIN01 binary, or "
        "./x.nefud with nefupack.",
        "Admin & Security\tAdmin user is lbinm. The password is injected at build time "
        "via NEFU_ADMIN_PASSWORD and stored only as a salted SHA-256 hash.~'su "
        "<password>' unlocks admin. A passive honeypot records decoy-service attempts "
        "in /var/log/honeypot.log.",
        "Database\tSystem data lives in /var/lib/nefuos/db: system.db (key/value, "
        "admin-write) and wiki.db (this encyclopedia).~An external 'you-sql' connector "
        "string is reserved in the DSN field for future cloud sync.",
        "Online Wiki\tThe deployed online Wiki lives at https://miaoda.feishu.cn/app/app_17eecj9ct6k/~"
        "Guests can read; admin (admin/admin) can edit pages.~"
        "An offline mirror of the 3 online pages is stored in this local DB, "
        "so the Wiki keeps working without a network. Click Online to open the "
        "live site in the browser.",
        "nefuOS \u4ecb\u7ecd (Online)\tnefuOS \u662f\u4e00\u4e2a\u9762\u5411\u6559\u80b2\u573a\u666f\u7684\u5f00\u6e90\u64cd\u4f5c\u7cfb\u7edf\u9879\u76ee\uff0c\u81f4\u529b\u4e8e\u4e3a\u9ad8\u6821\u5e08\u751f\u63d0\u4f9b\u4e00\u4e2a\u5b89\u5168\u3001\u9ad8\u6548\u3001\u6613\u7528\u7684\u8ba1\u7b97\u73af\u5883\u3002~"
        "\u4e3b\u8981\u7279\u6027: ~- \u5f00\u6e90\u514d\u8d39: \u57fa\u4e8e Linux \u5185\u6838\uff0c\u5b8c\u5168\u5f00\u6e90~- \u6559\u80b2\u4f18\u5316: \u9884\u88c5\u5e38\u7528\u6559\u5b66\u8f6f\u4ef6\u548c\u5f00\u53d1\u5de5\u5177~- \u5b89\u5168\u7a33\u5b9a: \u5b9a\u671f\u5b89\u5168\u66f4\u65b0\uff0c\u957f\u671f\u652f\u6301~- \u4e2d\u6587\u53cb\u597d: \u5b8c\u5584\u7684\u4e2d\u6587\u672c\u5730\u5316\u652f\u6301~"
        "\u5feb\u901f\u5f00\u59cb: 1. \u4e0b\u8f7d\u6700\u65b0\u955c\u50cf 2. \u5236\u4f5c\u5b89\u88c5\u4ecb\u8d28 3. \u6309\u5b89\u88c5\u5411\u5bfc\u5b8c\u6210\u5b89\u88c5~"
        "\u66f4\u591a\u4fe1\u606f: https://miaoda.feishu.cn/app/app_17eecj9ct6k/page/nefos-intro",
        "\u5b89\u88c5\u6307\u5357 (Online)\t\u7cfb\u7edf\u8981\u6c42: CPU \u53cc\u6838 2GHz / \u5185\u5b58 4GB / \u786c\u76d8 20GB\u3002~"
        "\u5b89\u88c5\u6b65\u9aa4: 1. \u4ece\u5b98\u65b9\u7f51\u7ad9\u4e0b\u8f7d\u6700\u65b0 ISO \u955c\u50cf 2. \u7528 Rufus \u6216 dd \u5c06\u955c\u50cf\u5199\u5165 USB 3. \u4ece USB \u542f\u52a8\u6309\u5411\u5bfc\u5b89\u88c5\u3002~"
        "\u5e38\u89c1\u95ee\u9898: \u542f\u52a8\u5931\u8d25\u68c0\u67e5 BIOS \u7684 USB \u542f\u52a8\u9009\u9879\uff1b\u5b89\u88c5\u5361\u4f4f\u66f4\u6362 USB \u63a5\u53e3\u6216\u91cd\u505a\u542f\u52a8\u76d8\u3002~"
        "\u66f4\u591a\u4fe1\u606f: https://miaoda.feishu.cn/app/app_17eecj9ct6k/page/install-guide",
        "\u5e38\u7528\u547d\u4ee4\u901f\u67e5 (Online)\t\u6587\u4ef6: ls -la / cd path / cp src dst / mv src dst / rm file / mkdir dir~"
        "\u5305\u7ba1\u7406: sudo apt update / install pkg / remove pkg / upgrade~"
        "\u7f51\u7edc: ip addr / ping host / curl url~"
        "\u8fdb\u7a0b: ps aux / kill pid / top~"
        "\u66f4\u591a\u4fe1\u606f: https://miaoda.feishu.cn/app/app_17eecj9ct6k/page/command-cheatsheet",
    };
    g_vfs->mkdir("/var/lib/nefuos/db");
    FSNode* f = g_vfs->create_file(WIKI_DB);
    if (!f) return;
    uint8_t* buf = (uint8_t*)kalloc(8192);
    if (!buf) return;
    uint8_t* p = buf;
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        uint32_t n = (uint32_t)strlen(pages[i]);
        if ((uint32_t)(p - buf) + n + 2 > 8192) break;
        memcpy(p, pages[i], (size_t)n);
        p += n;
        *p++ = '\n';
    }
    g_vfs->write_file(f, buf, (uint32_t)(p - buf));
    kfree(buf);
}

static void wiki_load(WikiState* st) {
    st->entries.clear();
    FSNode* f = g_vfs->resolve(WIKI_DB);
    if (!f || f->is_dir || f->size == 0) {
        wiki_seed();
        f = g_vfs->resolve(WIKI_DB);
    }
    if (!f || f->is_dir) return;
    List<String> lines;
    file_to_lines(f, lines, 200000);
    for (int i = 0; i < lines.size(); i++) {
        int tab = lines[i].find('\t');
        WikiEntry e;
        if (tab > 0) {
            e.title = lines[i].substr(0, tab);
            e.body = lines[i].substr(tab + 1, lines[i].len() - tab - 1);
        } else if (lines[i].len() > 0) {
            e.title = lines[i];
        } else {
            continue;
        }
        if (!e.title.empty()) st->entries.push(e);
    }
    if (st->sel >= st->entries.size()) st->sel = st->entries.size() - 1;
    if (st->sel < 0 && st->entries.size() > 0) st->sel = 0;
}

static void wiki_save(WikiState* st) {
    uint32_t total = 1;
    for (int i = 0; i < st->entries.size(); i++)
        total += (uint32_t)(st->entries[i].title.len() + st->entries[i].body.len()) + 2;
    uint8_t* buf = (uint8_t*)kalloc(total);
    if (!buf) return;
    uint8_t* p = buf;
    for (int i = 0; i < st->entries.size(); i++) {
        const char* t = st->entries[i].title.c_str();
        const char* b = st->entries[i].body.c_str();
        uint32_t tn = (uint32_t)st->entries[i].title.len();
        uint32_t bn = (uint32_t)st->entries[i].body.len();
        memcpy(p, t, tn); p += tn;
        *p++ = '\t';
        memcpy(p, b, bn); p += bn;
        *p++ = '\n';
    }
    FSNode* f = g_vfs->resolve(WIKI_DB);
    if (!f) {
        g_vfs->mkdir("/var/lib/nefuos/db");
        f = g_vfs->create_file(WIKI_DB);
    }
    if (f) g_vfs->write_file(f, buf, (uint32_t)(p - buf));
    kfree(buf);
    st->edit_dirty = false;
}

// ---------- rendering ----------

static void wiki_paint(Window* w) {
    WikiState* st = (WikiState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);

    // top bar
    gfx::text(s, 8, 4, T("维基", "Wiki"), color::BLUE, color::WHITE);
    gfx::rect(s, 56, 2, 200, 18, 0x00B0B0B0);
    if (st->search_focus) gfx::rect(s, 56, 2, 200, 18, color::BLUE);
    gfx::text(s, 60, 4, st->search.c_str(), color::TEXT, color::WHITE);
    gfx::text(s, 60, 4, st->search_focus ? "_" : "", color::TEXT2, color::WHITE);
    gfx::hline(s, 0, s.width, 22, color::BORDER);

    // admin password prompt (login_mode): password is never echoed
    if (st->login_mode) {
        gfx::fillrect(s, 0, 24, s.width, 20, 0x00FFF3CD);
        gfx::text(s, 8, 26, T("密码: ", "Password: "), color::TEXT, 0x00FFF3CD);
        int pw = 60;
        for (int i = 0; i < st->pw_buf.len() && i < 40; i++) {
            gfx::text(s, pw + i * 8, 26, "*", color::TEXT, 0x00FFF3CD);
        }
        gfx::text(s, pw + st->pw_buf.len() * 8, 26, (platform_tick_ms() / 400) % 2 ? "_" : " ", color::TEXT2, 0x00FFF3CD);
        gfx::text(s, 320, 26, T("回车登录 Esc 取消", "Enter login, Esc cancel"), color::TEXT2, 0x00FFF3CD);
        gfx::hline(s, 0, s.width, 45, color::BORDER);
    }

    // left: entry list
    int list_w = 150;
    int top = 26;
    int vis_rows = (s.height - top - 26) / 16;
    gfx::fillrect(s, 0, top, list_w, s.height - top, 0x00F0F3F8);
    for (int i = st->list_scroll; i < st->entries.size() && i < st->list_scroll + vis_rows; i++) {
        int y = top + (i - st->list_scroll) * 16;
        bool sel = (i == st->sel);
        if (sel) gfx::fillrect(s, 2, y, list_w - 4, 15, 0x003E87B5);
        gfx::text(s, 6, y + 1, st->entries[i].title.c_str(), sel ? color::WHITE : color::TEXT,
                  sel ? 0x003E87B5 : 0x00F0F3F8);
    }
    gfx::vline(s, list_w, top, s.height, color::BORDER);

    // right: body (edit mode renders the editor)
    int bx = list_w + 8;
    int bw = s.width - bx - 8;
    if (st->edit_mode) {
        // simple multi-line editor: '~' = line break
        int line = 0, col = 0;
        for (int i = 0; i < st->edit_row; i++) {
            int nxt = st->edit_buf.find('~');
            if (nxt < 0) break;
            line++;
            col = nxt + 1;
        }
        int start = col;
        int y = top;
        int p = 0;
        (void)start;
        (void)line;
        // draw from current line start
        while (p < st->edit_buf.len()) {
            if (st->edit_buf[p] == '~') {
                y += 16;
                if (y > s.height - 30) break;
            }
            p++;
        }
        y = top;
        p = 0;
        int cur_y = top, cur_x = bx;
        int r = 0;
        String linebuf;
        for (int i = 0; i < st->edit_buf.len(); i++) {
            char c = st->edit_buf[i];
            if (c == '~') {
                if (r >= st->edit_scroll)
                    gfx::text(s, bx, y + (r - st->edit_scroll) * 16, linebuf.c_str(), color::TEXT, color::WHITE);
                if (r == st->edit_row) { cur_y = y + (r - st->edit_scroll) * 16; cur_x = bx + st->edit_col * 8; }
                linebuf.clear();
                r++;
                continue;
            }
            linebuf += c;
            if (r == st->edit_row && linebuf.len() == st->edit_col + 1) {
                cur_y = y + (r - st->edit_scroll) * 16;
                cur_x = bx + linebuf.len() * 8;
            }
        }
        if (r >= st->edit_scroll && linebuf.len() > 0)
            gfx::text(s, bx, y + (r - st->edit_scroll) * 16, linebuf.c_str(), color::TEXT, color::WHITE);
        if (st->edit_row == r) { cur_y = y + (r - st->edit_scroll) * 16; cur_x = bx + st->edit_col * 8; }
        if ((platform_tick_ms() / 400) % 2 == 0 && cur_y >= top && cur_y < s.height - 30)
            gfx::fillrect(s, cur_x, cur_y, 2, 15, color::RED);
        gfx::text(s, bx, s.height - 26, T("编辑中 - Enter 换行(~) Esc 退出", "editing - Enter newline(~), Esc cancel"), color::TEXT2, color::WHITE);
    } else if (st->sel >= 0 && st->sel < st->entries.size()) {
        const WikiEntry& e = st->entries[st->sel];
        gfx::text(s, bx, top, e.title.c_str(), color::BLUE, color::WHITE);
        gfx::hline(s, bx, bx + bw, top + 18, color::BORDER);
        int y = top + 24;
        int line_start = 0;
        for (int i = 0; i <= e.body.len(); i++) {
            if (i == e.body.len() || e.body[i] == '~') {
                if (y - (top + 24) >= st->body_scroll) {
                    if (y - st->body_scroll > s.height - 30) break;
                    String ln = e.body.substr(line_start, i - line_start);
                    gfx::text(s, bx, y - st->body_scroll, ln.c_str(), color::TEXT, color::WHITE);
                }
                y += 16;
                line_start = i + 1;
            }
        }
        gfx::text(s, bx, s.height - 26, T("游客只读 - 登录 admin 后可编辑", "read-only (guest) - login as admin to edit"), color::TEXT2, color::WHITE);
    } else {
        gfx::text(s, bx, top, T("选择一个词条，或创建新词条。", "Select an entry, or create a new one."), color::TEXT2, color::WHITE);
    }

    // bottom buttons
    int by = s.height - 22;
    int bxi = s.width - 72;
    const char* labels[6] = { "Online", "Login", "Edit", "New", "Save", "Cancel" };
    if (admin_is_admin()) labels[1] = "Logout";
    bool show_edit = !st->edit_mode && admin_is_admin();
    for (int i = 0; i < 6; i++) {
        Button& b = st->btns[i];
        b.x = bxi - (5 - i) * 62;
        b.y = by;
        b.w = 56;
        b.h = 18;
        b.id = i;
        b.label = labels[i];
        bool vis = (i == 0) || (i == 1) || (show_edit && (i == 2 || i == 3)) || (st->edit_mode && (i == 4 || i == 5));
        if (vis) ui::draw_button(s, b);
    }
}

// ---------- actions ----------

static void wiki_begin_edit(WikiState* st) {
    if (st->sel < 0 || st->sel >= st->entries.size()) return;
    st->edit_buf = st->entries[st->sel].body;
    st->edit_row = 0;
    st->edit_col = 0;
    st->edit_scroll = 0;
    st->edit_mode = true;
    st->edit_dirty = false;
}

static void wiki_commit_edit(WikiState* st) {
    if (st->sel >= 0 && st->sel < st->entries.size()) {
        st->entries[st->sel].body = st->edit_buf;
        wiki_save(st);
        st->edit_mode = false;
        st->body_scroll = 0;
    }
}

static void wiki_new_page(WikiState* st) {
    // insert a new empty entry and start editing its title-less body
    WikiEntry e;
    e.title = String("Untitled");
    e.body = String("New page body. Replace this text.");
    st->entries.insert(st->sel + 1, e);
    st->sel = st->sel + 1;
    wiki_save(st);
    wiki_begin_edit(st);
}

// ---------- input ----------

static void wiki_mouse(Window* w, int mx, int my, uint8_t buttons) {
    WikiState* st = (WikiState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    st->last_buttons = buttons;
    if (!pressed) return;

    // top search box
    if (my >= 2 && my < 20 && mx >= 56 && mx < 256) { st->search_focus = true; return; }

    // bottom buttons
    for (int i = 0; i < 6; i++) {
        Button& b = st->btns[i];
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            bool vis = (i == 0) || (i == 1) || (admin_is_admin() && !st->edit_mode && (i == 2 || i == 3)) ||
                       (st->edit_mode && (i == 4 || i == 5));
            if (!vis) return;
            if (i == 0) { browser_launch_url(WIKI_ONLINE_URL); return; }
            if (i == 1) {
                if (admin_is_admin()) { admin_logout(); }
                else { st->login_mode = true; st->pw_buf.clear(); st->search_focus = false; return; }
            }
            if (i == 2) { wiki_begin_edit(st); return; }
            if (i == 3) { wiki_new_page(st); return; }
            if (i == 4) { wiki_commit_edit(st); return; }
            if (i == 5) { st->edit_mode = false; return; }
        }
    }

    // entry list click
    int top = 26;
    if (mx < 150 && my >= top) {
        int idx = st->list_scroll + (my - top) / 16;
        if (idx >= 0 && idx < st->entries.size()) {
            st->sel = idx;
            st->body_scroll = 0;
        }
        return;
    }

    // clicking body area clears search focus
    st->search_focus = false;
    if (st->login_mode && (mx >= 300 || my > 60)) { /* keep prompt until Enter/Esc */ }
}

static void wiki_key(Window* w, const KeyEvent* e) {
    WikiState* st = (WikiState*)w->userdata;
    if (!e->down) return;

    // password prompt gets all keys first
    if (st->login_mode) {
        if (e->keycode == KEY_ESC) { st->login_mode = false; st->pw_buf.clear(); return; }
        if (e->keycode == KEY_ENTER) {
            bool ok = admin_login(st->pw_buf.c_str());
            st->login_mode = false;
            st->pw_buf.clear();
            if (!ok) {
                // failed login: flash by clearing edit rights (admin stays logged out)
            }
            return;
        }
        if (e->keycode == KEY_BACKSPACE) {
            if (st->pw_buf.len() > 0) st->pw_buf = st->pw_buf.substr(0, st->pw_buf.len() - 1);
            return;
        }
        if (e->ascii >= 32 && e->ascii < 127) {
            st->pw_buf += (char)e->ascii;
            return;
        }
        return;
    }

    if (st->edit_mode) {
        if (e->keycode == KEY_ESC) { st->edit_mode = false; return; }
        if (e->keycode == KEY_ENTER) { st->edit_buf += '~'; st->edit_row++; st->edit_col = 0; st->edit_dirty = true; return; }
        if (e->keycode == KEY_BACKSPACE) {
            if (st->edit_col > 0) {
                st->edit_buf = st->edit_buf.substr(0, st->edit_buf.len() - 1);
                st->edit_col--;
            }
            st->edit_dirty = true;
            return;
        }
        if (e->ascii >= 32 && e->ascii < 127) {
            st->edit_buf += (char)e->ascii;
            st->edit_col++;
            st->edit_dirty = true;
            return;
        }
        return;
    }

    if (st->search_focus) {
        if (e->keycode == KEY_BACKSPACE) {
            if (st->search.len() > 0) {
                st->search = st->search.substr(0, st->search.len() - 1);
                // jump to first matching entry
                for (int i = 0; i < st->entries.size(); i++) {
                    if (st->entries[i].title.find(st->search.c_str()) >= 0) { st->sel = i; break; }
                }
            }
            return;
        }
        if (e->ascii >= 32 && e->ascii < 127) {
            st->search += (char)e->ascii;
            for (int i = 0; i < st->entries.size(); i++) {
                if (st->entries[i].title.find(st->search.c_str()) >= 0) { st->sel = i; break; }
            }
            return;
        }
        if (e->keycode == KEY_ENTER) { st->search_focus = false; return; }
        if (e->keycode == KEY_TAB) { st->search_focus = false; return; }
        return;
    }

    // navigation keys
    if (e->keycode == KEY_UP && st->sel > 0) { st->sel--; st->body_scroll = 0; }
    if (e->keycode == KEY_DOWN && st->sel < st->entries.size() - 1) { st->sel++; st->body_scroll = 0; }
    if (e->keycode == KEY_PGDN) { st->body_scroll += 12; }
    if (e->keycode == KEY_PGUP) { st->body_scroll -= 12; if (st->body_scroll < 0) st->body_scroll = 0; }
    if (e->keycode == KEY_TAB) { st->search_focus = true; }

    // Ctrl+L login prompt (password entry, never echoed)
    if ((e->keycode == KEY_CTRL || e->keycode == 0) && (e->ascii == 12 || e->ascii == 'l')) {
        st->search_focus = false;
        return;
    }
    (void)st->last_key_t;
}

static void wiki_scroll(Window* w, int delta) {
    WikiState* st = (WikiState*)w->userdata;
    if (st->edit_mode) return;
    int d = (delta > 0) ? 4 : -4;
    st->body_scroll += d;
    if (st->body_scroll < 0) st->body_scroll = 0;
}

static void wiki_close(Window* w) {
    WikiState* st = (WikiState*)w->userdata;
    if (st && st->edit_mode && st->edit_dirty) wiki_commit_edit(st);
    if (st) delete st;
    w->userdata = 0;
}

void wiki_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("数据库维基", "Wiki"), x, y, 620, 440);
    if (!w) return;
    WikiState* st = new WikiState();
    st->win = w;
    st->sel = -1;
    st->list_scroll = 0;
    st->body_scroll = 0;
    st->search_focus = false;
    st->edit_mode = false;
    st->edit_dirty = false;
    st->login_mode = false;
    st->last_buttons = 0;
    st->last_key_t = 0;
    wiki_load(st);
    w->userdata = st;
    w->on_paint = wiki_paint;
    w->on_mouse = wiki_mouse;
    w->on_key = wiki_key;
    w->on_scroll = wiki_scroll;
    w->on_close = wiki_close;
}

} // namespace nefu
