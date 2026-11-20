/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS video player - LVGL-style window.
// Host (Win32): real playback of common video/audio formats via the
// Media Foundation engine (backends/win32/media_win32.cpp) — MP4/MOV/AVI/
// WMV/MPG/3GP/... whatever codecs the OS provides. "Open" opens a real file
// with the host file dialog. Bare metal: no host decoder, so the window plays
// real WAV files through the integer audio engine (AC'97 / HD Audio) with a
// live waveform, seek and volume controls; the Open button routes to the file
// manager in that build.
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../audio/audio_engine.h"
#include "../audio/audio_manager.h"   // g_audio / AudioFormat for the demo track
#include "qoi_codec.h"
#include <cstring>

#ifdef NEFU_BARE
#include "nvid_demo.h"               // built-in demo video (QOI frames)
#endif

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
    // bare/real WAV playback via the integer audio engine (host + bare)
    const uint8_t* bare_data;     // VFS buffer (borrowed, not owned)
    uint32_t       bare_size;
    uint32_t       bare_rate;     // sample rate from the WAV header
    uint32_t       bare_channels;
    uint32_t       bare_pcm_off;  // offset of the PCM payload
    uint32_t       bare_frames;   // total frames in the payload
    bool           bare_loaded;
    bool           demo_sound_on; // demo tone currently streaming to the card
#ifdef NEFU_BARE
    // ---- bare video: NVID container (QOI frames), decoded on demand ----
    const uint8_t* vid_src;       // container bytes (borrowed)
    uint32_t       vid_src_size;
    uint32_t       vid_w, vid_h;  // frame size
    uint32_t       vid_fps;       // playback rate
    uint32_t       vid_count;     // frame count in the container
    uint32_t       vid_off[96];   // per-frame QOI blob offset (relative to vid_src)
    uint32_t       vid_len[96];   // per-frame QOI blob length
    uint8_t*       vid_rgba;      // decode scratch, vid_w*vid_h*4
    int            vid_idx;       // index of the frame currently in st->frame
    uint32_t       vid_ms;        // played milliseconds (pause aware)
    uint32_t       vid_last_tick;
    bool           vid_loaded;
    bool           vid_own_src;   // container was allocated by us (VFS copy)
