// nefuOS JSON Viewer — parse and browse JSON documents
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../lib/json.h"
#include "../vfs/vfs.h"

namespace nefu {

namespace {

struct JsonView {
    int w, h;
    String path;
    String text;          // pretty-printed
    int scroll;           // line scroll
    int lines;
};

bool jv_load(JsonView& g, const char* path) {
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir) return false;
    g.path = path;
    uint8_t* d = (uint8_t*)kalloc(f->size + 1);
    if (!d) return false;
    memcpy(d, f->data, f->size);
    d[f->size] = 0;
    json::Value v;
    const char* err = 0;
    if (!json::parse((const char*)d, v, &err)) {
        g.text = "json parse error: ";
        g.text += err ? err : "unknown";
        kfree(d);
        g.lines = 1;
        return true;
    }
    String pretty;
    json::to_string(v, pretty, true);
    g.text = pretty;
    kfree(d);
    g.lines = 1;
    for (int i = 0; i < g.text.len(); i++) if (g.text[i] == '\n') g.lines++;
    return true;
}

void jv_paint(Window* win) {
    JsonView* g = (JsonView*)win->userdata;
    Surface& s = win->back;
    s.fill(0x001B1B2A);
    char buf[128];
    ksprintf(buf, sizeof(buf), "JSON Viewer  %s  (%d lines)", g->path.c_str(), g->lines);
    gfx::text(s, 6, 4, buf, color::TEXT, 0x001B1B2A);
    int y = 26;
    int line = g->scroll;
    int start = 0;
    // walk to line 'scroll'
    int cur = 0;
    for (int i = 0; i < g->text.len() && cur < g->scroll; i++) {
        if (g->text[i] == '\n') { cur++; start = i + 1; }
    }
    while (y < g->h - 6) {
        int end = start;
        while (end < g->text.len() && g->text[end] != '\n') end++;
        if (start >= g->text.len()) break;
        // simple syntax coloring: keys are strings before ':' ; skip full impl
        char linebuf[512];
        int n = end - start;
        if (n > 511) n = 511;
        for (int i = 0; i < n; i++) linebuf[i] = g->text[start + i];
        linebuf[n] = 0;
        gfx::text(s, 8, y, linebuf, 0x00D8D8E8, 0x001B1B2A);
        y += 16;
        if (end >= g->text.len()) break;
        start = end + 1;
    }
    (void)line;
    gfx::text(s, 6, g->h - 16, "Scroll wheel: scroll   ESC: close", 0x00888888, 0x001B1B2A);
}

void jv_scroll(Window* w, int delta) {
    JsonView* g = (JsonView*)w->userdata;
    int step = delta > 0 ? -3 : 3;
    g->scroll += step;
    if (g->scroll < 0) g->scroll = 0;
    if (g->scroll > g->lines - 1) g->scroll = g->lines - 1;
    if (g->scroll < 0) g->scroll = 0;
}

void jv_close(Window* w) {
    if (w->userdata) delete (JsonView*)w->userdata;
    w->userdata = 0;
}

} // namespace

void jsonview_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("JSON Viewer", x, y, 460, 400);
    if (!w) return;
    JsonView* g = new JsonView();
    g->w = w->content_w;
    g->h = w->content_h;
    g->scroll = 0;
    g->lines = 1;
    if (!jv_load(*g, "/etc/nefu.conf")) {
        // create sample config if missing
        FSNode* f = g_vfs->resolve("/etc/nefu.conf");
        if (!f) {
            f = g_vfs->create_file("/etc/nefu.conf");
            if (f) {
                const char* sample =
                    "{\n  \"system\": {\n    \"hostname\": \"nefuos\",\n    \"version\": \"0.3.0\",\n"
                    "    \"browser_engine\": \"minijs\"\n  },\n  \"network\": {\n    \"dhcp\": true,\n"
                    "    \"dns\": [\"8.8.8.8\", \"1.1.1.1\"],\n    \"mtu\": 1500\n  },\n"
                    "  \"users\": [\n    {\"name\": \"user\", \"admin\": false},\n"
                    "    {\"name\": \"lbinm\", \"admin\": true}\n  ]\n}\n";
                g_vfs->write_file(f, (const uint8_t*)sample, (uint32_t)strlen(sample));
            }
        }
        jv_load(*g, "/etc/nefu.conf");
    }
    w->userdata = g;
    w->on_paint = jv_paint;
    w->on_scroll = jv_scroll;
    w->on_close = jv_close;
    g_wm->raise(w);
}

} // namespace nefu
