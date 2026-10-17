// core/x11/xserver.h
//
// nefuOS embedded X11 protocol server (Xvfb-style core subset).
//
// The guest Linux static ELF (a real Xlib client, e.g. one shipped inside a
// .deb) speaks the X11 wire protocol over a socket.  elfrun maps the guest's
// socket()/connect()/write()/read() calls onto this server: guest bytes in
// -> xs_handle() parses requests and paints into the X framebuffer; the
// kernel then blits xs_fb over the LVGL/screen surface.  Events (Expose,
// key/button/motion) are generated on the host side and fed back to the
// guest through xs_poll().
//
// Protocol coverage (a pragmatic Xvfb subset that runs typical simple Xlib
// clients): setup handshake, CreateWindow/MapWindow/ConfigureWindow/
// DestroyWindow, GC create/change/free, PolyPoint/PolyLine/PolySegment/
// PolyRectangle/PolyFillRectangle, PutImage, ImageText8, CreatePixmap/
// FreePixmap, CopyArea, ClearArea, GetGeometry/GetWindowAttributes/
// QueryTree/InternAtom/ChangeProperty, CreateColormap/AllocColor,
// QueryExtension/ListExtensions/GetKeyboardMapping/GetFontPath.
//
// All drawing is in 24bpp RGB (screen space), byte order as chosen by the
// client.  One root window, depth 24.  This is intentionally small: it is a
// rendering target, not a security boundary.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace x11 {

enum {
    XS_W = 1024,
    XS_H = 768,
    XS_MAX_WINDOWS = 64,
    XS_MAX_GC = 32,
    XS_MAX_PIXMAPS = 16,
    XS_EVQ_CAP = 4096
};

// Screen-space RGB framebuffer (packed 0x00RRGGBB).  The GUI layer blits
// the dirty range to the visible surface after each batch.
extern uint32_t xs_fb[XS_W * XS_H];
extern int xs_dirty_x0, xs_dirty_y0, xs_dirty_x1, xs_dirty_y1;

// ---- guest (elfrun) facing API ----
bool xs_connected();                       // a guest client is attached
int  xs_guest_socket(int domain, int type);   // -> guest fd, or -1
int  xs_guest_connect(int fd, const void* addr, int alen);
int  xs_guest_write(int fd, const void* buf, int len);   // requests in
int  xs_guest_read(int fd, void* buf, int len);          // replies/events out
int  xs_guest_close(int fd);

// ---- host side ----
void xs_init();
void xs_dirty_reset();

// Host-generated input events (called by the GUI/input layer).
void xs_inject_key(int keysym, int down);        // X keycode mapping
void xs_inject_pointer(int x, int y, int buttons);

// Diagnostics for the headless QEMU selftest.
extern uint32_t xs_req_count;          // requests handled
extern uint32_t xs_draw_ops;           // drawing ops painted
extern int xs_window_count;

} // namespace x11
} // namespace nefu
