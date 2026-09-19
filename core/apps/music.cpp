// nefuOS music player - LVGL GUI (real synthesized WAV playback via waveOut)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../platform.h"

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
    LvglWin* lw;
    lv_obj_t* canvas;
    uint8_t* buf;
    int w, h;
    List<Song> songs;
    int cur_song;
    bool playing;
    uint32_t play_start;
    uint32_t pause_offset;
    int bars[24];
    lv_obj_t* btns[5];
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

static int music_progress_ms(MusicLvState* st) {
    if (st->songs.empty()) return 0;
    int dur = st->songs[st->cur_song].duration * 1000;
    if (dur <= 0) return 0;
    uint32_t elapsed = st->pause_offset;
    if (st->playing) elapsed += platform_tick_ms() - st->play_start;
    return (int)(elapsed % (uint32_t)dur);
}

static void music_lv_draw(MusicLvState* st) {
    if (!st->canvas || !st->buf) return;
    int W = st->w, H = st->h;
    Surface s;
    s.addr = st->buf;
    s.width = W;
    s.height = H;
    s.pitch = W * 4;
    s.fill(0x0014181E);
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
    char buf[128];
    if (st->songs.size() > 0) {
        ksprintf(buf, sizeof(buf), "Now Playing: %s", st->songs[st->cur_song].name.c_str());
        gfx::text(s, 32, by + bh + 18, buf, color::WHITE, 0x0014181E);
    }
    int pb_y = by + bh + 42;
    gfx::rect(s, 32, pb_y, W - 64, 10, 0x00304050);
    if (st->songs.size() > 0) {
        int prog = music_progress_ms(st);
        int dur = st->songs[st->cur_song].duration * 1000;
        int fw = (W - 64) * prog / (dur > 0 ? dur : 1);
        if (fw > W - 64) fw = W - 64;
        gfx::fillrect(s, 32, pb_y, fw, 10, color::BLUE_LT);
        ksprintf(buf, sizeof(buf), "%02d:%02d / %02d:%02d",
                 prog / 60000, (prog / 1000) % 60, dur / 60000, (dur / 1000) % 60);
        gfx::text(s, 32, pb_y + 14, buf, color::TEXT2, 0x0014181E);
        if (st->playing && prog >= dur - 100) {
            st->cur_song = (st->cur_song + 1) % st->songs.size();
            st->pause_offset = 0;
            st->play_start = platform_tick_ms();
            play_current_track(st);
        }
    }
    gfx::hline(s, 8, W - 8, pb_y + 36, 0x002A323C);
    int ly = pb_y + 44;
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
    lv_obj_invalidate(st->canvas);
}

static MusicLvState* s_mu_st = 0;

static void music_lv_tick(lv_timer_t* t) {
    (void)t;
    if (!s_mu_st) return;
    music_lv_draw(s_mu_st);
}

static void music_lv_btn(lv_event_t* e) {
    MusicLvState* st = s_mu_st;
    if (!st) return;
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    if (action == 0) {           // Play
        if (!st->playing) { st->play_start = platform_tick_ms(); st->playing = true; play_current_track(st); }
    } else if (action == 1) {    // Pause
        if (st->playing) { st->pause_offset += platform_tick_ms() - st->play_start; st->playing = false; platform_stop_sound(); }
    } else if (action == 2) {    // Prev
        if (st->songs.size() > 0) {
            st->cur_song = (st->cur_song - 1 + st->songs.size()) % st->songs.size();
            st->pause_offset = 0; st->play_start = platform_tick_ms(); st->playing = true;
            play_current_track(st);
        }
    } else if (action == 3) {    // Next
        if (st->songs.size() > 0) {
            st->cur_song = (st->cur_song + 1) % st->songs.size();
            st->pause_offset = 0; st->play_start = platform_tick_ms(); st->playing = true;
            play_current_track(st);
        }
    } else {                     // Stop
        st->playing = false; st->pause_offset = 0; platform_stop_sound();
    }
}

void music_launch() {
    ensure_music_lib();
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Music Player", x, y, 480, 440);
    if (!lw) return;
    MusicLvState* st = new MusicLvState();
    st->lw = lw;
    st->cur_song = 0;
    st->playing = false;
    st->play_start = 0;
    st->pause_offset = 0;
    for (int i = 0; i < 24; i++) st->bars[i] = 0;
    s_mu_st = st;
    lw->userdata = st;

    const char* labels[5] = { "Prev", "Play", "Pause", "Next", "Stop" };
    for (int i = 0; i < 5; i++) {
        lv_obj_t* b = lv_button_create(lw->content);
        lv_obj_set_pos(b, 8 + i * 78, 6);
        lv_obj_set_size(b, 70, 24);
        lv_obj_set_style_radius(b, 5, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x3D4B66), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2B3347), LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, music_lv_btn, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* lbl = lv_label_create(b);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        st->btns[i] = b;
    }

    st->w = 464;
    st->h = 368;
    st->canvas = lv_canvas_create(lw->content);
    lv_obj_set_pos(st->canvas, 8, 38);
    lv_obj_set_size(st->canvas, st->w, st->h);
    int bufsz = lv_canvas_buf_size(st->w, st->h, 32, 4);
    st->buf = new uint8_t[bufsz];
    memset(st->buf, 0xFF, (size_t)bufsz);
    lv_canvas_set_buffer(st->canvas, st->buf, st->w, st->h, LV_COLOR_FORMAT_ARGB8888);

    music_scan(st);
    lv_timer_create(music_lv_tick, 120, st);
    music_lv_draw(st);
}
} // namespace nefu