#endif
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
// `allow_up` lets a small bare-metal frame fill the viewport; the sampler then
// averages the 2x2 source block under the target pixel so the upscale does not
// look like a pile of blocks.
static void vp_rebuild_scaled(VideoPlayerState* st, bool allow_up) {
    if (!st->frame || st->frame_w <= 0 || st->frame_h <= 0) return;
    int avail_w = st->w - 16;
    int avail_h = st->h - 100;
    if (avail_w < 8 || avail_h < 8) return;
    int dw = st->frame_w, dh = st->frame_h;
    if (dw > avail_w || dh > avail_h || allow_up) {
        int rw = avail_w * 1024 / dw;
        int rh = avail_h * 1024 / dh;
        int r = rw < rh ? rw : rh;
        if (r < 1) r = 1;
        if (!allow_up && r > 1024) r = 1024;      // never enlarge the host decoder
        if (allow_up && r > 8 * 1024) r = 8 * 1024;
        dw = dw * r / 1024;
        dh = dh * r / 1024;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    } else {
        dw = st->frame_w;
        dh = st->frame_h;
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
    bool up = (dw > st->frame_w);
    for (int y = 0; y < dh; y++) {
        int sy = (y * st->frame_h) / dh;
        const uint32_t* srow = (const uint32_t*)(st->frame + (size_t)sy * st->frame_w * 4);
        uint32_t* drow = (uint32_t*)(st->scaled.addr + (size_t)y * st->scaled.pitch);
        if (!up) {
            for (int x = 0; x < dw; x++) {
                int sx = (x * st->frame_w) / dw;
                drow[x] = srow[sx];
            }
        } else {
            for (int x = 0; x < dw; x++) {
                int sx = (x * st->frame_w) / dw;
                int sx2 = sx + 1; if (sx2 >= st->frame_w) sx2 = sx;
                int sy2 = sy + 1; if (sy2 >= st->frame_h) sy2 = sy;
                const uint32_t* srow2 = (const uint32_t*)(st->frame + (size_t)sy2 * st->frame_w * 4);
                uint32_t a = srow[sx], b = srow[sx2], c = srow2[sx], d = srow2[sx2];
                /* 2x2 box average, channel by channel (no float maths on bare) */
                uint32_t r = (((a >> 16) & 0xFF) + ((b >> 16) & 0xFF) + ((c >> 16) & 0xFF) + ((d >> 16) & 0xFF)) >> 2;
                uint32_t g = (((a >> 8) & 0xFF) + ((b >> 8) & 0xFF) + ((c >> 8) & 0xFF) + ((d >> 8) & 0xFF)) >> 2;
                uint32_t bl = ((a & 0xFF) + (b & 0xFF) + (c & 0xFF) + (d & 0xFF)) >> 2;
                drow[x] = (r << 16) | (g << 8) | bl;
            }
        }
    }
}

#ifdef NEFU_BARE
// ---- bare metal: real WAV playback ----

// Parse a RIFF/WAVE header.  Only 16-bit PCM is supported (what the engine and
// the demo generator produce).  Returns true on success and fills *rate,
// *channels, *pcm_off (byte offset of the payload) and *frames.
static bool vp_wav_info(const uint8_t* d, uint32_t size,
                        uint32_t* rate, uint32_t* channels,
                        uint32_t* pcm_off, uint32_t* frames) {
    if (!d || size < 44) return false;
    if (memcmp(d, "RIFF", 4) != 0) return false;
    if (memcmp(d + 8, "WAVE", 4) != 0) return false;
    uint32_t off = 12;
    while (off + 8 <= size) {
        uint32_t ck_id = *(const uint32_t*)(d + off);
        uint32_t ck_len = *(const uint32_t*)(d + off + 4);
        if (ck_id == 0x20746D66u) {                 // "fmt "
            if (off + 8 + ck_len > size || ck_len < 16) return false;
            uint16_t fmt = *(const uint16_t*)(d + off + 8);
            uint16_t ch  = *(const uint16_t*)(d + off + 10);
            uint32_t r   = *(const uint32_t*)(d + off + 12);
            uint16_t bits = *(const uint16_t*)(d + off + 8 + 14);
            if (fmt != 1 || bits != 16) return false;   // PCM 16-bit only
            *rate = r;
            *channels = ch > 0 ? ch : 1u;
        } else if (ck_id == 0x61746164u) {          // "data"
            *pcm_off = off + 8;
            uint32_t avail = size - (off + 8);
            uint32_t len = ck_len < avail ? ck_len : avail;
            uint32_t bpf = *channels * 2u;
            if (bpf == 0) return false;
            *frames = len / bpf;
            return true;
        }
        off += 8 + ck_len + (ck_len & 1u);          // chunks are word-aligned
    }
    return false;
}

// Open a WAV buffer through the integer audio engine and start it.
static void vp_bare_open(VideoPlayerState* st, const uint8_t* data, uint32_t size, const char* name) {
    st->bare_data = 0;
    st->bare_size = 0;
    st->bare_rate = 0;
    st->bare_channels = 0;
    st->bare_pcm_off = 0;
    st->bare_frames = 0;
    st->bare_loaded = false;
    if (!data || size < 44 || !name || !name[0]) return;
    uint32_t rate = 0, ch = 0, off = 0, frames = 0;
    if (!vp_wav_info(data, size, &rate, &ch, &off, &frames)) return;
    if (rate == 0 || frames == 0) return;
    st->bare_data = data;
    st->bare_size = size;
    st->bare_rate = rate;
    st->bare_channels = ch;
    st->bare_pcm_off = off;
    st->bare_frames = frames;
    st->file_name[0] = 0;
    strncpy(st->file_name, name, sizeof(st->file_name) - 1);
    st->file_name[sizeof(st->file_name) - 1] = 0;
    if (!nefu::audio::media_open_mem(data, size)) return;
    st->bare_loaded = true;
    st->pos_sec = 0;
    st->dur_sec = (int)(frames / (rate > 0 ? rate : 1u));
    st->has_video = false;
    nefu::audio::media_set_volume(st->vol);
    nefu::audio::media_play();
    st->playing = true;
    st->paused = false;
}

static void vp_bare_play(VideoPlayerState* st) {
    if (!st->bare_loaded) return;
    st->playing = true;
    st->paused = false;
    nefu::audio::media_play();
}

static void vp_bare_pause(VideoPlayerState* st) {
    if (!st->bare_loaded) return;
    st->playing = false;
    st->paused = true;
    nefu::audio::media_pause();
}

static void vp_bare_stop(VideoPlayerState* st) {
    if (!st->bare_loaded) return;
    st->playing = false;
    st->paused = false;
    st->pos_sec = 0;
    nefu::audio::media_stop();
}

static void vp_bare_seek(VideoPlayerState* st, int sec) {
    if (!st->bare_loaded) return;
    if (sec < 0) sec = 0;
    nefu::audio::media_seek_sec(sec);
}

// Demo tone that actually reaches the sound card: a 22050 Hz integer triangle
// wave (440 -> 660 Hz over 3 s, looping), streamed through the mixer so the
// hardware pump feeds it to the AC'97 / HDA DMA engine.
static void vp_demo_sound_start(VideoPlayerState* st) {
    if (st->demo_sound_on) return;
    if (!nefu::audio::engine_init()) return;
    int sid = nefu::audio::g_audio.create_stream(nefu::audio::AUDIO_FORMAT_PCM_S16_LE, 22050, 1, 22050 * 3);
    if (sid < 0) return;
    uint32_t n = 22050u * 3u;
    int16_t* buf = (int16_t*)kalloc(n * 2u);
    if (!buf) return;
    for (uint32_t i = 0; i < n; i++) {
        int32_t period = 22050;
        uint32_t ph = i % (uint32_t)period;
        int32_t half = period / 2;
        int32_t v;
        if (ph < (uint32_t)half) v = ph * 32767 / (half > 0 ? half : 1);
        else v = ((int32_t)period - (int32_t)ph) * 32767 / (half > 0 ? half : 1);
        // 440 -> 660 Hz glide: frequency = 440 + 220 * (i / n)
        // period in samples = 22050 / freq; approximate with the 440 Hz table
        // and a linear phase multiplier for a smooth glide
        uint32_t freq = 440u + (220u * i / (n > 0 ? n : 1u));
        int32_t scale = 22050 * 4096 / (int32_t)freq;         // fixed point
        int32_t ph2 = (int32_t)(((int64_t)i * scale) >> 12) % 22050;
        int32_t half2 = 11025;
        if (ph2 < half2) v = ph2 * 32767 / half2;
        else v = (22050 - ph2) * 32767 / half2;
        buf[i] = (int16_t)(v >> 1);                            // half volume
    }
    nefu::audio::g_audio.load_stream_data(sid, buf, n * 2u);
    nefu::audio::g_audio.set_stream_looping(sid, true);
    nefu::audio::g_audio.play_stream(sid);
    kfree(buf);
    st->demo_sound_on = true;
}

// ---- bare video: NVID container (QOI frames), decoded on demand ----
// Container: "NVID" | u16 w | u16 h | u16 fps | u16 count |
//            count x (u32 blob_len, blob...)
// Frames decode straight into the player's RGB32 frame buffer, so the existing
// fit/blit pipeline and the progress bar work untouched.
static void vp_bare_video_show(VideoPlayerState* st, int idx);

static void vp_bare_video_close(VideoPlayerState* st) {
    st->vid_loaded = false;
    st->vid_src = 0;
    st->vid_src_size = 0;
    st->vid_count = 0;
    st->vid_idx = -1;
    st->vid_ms = 0;
    if (st->vid_rgba) { kfree(st->vid_rgba); st->vid_rgba = 0; }
}

static bool vp_bare_video_open(VideoPlayerState* st, const uint8_t* data, uint32_t size, const char* name) {
    if (st->vid_loaded) vp_bare_video_close(st);
    if (!data || size < 12) return false;
    if (memcmp(data, "NVID", 4) != 0) return false;
    uint16_t w = (uint16_t)(data[4] | (data[5] << 8));
    uint16_t h = (uint16_t)(data[6] | (data[7] << 8));
    uint16_t fps = (uint16_t)(data[8] | (data[9] << 8));
    uint16_t count = (uint16_t)(data[10] | (data[11] << 8));
    if (w == 0 || h == 0 || fps == 0 || count == 0 || count > 96) return false;

    uint32_t off = 12;
    uint32_t got = 0;
    while (got < count) {
        if (off + 4 > size) return false;
        uint32_t len = (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8)
                     | ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
        off += 4;
        if (len == 0 || off + len > size) return false;
        st->vid_off[got] = off;
        st->vid_len[got] = len;
        off += len;
        got++;
    }

    st->vid_src = data;
    st->vid_src_size = size;
    st->vid_w = w;
    st->vid_h = h;
    st->vid_fps = fps;
    st->vid_count = count;
    st->vid_idx = -1;
    st->vid_ms = 0;
    st->vid_last_tick = platform_tick_ms();

    if (!st->vid_rgba) st->vid_rgba = (uint8_t*)kalloc((size_t)w * (size_t)h * 4u);
    if (!st->vid_rgba) return false;
    vp_free_frames(st);
    st->frame = (uint8_t*)kalloc((size_t)w * (size_t)h * 4u);
    if (!st->frame) return false;
    st->frame_w = w;
    st->frame_h = h;

    st->vid_loaded = true;
    st->file_name[0] = 0;
    if (name && name[0]) strncpy(st->file_name, name, sizeof(st->file_name) - 1);
    st->file_name[sizeof(st->file_name) - 1] = 0;
    st->dur_sec = (int)(count / (fps ? fps : 1u));
    st->pos_sec = 0;
    st->has_video = true;
    st->playing = true;
    st->paused = false;
    vp_bare_video_show(st, 0);
    return true;
}

static void vp_bare_video_show(VideoPlayerState* st, int idx) {
    if (!st->vid_loaded || !st->vid_rgba || !st->frame) return;
    if (idx < 0) idx = 0;
    if ((uint32_t)idx >= st->vid_count) idx = (int)(st->vid_count - 1);
    uint32_t cap = st->vid_w * st->vid_h * 4u;
    if (!qoi_decode(st->vid_src + st->vid_off[idx], st->vid_len[idx], st->vid_rgba, cap)) return;
    uint32_t n = st->vid_w * st->vid_h;
    uint32_t* dst = (uint32_t*)st->frame;
    const uint8_t* src = st->vid_rgba;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = ((uint32_t)src[i * 4] << 16) | ((uint32_t)src[i * 4 + 1] << 8) | src[i * 4 + 2];
    }
    st->vid_idx = idx;
    vp_rebuild_scaled(st, true);
}

