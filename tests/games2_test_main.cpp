// games2_test_main.cpp —— 第二批小游戏公共工具的宿主机自测
//
// 编译命令（在 nefuOS 根目录）：
//   g++ -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 -I core ^
//       tests\games2_test_main.cpp core\apps\games2_util.cpp ^
//       -o games2_test.exe
//
// 本文件自己提供 kalloc/kfree/krealloc/platform_dbg 桩，
// 让 games2_util.cpp 脱离内核单独在宿主机运行。
//
// 返回 0 表示全部通过；非 0 表示失败数。
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace nefu {
void* kalloc(size_t s) { return malloc(s ? s : 1); }
void  kfree(void* p) { free(p); }
void* krealloc(void* p, size_t s) { return realloc(p, s); }
void platform_dbg(const char* s) { (void)s; }
}

// 注意：games2_ai.h 不依赖 gfx，可直接包含
#include "../core/apps/games2_ai.h"
// games2_util.h 中含 inline 绘制函数（未被调用时不产生符号）
#include "../core/apps/games2_util.h"

using namespace nefu;

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else         { printf("ok:   %s\n", msg); } \
} while (0)

// ---------------------------------------------------------------------------
//  1. Rng
// ---------------------------------------------------------------------------
static void test_rng() {
    printf("== Rng ==\n");
    games2::Rng a(12345);
    games2::Rng b(12345);
    uint32_t s1 = a.next(), s2 = b.next();
    CHECK(s1 == s2, "same seed produces same first value");

    // 连续多次也要一致
    uint32_t v1 = a.next();
    uint32_t v2 = b.next();
    CHECK(v1 == v2, "same seed stays deterministic");

    games2::Rng c(12345);
    bool ok = true;
    for (int i = 0; i < 1000; i++) {
        int v = c.range(10, 20);
        if (v < 10 || v > 20) { ok = false; break; }
    }
    CHECK(ok, "range(10,20) always in bounds over 1000 samples");

    games2::Rng d(999);
    int lo = d.range(5, 5);
    CHECK(lo == 5, "range with lo==hi returns lo");
}

// ---------------------------------------------------------------------------
//  2. 碰撞检测
// ---------------------------------------------------------------------------
static void test_collision() {
    printf("== collision ==\n");
    games2::Rect a{0, 0, 10, 10};
    games2::Rect b{5, 5, 10, 10};
    CHECK(games2::rect_overlap(a, b), "overlapping rects detected");

    games2::Rect c{20, 20, 5, 5};
    CHECK(!games2::rect_overlap(a, c), "disjoint rects not overlapping");

    games2::Rect d{10, 0, 5, 5};
    // a.x+a.w == d.x，恰好边贴边，按我们的实现不算相交
    CHECK(!games2::rect_overlap(a, d), "edge-touching rects not overlapping");

    // 圆心在矩形内
    games2::Rect e{10, 10, 20, 20};
    CHECK(games2::circle_rect_collide(15, 15, 3, e), "circle center inside rect");
    // 圆心在矩形外，但半径覆盖到矩形
    CHECK(games2::circle_rect_collide(5, 15, 6, e), "circle touching rect edge");
    // 完全在外
    CHECK(!games2::circle_rect_collide(0, 0, 2, e), "circle far from rect not colliding");

    CHECK(games2::point_in_rect(12, 12, e), "point inside rect");
    CHECK(!games2::point_in_rect(30, 30, e), "point outside rect");
}

// ---------------------------------------------------------------------------
//  3. HighTable
// ---------------------------------------------------------------------------
static void test_highscore() {
    printf("== highscore ==\n");
    games2::HighTable t;
    t.submit("alice", 100);
    t.submit("bob", 300);
    t.submit("carol", 200);
    CHECK(t.count == 3, "three submissions counted");
    CHECK(t.scores[0] == 300, "highest score on top");
    CHECK(t.scores[1] == 200, "middle score second");
    CHECK(t.scores[2] == 100, "lowest score third");

    // 再插一个 250，应该排第二
    int pos = t.submit("dave", 250);
    CHECK(pos == 1, "250 inserts at position 1");
    CHECK(t.scores[1] == 250, "250 now second");

    // 低分挤不进 top-8
    games2::HighTable t2;
    for (int i = 0; i < 8; i++) t2.submit("x", 1000 + i);
    int p = t2.submit("low", 5);
    CHECK(p == -1, "very low score rejected from top-8");

    t.reset();
    CHECK(t.count == 0, "reset clears table");
}

