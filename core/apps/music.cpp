// nefuOS 音乐播放器：播放列表、播放/暂停/切歌、进度条、频谱可视化
// 注：裸机内核暂无音频驱动，播放为"界面级"模拟（进度 + 频谱动画真实运行）
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

static uint32_t m_lcg = 8675309;
static uint32_t mrand() {
    m_lcg = m_lcg * 1103515245u + 12345u;
    return (m_lcg >> 16) & 0x7FFF;
}

struct Song {
    String name;
    int duration;      // 秒
};

struct MusicState {
    List<Song> songs;
    int cur_song;
    bool playing;
    uint32_t play_start;      // 当前曲目开始播放时刻(ms)
    uint32_t pause_offset;    // 暂停前累计播放 ms
    int scroll;
    int bars[24];             // 频谱条（0..100，整数平滑）
    Window* win;
    Button btns[5];
    Button* cur;
    uint8_t last_buttons;
};

static const char* MUSIC_DIR = "/home/user/Music";

// ---------- 内置曲库 ----------
static const char* BUILTIN_SONGS[6] = {
    "Sunrise Drive", "Ocean Breeze", "Night Pulse",
    "Rainy Window", "Neon City", "Golden Hour"
};

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
        char path[96];
        ksprintf(path, sizeof(path), "%s/t%d.song", MUSIC_DIR, i + 1);
        char buf[128];
        int dur = 150 + i * 37;
        int n = ksprintf(buf, sizeof(buf), "%s\n%d\n", BUILTIN_SONGS[i], dur);
        FSNode* f = g_vfs->resolve(path);
        if (!f) f = g_vfs->create_file(path);
        if (f) g_vfs->write_file(f, (const uint8_t*)buf, (uint32_t)n);
    }
}

static void music_scan(MusicState* st) {
    st->songs.clear();
    FSNode* d = g_vfs->resolve(MUSIC_DIR);
    if (!d || !d->is_dir) return;
    for (int i = 0; i < d->children.size(); i++) {
        FSNode* c = d->children[i];
        if (c->is_dir || c->size == 0) continue;
        const char* n = c->name.c_str();
        int len = c->name.len();
        if (len < 6 || strcmp(n + len - 5, ".song") != 0) continue;
        // 解析 name / duration
        char* buf = (char*)kalloc((size_t)c->size + 1);
        if (!buf) continue;
        memcpy(buf, c->data, c->size);
        buf[c->size] = 0;
        Song s;
        char* nl = strchr(buf, '\n');
        if (nl) {
            *nl = 0;
            s.name = buf;
            s.duration = atoi(nl + 1);
        } else {
            s.name = c->name;
            s.duration = 120;
        }
        if (s.duration <= 0) s.duration = 120;
        st->songs.push(s);
        kfree(buf);
    }
    if (st->songs.empty()) {
        Song s;
        s.name = "No songs found";
        s.duration = 60;
        st->songs.push(s);
    }
    if (st->cur_song >= st->songs.size()) st->cur_song = 0;
}

static int music_progress_ms(MusicState* st) {
    if (st->songs.empty()) return 0;
    int dur = st->songs[st->cur_song].duration * 1000;
    if (dur <= 0) return 0;
    uint32_t elapsed = st->pause_offset;
    if (st->playing) elapsed += platform_tick_ms() - st->play_start;
    return (int)(elapsed % (uint32_t)dur);
}

// ---------- 交互 ----------
static void music_click(void* ud) {
    MusicState* st = (MusicState*)ud;
    if (!st->cur) return;
    const char* lab = st->cur->label;
    if (strcmp(lab, "Play") == 0) {
        if (!st->playing) {
            st->play_start = platform_tick_ms();
            st->playing = true;
        }
    } else if (strcmp(lab, "Pause") == 0) {
        if (st->playing) {
            st->pause_offset += platform_tick_ms() - st->play_start;
            st->playing = false;
        }
    } else if (strcmp(lab, "Prev") == 0) {
        if (st->songs.size() > 0) {
            st->cur_song = (st->cur_song - 1 + st->songs.size()) % st->songs.size();
            st->pause_offset = 0;
            st->play_start = platform_tick_ms();
            st->playing = true;
        }
    } else if (strcmp(lab, "Next") == 0) {
        if (st->songs.size() > 0) {
            st->cur_song = (st->cur_song + 1) % st->songs.size();
            st->pause_offset = 0;
            st->play_start = platform_tick_ms();
            st->playing = true;
        }
    } else if (strcmp(lab, "Stop") == 0) {
        st->playing = false;
        st->pause_offset = 0;
    }
}