static void vp_bare_video_seek_ms(VideoPlayerState* st, uint32_t ms) {
    if (!st->vid_loaded) return;
    st->vid_ms = ms;
    st->vid_last_tick = platform_tick_ms();
}
#endif

static void vp_tick(Window* w) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st) return;
    st->w = w->content_w;      // window may have resized since open
    st->h = w->content_h;
#ifdef NEFU_BARE
    if (st->vid_loaded) {
        uint32_t now = platform_tick_ms();
        uint32_t dt = now - st->vid_last_tick;
        st->vid_last_tick = now;
        if (st->playing && dt < 1000) st->vid_ms += dt;   // ignore long stalls
        uint32_t dur = st->vid_count * 1000u / (st->vid_fps ? st->vid_fps : 1u);
        if (dur == 0) dur = 1;
        uint32_t looped = st->vid_ms % dur;
        int idx = (int)(looped * st->vid_fps / 1000u);
        if (idx >= (int)st->vid_count) idx = (int)st->vid_count - 1;
        if (idx != st->vid_idx) vp_bare_video_show(st, idx);
        st->pos_sec = (int)(looped / 1000u);
        st->dur_sec = (int)(dur / 1000u);
        return;
    }
    if (st->bare_loaded) {
        if (st->playing) {
            int p = nefu::audio::media_position_sec();
            int d = nefu::audio::media_duration_sec();
            if (p >= 0) st->pos_sec = p;
            if (d >= 0) st->dur_sec = d;
            if (st->dur_sec > 0 && st->pos_sec >= st->dur_sec - 1) {
                st->playing = false;
                st->paused = false;
                st->pos_sec = st->dur_sec;
                nefu::audio::media_stop();
            }
        }
        return;
    }
    if (!nefu::audio::engine_available() && !st->demo_sound_on) vp_demo_sound_start(st);
