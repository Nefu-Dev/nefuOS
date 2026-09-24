// nefuOS video player - LVGL-style window.
// Host (Win32): real playback of common video/audio formats via the
// Media Foundation engine (backends/win32/media_win32.cpp) — MP4/MOV/AVI/
// WMV/MPG/3GP/... whatever codecs the OS provides. "Open" opens a real file
// with the host file dialog. Bare metal: no host decoder, so the window runs
// an animated demo pattern and shows where to find real playback.
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include <cstring>

namespace nefu {

struct VideoPlayerState {
    int w, h;                 // window content size
    char host_path[260];      // real host file currently open ("" = none)
    char file_name[128];      // short name shown in the status bar
    bool host_loaded;         // platform_media_open succeeded
    bool playing;
    bool paused;
    bool has_video;           // file carries a video track
    uint8_t* frame;           // RGB32 buffer (frame_w*frame_h*4)
    int frame_w, frame_h;     // native decoded size
    Surface scaled;           // fitted preview (persistent, rebuilt in tick)
    int vol;                  // 0..100
    int pos_sec, dur_sec;     // last queried position/duration
    uint32_t demo_t0;         // demo animation clock
    int demo_x, demo_y, demo_dx, demo_dy;
};

static void vp_free_frames(VideoPlayerState* st) {
    if (st->frame) { kfree(st->frame); st->frame = 0; }
    st->frame_w = st->frame_h = 0;
    if (st->scaled.addr) { kfree(st->scaled.addr); st->scaled.addr = 0; }
    st->scaled.width = st->scaled.height = 0;
}

static void vp_open_host_file(VideoPlayerState* st, const char* path) {
    vp_free_frames(st);
    if (st->host_loaded) { platform_media_close(); st->host_loaded = false; }
    st->playing = false;
    st->paused = false;
    st->has_video = false;
    st->pos_sec = 0;
    st->dur_sec = 0;
    if (!path || !path[0]) return;

    char shortname[128];
    const char* slash = strrchr(path, '\\');
    const char* slash2 = strrchr(path, '/');
    const char* base = slash ? slash + 1 : (slash2 ? slash2 + 1 : path);
    const char* dot = strrchr(base, '.');
    int n = (int)(dot ? (dot - base) : strlen(base));
    if (n > 120) n = 120;
    memcpy(shortname, base, (size_t)n);
    shortname[n] = 0;
    ksprintf(st->file_name, sizeof(st->file_name), "%s", shortname);
    strncpy(st->host_path, path, sizeof(st->host_path) - 1);
    st->host_path[sizeof(st->host_path) - 1] = 0;

    if (!platform_media_available()) return;
    if (!platform_media_open(path, false)) return;
    st->host_loaded = true;
    st->has_video = platform_media_has_video();
    platform_media_set_volume(st->vol);
}

// ---- demo animation (no host decoder) ----
static void vp_demo_tick(VideoPlayerState* st) {
    int vw = st->w - 16, vh = st->h - 100;
    if (vw < 8 || vh < 8) return;
    st->demo_x += st->demo_dx;
    st->demo_y += st->demo_dy;
    if (st->demo_x < 0) { st->demo_x = 0; st->demo_dx = 4; }
    if (st->demo_y < 0) { st->demo_y = 0; st->demo_dy = 3; }
    if (st->demo_x + 60 > vw) { st->demo_x = vw - 60; st->demo_dx = -4; }
    if (st->demo_y + 60 > vh) { st->demo_y = vh - 60; st->demo_dy = -3; }
}

// ---- fit video frame into the viewport (fixed-point, integer only) ----
static void vp_rebuild_scaled(VideoPlayerState* st) {
    if (!st->frame || st->frame_w <= 0 || st->frame_h <= 0) return;
    int avail_w = st->w - 16;
    int avail_h = st->h - 100;
    if (avail_w < 8 || avail_h < 8) return;
    int dw = st->frame_w, dh = st->frame_h;
    if (dw > avail_w || dh > avail_h) {
        int rw = avail_w * 1024 / dw;
        int rh = avail_h * 1024 / dh;
        int r = rw < rh ? rw : rh;
        if (r < 1) r = 1;
        dw = dw * r / 1024;
        dh = dh * r / 1024;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    if (st->scaled.addr && st->scaled.width == dw && st->scaled.height == dh) {
        // same size: re-sample in place
    } else {
        if (st->scaled.addr) kfree(st->scaled.addr);
        st->scaled.addr = (uint8_t*)kalloc((size_t)dw * (size_t)dh * 4);
        if (!st->scaled.addr) return;
        st->scaled.width = dw;
        st->scaled.height = dh;
        st->scaled.pitch = dw * 4;
    }
    for (int y = 0; y < dh; y++) {
        int sy = (y * st->frame_h) / dh;
        const uint32_t* srow = (const uint32_t*)(st->frame + (size_t)sy * st->frame_w * 4);
        uint32_t* drow = (uint32_t*)(st->scaled.addr + (size_t)y * st->scaled.pitch);
        for (int x = 0; x < dw; x++) {
            int sx = (x * st->frame_w) / dw;
            drow[x] = srow[sx];
        }
    }
}

static void vp_tick(Window* w) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st) return;
    if (st->host_loaded && st->playing) {
        int fw = 0, fh = 0;
        if (platform_media_frame_info(&fw, &fh) && fw > 0 && fh > 0) {
            if (fw != st->frame_w || fh != st->frame_h || !st->frame) {
                vp_free_frames(st);
                st->frame = (uint8_t*)kalloc((size_t)fw * (size_t)fh * 4);
                st->frame_w = fw;
                st->frame_h = fh;
            }
            if (st->frame && platform_media_grab_frame(st->frame)) {
                vp_rebuild_scaled(st);
            }
        }
        int p = platform_media_position_sec();
        int d = platform_media_duration_sec();
        if (p >= 0) st->pos_sec = p;
        if (d >= 0) st->dur_sec = d;
        if (st->dur_sec > 0 && st->pos_sec >= st->dur_sec - 1) {
            st->playing = false;
            st->pos_sec = st->dur_sec;
        }
    } else if (!st->host_loaded) {
        vp_demo_tick(st);
    }
}

