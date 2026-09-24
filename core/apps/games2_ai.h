// games2_ai.h —— 棋类 AI 的纯逻辑实现（inline，无 gfx 依赖，可被单测直接链接）
//
// 包含：
//   - nefu::ttt  井字棋 minimax
//   - nefu::c4   四子棋 alpha-beta + 启发式评估
//
// 这些函数不依赖任何图形/窗口代码，tests/games2_test_main.cpp
// 可以直接 #include 本头文件并调用。
#pragma once

#include <stdint.h>

namespace nefu {

// ============================================================================
//  井字棋 minimax
// ============================================================================
namespace ttt {

const int EMPTY = 0;
const int HUMAN = 1;   // X
const int AI    = 2;   // O

inline int winner(const int b[9]) {
    static const int LINES[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };
    for (int i = 0; i < 8; i++) {
        int a = b[LINES[i][0]], c = b[LINES[i][1]], d = b[LINES[i][2]];
        if (a != EMPTY && a == c && a == d) return a;
    }
    return 0;
}

inline bool full(const int b[9]) {
    for (int i = 0; i < 9; i++) if (b[i] == EMPTY) return false;
    return true;
}

inline int minimax(const int b[9], int depth, bool maximizing) {
    int w = winner(b);
    if (w == AI)    return 10 - depth;
    if (w == HUMAN) return depth - 10;
    if (full(b))    return 0;

    if (maximizing) {
        int best = -1000;
        for (int i = 0; i < 9; i++) {
            if (b[i] != EMPTY) continue;
            int nb[9];
            for (int k = 0; k < 9; k++) nb[k] = b[k];
            nb[i] = AI;
            int v = minimax(nb, depth + 1, false);
            if (v > best) best = v;
        }
        return best;
    } else {
        int best = 1000;
        for (int i = 0; i < 9; i++) {
            if (b[i] != EMPTY) continue;
            int nb[9];
            for (int k = 0; k < 9; k++) nb[k] = b[k];
            nb[i] = HUMAN;
            int v = minimax(nb, depth + 1, true);
            if (v < best) best = v;
        }
        return best;
    }
}

inline int best_move(const int b[9]) {
    if (winner(b) != EMPTY || full(b)) return -1;
    int best = -1000, move = -1;
    for (int i = 0; i < 9; i++) {
        if (b[i] != EMPTY) continue;
        int nb[9];
        for (int k = 0; k < 9; k++) nb[k] = b[k];
        nb[i] = AI;
        int v = minimax(nb, 1, false);
        if (v > best) { best = v; move = i; }
    }
    return move;
}

} // namespace ttt

// ============================================================================
//  四子棋 alpha-beta
// ============================================================================
namespace c4 {

const int ROWS = 6;
const int COLS = 7;
const int EMPTY = 0;
const int HUMAN = 1;
const int AI    = 2;

inline int drop_piece(int b[ROWS][COLS], int col, int who) {
    if (col < 0 || col >= COLS) return -1;
    for (int r = ROWS - 1; r >= 0; r--) {
        if (b[r][col] == EMPTY) { b[r][col] = who; return r; }
    }
    return -1;
}

inline bool column_full(const int b[ROWS][COLS], int col) {
    return b[0][col] != EMPTY;
}

inline bool board_full(const int b[ROWS][COLS]) {
    for (int c = 0; c < COLS; c++) if (!column_full(b, c)) return false;
    return true;
}

inline int winner(const int b[ROWS][COLS]) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c <= COLS - 4; c++) {
            int v = b[r][c];
            if (v != EMPTY && v == b[r][c+1] && v == b[r][c+2] && v == b[r][c+3]) return v;
        }
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r <= ROWS - 4; r++) {
            int v = b[r][c];
            if (v != EMPTY && v == b[r+1][c] && v == b[r+2][c] && v == b[r+3][c]) return v;
        }
    for (int r = 0; r <= ROWS - 4; r++)
        for (int c = 0; c <= COLS - 4; c++) {
            int v = b[r][c];
            if (v != EMPTY && v == b[r+1][c+1] && v == b[r+2][c+2] && v == b[r+3][c+3]) return v;
        }
    for (int r = 3; r < ROWS; r++)
        for (int c = 0; c <= COLS - 4; c++) {
            int v = b[r][c];
            if (v != EMPTY && v == b[r-1][c+1] && v == b[r-2][c+2] && v == b[r-3][c+3]) return v;
        }
    return 0;
}

