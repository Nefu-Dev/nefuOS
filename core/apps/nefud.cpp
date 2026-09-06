// nefuOS application package viewer / launcher.
// Handles two formats:
//   .nefud - text manifest that binds to a built-in app
//   .bin   - real binary package (NEFBIN01) that either binds to a built-in
//            app or carries NEFVM bytecode that actually executes here
#include "apps.h"
#include "nefvm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

struct NefudState {
    FSNode* file;
    char name[48];
    char desc[96];
    char author[48];
    char ver[16];
    int app_id;
    bool is_bin;          // NEFBIN01 package
    char outbuf[600];     // VM output buffer
    int outlen;
    Button btn;           // main action (Launch / Run)
};

// VM output callback: append chunks into the window buffer
static void vm_out(const char* s, void* ud) {
    NefudState* st = (NefudState*)ud;
    if (!s || !st) return;
    int n = 0;
    while (s[n] && st->outlen + n < (int)sizeof(st->outbuf) - 1) { n++; }
    if (n > 0) {
        memcpy(st->outbuf + st->outlen, s, (size_t)n);
        st->outlen += n;
        st->outbuf[st->outlen] = 0;
    }
}

static void nefud_parse(NefudState* st) {
    FSNode* f = st->file;
    st->name[0] = 0; st->desc[0] = 0; st->author[0] = 0; st->ver[0] = 0;
    st->app_id = -1; st->is_bin = false;
    if (!f || f->is_dir || f->size == 0) return;
    const uint8_t* d = f->data;
    // .bin package: fixed binary header
    if (f->size >= 64 &&
        d[0] == 'N' && d[1] == 'E' && d[2] == 'F' && d[3] == 'B' &&
        d[4] == 'I' && d[5] == 'N' && d[6] == '0' && d[7] == '1') {
        st->is_bin = true;
        for (int i = 0; i < 24 && d[8 + i]; i++) if (i < 47) st->name[i] = (char)d[8 + i];
        for (int i = 0; i < 32 && d[32 + i]; i++) if (i < 95) st->desc[i] = (char)d[32 + i];
        strcpy(st->author, "nefuOS");
        strcpy(st->ver, "1.0");
        if (st->name[0]) st->app_id = nefud_name_to_app_id(st->name);
        return;
    }
    // .nefud text manifest
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) return;
    memcpy(buf, d, f->size);
    buf[f->size] = 0;
    char* line = buf;
    for (uint32_t i = 0; i < f->size; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            if (strncmp(line, "name=", 5) == 0) strncpy(st->name, line + 5, sizeof(st->name) - 1);
            else if (strncmp(line, "desc=", 5) == 0) strncpy(st->desc, line + 5, sizeof(st->desc) - 1);
            else if (strncmp(line, "author=", 7) == 0) strncpy(st->author, line + 7, sizeof(st->author) - 1);
            else if (strncmp(line, "ver=", 4) == 0) strncpy(st->ver, line + 4, sizeof(st->ver) - 1);
            line = buf + i + 1;
        }
    }
    if (strncmp(line, "name=", 5) == 0) strncpy(st->name, line + 5, sizeof(st->name) - 1);
    else if (strncmp(line, "desc=", 5) == 0) strncpy(st->desc, line + 5, sizeof(st->desc) - 1);
    else if (strncmp(line, "author=", 7) == 0) strncpy(st->author, line + 7, sizeof(st->author) - 1);
    else if (strncmp(line, "ver=", 4) == 0) strncpy(st->ver, line + 4, sizeof(st->ver) - 1);
    kfree(buf);
    if (st->name[0]) st->app_id = nefud_name_to_app_id(st->name);
}