static void vp_paint(Window* w) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st) return;
    int W = st->w, H = st->h;
    Surface& s = w->back;
    s.fill(0x0010141A);

    // ---- toolbar ----
    const char* labels[8] = { "Open", "Play", "Pause", "Stop", "<<", ">>", "Vol-", "Vol+" };
    for (int i = 0; i < 8; i++) {
        int bx = 8 + i * 72;
        uint32_t bg = 0x003D4B66;
        if (i == 1 && st->playing) bg = 0x002F6FB6;
        gfx::fillrect(s, bx, 6, 66, 24, bg);
        gfx::rect(s, bx, 6, 66, 24, 0x002B3347);
        gfx::text(s, bx + (66 - gfx::text_width(labels[i])) / 2, 11, labels[i], color::WHITE, bg);
    }

    // ---- video viewport ----
    int vx = 8, vy = 38, vw = W - 16, vh = H - 100;
    gfx::rect(s, vx, vy, vw, vh, 0x00304050);
    gfx::fillrect(s, vx + 1, vy + 1, vw - 2, vh - 2, 0x00060A10);

    if (st->host_loaded && st->has_video && st->scaled.addr) {
        int dx = vx + (vw - st->scaled.width) / 2;
        int dy = vy + (vh - st->scaled.height) / 2;
        if (dx < vx) dx = vx;
        if (dy < vy) dy = vy;
        gfx::blit_clip(s, st->scaled, dx, dy, 0, 0, vw, vh);
    } else if (st->host_loaded && !st->has_video) {
        gfx::text(s, vx + 12, vy + 12, "Audio-only file - no video track", color::TEXT2, 0x00060A10);
        gfx::text(s, vx + 12, vy + 34, "Use the Music Player to listen", color::TEXT2, 0x00060A10);
    } else if (st->host_loaded) {
        gfx::text(s, vx + 12, vy + 12, "Decoding...", color::TEXT2, 0x00060A10);
    } else {
        // demo animation: SMPTE-ish color bars + bouncing block
        int bar_h = 40;
        const uint32_t bars[7] = { 0x00C0C0C0, 0x00C0C000, 0x0000C0C0, 0x0000C000,
                                   0x00C000C0, 0x00C00000, 0x000000C0 };
        for (int i = 0; i < 7; i++) {
            int bx = vx + 2 + i * (vw - 4) / 7;
            int bw = (vw - 4) / 7 + 1;
            gfx::fillrect(s, bx, vy + 2, bw, bar_h, bars[i]);
        }
        gfx::fillrect(s, vx + 2, vy + 2 + bar_h, vw - 4, vh - 4 - bar_h, 0x00060A10);
        gfx::fillrect(s, st->demo_x, st->demo_y, 60, 60, 0x0030A14A);
        gfx::rect(s, st->demo_x, st->demo_y, 60, 60, 0x005C9BD6);
        gfx::text(s, vx + 12, vy + 2 + bar_h + 6,
                  "DEMO PLAYBACK - no host decoder", color::YELLOW, 0x00060A10);
        gfx::text(s, vx + 12, vy + 2 + bar_h + 26,
                  "Press Open to play a real video file (Win32 host)", color::TEXT2, 0x00060A10);
    }

    // ---- progress bar ----
    int pb_y = H - 40;
    gfx::rect(s, vx, pb_y, vw, 8, 0x00304050);
    int dur = st->dur_sec > 0 ? st->dur_sec : (st->host_loaded ? 0 : 30);
    int pos = st->pos_sec;
    if (!st->host_loaded) pos = (int)((platform_tick_ms() - st->demo_t0) / 1000) % (dur > 0 ? dur : 30);
    if (dur > 0) {
        int fw = vw * pos / dur;
        if (fw > vw) fw = vw;
        if (fw > 0) gfx::fillrect(s, vx, pb_y, fw, 8, color::BLUE_LT);
    }
    char tbuf[64];
    int pm = pos / 60, ps = pos % 60;
    int dm = dur / 60, ds = dur % 60;
    ksprintf(tbuf, sizeof(tbuf), "%02d:%02d / %02d:%02d", pm, ps, dm, ds);
    gfx::text(s, vx, pb_y + 12, tbuf, color::TEXT2, 0x0010141A);

    // ---- status line ----
    gfx::hline(s, 8, W - 8, H - 24, 0x002A323C);
    char sbuf[200];
    const char* mode = st->host_loaded ? "host decoder (Media Foundation)" : "demo mode";
    const char* state = st->playing ? "playing" : (st->paused ? "paused" : "stopped");
    if (st->host_loaded) {
        ksprintf(sbuf, sizeof(sbuf), "%s  [%s]  %s", st->file_name, mode, state);
    } else {
        ksprintf(sbuf, sizeof(sbuf), "no media  [%s]  %s", mode, state);
    }
    gfx::text(s, 8, H - 20, sbuf, color::TEXT2, 0x0010141A);
    gfx::text(s, W / 2, H - 20, "Space play/pause  <- -> seek  +/- vol  O open", color::TEXT2, 0x0010141A);
}