static void music_paint(Window* w) {
    MusicState* st = (MusicState*)w->userdata;
    Surface& s = w->back;
    s.fill(0x0014181E);
    int W = s.width, H = s.height;

    // 频谱动画（每帧更新；暂停时衰减到静音）
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

    // 当前曲目
    char buf[128];
    if (st->songs.size() > 0) {
        ksprintf(buf, sizeof(buf), "Now Playing: %s", st->songs[st->cur_song].name.c_str());
        gfx::text(s, 32, by + bh + 18, buf, color::WHITE, 0x0014181E);
    }
    // 进度条
    int pb_y = by + bh + 42;
    gfx::rect(s, 32, pb_y, W - 64, 10, 0x00304050);
    if (st->songs.size() > 0) {
        int prog = music_progress_ms(st);
        int dur = st->songs[st->cur_song].duration * 1000;
        int fw = (W - 64) * prog / (dur > 0 ? dur : 1);
        if (fw > W - 64) fw = W - 64;
        gfx::fillrect(s, 32, pb_y, fw, 10, color::BLUE_LT);
        ksprintf(buf, sizeof(buf), "%02d:%02d / %02d:%02d",
                 prog / 60000, (prog / 1000) % 60,
                 dur / 60000, (dur / 1000) % 60);
        gfx::text(s, 32, pb_y + 14, buf, color::TEXT2, 0x0014181E);
        // 曲目结束自动下一首
        if (st->playing && prog >= dur - 100) {
            st->cur_song = (st->cur_song + 1) % st->songs.size();
            st->pause_offset = 0;
            st->play_start = platform_tick_ms();
        }
    }

    // 按钮
    for (int i = 0; i < 5; i++) ui::draw_button(s, st->btns[i]);
    // 播放列表（底部滚动区）
    gfx::hline(s, 8, W - 8, pb_y + 36, 0x002A323C);
    int ly = pb_y + 44;
    gfx::text(s, 32, ly, "Playlist", color::BLUE_LT, 0x0014181E);
    ly += 18;
    int vis = (H - ly - 8) / 16;
    for (int i = st->scroll; i < st->songs.size() && i < st->scroll + vis; i++) {
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
}

static void music_mouse(Window* w, int mx, int my, uint8_t buttons) {
    MusicState* st = (MusicState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
    for (int i = 0; i < 5; i++) {
        st->cur = &st->btns[i];
        ui::button_event(st->btns[i], mx, my, buttons, pressed, released);
    }
    st->cur = 0;
}

static void music_scroll(Window* w, int delta) {
    MusicState* st = (MusicState*)w->userdata;
    st->scroll += delta > 0 ? -2 : 2;
    if (st->scroll < 0) st->scroll = 0;
    (void)w;
}

static void music_close(Window* w) {
    if (w->userdata) delete (MusicState*)w->userdata;
    w->userdata = 0;
}

void music_launch() {
    ensure_music_lib();
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Music Player", x, y, 480, 440);
    if (!w) return;
    MusicState* st = new MusicState();
    st->win = w;
    st->cur_song = 0;
    st->playing = false;
    st->play_start = 0;
    st->pause_offset = 0;
    st->scroll = 0;
    st->cur = 0;
    st->last_buttons = 0;
    for (int i = 0; i < 24; i++) st->bars[i] = 0;
    music_scan(st);
    const char* labels[5] = { "Prev", "Play", "Pause", "Next", "Stop" };
    for (int i = 0; i < 5; i++) {
        Button& b = st->btns[i];
        b.x = 32 + i * 74;
        b.y = 4;
        b.w = 66;
        b.h = 24;
        b.label = labels[i];
        b.id = i;
        b.pressed = false;
        b.on_click = music_click;
        b.ud = st;
    }
    w->userdata = st;
    w->on_paint = music_paint;
    w->on_mouse = music_mouse;
    w->on_scroll = music_scroll;
    w->on_close = music_close;
}

} // namespace nefu
