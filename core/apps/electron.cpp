// core/apps/electron.cpp
//
// nefuOS Electron-style runtime (main / renderer model).
//
//   main     = this window (nefuOS process): owns the window, blits the
//              renderer's output to the screen, forwards input.
//   renderer = a guest Linux static ELF (typically shipped inside a .deb app)
//              that draws through the X11 wire protocol into the embedded X
//              server (core/x11).  elfrun maps the guest's socket syscalls
//              onto the server, so a real Xlib client can paint windows.
//
// Because the kernel is single-threaded, execute() runs synchronously with a
// syscall budget (see elfrun SysCtx::syscall_budget): a guest that draws once
// and exits paints its final frame into xs_fb, which this window then shows.
// Guests that would busy-wait forever (interactive event loops) are
// force-terminated by the budget and their last frame is still displayed.
//
// Terminal:  electron <path.deb|elf>      run a .deb app / ELF as a renderer
//            electron <url>               open the URL in the browser engine

#include "apps.h"
#include "../elf/elfrun.h"
#include "../x11/xserver.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../vfs/vfs.h"
#include "../klib/klib.h"
#include "../platform.h"
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace electron {

using namespace nefu::x11;

struct ElState {
    Window* w;
    char title[48];
    uint8_t* guest_mem;
};

static bool el_ends_with(const char* s, const char* suf) {
    int ls = 0, lf = 0;
    while (s[ls]) ls++;
    while (suf[lf]) lf++;
    if (lf > ls) return false;
    return memcmp(s + ls - lf, suf, (size_t)lf) == 0;
}

static void el_paint(Window* w) {
    ElState* st = (ElState*)w->userdata;
    Surface& s = w->back;
    int cw = w->content_w, ch = w->content_h;
    // scale the 1024x768 X framebuffer into the content area (letterboxed)
    float sc = (float)cw / (float)XS_W;
    if ((float)XS_H * sc > (float)ch) sc = (float)ch / (float)XS_H;
    int sw = (int)((float)XS_W * sc);
    int sh = (int)((float)XS_H * sc);
    if (sw < 1 || sh < 1) return;
    int offx = (cw - sw) / 2, offy = (ch - sh) / 2;
    for (int y = 0; y < sh; y++) {
        int sy = (int)((float)y / sc);
        if (sy >= XS_H) break;
        const uint32_t* row = xs_fb + (size_t)sy * XS_W;
        for (int x = 0; x < sw; x++) {
            int sx = (int)((float)x / sc);
            if (sx >= XS_W) break;
            s.setpx(offx + x, offy + y, row[sx]);
        }
    }
    // caption
    char cap[96];
    ksprintf(cap, sizeof(cap), "nefuOS Electron  [%s]", st ? st->title : "renderer");
    gfx::text(s, 6, 2, cap, 0x00FFFFFF, 0x00131A2A);
    gfx::text(s, cw - 170, 2, "X11 renderer: nefuOS xserver", 0x0099AACC, 0x00131A2A);
}

static void el_key(Window* w, const KeyEvent* e) {
    int ks = e->ascii ? (int)(unsigned char)e->ascii : (e->keycode + 8);
    if (ks < 1) ks = 32;
    xs_inject_key(ks, e->down ? 1 : 0);
}

static void el_mouse(Window* w, int mx, int my, uint8_t buttons) {
    (void)w;
    xs_inject_pointer(mx, my, buttons);
}

static void el_close(Window* w) {
    ElState* st = (ElState*)w->userdata;
    if (st) {
        xs_guest_close(3);
        if (st->guest_mem) kfree(st->guest_mem);
        delete st;
        w->userdata = 0;
    }
}

// Run a guest renderer ELF against the embedded X server, then show the frame.
static bool el_run_renderer(FSNode* elf, const char* title, ElState* st) {
    elf::Image img; memset(&img, 0, sizeof(img));
    if (elf::parse(elf->data, elf->size, img) != elf::E_OK) return false;
    elf::Guest g; memset(&g, 0, sizeof(g));
    if (elf::build_guest(img, title, g) != 0) return false;
    elf::SysCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.x_fd = -1;                       // socket() opens the X connection
    ctx.write_fn = 0;                    // guest stdout is discarded (GUI app)
    ctx.brk_cur = g.brk; ctx.brk_limit = g.brk_limit;
    ctx.mmap_cur = g.brk; ctx.mmap_limit = g.brk_limit;
    ctx.syscall_budget = 200000;         // guard against busy-wait renderers
    st->guest_mem = g.mem;               // freed when the window closes
    x11::xs_init();
    x11::xs_dirty_reset();
    elf::execute(g, ctx);
    return true;
}

// Public entry: launch an Electron window for a .deb app / ELF renderer.
void electron_launch_deb(const char* path) {
    if (!path || !*path) return;
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir || f->size == 0) return;

    // If it is a .deb, install it first (data.tar -> VFS), then locate the ELF.
    const char* elf_path = path;
    char vp[300];
    if (el_ends_with(path, ".deb")) {
        char msg[512];
        int n = nefud_deb_install(f, msg, (int)sizeof(msg));
        if (n < 0 || !nefud_deb_find_elf(f, vp, (int)sizeof(vp))) return;
        elf_path = vp;
        f = g_vfs->resolve(elf_path);
        if (!f || f->is_dir || f->size == 0) return;
    }

    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Electron", x, y, 880, 660);
    if (!w) return;
    ElState* st = new ElState();
    st->w = w;
    st->guest_mem = 0;
    // title = file name
    {
        const char* base = elf_path;
        const char* sl = base;
        for (const char* p = base; *p; p++) if (*p == '/' || *p == '\\') sl = p + 1;
        int len = 0;
        while (sl[len] && len < (int)sizeof(st->title) - 1) len++;
        memcpy(st->title, sl, (size_t)len);
        st->title[len] = 0;
    }
    w->userdata = st;
    w->on_paint = el_paint;
    w->on_key = el_key;
    w->on_mouse = el_mouse;
    w->on_close = el_close;
    g_wm->raise(w);
    // run the renderer (synchronous; paints its final frame into xs_fb)
    el_run_renderer(f, elf_path, st);
}

} // namespace electron
} // namespace nefu