static void vp_mouse(Window* w, int mx, int my, uint8_t buttons) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st || !buttons) return;
    if (my >= 6 && my < 30) {
        for (int i = 0; i < 8; i++) {
            int bx = 8 + i * 72;
            if (mx >= bx && mx < bx + 66) {
                if (i == 0) {                       // Open
                    char path[260];
                    if (platform_host_file_dialog(path, sizeof(path),
                        "Video files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.mpg;*.mpeg;*.m4v;*.webm;*.3gp;*.flv;*.ts;*.ogv\0All files\0*.*\0\0", "*.mp4;*.avi"))
                        vp_open_host_file(st, path);
                    if (st->host_loaded) { st->playing = true; st->paused = false; platform_media_play(); }
                } else if (i == 1) {                // Play
                    if (st->host_loaded) { st->playing = true; st->paused = false; platform_media_play(); }
                } else if (i == 2) {                // Pause
                    if (st->host_loaded && st->playing) { st->playing = false; st->paused = true; platform_media_pause(); }
                } else if (i == 3) {                // Stop
                    if (st->host_loaded) { st->playing = false; st->paused = false; st->pos_sec = 0; platform_media_stop(); }
                } else if (i == 4) {                // seek -5
                    if (st->host_loaded) platform_media_seek_sec(st->pos_sec - 5);
                } else if (i == 5) {                // seek +5
                    if (st->host_loaded) platform_media_seek_sec(st->pos_sec + 5);
                } else if (i == 6) {                // Vol-
                    st->vol -= 10; if (st->vol < 0) st->vol = 0;
                    platform_media_set_volume(st->vol);
                } else {                            // Vol+
                    st->vol += 10; if (st->vol > 100) st->vol = 100;
                    platform_media_set_volume(st->vol);
                }
                return;
            }
        }
    }
    // click inside the viewport toggles play/pause
    int vx = 8, vy = 38, vw = st->w - 16, vh = st->h - 100;
    if (mx >= vx && mx < vx + vw && my >= vy && my < vy + vh) {
        if (st->host_loaded) {
            if (st->playing) { st->playing = false; st->paused = true; platform_media_pause(); }
            else { st->playing = true; st->paused = false; platform_media_play(); }
        }
        return;
    }
    // click on the progress bar seeks
    int pb_y = st->h - 40;
    if (my >= pb_y && my < pb_y + 8 && st->host_loaded) {
        int dur = st->dur_sec;
        if (dur > 0) {
            int vw2 = st->w - 16;
            int rel = mx - vx;
            if (rel < 0) rel = 0;
            if (rel > vw2) rel = vw2;
            platform_media_seek_sec(rel * dur / vw2);
        }
    }
}