#endif
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
                vp_rebuild_scaled(st, false);
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
    st->w = w->content_w;
    st->h = w->content_h;
    int W = st->w, H = st->h;
    Surface& s = w->back;
    s.fill(0x0010141A);

    // ---- toolbar ----
    const char* labels[8] = { T("打开", "Open"), T("播放", "Play"), T("暂停", "Pause"),
                              T("停止", "Stop"), "<<", ">>",
                              T("音量-", "Vol-"), T("音量+", "Vol+") };
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

#ifdef NEFU_BARE
    // ---- bare video: decoded frames from the built-in NVID clip ----
    if (st->vid_loaded && st->scaled.addr) {
        int dx = vx + (vw - st->scaled.width) / 2;
        int dy = vy + (vh - st->scaled.height) / 2;
        if (dx < vx) dx = vx;
        if (dy < vy) dy = vy;
        gfx::blit_clip(s, st->scaled, dx, dy, 0, 0, vw, vh);
        gfx::text(s, vx + 8, vy + vh - 36, T("内置演示视频 - 裸机 QOI 解码",
                                             "built-in demo clip - bare QOI decoder"),
                  color::YELLOW, 0);
        gfx::text(s, vx + 8, vy + vh - 20, st->file_name, color::TEXT2, 0);
    } else