// ---------------------------------------------------------------------------
//  4. 井字棋 AI
// ---------------------------------------------------------------------------
static void test_ttt() {
    printf("== ttt AI ==\n");
    // 空棋盘：AI 应返回 0..8
    int b0[9] = {0};
    int m = ttt::best_move(b0);
    CHECK(m >= 0 && m < 9, "empty board: AI picks a valid cell");

    // AI 已经有两个连子，必须补第三个获胜
    int b1[9] = {
        ttt::AI, ttt::AI, 0,
        0, 0, 0,
        0, 0, 0
    };
    m = ttt::best_move(b1);
    CHECK(m == 2, "AI completes three-in-a-row to win");

    // 人类有两个连子，AI 必须堵
    int b2[9] = {
        ttt::HUMAN, ttt::HUMAN, 0,
        0, 0, 0,
        0, 0, 0
    };
    m = ttt::best_move(b2);
    CHECK(m == 2, "AI blocks human's three-in-a-row threat");

    // 已结束的棋盘
    int b3[9] = {
        ttt::HUMAN, ttt::HUMAN, ttt::HUMAN,
        0, 0, 0,
        0, 0, 0
    };
    CHECK(ttt::winner(b3) == ttt::HUMAN, "winner detects human win");
    CHECK(ttt::best_move(b3) == -1, "no move after game over");
}

// ---------------------------------------------------------------------------
//  5. 四子棋 AI
// ---------------------------------------------------------------------------
static void test_c4() {
    printf("== c4 AI ==\n");
    int b[c4::ROWS][c4::COLS];
    for (int r = 0; r < c4::ROWS; r++)
        for (int c = 0; c < c4::COLS; c++) b[r][c] = c4::EMPTY;

    // AI 竖直三连，下一列即赢
    b[5][3] = c4::AI;
    b[4][3] = c4::AI;
    b[3][3] = c4::AI;
    int m = c4::best_move(b, 6);
    CHECK(m == 3, "AI takes immediate vertical win");

    // 人类竖直三连，AI 必须堵
    int b2[c4::ROWS][c4::COLS];
    for (int r = 0; r < c4::ROWS; r++)
        for (int c = 0; c < c4::COLS; c++) b2[r][c] = c4::EMPTY;
    b2[5][2] = c4::HUMAN;
    b2[4][2] = c4::HUMAN;
    b2[3][2] = c4::HUMAN;
    m = c4::best_move(b2, 6);
    CHECK(m == 2, "AI blocks human's vertical four threat");

    // winner 检测
    int b3[c4::ROWS][c4::COLS];
    for (int r = 0; r < c4::ROWS; r++)
        for (int c = 0; c < c4::COLS; c++) b3[r][c] = c4::EMPTY;
    b3[5][0] = c4::HUMAN; b3[5][1] = c4::HUMAN;
    b3[5][2] = c4::HUMAN; b3[5][3] = c4::HUMAN;
    CHECK(c4::winner(b3) == c4::HUMAN, "c4 winner detects horizontal four");

    // 空棋盘：AI 必选一列
    int b4[c4::ROWS][c4::COLS];
    for (int r = 0; r < c4::ROWS; r++)
        for (int c = 0; c < c4::COLS; c++) b4[r][c] = c4::EMPTY;
    m = c4::best_move(b4, 5);
    CHECK(m >= 0 && m < c4::COLS, "empty board: AI picks a column");

    // evaluate_window：己方 3 连应返回 100
    int w4[4] = { c4::AI, c4::AI, c4::AI, c4::EMPTY };
    CHECK(c4::evaluate_window(w4, c4::AI) == 100, "evaluate_window: 3-in-row = 100");
    int w5[4] = { c4::AI, c4::HUMAN, c4::AI, c4::AI };
    CHECK(c4::evaluate_window(w5, c4::AI) == 0, "evaluate_window: contested window = 0");
}

// 完整对局：AI 后手，人类贪心地占第一个空位，AI 永不败
static void test_ttt_full_game() {
    printf("== ttt full game (AI never loses) ==\n");
    int b[9] = {0};
    for (int turn = 0; turn < 9; turn++) {
        if (turn % 2 == 0) {
            for (int i = 0; i < 9; i++) if (b[i] == ttt::EMPTY) { b[i] = ttt::HUMAN; break; }
        } else {
            int mv = ttt::best_move(b);
            if (mv < 0) break;
            b[mv] = ttt::AI;
        }
        if (ttt::winner(b) != 0) break;
    }
    int human_wins = (ttt::winner(b) == ttt::HUMAN) ? 1 : 0;
    CHECK(human_wins == 0, "AI never loses to a naive human");
}

// 圆-圆碰撞与 rect_overlap_xy
static void test_extra() {
    printf("== extra collision ==\n");
    CHECK(games2::circle_circle_collide(0,0,5, 8,0,5), "circle-circle touch");
    CHECK(!games2::circle_circle_collide(0,0,5, 20,0,5), "circle-circle far");
    CHECK(games2::rect_overlap_xy(0,0,10,10, 5,5,10,10), "rect_overlap_xy works");
    CHECK(!games2::rect_overlap_xy(0,0,5,5, 10,10,5,5), "rect_overlap_xy disjoint");
}

int main() {
    printf("games2 self test starting...\n");
    test_rng();
    test_collision();
    test_highscore();
    test_ttt();
    test_c4();
    test_ttt_full_game();
    test_extra();
    printf("----\n");
    printf("games2 self test: %d failures\n", failures);
    return failures != 0;
}
