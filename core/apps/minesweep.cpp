// nefuOS minesweeper - LVGL GUI (canvas, left-click reveal, F flag, R restart)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../gui/desktop.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct MinerCore {
    static const int N = 9, MINES = 10;
    bool mine[N][N];
    bool open[N][N];
    bool flag[N][N];
    int around[N][N];
    bool over;
    bool won;
    int opened;
    int flags;
};

static void miner_place(MinerCore* st, int skip_x, int skip_y) {
    int placed = 0;
    for (int attempt = 0; attempt < 500 && placed < MinerCore::MINES; attempt++) {
        int x = (int)((platform_tick_ms() * 2654435761u + attempt * 131) >> 13) % MinerCore::N;
        int y = (int)((platform_tick_ms() * 40503u + attempt * 89) >> 13) % MinerCore::N;
        if (x == skip_x && y == skip_y) continue;
        if (st->mine[y][x]) continue;
        st->mine[y][x] = true;
        placed++;
    }
    for (int y = 0; y < MinerCore::N; y++)
        for (int x = 0; x < MinerCore::N; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < MinerCore::N && ny < MinerCore::N && st->mine[ny][nx]) c++;
                }
            st->around[y][x] = c;
        }
}

static void miner_reveal(MinerCore* st, int x, int y) {
    if (x < 0 || y < 0 || x >= MinerCore::N || y >= MinerCore::N) return;
    if (st->open[y][x] || st->flag[y][x]) return;
    st->open[y][x] = true;
    st->opened++;
    if (st->mine[y][x]) { st->over = true; return; }
    if (st->around[y][x] == 0) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) miner_reveal(st, x + dx, y + dy);
    }
    if (st->opened >= MinerCore::N * MinerCore::N - MinerCore::MINES) st->won = true;
}

static void miner_reset(MinerCore* st) {
    for (int y = 0; y < MinerCore::N; y++)
        for (int x = 0; x < MinerCore::N; x++) {
            st->mine[y][x] = false;
            st->open[y][x] = false;
            st->flag[y][x] = false;
            st->around[y][x] = 0;
        }
    st->over = false;
    st->won = false;
    st->opened = 0;
    st->flags = 0;
    miner_place(st, -1, -1);
}

struct MinerLvState {
    LvglWin* lw;
    lv_obj_t* canvas;
    uint8_t* buf;
    int w, h;
    int ox, oy;
    MinerCore core;
};

static void miner_lv_draw(MinerLvState* st) {
    if (!st->canvas || !st->buf) return;
    int w = st->w, h = st->h;
    Surface s;
    s.addr = st->buf;
    s.width = w;
    s.height = h;
    s.pitch = w * 4;
    s.fill(0x00E8E8E3);
    MinerCore* c = &st->core;
    int cs = 30;
    st->ox = (w - cs * MinerCore::N) / 2;
    st->oy = 34 + (h - 34 - cs * MinerCore::N) / 2;
    for (int y = 0; y < MinerCore::N; y++) {
        for (int x = 0; x < MinerCore::N; x++) {
            int px = st->ox + x * cs, py = st->oy + y * cs;
            if (c->open[y][x]) {
                gfx::fillrect(s, px + 1, py + 1, cs - 2, cs - 2, 0x00F5F5F0);
                if (c->mine[y][x]) {
                    gfx::fillcircle(s, px + cs / 2, py + cs / 2, 6, 0x002E2E2E);
                } else if (c->around[y][x] > 0) {
                    char num[2] = { (char)('0' + c->around[y][x]), 0 };
                    uint32_t col = 0x001F6FB6;
                    if (c->around[y][x] == 1) col = 0x001F6FB6;
                    else if (c->around[y][x] == 2) col = 0x001E8448;
                    else if (c->around[y][x] == 3) col = 0x00D64545;
                    else if (c->around[y][x] == 4) col = 0x006A3DB5;
                    else col = 0x00876444;
                    gfx::text(s, px + 11, py + 7, num, col, 0x00F5F5F0);
                }
                gfx::rect(s, px, py, cs, cs, 0x00C9C8C2);
            } else {
                gfx::fillrect(s, px + 1, py + 1, cs - 2, cs - 2, 0x00C6CBD4);
                if (c->flag[y][x]) {
                    gfx::fillrect(s, px + 8, py + 6, 5, 12, color::RED);
                    gfx::fillrect(s, px + 13, py + 6, 5, 3, color::RED);
                    gfx::fillrect(s, px + 6, py + 18, 16, 3, 0x00555A63);
                }
                gfx::rect(s, px, py, cs, cs, 0x00FFFFFF);
                gfx::rect(s, px + 1, py + 1, cs - 2, cs - 2, 0x00888D96);
            }
        }
    }
    char buf[64];
    if (c->won) ksprintf(buf, sizeof(buf), "YOU WIN!  mines: %d", MinerCore::MINES);
    else if (c->over) ksprintf(buf, sizeof(buf), "BOOM!  flags: %d/%d  (R restart)", c->flags, MinerCore::MINES);
    else ksprintf(buf, sizeof(buf), "Mines: %d  Flags: %d", MinerCore::MINES, c->flags);
    gfx::text(s, 10, 8, buf, c->over ? color::RED : color::TEXT, 0x00E8E8E3);
    if (c->over) gfx::text(s, 10, 22, "Press F to flag, R to restart", color::TEXT2, 0x00E8E8E3);
    lv_obj_invalidate(st->canvas);
}