#endif
#ifdef NEFU_BARE
    // ---- real audio playback: live waveform of the current window ----
    if (st->bare_loaded && st->bare_data && st->bare_rate > 0) {
        gfx::text(s, vx + 12, vy + 12, "Audio track - real playback (AC'97/HD Audio)", color::YELLOW, 0x00060A10);
        gfx::text(s, vx + 12, vy + 30, st->file_name, color::TEXT2, 0x00060A10);
        int wave_x = vx + 12, wave_y = vy + 46;
        int wave_w = vw - 24, wave_h = vh - 66;
        if (wave_w > 8 && wave_h > 8) {
            gfx::fillrect(s, wave_x, wave_y, wave_w, wave_h, 0x000A1220);
            gfx::rect(s, wave_x, wave_y, wave_w, wave_h, 0x00304050);
            const int16_t* pcm = (const int16_t*)(st->bare_data + st->bare_pcm_off);
            uint32_t ch = st->bare_channels > 1 ? st->bare_channels : 1;
            uint32_t pos = (uint32_t)st->pos_sec * st->bare_rate;
            if (pos > st->bare_frames) pos = st->bare_frames;
            uint32_t win = st->bare_rate / 2u;               // 0.5 s window
            if (win > st->bare_frames) win = st->bare_frames;
            uint32_t start = (pos > win / 2u) ? pos - win / 2u : 0u;
            uint32_t seg = 8u;                               // frames per column
            uint32_t cols = (uint32_t)(wave_w / 2);
            if (cols > 512) cols = 512;
            for (uint32_t c = 0; c < cols; c++) {
                uint32_t f0 = start + c * seg;
                if (f0 + seg > st->bare_frames) break;
                int32_t peak = 0;
                for (uint32_t f = f0; f < f0 + seg; f++) {
                    int32_t l = pcm[f * ch];
                    int32_t r = ch > 1 ? pcm[f * ch + 1] : l;
                    int32_t m = (l + r) / 2;
                    if (m < 0) m = -m;
                    if (m > peak) peak = m;
                }
                int h = (int)(((int64_t)peak * wave_h) / 32768);
                if (h < 1) h = 1;
                if (h > wave_h) h = wave_h;
                gfx::fillrect(s, wave_x + (int)c * 2, wave_y + wave_h - h, 1, h, 0x0030A14A);
            }
            // playhead marker
            if (win > 0 && start + win <= st->bare_frames) {
                int px = wave_x + (int)(((int64_t)(pos - start) * wave_w) / win);
                if (px >= wave_x && px < wave_x + wave_w)
                    gfx::vline(s, px, wave_y, wave_h, 0x005C9BD6);
            }
        }
        gfx::text(s, vx + 12, vy + vh - 18, "Space play/pause  <- -> seek  +/- vol", color::TEXT2, 0x00060A10);
    } else
#endif
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
                  "DEMO PLAYBACK - synthesized tone", color::YELLOW, 0x00060A10);
        gfx::text(s, vx + 12, vy + 2 + bar_h + 26,
                  "Open a WAV from the file manager to play it for real", color::TEXT2, 0x00060A10);
    }

    // ---- progress bar ----
    int pb_y = H - 40;
    gfx::rect(s, vx, pb_y, vw, 8, 0x00304050);
    int dur = st->dur_sec > 0 ? st->dur_sec : ((st->host_loaded || st->bare_loaded) ? 0 : 30);
    int pos = st->pos_sec;
#ifdef NEFU_BARE
    if (!st->host_loaded && !st->bare_loaded && !st->vid_loaded)
        pos = (int)((platform_tick_ms() - st->demo_t0) / 1000) % (dur > 0 ? dur : 30);