inline int evaluate_window(int w[4], int who) {
    int mine = 0, opp = 0;
    int opp_who = (who == AI) ? HUMAN : AI;
    for (int i = 0; i < 4; i++) {
        if (w[i] == who) mine++;
        else if (w[i] == opp_who) opp++;
    }
    if (mine > 0 && opp > 0) return 0;
    if (mine == 4) return 100000;
    if (mine == 3) return 100;
    if (mine == 2) return 10;
    return 0;
}

inline int evaluate(const int b[ROWS][COLS], int who) {
    int score = 0;
    int w[4];
    for (int r = 0; r < ROWS; r++)
        if (b[r][COLS/2] == who) score += 6;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c <= COLS - 4; c++) {
            for (int i = 0; i < 4; i++) w[i] = b[r][c+i];
            score += evaluate_window(w, who);
        }
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r <= ROWS - 4; r++) {
            for (int i = 0; i < 4; i++) w[i] = b[r+i][c];
            score += evaluate_window(w, who);
        }
    for (int r = 0; r <= ROWS - 4; r++)
        for (int c = 0; c <= COLS - 4; c++) {
            for (int i = 0; i < 4; i++) w[i] = b[r+i][c+i];
            score += evaluate_window(w, who);
        }
    for (int r = 3; r < ROWS; r++)
        for (int c = 0; c <= COLS - 4; c++) {
            for (int i = 0; i < 4; i++) w[i] = b[r-i][c+i];
            score += evaluate_window(w, who);
        }
    return score;
}

inline int alphabeta(int b[ROWS][COLS], int depth, int alpha, int beta, bool maximizing) {
    int w = winner(b);
    if (w == AI)  return 1000000 + depth;
    if (w == HUMAN) return -1000000 - depth;
    if (board_full(b)) return 0;
    if (depth == 0) return evaluate(b, AI);

    if (maximizing) {
        int value = -10000000;
        for (int c = 0; c < COLS; c++) {
            if (column_full(b, c)) continue;
            drop_piece(b, c, AI);
            value = alphabeta(b, depth - 1, alpha, beta, false);
            for (int r = 0; r < ROWS; r++) { if (b[r][c] == AI) { b[r][c] = EMPTY; break; } }
            if (value > alpha) alpha = value;
            if (alpha >= beta) break;
        }
        return alpha;
    } else {
        int value = 10000000;
        for (int c = 0; c < COLS; c++) {
            if (column_full(b, c)) continue;
            drop_piece(b, c, HUMAN);
            value = alphabeta(b, depth - 1, alpha, beta, true);
            for (int r = 0; r < ROWS; r++) { if (b[r][c] == HUMAN) { b[r][c] = EMPTY; break; } }
            if (value < beta) beta = value;
            if (alpha >= beta) break;
        }
        return beta;
    }
}

inline int best_move(const int in[ROWS][COLS], int depth) {
    if (winner(in) != EMPTY) return -1;
    int b[ROWS][COLS];
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) b[r][c] = in[r][c];

    int best = -10000000, move = -1;
    int alpha = -10000000;
    for (int c = 0; c < COLS; c++) {
        if (column_full(b, c)) continue;
        drop_piece(b, c, AI);
        int v = alphabeta(b, depth - 1, alpha, 10000000, false);
        for (int r = 0; r < ROWS; r++) { if (b[r][c] == AI) { b[r][c] = EMPTY; break; } }
        if (v > best) { best = v; move = c; }
        if (best > alpha) alpha = best;
    }
    return move;
}

} // namespace c4

} // namespace nefu
