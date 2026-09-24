// tictactoe.cpp —— nefuOS 井字棋（人机对战，minimax AI）
//
// 玩法：
//   数字键 1-9    按行优先落子（你执 X）
//   R             重开
//
// AI 逻辑来自 games2_ai.h（nefu::ttt 命名空间），
// 与 tests/games2_test_main.cpp 共用同一份实现。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_ai.h"

namespace nefu {

namespace {

const int CELL = 80;
const int GW = CELL * 3;
const int GH = CELL * 3 + 56;

struct TicTacToe {
    int board[9];
    int turn;       // ttt::HUMAN / ttt::AI
    int result;     // 0=进行中 1=人胜 2=AI胜 3=平局
    bool easy;      // 简单模式：AI 随机走
    int wins_h, wins_ai, draws;

    TicTacToe() : easy(false), wins_h(0), wins_ai(0), draws(0) {
        for (int i = 0; i < 9; i++) board[i] = ttt::EMPTY;
        turn = ttt::HUMAN;
        result = 0;
    }
    void reset() {
        for (int i = 0; i < 9; i++) board[i] = ttt::EMPTY;
        turn = ttt::HUMAN;
        result = 0;
    }
    void human_move(int i) {
        if (result != 0 || turn != ttt::HUMAN) return;
        if (board[i] != ttt::EMPTY) return;
        board[i] = ttt::HUMAN;
        after_move();
    }
    void ai_move() {
        int m;
        if (easy) {
            // 简单模式：随机选一个空位
            int empties[9], n = 0;
            for (int i = 0; i < 9; i++)
                if (board[i] == ttt::EMPTY) empties[n++] = i;
            if (n == 0) return;
            // 用一个简单的 LCG 选下标
            static uint32_t s = 12345;
            s = s * 1103515245u + 12345u;
            m = empties[(s >> 16) % n];
        } else {
            m = ttt::best_move(board);
        }
        if (m < 0) return;
        board[m] = ttt::AI;
        after_move();
    }
    void after_move() {
        int w = ttt::winner(board);
        if (w == ttt::HUMAN) { result = 1; wins_h++; }
        else if (w == ttt::AI) { result = 2; wins_ai++; }
        else if (ttt::full(board)) { result = 3; draws++; }
        turn = (turn == ttt::HUMAN) ? ttt::AI : ttt::HUMAN;
    }
};

static void ttt_paint(Window* w) {
    TicTacToe* g = (TicTacToe*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, GW, GH, 0x00101420);

    for (int i = 1; i < 3; i++) {
        gfx::fillrect(s, i * CELL - 2, 0, 4, CELL * 3, 0x00FFFFFF);
        gfx::fillrect(s, 0, i * CELL - 2, CELL * 3, 4, 0x00FFFFFF);
    }
    for (int i = 0; i < 9; i++) {
        int x = (i % 3) * CELL;
        int y = (i / 3) * CELL;
        if (g->board[i] == ttt::HUMAN) {
            gfx::line(s, x + 15, y + 15, x + CELL - 15, y + CELL - 15, 0x0040C0FF);
            gfx::line(s, x + CELL - 15, y + 15, x + 15, y + CELL - 15, 0x0040C0FF);
        } else if (g->board[i] == ttt::AI) {
            gfx::circle(s, x + CELL / 2, y + CELL / 2, CELL / 2 - 15, 0x00FF6060);
        }
    }

    // 胜利连线：找到三连的那条线，画黄色粗线
    if (g->result == 1 || g->result == 2) {
        int p = (g->result == 1) ? ttt::HUMAN : ttt::AI;
        static const int lines[8][3] = {
            {0,1,2},{3,4,5},{6,7,8},
            {0,3,6},{1,4,7},{2,5,8},
            {0,4,8},{2,4,6}
        };
        for (int L = 0; L < 8; L++) {
            int a = lines[L][0], b = lines[L][1], c = lines[L][2];
            if (g->board[a] == p && g->board[b] == p && g->board[c] == p) {
                int x0 = (a % 3) * CELL + CELL / 2;
                int y0 = (a / 3) * CELL + CELL / 2;
                int x1 = (c % 3) * CELL + CELL / 2;
                int y1 = (c / 3) * CELL + CELL / 2;
                gfx::line(s, x0, y0, x1, y1, 0x00FFFF40);
                gfx::line(s, x0, y0 - 2, x1, y1 - 2, 0x00FFFF40);
                gfx::line(s, x0, y0 + 2, x1, y1 + 2, 0x00FFFF40);
                break;
            }
        }
    }

    char buf[64];
    if (g->result == 0) {
        ksprintf(buf, sizeof(buf), "Your turn (X) - press 1-9 [%s]",
                 g->easy ? "EASY" : "HARD");
    } else if (g->result == 1) {
        ksprintf(buf, sizeof(buf), "YOU WIN! (R to restart)");
    } else if (g->result == 2) {
        ksprintf(buf, sizeof(buf), "AI wins. (R to restart)");
    } else {
        ksprintf(buf, sizeof(buf), "Draw. (R to restart)");
    }
    gfx::text(s, 8, CELL * 3 + 10, buf, 0x00FFFFFF, 0x00101420);
    ksprintf(buf, sizeof(buf), "Score You %d - AI %d - Draw %d  [E] difficulty",
            g->wins_h, g->wins_ai, g->draws);
    gfx::text(s, 8, CELL * 3 + 26, buf, 0x00AAAAAA, 0x00101420);
}

static void ttt_tick(Window* w) {
    TicTacToe* g = (TicTacToe*)w->userdata;
    if (g->result == 0 && g->turn == ttt::AI) g->ai_move();
}

static void ttt_key(Window* w, const KeyEvent* e) {
    TicTacToe* g = (TicTacToe*)w->userdata;
    if (!e->down) return;
    if (e->ascii >= '1' && e->ascii <= '9') {
        g->human_move(e->ascii - '1');
        return;
    }
    if (e->ascii == 'r' || e->ascii == 'R') { g->reset(); return; }
    if (e->ascii == 'e' || e->ascii == 'E') { g->easy = !g->easy; return; }
}

static void ttt_close(Window* w) {
    if (w->userdata) delete (TicTacToe*)w->userdata;
    w->userdata = 0;
}

} // namespace

void tictactoe_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Tic-Tac-Toe", x, y, GW, GH);
    if (!w) return;
    TicTacToe* g = new TicTacToe();
    g->reset();
    w->userdata = g;
    w->on_paint = ttt_paint;
    w->on_tick = ttt_tick;
    w->on_key = ttt_key;
    w->on_close = ttt_close;
    g_wm->raise(w);
}

} // namespace nefu
