// connect4.cpp —— nefuOS 四子棋（人机对战，alpha-beta 剪枝）
//
// 玩法：
//   数字键 1-7    选择列落子（你执红，从底部堆起）
//   R             重开
//
// AI 逻辑来自 games2_ai.h（nefu::c4 命名空间），
// 与 tests/games2_test_main.cpp 共用同一份实现。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_ai.h"

namespace nefu {

namespace {

const int CELL = 50;
const int GW = c4::COLS * CELL;
const int GH = c4::ROWS * CELL + 60;

struct Connect4 {
    int board[c4::ROWS][c4::COLS];
    int result;     // 0=进行中 1=人胜 2=AI胜 3=平局
    int turn;
    int cursor_col;
    int ai_depth;   // 3=易 6=难
    int wins_h, wins_ai, draws;

    Connect4() : ai_depth(6), wins_h(0), wins_ai(0), draws(0) {
        reset();
    }
    void reset() {
        for (int r = 0; r < c4::ROWS; r++)
            for (int c = 0; c < c4::COLS; c++) board[r][c] = c4::EMPTY;
        result = 0;
        turn = c4::HUMAN;
        cursor_col = 3;
    }
    void human_drop(int col) {
        if (result != 0 || turn != c4::HUMAN) return;
        if (c4::column_full(board, col)) return;
        c4::drop_piece(board, col, c4::HUMAN);
        after();
    }
    void ai_drop() {
        int c = c4::best_move(board, ai_depth);
        if (c < 0) return;
        c4::drop_piece(board, c, c4::AI);
        after();
    }
    void after() {
        int w = c4::winner(board);
        if (w == c4::HUMAN) { result = 1; wins_h++; }
        else if (w == c4::AI) { result = 2; wins_ai++; }
        else if (c4::board_full(board)) { result = 3; draws++; }
        turn = (turn == c4::HUMAN) ? c4::AI : c4::HUMAN;
    }
};

static void c4_paint(Window* w) {
    Connect4* g = (Connect4*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, GW, GH, 0x000020A0);

    for (int r = 0; r < c4::ROWS; r++) {
        for (int c = 0; c < c4::COLS; c++) {
            int x = c * CELL + CELL / 2;
            int y = r * CELL + CELL / 2;
            uint32_t col = 0x00F0F0F0;
            if (g->board[r][c] == c4::HUMAN) col = 0x00FF4040;
            else if (g->board[r][c] == c4::AI) col = 0x00FFC020;
            gfx::fillcircle(s, x, y, CELL / 2 - 4, col);
        }
    }

    gfx::fillrect(s, g->cursor_col * CELL, 0, CELL, 4, 0x00FFFFFF);

    // 胜利高亮：找到连成四的那组棋子，画黄色外圈
    if (g->result == 1 || g->result == 2) {
        int p = (g->result == 1) ? c4::HUMAN : c4::AI;
        for (int r = 0; r < c4::ROWS; r++) {
            for (int c = 0; c < c4::COLS; c++) {
                // 水平
                if (c + 3 < c4::COLS && g->board[r][c]==p && g->board[r][c+1]==p &&
                    g->board[r][c+2]==p && g->board[r][c+3]==p) {
                    for (int k = 0; k < 4; k++)
                        gfx::circle(s, (c+k)*CELL+CELL/2, r*CELL+CELL/2, CELL/2-2, 0x00FFFF40);
                }
                // 垂直
                if (r + 3 < c4::ROWS && g->board[r][c]==p && g->board[r+1][c]==p &&
                    g->board[r+2][c]==p && g->board[r+3][c]==p) {
                    for (int k = 0; k < 4; k++)
                        gfx::circle(s, c*CELL+CELL/2, (r+k)*CELL+CELL/2, CELL/2-2, 0x00FFFF40);
                }
                // 主对角
                if (r+3<c4::ROWS && c+3<c4::COLS && g->board[r][c]==p &&
                    g->board[r+1][c+1]==p && g->board[r+2][c+2]==p && g->board[r+3][c+3]==p) {
                    for (int k = 0; k < 4; k++)
                        gfx::circle(s, (c+k)*CELL+CELL/2, (r+k)*CELL+CELL/2, CELL/2-2, 0x00FFFF40);
                }
                // 副对角
                if (r-3>=0 && c+3<c4::COLS && g->board[r][c]==p &&
                    g->board[r-1][c+1]==p && g->board[r-2][c+2]==p && g->board[r-3][c+3]==p) {
                    for (int k = 0; k < 4; k++)
                        gfx::circle(s, (c+k)*CELL+CELL/2, (r-k)*CELL+CELL/2, CELL/2-2, 0x00FFFF40);
                }
            }
        }
    }

    char buf[80];
    if (g->result == 0) ksprintf(buf, sizeof(buf), "Your turn - keys 1-7 [%s]",
                                 g->ai_depth == 6 ? "HARD" : "EASY");
    else if (g->result == 1) ksprintf(buf, sizeof(buf), "YOU WIN! (R)");
    else if (g->result == 2) ksprintf(buf, sizeof(buf), "AI wins. (R)");
    else ksprintf(buf, sizeof(buf), "Draw. (R)");
    gfx::text(s, 8, c4::ROWS * CELL + 12, buf, 0x00FFFFFF, 0x000020A0);
    ksprintf(buf, sizeof(buf), "You %d - AI %d - Draw %d  [E] difficulty",
            g->wins_h, g->wins_ai, g->draws);
    gfx::text(s, 8, c4::ROWS * CELL + 28, buf, 0x00AAAAAA, 0x000020A0);
}

static void c4_tick(Window* w) {
    Connect4* g = (Connect4*)w->userdata;
    if (g->result == 0 && g->turn == c4::AI) g->ai_drop();
}

static void c4_key(Window* w, const KeyEvent* e) {
    Connect4* g = (Connect4*)w->userdata;
    if (!e->down) return;
    if (e->ascii >= '1' && e->ascii <= '7') {
        int c = e->ascii - '1';
        g->cursor_col = c;
        g->human_drop(c);
        return;
    }
    if (e->ascii == 'r' || e->ascii == 'R') { g->reset(); return; }
    if (e->ascii == 'e' || e->ascii == 'E') {
        g->ai_depth = (g->ai_depth == 6) ? 3 : 6;
        return;
    }
}

static void c4_close(Window* w) {
    if (w->userdata) delete (Connect4*)w->userdata;
    w->userdata = 0;
}

} // namespace

void connect4_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Connect Four", x, y, GW, GH);
    if (!w) return;
    Connect4* g = new Connect4();
    g->reset();
    w->userdata = g;
    w->on_paint = c4_paint;
    w->on_tick = c4_tick;
    w->on_key = c4_key;
    w->on_close = c4_close;
    g_wm->raise(w);
}

} // namespace nefu