static void vp_key(Window* w, const KeyEvent* e) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st || !e->down) return;
    if (e->keycode == KEY_SPACE || e->ascii == ' ') {
        if (st->host_loaded) {
            if (st->playing) { st->playing = false; st->paused = true; platform_media_pause(); }
            else { st->playing = true; st->paused = false; platform_media_play(); }
        }
    } else if (e->keycode == KEY_LEFT) {
        if (st->host_loaded) platform_media_seek_sec(st->pos_sec - 5);
    } else if (e->keycode == KEY_RIGHT) {
        if (st->host_loaded) platform_media_seek_sec(st->pos_sec + 5);
    } else if (e->keycode == KEY_UP) {
        st->vol += 10; if (st->vol > 100) st->vol = 100;
        platform_media_set_volume(st->vol);
    } else if (e->keycode == KEY_DOWN) {
        st->vol -= 10; if (st->vol < 0) st->vol = 0;
        platform_media_set_volume(st->vol);
    } else if (e->ascii == '+' || e->ascii == '=') {
        st->vol += 10; if (st->vol > 100) st->vol = 100;
        platform_media_set_volume(st->vol);
    } else if (e->ascii == '-' || e->ascii == '_') {
        st->vol -= 10; if (st->vol < 0) st->vol = 0;
        platform_media_set_volume(st->vol);
    } else if (e->ascii == 'o' || e->ascii == 'O') {
        char path[260];
        if (platform_host_file_dialog(path, sizeof(path),
            "Video files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.mpg;*.mpeg;*.m4v;*.webm;*.3gp;*.flv;*.ts;*.ogv\0All files\0*.*\0\0", "*.mp4;*.avi")) {
            vp_open_host_file(st, path);
            if (st->host_loaded) { st->playing = true; st->paused = false; platform_media_play(); }
        }
    }
}

static void vp_close(Window* w) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st) return;
    if (st->host_loaded) { platform_media_close(); st->host_loaded = false; }
    vp_free_frames(st);
}

// ---- test hook: --media (Win32 host builds) autoplays a real file ----
static const char* s_autopath = 0;

static void video_player_open(FSNode* target) {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Video Player", x, y, 640, 460);
    if (!w) return;
    VideoPlayerState* st = new VideoPlayerState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->host_path[0] = 0;
    st->file_name[0] = 0;
    st->host_loaded = false;
    st->playing = false;
    st->paused = false;
    st->has_video = false;
    st->frame = 0;
    st->frame_w = st->frame_h = 0;
    st->scaled.addr = 0;
    st->scaled.width = st->scaled.height = 0;
    st->vol = 70;
    st->pos_sec = 0;
    st->dur_sec = 0;
    st->demo_t0 = platform_tick_ms();
    st->demo_x = 20;
    st->demo_y = 80;
    st->demo_dx = 4;
    st->demo_dy = 3;
    (void)target;   // VFS files have no real host path; use Open for real files
    w->userdata = st;
    w->on_paint = vp_paint;
    w->on_tick = vp_tick;
    w->on_mouse = vp_mouse;
    w->on_key = vp_key;
    w->on_close = vp_close;
    g_wm->raise(w);
    if (s_autopath && s_autopath[0]) {
        // --media test hook: open and start the file right after launch
        vp_open_host_file(st, s_autopath);
        if (st->host_loaded) {
            st->playing = true;
            st->paused = false;
            platform_media_play();
        }
        s_autopath = 0;
    }
}

void video_player_launch() { video_player_open(0); }
void app_show_video(FSNode* f) { video_player_open(f); }

// ---- test hook: --media (Win32 host builds) autoplays a real file ----
void video_player_autoplay(const char* host_path) { s_autopath = host_path; }

} // namespace nefu