#else
    if (!st->host_loaded) pos = (int)((platform_tick_ms() - st->demo_t0) / 1000) % (dur > 0 ? dur : 30);
#endif
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
    const char* mode = st->host_loaded ? T("系统解码 (Media Foundation)", "host decoder (Media Foundation)")
#ifdef NEFU_BARE
                      : (st->vid_loaded ? T("裸机 QOI 视频解码", "bare QOI video decoder")
                                        : (st->bare_loaded ? T("音频 (AC'97/HD Audio)", "real audio (AC'97/HD Audio)")
                                                           : T("演示模式", "demo mode")));
#else
                      : T("演示模式", "demo mode");
#endif
    const char* state = st->playing ? T("播放中", "playing")
                                    : (st->paused ? T("已暂停", "paused") : T("已停止", "stopped"));
    bool media_open = st->host_loaded || st->bare_loaded;
#ifdef NEFU_BARE
    if (st->vid_loaded) media_open = true;
#endif
    if (media_open) {
        ksprintf(sbuf, sizeof(sbuf), "%s  [%s]  %s", st->file_name, mode, state);
    } else {
        ksprintf(sbuf, sizeof(sbuf), T("无媒体  [%s]  %s", "no media  [%s]  %s"), mode, state);
    }
    gfx::text(s, 8, H - 20, sbuf, color::TEXT2, 0x0010141A);
    const char* hint = T("空格 暂停  ←→ 跳转  +/- 音量",
                         "Space pause  ←→ seek  +/- vol");
    int hw = gfx::text_width(hint);
    int sw = gfx::text_width(sbuf);
    int hx = W - hw - 8;                     // right-aligned, only if it clears the status text
    if (hx > 8 + sw + 12) gfx::text(s, hx, H - 20, hint, color::TEXT2, 0x0010141A);
}