static void nefud_paint(Window* w) {
    NefudState* st = (NefudState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    gfx::text(s, 14, 12, st->is_bin ? "nefuOS Binary Application (.bin)" : "nefuOS Application Package (.nefud)",
              color::BLUE, color::WHITE);
    int y = 42;
    char buf[160];
    gfx::text(s, 14, y, "Name   :", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->name[0] ? st->name : "(none)", color::TEXT, color::WHITE); y += 20;
    gfx::text(s, 14, y, "Desc   :", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->desc[0] ? st->desc : "(none)", color::TEXT, color::WHITE); y += 20;
    gfx::text(s, 14, y, "Author :", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->author[0] ? st->author : "(unknown)", color::TEXT, color::WHITE); y += 20;
    gfx::text(s, 14, y, "Version:", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->ver[0] ? st->ver : "(?)", color::TEXT, color::WHITE); y += 26;

    if (st->app_id >= 0) {
        st->btn.x = 14; st->btn.y = y; st->btn.w = 120; st->btn.h = 28;
        st->btn.label = "Launch";
        ui::draw_button(s, st->btn);
        gfx::text(s, 150, y + 8, "bound to a built-in app - click to run", color::BLUE_LT, color::WHITE);
    } else if (st->is_bin) {
        // executable bytecode: Run button + output box
        st->btn.x = 14; st->btn.y = y; st->btn.w = 120; st->btn.h = 28;
        st->btn.label = "Run";
        ui::draw_button(s, st->btn);
        gfx::text(s, 150, y + 8, "NEFVM bytecode - click to execute", color::BLUE_LT, color::WHITE);
        y += 44;
        // output box
        gfx::fillrect(s, 14, y, w->content_w - 28, 66, 0x00F2F2F0);
        gfx::rect(s, 14, y, w->content_w - 28, 66, color::BORDER);
        gfx::text(s, 20, y + 8, st->outlen > 0 ? st->outbuf : "(no output yet - press Run)", color::TEXT, 0x00F2F2F0);
    } else {
        st->btn.x = 0; st->btn.y = 0; st->btn.w = 0; st->btn.h = 0;
        st->btn.label = 0;
        gfx::text(s, 14, y, "No built-in app bound - showing raw text.", color::RED, color::WHITE);
        y += 22;
        if (st->file && !st->file->is_dir && st->file->size > 0) {
            char* raw = (char*)kalloc((size_t)st->file->size + 1);
            if (raw) {
                memcpy(raw, st->file->data, st->file->size);
                raw[st->file->size] = 0;
                gfx::text(s, 14, y, raw, color::TEXT, color::WHITE);
                kfree(raw);
            }
        }
    }
    ksprintf(buf, sizeof(buf), "path: %s", node_path(st->file).c_str());
    gfx::text(s, 14, 200, buf, color::TEXT2, color::WHITE);
}

static void nefud_mouse(Window* w, int mx, int my, uint8_t buttons) {
    NefudState* st = (NefudState*)w->userdata;
    if (!st->btn.label) return;
    static uint8_t last = 0;
    bool pressed = buttons && !last;
    bool released = !buttons && last;
    last = buttons;
    if (ui::button_event(st->btn, mx, my, buttons, pressed, released)) {
        if (st->app_id >= 0) {
            app_launch(st->app_id);
        } else if (st->is_bin && st->file) {
            st->outlen = 0;
            st->outbuf[0] = 0;
            nefvm_run(st->file->data, st->file->size, vm_out, st);
        }
    }
}

static void nefud_close(Window* w) {
    if (w->userdata) {
        NefudState* st = (NefudState*)w->userdata;
        delete st;
        w->userdata = 0;
    }
}

// ensure the sample .bin packages exist under /usr/share/apps
static void ensure_bin_samples() {
    g_vfs->mkdir("/usr/share/apps");
    FSNode* f = g_vfs->resolve("/usr/share/apps/hello.bin");
    if (!f) {
        f = g_vfs->create_file("/usr/share/apps/hello.bin");
        if (f) {
            // header: magic + empty name + desc, then bytecode
            uint8_t pk[128];
            memset(pk, 0, sizeof(pk));
            memcpy(pk, "NEFBIN01", 8);
            const char* desc = "NEFVM demo: hello + arithmetic";
            for (int i = 0; desc[i] && i < 32; i++) pk[32 + i] = (uint8_t)desc[i];
            const char* msg = "Hello, NEFUOS!";
            int p = 64;
            for (const char* c = msg; *c; c++) { pk[p++] = 0x01; pk[p++] = (uint8_t)*c; pk[p++] = 0x09; }
            pk[p++] = 0x01; pk[p++] = 10; pk[p++] = 0x09;      // newline
            pk[p++] = 0x01; pk[p++] = 6;  pk[p++] = 0x01; pk[p++] = 7; pk[p++] = 0x06; pk[p++] = 0x08; pk[p++] = 0x0A; // 6*7
            pk[p++] = 0x01; pk[p++] = 100; pk[p++] = 0x01; pk[p++] = 8; pk[p++] = 0x05; pk[p++] = 0x08; pk[p++] = 0x0A; // 100-8
            pk[p++] = 0x00; // HALT
            g_vfs->write_file(f, pk, (uint32_t)p);
        }
    }
    FSNode* g = g_vfs->resolve("/usr/share/apps/calculator.nefud");
    if (!g) {
        g = g_vfs->create_file("/usr/share/apps/calculator.nefud");
        if (g) {
            const char* t = "NEFUD1\nname=Calculator\ndesc=Arithmetic calculator\nauthor=nefuOS\nver=1.0\n";
            g_vfs->write_file(g, (const uint8_t*)t, (uint32_t)strlen(t));
        }
    }
}

void app_show_nefud(FSNode* f) {
    if (!f) return;
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("App Package", x, y, 480, 250);
    if (!w) return;
    NefudState* st = new NefudState();
    st->file = f;
    st->btn.label = 0;
    st->outlen = 0;
    st->outbuf[0] = 0;
    nefud_parse(st);
    w->userdata = st;
    w->on_paint = nefud_paint;
    w->on_mouse = nefud_mouse;
    w->on_close = nefud_close;
}

void nefud_launch() {
    ensure_bin_samples();
    FSNode* f = g_vfs->resolve("/usr/share/apps/hello.bin");
    if (!f) f = g_vfs->resolve("/usr/share/apps/calculator.nefud");
    if (f) app_show_nefud(f);
}

} // namespace nefu
