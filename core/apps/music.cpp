// nefuOS music player - LVGL GUI (real synthesized WAV playback via waveOut)
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../platform.h"

#ifndef NEFU_BARE
extern "C" char* getenv(const char*);
#endif

namespace nefu {

static uint32_t m_lcg = 8675309;
static uint32_t mrand() {
    m_lcg = m_lcg * 1103515245u + 12345u;
    return (m_lcg >> 16) & 0x7FFF;
}

struct Song {
    String name;
    int duration;
};

static const char* MUSIC_DIR = "/home/user/Music";
static const char* BUILTIN_SONGS[6] = {
    "Sunrise Drive", "Ocean Breeze", "Night Pulse",
    "Rainy Window", "Neon City", "Golden Hour"
};
static const int WAV_SR = 8000;
static const int WAV_DUR_MS = 4000;
static const int WAV_N = WAV_SR * WAV_DUR_MS / 1000;
static uint8_t s_wav_buf[44 + 8000 * 4];
static const uint16_t TRACK_HZ[6] = { 440, 494, 523, 587, 659, 698 };

static void synth_wav(int idx, uint32_t* out_size) {
    int n = WAV_N;
    int total = 44 + n;
    if (total > (int)sizeof(s_wav_buf)) total = (int)sizeof(s_wav_buf);
    n = total - 44;
    memcpy(s_wav_buf + 0, "RIFF", 4);
    s_wav_buf[4] = (uint8_t)(total - 8); s_wav_buf[5] = (uint8_t)((total - 8) >> 8);
    s_wav_buf[6] = (uint8_t)((total - 8) >> 16); s_wav_buf[7] = (uint8_t)((total - 8) >> 24);
    memcpy(s_wav_buf + 8, "WAVE", 4);
    memcpy(s_wav_buf + 12, "fmt ", 4);
    s_wav_buf[16] = 16; s_wav_buf[17] = 0; s_wav_buf[18] = 0; s_wav_buf[19] = 0;
    s_wav_buf[20] = 1; s_wav_buf[21] = 0;
    s_wav_buf[22] = 1; s_wav_buf[23] = 0;
    s_wav_buf[24] = (uint8_t)(WAV_SR & 0xFF); s_wav_buf[25] = (uint8_t)((WAV_SR >> 8) & 0xFF);
    s_wav_buf[26] = (uint8_t)((WAV_SR >> 16) & 0xFF); s_wav_buf[27] = (uint8_t)((WAV_SR >> 24) & 0xFF);
    int brate = WAV_SR;
    s_wav_buf[28] = (uint8_t)(brate & 0xFF); s_wav_buf[29] = (uint8_t)((brate >> 8) & 0xFF);
    s_wav_buf[30] = (uint8_t)((brate >> 16) & 0xFF); s_wav_buf[31] = (uint8_t)((brate >> 24) & 0xFF);
    s_wav_buf[32] = 1; s_wav_buf[33] = 0;
    s_wav_buf[34] = 8; s_wav_buf[35] = 0;
    memcpy(s_wav_buf + 36, "data", 4);
    s_wav_buf[40] = (uint8_t)(n & 0xFF); s_wav_buf[41] = (uint8_t)((n >> 8) & 0xFF);
    s_wav_buf[42] = (uint8_t)((n >> 16) & 0xFF); s_wav_buf[43] = (uint8_t)((n >> 24) & 0xFF);
    uint32_t ph = 0;
    uint32_t step = (uint32_t)((uint64_t)TRACK_HZ[idx] * 65536 * 16 / WAV_SR);
    for (int i = 0; i < n; i++) {
        ph += step;
        uint32_t a = ph >> 16;
        int32_t ang = (int32_t)(a & 65535);
        int32_t v = (ang * (65536 - ang)) >> 9;
        v = (v * 3) >> 3;
        if (v > 255) v = 255;
        int env = 255;
        if (i < 160) env = i * 255 / 160;
        int rem = n - i;
        if (rem < 160) env = rem * 255 / 160;
        s_wav_buf[44 + i] = (uint8_t)((v * env) >> 8);
    }
    *out_size = (uint32_t)(44 + n);
}

static void ensure_music_lib() {
    FSNode* d = g_vfs->resolve(MUSIC_DIR);
    bool has = false;
    if (d && d->is_dir) {
        for (int i = 0; i < d->children.size(); i++) {
            if (!d->children[i]->is_dir) { has = true; break; }
        }
    }
    if (has) return;
    g_vfs->mkdir(MUSIC_DIR);
    for (int i = 0; i < 6; i++) {
        uint32_t sz = 0;
        synth_wav(i, &sz);
        char path[96];
        ksprintf(path, sizeof(path), "%s/t%d.wav", MUSIC_DIR, i + 1);
        FSNode* f = g_vfs->create_file(path);
        if (f) g_vfs->write_file(f, s_wav_buf, sz);
    }
}

struct MusicLvState {
    int w, h;
    List<Song> songs;
    int cur_song;
    bool playing;
    uint32_t play_start;
    uint32_t pause_offset;
    int bars[24];
    // real audio file (host backend, Media Foundation: mp3/wav/m4a/flac/...)
    char real_path[520];
    bool real_active;    // a real host audio file is open in the media engine
    bool real_playing;   // it is currently playing (vs paused/stopped)
    int vol;             // 0..100 (media engine volume)
};

static void music_scan(MusicLvState* st) {
    st->songs.clear();
    FSNode* d = g_vfs->resolve(MUSIC_DIR);
    if (!d || !d->is_dir) return;
    for (int i = 0; i < d->children.size(); i++) {
        FSNode* c = d->children[i];
        if (c->is_dir || c->size == 0) continue;
        const char* n = c->name.c_str();
        int len = c->name.len();
        if (len < 5 || strcmp(n + len - 4, ".wav") != 0) continue;
        Song s;
        int idx = 0;
        if (n[0] == 't' && n[1] >= '1' && n[1] <= '6') idx = n[1] - '1';
        s.name = BUILTIN_SONGS[idx];
        s.duration = WAV_DUR_MS / 1000;
        st->songs.push(s);
    }
    if (st->songs.empty()) {
        Song s;
        s.name = "No songs found";
        s.duration = 60;
        st->songs.push(s);
    }
    if (st->cur_song >= st->songs.size()) st->cur_song = 0;
}

static void play_current_track(MusicLvState* st) {
    if (st->songs.empty()) return;
    char path[96];
    ksprintf(path, sizeof(path), "%s/t%d.wav", MUSIC_DIR, (st->cur_song % 6) + 1);
    platform_play_wav_path(path);
}

// ---- real audio files (host backend, Media Foundation) ----
static void music_stop_real(MusicLvState* st) {
    if (st->real_active) {
        platform_media_close();
        st->real_active = false;
        st->real_playing = false;
    }
}

static void music_open_real(MusicLvState* st) {
    if (!platform_media_available()) return;
    char path[520];
    if (!platform_host_file_dialog(path, sizeof(path),
        "Audio files\0*.mp3;*.wav;*.flac;*.m4a;*.aac;*.ogg;*.wma;*.opus;*.mp4;*.wmv\0All files\0*.*\0\0",
        "*.mp3;*.wav")) return;
    platform_stop_sound();           // stop any synthesized track
    st->playing = false;
    st->pause_offset = 0;
    music_stop_real(st);
    if (platform_media_open(path, true)) {
        st->real_active = true;
        strncpy(st->real_path, path, sizeof(st->real_path) - 1);
        st->real_path[sizeof(st->real_path) - 1] = 0;
        platform_media_set_volume(st->vol);
        platform_media_play();
        st->real_playing = true;
        st->playing = true;
    }
}

static void music_play(MusicLvState* st) {
    if (st->real_active) {
        platform_media_play();
        st->real_playing = true;
        st->playing = true;
        return;
    }
    if (!st->playing) { st->play_start = platform_tick_ms(); st->playing = true; play_current_track(st); }
}

static void music_pause(MusicLvState* st) {
    if (st->real_active) {
        platform_media_pause();
        st->real_playing = false;
        st->playing = false;
        return;
    }
    if (st->playing) { st->pause_offset += platform_tick_ms() - st->play_start; st->playing = false; platform_stop_sound(); }
}

static void music_stop(MusicLvState* st) {
    if (st->real_active) {
        platform_media_stop();
        st->real_playing = false;
        st->playing = false;
        st->pause_offset = 0;
        return;
    }
    st->playing = false; st->pause_offset = 0; platform_stop_sound();
}

static void music_prev(MusicLvState* st) {
    if (st->songs.size() > 0) {
        music_stop_real(st);
        st->cur_song = (st->cur_song - 1 + st->songs.size()) % st->songs.size();
        st->pause_offset = 0; st->play_start = platform_tick_ms(); st->playing = true;
        play_current_track(st);
    }
}

static void music_next(MusicLvState* st) {
    if (st->songs.size() > 0) {
        music_stop_real(st);
        st->cur_song = (st->cur_song + 1) % st->songs.size();
        st->pause_offset = 0; st->play_start = platform_tick_ms(); st->playing = true;
        play_current_track(st);
    }
}

static int music_progress_ms(MusicLvState* st) {
    if (st->real_active) {
        int pos = platform_media_position_sec();
        return pos >= 0 ? pos * 1000 : 0;
    }
    if (st->songs.empty()) return 0;
    int dur = st->songs[st->cur_song].duration * 1000;
    if (dur <= 0) return 0;
    uint32_t elapsed = st->pause_offset;
    if (st->playing) elapsed += platform_tick_ms() - st->play_start;
    return (int)(elapsed % (uint32_t)dur);
}

static int music_duration_ms(MusicLvState* st) {
    if (st->real_active) {
        int d = platform_media_duration_sec();
        return d > 0 ? d * 1000 : 0;
    }
    if (st->songs.empty()) return 0;
    return st->songs[st->cur_song].duration * 1000;
}

static void music_wm_paint(Window* w) {
    MusicLvState* st = (MusicLvState*)w->userdata;
    int W = st->w, H = st->h;
    Surface& s = w->back;
    s.fill(0x0014181E);
    // transport buttons Open/Prev/Play/Pause/Next/Stop
    const char* labels[6] = { "Open", "Prev", "Play", "Pause", "Next", "Stop" };
    for (int i = 0; i < 6; i++) {
        int bx = 8 + i * 78;
        uint32_t bg = 0x003D4B66;
        if (i == 2 && st->playing) bg = 0x002F6FB6;
        gfx::fillrect(s, bx, 6, 70, 24, bg);
        gfx::rect(s, bx, 6, 70, 24, 0x002B3347);
        gfx::text(s, bx + (70 - gfx::text_width(labels[i])) / 2, 11, labels[i], color::WHITE, bg);
    }
    for (int i = 0; i < 24; i++) {
        int target = st->playing ? (int)(mrand() % 100) : 5;
        st->bars[i] += (target - st->bars[i]) * 35 / 100;
    }
    int bw = (W - 64) / 24;
    int bh = H * 2 / 5;
    int by = 56;
    for (int i = 0; i < 24; i++) {
        int h = st->bars[i] * (bh - 8) / 100 + 2;
        uint32_t c = 0x002F6FB6;
        if (st->bars[i] > 55) c = 0x0030A14A;
        if (st->bars[i] > 80) c = 0x00F0C040;
        gfx::fillrect(s, 32 + i * bw, by + bh - h, bw - 4, h, c);
    }
    gfx::rect(s, 30, by, W - 60, bh, 0x00304050);
    char buf[160];
    if (st->real_active) {
        // real host file: shorten to its file name
        const char* base = st->real_path;
        const char* slash = 0;
        for (const char* p = st->real_path; *p; p++) if (*p == '\\' || *p == '/') slash = p;
        if (slash) base = slash + 1;
        ksprintf(buf, sizeof(buf), "Now Playing: %s  [real audio file]", base);
        gfx::text(s, 32, by + bh + 18, buf, color::WHITE, 0x0014181E);
    } else if (st->songs.size() > 0) {
        ksprintf(buf, sizeof(buf), "Now Playing: %s", st->songs[st->cur_song].name.c_str());
        gfx::text(s, 32, by + bh + 18, buf, color::WHITE, 0x0014181E);
    }
    int pb_y = by + bh + 42;
    gfx::rect(s, 32, pb_y, W - 64, 10, 0x00304050);
    {
        int prog = music_progress_ms(st);
        int dur = music_duration_ms(st);
        if (dur > 0) {
            int fw = (W - 64) * prog / dur;
            if (fw > W - 64) fw = W - 64;
            gfx::fillrect(s, 32, pb_y, fw, 10, color::BLUE_LT);
            ksprintf(buf, sizeof(buf), "%02d:%02d / %02d:%02d",
                     prog / 60000, (prog / 1000) % 60, dur / 60000, (dur / 1000) % 60);
            gfx::text(s, 32, pb_y + 14, buf, color::TEXT2, 0x0014181E);
            if (st->playing && !st->real_active && prog >= dur - 100) {
                st->cur_song = (st->cur_song + 1) % st->songs.size();
                st->pause_offset = 0;
                st->play_start = platform_tick_ms();
                play_current_track(st);
            }
        } else if (st->real_active) {
            ksprintf(buf, sizeof(buf), "position unavailable");
            gfx::text(s, 32, pb_y + 14, buf, color::TEXT2, 0x0014181E);
        }
    }
    gfx::hline(s, 8, W - 8, pb_y + 36, 0x002A323C);
    int ly = pb_y + 44;
    if (st->real_active) {
        gfx::text(s, 32, ly, "Real audio file (Media Foundation decoder)", color::BLUE_LT, 0x0014181E);
        ly += 18;
        char vb[64];
        ksprintf(vb, sizeof(vb), "Volume: %d%%", st->vol);
        gfx::text(s, 32, ly, vb, color::LIGHT, 0x0014181E);
        ly += 18;
        gfx::text(s, 32, ly, "Open another file, or Prev/Next returns to the built-in tracks.",
                  color::TEXT2, 0x0014181E);
        ly += 16;
        gfx::text(s, 32, ly, "Space play/pause   <-  -> seek 5s   +/- volume", color::TEXT2, 0x0014181E);
    } else {
        gfx::text(s, 32, ly, "Playlist", color::BLUE_LT, 0x0014181E);
        ly += 18;
        for (int i = 0; i < st->songs.size() && i < 8; i++) {
            bool sel = (i == st->cur_song);
            uint32_t bg = sel ? 0x002F3B4C : 0x0014181E;
            gfx::fillrect(s, 24, ly, W - 48, 16, bg);
            char num[8];
            ksprintf(num, sizeof(num), "%d.", i + 1);
            gfx::text(s, 28, ly + 1, num, color::TEXT2, bg);
            gfx::text(s, 52, ly + 1, st->songs[i].name.c_str(), sel ? color::WHITE : color::LIGHT, bg);
            int dur = st->songs[i].duration;
            ksprintf(buf, sizeof(buf), "%02d:%02d", dur / 60, dur % 60);
            gfx::text(s, W - 72, ly + 1, buf, color::TEXT2, bg);
            if (sel && st->playing) gfx::text(s, W - 92, ly + 1, ">>", color::GREEN, bg);
            ly += 16;
        }
        char vb[64];
        ksprintf(vb, sizeof(vb), "Volume: %d%%", st->vol);
        gfx::text(s, W - gfx::text_width(vb) - 24, ly + 2, vb, color::TEXT2, 0x0014181E);
        gfx::text(s, 32, ly + 18, "Open = play a real audio file (mp3/wav/flac/m4a/...)   +/- volume",
                  color::TEXT2, 0x0014181E);
    }
}

static void music_wm_mouse(Window* w, int mx, int my, uint8_t buttons) {
    MusicLvState* st = (MusicLvState*)w->userdata;
    if (!st || !buttons) return;
    if (my >= 6 && my < 30) {
        int action = -1;
        for (int i = 0; i < 6; i++) {
            int bx = 8 + i * 78;
            if (mx >= bx && mx < bx + 70) { action = i; break; }
        }
        if (action == 0) music_open_real(st);
        else if (action == 1) music_prev(st);
        else if (action == 2) music_play(st);
        else if (action == 3) music_pause(st);
        else if (action == 4) music_next(st);
        else if (action == 5) music_stop(st);
        return;
    }
    // click on the progress bar seeks (real files only)
    int by = 56;
    int pb_y = by + (st->h * 2 / 5) + 42;
    if (my >= pb_y && my < pb_y + 10 && st->real_active) {
        int dur = platform_media_duration_sec();
        if (dur > 0) {
            int rel = mx - 32;
            int w2 = st->w - 64;
            if (rel < 0) rel = 0;
            if (rel > w2) rel = w2;
            platform_media_seek_sec(rel * dur / w2);
        }
        return;
    }
}

static void music_wm_key(Window* w, const KeyEvent* e) {
    MusicLvState* st = (MusicLvState*)w->userdata;
    if (!st || !e->down) return;
    if (e->keycode == KEY_SPACE || e->ascii == ' ') {
        if (st->playing) music_pause(st);
        else music_play(st);
    } else if (e->keycode == KEY_LEFT) {
        if (st->real_active) {
            int p = platform_media_position_sec();
            if (p > 5) platform_media_seek_sec(p - 5); else platform_media_seek_sec(0);
        } else music_prev(st);
    } else if (e->keycode == KEY_RIGHT) {
        if (st->real_active) {
            int p = platform_media_position_sec();
            platform_media_seek_sec(p + 5);
        } else music_next(st);
    } else if (e->keycode == KEY_UP || e->ascii == '+' || e->ascii == '=') {
        st->vol += 10; if (st->vol > 100) st->vol = 100;
        platform_media_set_volume(st->vol);
    } else if (e->keycode == KEY_DOWN || e->ascii == '-' || e->ascii == '_') {
        st->vol -= 10; if (st->vol < 0) st->vol = 0;
        platform_media_set_volume(st->vol);
    } else if (e->ascii == 'o' || e->ascii == 'O') {
        music_open_real(st);
    }
}

static void music_wm_close(Window* w) {
    MusicLvState* st = (MusicLvState*)w->userdata;
    if (!st) return;
    if (st->real_active) platform_media_close();
    st->real_active = false;
    platform_stop_sound();
}

void music_launch() {
    ensure_music_lib();
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Music Player", x, y, 480, 460);
    if (!w) return;
    MusicLvState* st = new MusicLvState();
    st->cur_song = 0;
    st->playing = false;
    st->play_start = 0;
    st->pause_offset = 0;
    st->real_path[0] = 0;
    st->real_active = false;
    st->real_playing = false;
    st->vol = 70;
    for (int i = 0; i < 24; i++) st->bars[i] = 0;
    st->w = w->content_w;
    st->h = w->content_h;
    w->userdata = st;
    w->on_paint = music_wm_paint;
    w->on_mouse = music_wm_mouse;
    w->on_key = music_wm_key;
    w->on_close = music_wm_close;
    music_scan(st);
    g_wm->raise(w);
#ifndef NEFU_BARE
    // host test hook: NEFU_AUTO_MEDIA=<path> auto-opens a real audio file
    const char* am = getenv("NEFU_AUTO_MEDIA");
    if (am && am[0] && platform_media_available()) {
        if (platform_media_open(am, true)) {
            st->real_active = true;
            strncpy(st->real_path, am, sizeof(st->real_path) - 1);
            st->real_path[sizeof(st->real_path) - 1] = 0;
            platform_media_set_volume(st->vol);
            platform_media_play();
            st->real_playing = true;
            st->playing = true;
        }
    }
#endif
}
} // namespace nefu