static void vp_mouse(Window* w, int mx, int my, uint8_t buttons) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st || !buttons) return;
    st->w = w->content_w;
    st->h = w->content_h;
    if (my >= 6 && my < 30) {
        for (int i = 0; i < 8; i++) {
            int bx = 8 + i * 72;
            if (mx >= bx && mx < bx + 66) {
                if (i == 0) {                       // Open
#ifdef NEFU_BARE
                    // bare metal: no host file dialog - (re)load the built-in clip
                    if (vp_bare_video_open(st, nefu::g_nvid_demo,
                                           (uint32_t)nefu::g_nvid_demo_size,
                                           T("内置演示视频 (NVID)", "built-in demo clip (NVID)")))
                        st->vid_last_tick = platform_tick_ms();
#else
                    char path[260];
                    if (platform_host_file_dialog(path, sizeof(path),
                        "Video files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.mpg;*.mpeg;*.m4v;*.webm;*.3gp;*.flv;*.ts;*.ogv\0All files\0*.*\0\0", "*.mp4;*.avi"))
                        vp_open_host_file(st, path);
                    if (st->host_loaded) { st->playing = true; st->paused = false; platform_media_play(); }
#endif
                } else if (i == 1) {                // Play
#ifdef NEFU_BARE
                    if (st->vid_loaded) { st->playing = true; st->paused = false; st->vid_last_tick = platform_tick_ms(); }
                    else if (st->bare_loaded) { st->playing = true; st->paused = false; vp_bare_play(st); }
#else
                    if (st->host_loaded) { st->playing = true; st->paused = false; platform_media_play(); }
#endif
                } else if (i == 2) {                // Pause
#ifdef NEFU_BARE
                    if (st->vid_loaded) { if (st->playing) { st->playing = false; st->paused = true; } }
                    else if (st->bare_loaded && st->playing) vp_bare_pause(st);
#else
                    if (st->host_loaded && st->playing) { st->playing = false; st->paused = true; platform_media_pause(); }
#endif
                } else if (i == 3) {                // Stop
#ifdef NEFU_BARE
                    if (st->vid_loaded) {
                        st->playing = false; st->paused = false;
                        vp_bare_video_seek_ms(st, 0);
                        vp_bare_video_show(st, 0);
                    } else if (st->bare_loaded) vp_bare_stop(st);
#else
                    if (st->host_loaded) { st->playing = false; st->paused = false; st->pos_sec = 0; platform_media_stop(); }
#endif
                } else if (i == 4) {                // seek -5
#ifdef NEFU_BARE
                    if (st->vid_loaded) {
                        int t = st->pos_sec - 5;
                        if (t < 0) t = 0;
                        vp_bare_video_seek_ms(st, (uint32_t)t * 1000u);
                    } else if (st->bare_loaded) vp_bare_seek(st, st->pos_sec - 5);
#else
                    if (st->host_loaded) platform_media_seek_sec(st->pos_sec - 5);
#endif
                } else if (i == 5) {                // seek +5
#ifdef NEFU_BARE
                    if (st->vid_loaded) {
                        vp_bare_video_seek_ms(st, (uint32_t)(st->pos_sec + 5) * 1000u);
                    } else if (st->bare_loaded) vp_bare_seek(st, st->pos_sec + 5);
#else
                    if (st->host_loaded) platform_media_seek_sec(st->pos_sec + 5);
#endif
                } else if (i == 6) {                // Vol-
                    st->vol -= 10; if (st->vol < 0) st->vol = 0;
#ifdef NEFU_BARE
                    if (st->bare_loaded) nefu::audio::media_set_volume(st->vol);
#else
                    platform_media_set_volume(st->vol);
#endif
                } else {                            // Vol+
                    st->vol += 10; if (st->vol > 100) st->vol = 100;
#ifdef NEFU_BARE
                    if (st->bare_loaded) nefu::audio::media_set_volume(st->vol);
#else
                    platform_media_set_volume(st->vol);
#endif
                }
                return;
            }
        }
    }
    // click inside the viewport toggles play/pause
    int vx = 8, vy = 38, vw = st->w - 16, vh = st->h - 100;
    if (mx >= vx && mx < vx + vw && my >= vy && my < vy + vh) {
#ifdef NEFU_BARE
        if (st->vid_loaded) {
            if (st->playing) { st->playing = false; st->paused = true; }
            else { st->playing = true; st->paused = false; st->vid_last_tick = platform_tick_ms(); }
        } else if (st->bare_loaded) {
            if (st->playing) vp_bare_pause(st);
            else { st->playing = true; st->paused = false; vp_bare_play(st); }
        }
#else
        if (st->host_loaded) {
            if (st->playing) { st->playing = false; st->paused = true; platform_media_pause(); }
            else { st->playing = true; st->paused = false; platform_media_play(); }
        }
#endif
        return;
    }
    // click on the progress bar seeks
    int pb_y = st->h - 40;
    if (my >= pb_y && my < pb_y + 8) {
        int dur = st->dur_sec;
        if (dur > 0 && (st->host_loaded || st->bare_loaded
#ifdef NEFU_BARE
                        || st->vid_loaded
#endif
                        )) {
            int vw2 = st->w - 16;
            int rel = mx - vx;
            if (rel < 0) rel = 0;
            if (rel > vw2) rel = vw2;
#ifdef NEFU_BARE
            if (st->vid_loaded) vp_bare_video_seek_ms(st, (uint32_t)(rel * dur / vw2) * 1000u);
            else if (st->bare_loaded) vp_bare_seek(st, rel * dur / vw2);
            else
#endif
            if (st->host_loaded) platform_media_seek_sec(rel * dur / vw2);
        }
    }
}