static void miner_lv_click(lv_event_t* e) {
    MinerLvState* st = (MinerLvState*)lv_event_get_user_data(e);
    if (!st) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int x = (p.x - st->ox) / 30;
    int y = (p.y - st->oy) / 30;
    if (x < 0 || y < 0 || x >= MinerCore::N || y >= MinerCore::N) return;
    if (st->core.over || st->core.won) return;
    miner_reveal(&st->core, x, y);
    miner_lv_draw(st);
}

static void miner_lv_key(lv_event_t* e) {
    MinerLvState* st = (MinerLvState*)lv_event_get_user_data(e);
    if (!st) return;
    uint32_t k = lv_event_get_key(e);
    if (k == 'r' || k == 'R') { miner_reset(&st->core); miner_lv_draw(st); }
    if (k == 'f' || k == 'F') {
        MinerCore* c = &st->core;
        if (c->over || c->won) return;
        for (int y = 0; y < MinerCore::N && !c->over; y++)
            for (int x = 0; x < MinerCore::N; x++)
                if (!c->open[y][x]) { c->flag[y][x] = !c->flag[y][x]; c->flags += c->flag[y][x] ? 1 : -1; return; }
    }
}

void miner_launch() {
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Minesweeper", x, y, 330, 380);
    if (!lw) return;
    MinerLvState* st = new MinerLvState();
    st->lw = lw;
    st->w = 314;
    st->h = 328;
    st->canvas = lv_canvas_create(lw->content);
    lv_obj_set_pos(st->canvas, 8, 8);
    lv_obj_set_size(st->canvas, st->w, st->h);
    int bufsz = lv_canvas_buf_size(st->w, st->h, 32, 4);
    st->buf = new uint8_t[bufsz];
    memset(st->buf, 0xFF, (size_t)bufsz);
    lv_canvas_set_buffer(st->canvas, st->buf, st->w, st->h, LV_COLOR_FORMAT_ARGB8888);
    lw->userdata = st;
    miner_reset(&st->core);
    miner_lv_draw(st);
    lv_obj_add_flag(st->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(st->canvas, miner_lv_click, LV_EVENT_CLICKED, st);
    lv_obj_add_event_cb(st->canvas, miner_lv_key, LV_EVENT_KEY, st);
    lv_group_t* grp = lvgl_kb_group();
    if (grp) {
        lv_group_add_obj(grp, st->canvas);
        lv_group_focus_obj(st->canvas);
    }
}
} // namespace nefu