static void vp_key(Window* w, const KeyEvent* e) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st || !e->down) return;
    if (e->keycode == KEY_SPACE || e->ascii == ' ') {
#ifdef NEFU_BARE
        if (st->vid_loaded) {
            if (st->playing) { st->playing = false; st->paused = true; }
            else { st->playing = true; st->paused = false; st->vid_last_tick = platform_tick_ms(); }
        } else if (st->bare_loaded) {
            if (st->playing) vp_bare_pause(st);
            else { st->playing = true; st->paused = false; vp_bare_play(st); }
        }
#else
        if (st->host_loaded) {
            if (st->playing) { st->playing = false; st->paused = true; platform_media_pause(); }
            else { st->playing = true; st->paused = false; platform_media_play(); }
        }
#endif
    } else if (e->keycode == KEY_LEFT) {
#ifdef NEFU_BARE
        if (st->vid_loaded) {
            int t = st->pos_sec - 5;
            if (t < 0) t = 0;
            vp_bare_video_seek_ms(st, (uint32_t)t * 1000u);
        } else if (st->bare_loaded) vp_bare_seek(st, st->pos_sec - 5);
#else
        if (st->host_loaded) platform_media_seek_sec(st->pos_sec - 5);
#endif
    } else if (e->keycode == KEY_RIGHT) {
#ifdef NEFU_BARE
        if (st->vid_loaded) {
            vp_bare_video_seek_ms(st, (uint32_t)(st->pos_sec + 5) * 1000u);
        } else if (st->bare_loaded) vp_bare_seek(st, st->pos_sec + 5);
#else
        if (st->host_loaded) platform_media_seek_sec(st->pos_sec + 5);
#endif
    } else if (e->keycode == KEY_UP) {
        st->vol += 10; if (st->vol > 100) st->vol = 100;
#ifdef NEFU_BARE
        if (st->bare_loaded) nefu::audio::media_set_volume(st->vol);
#else
        platform_media_set_volume(st->vol);
#endif
    } else if (e->keycode == KEY_DOWN) {
        st->vol -= 10; if (st->vol < 0) st->vol = 0;
#ifdef NEFU_BARE
        if (st->bare_loaded) nefu::audio::media_set_volume(st->vol);
#else
        platform_media_set_volume(st->vol);
#endif
    } else if (e->ascii == '+' || e->ascii == '=') {
        st->vol += 10; if (st->vol > 100) st->vol = 100;
#ifdef NEFU_BARE
        if (st->bare_loaded) nefu::audio::media_set_volume(st->vol);
#else
        platform_media_set_volume(st->vol);
#endif
    } else if (e->ascii == '-' || e->ascii == '_') {
        st->vol -= 10; if (st->vol < 0) st->vol = 0;
#ifdef NEFU_BARE
        if (st->bare_loaded) nefu::audio::media_set_volume(st->vol);
#else
        platform_media_set_volume(st->vol);
#endif
    } else if (e->ascii == 'o' || e->ascii == 'O') {
#ifndef NEFU_BARE
        char path[260];
        if (platform_host_file_dialog(path, sizeof(path),
            "Video files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.mpg;*.mpeg;*.m4v;*.webm;*.3gp;*.flv;*.ts;*.ogv\0All files\0*.*\0\0", "*.mp4;*.avi")) {
            vp_open_host_file(st, path);
            if (st->host_loaded) { st->playing = true; st->paused = false; platform_media_play(); }
        }
#endif
    }
}

static void vp_close(Window* w) {
    VideoPlayerState* st = (VideoPlayerState*)w->userdata;
    if (!st) return;
    if (st->host_loaded) { platform_media_close(); st->host_loaded = false; }
#ifdef NEFU_BARE
    if (st->bare_loaded) { nefu::audio::media_close(); st->bare_loaded = false; }
    vp_bare_video_close(st);
#endif
    vp_free_frames(st);
}

// ---- test hook: --media (Win32 host builds) autoplays a real file ----
static const char* s_autopath = 0;

static void video_player_open(FSNode* target) {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("视频播放器", "Video Player"), x, y, 640, 460);
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
#ifdef NEFU_BARE
    st->bare_data = 0;
    st->bare_size = 0;
    st->bare_rate = 0;
    st->bare_channels = 0;
    st->bare_pcm_off = 0;
    st->bare_frames = 0;
    st->bare_loaded = false;
    st->demo_sound_on = false;
    st->vid_src = 0;
    st->vid_src_size = 0;
    st->vid_w = st->vid_h = 0;
    st->vid_fps = 0;
    st->vid_count = 0;
    st->vid_rgba = 0;
    st->vid_idx = -1;
    st->vid_ms = 0;
    st->vid_last_tick = platform_tick_ms();
    st->vid_loaded = false;
    st->vid_own_src = false;
#endif
    w->userdata = st;
    w->on_paint = vp_paint;
    w->on_tick = vp_tick;
    w->on_mouse = vp_mouse;
    w->on_key = vp_key;
    w->on_close = vp_close;
    g_wm->raise(w);
#ifdef NEFU_BARE
    // real playback: open the WAV file right away (VFS file manager path)
    if (target && target->data && target->size >= 44 && !target->is_dir) {
        vp_bare_open(st, (const uint8_t*)target->data, target->size, target->name.c_str());
    }
    // no media handed in: start the built-in demo clip, so a bare-metal launch
    // is never a blank window (the host build uses Open / --media instead)
    if (!st->bare_loaded) {
        vp_bare_video_open(st, nefu::g_nvid_demo, (uint32_t)nefu::g_nvid_demo_size,
                           T("内置演示视频 (NVID)", "built-in demo clip (NVID)"));
    }
#else
    (void)target;   // VFS files have no real host path; use Open for real files
#endif
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

