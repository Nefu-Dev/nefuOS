// nefuOS Game Center Application
#pragma once

#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace apps {

// Game info
struct GameInfo {
    const char* name;
    const char* description;
    uint32_t color;
    int players;
};

// Game center state
struct GameCenterState {
    GameInfo games[8];
    int selected_game;
    
    GameCenterState() : selected_game(0) {
        games[0] = {"Snake", "Classic snake game", 0x27AE60, 1};
        games[1] = {"Tetris", "Block puzzle game", 0x2980B9, 1};
        games[2] = {"Pong", "Table tennis game", 0x8E44AD, 2};
        games[3] = {"Breakout", "Break the bricks", 0xE74C3C, 1};
        games[4] = {"Space Invaders", "Shoot invaders", 0xF39C12, 1};
        games[5] = {"Memory", "Card matching game", 0x1ABC9C, 1};
        games[6] = {"Calculator", "Math game", 0xE67E22, 1};
        games[7] = {"Paint", "Drawing game", 0x95A5A6, 1};
    }
};

static void games_paint(Window* w) {
    GameCenterState* st = (GameCenterState*)w->userdata;
    if (!st) return;
    
    Surface& s = w->back;
    
    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x1A1A2E);
    
    // Title
    gfx::text(s, 20, 20, "Game Center", 0xFFFFFF, 0x1A1A2E);
    gfx::text(s, 20, 40, "Select a game to play!", 0xAAAAAA, 0x1A1A2E);
    
    // Game grid
    int grid_x = 20;
    int grid_y = 70;
    int card_w = 150;
    int card_h = 180;
    int gap = 16;
    
    for (int i = 0; i < 8; i++) {
        int row = i / 4;
        int col = i % 4;
        int x = grid_x + col * (card_w + gap);
        int y = grid_y + row * (card_h + gap);
        
        uint32_t bg = st->games[i].color;
        if (i == st->selected_game) {
            bg = 0xFFFFFF;
        }
        
        gfx::fillrect(s, x, y, card_w, card_h, bg);
        
        // Game icon placeholder
        gfx::fillrect(s, x + 10, y + 10, card_w - 20, card_h - 60, 0x00000033);
        
        // Game name
        gfx::text(s, x + 10, y + card_h - 45, st->games[i].name, 0xFFFFFF, bg);
        
        // Description
        gfx::text(s, x + 10, y + card_h - 28, st->games[i].description, 0xDDDDDD, bg);
        
        // Players
        char players[16];
        ksprintf(players, sizeof(players), "%d player(s)", st->games[i].players);
        gfx::text(s, x + 10, y + card_h - 14, players, 0xAAAAAA, bg);
    }
}

// ============================================================
// Playable Snake game (real game logic, keyboard controlled)
// ============================================================
static const int SNAKE_MAX = 256;
static int s_snake_x[SNAKE_MAX], s_snake_y[SNAKE_MAX];
static int s_snake_len = 3;
static int s_snake_dir = 1;        // 0=up 1=right 2=down 3=left
static int s_food_x = 12, s_food_y = 9;
static bool s_snake_dead = false;
static uint32_t s_snake_last_move = 0;
static int s_snake_score = 0;
static const int SNAKE_CELL = 16;
static const int SNAKE_COLS = 24;
static const int SNAKE_ROWS = 16;

static void snake_reset() {
    s_snake_len = 3;
    s_snake_dir = 1;
    s_snake_dead = false;
    s_snake_score = 0;
    s_snake_x[0] = 4; s_snake_y[0] = 8;
    s_snake_x[1] = 3; s_snake_y[1] = 8;
    s_snake_x[2] = 2; s_snake_y[2] = 8;
    s_food_x = 12; s_food_y = 9;
    s_snake_last_move = platform_tick_ms();
}

static void snake_spawn_food() {
    // pick a cell that is not occupied by the snake
    for (int tries = 0; tries < 200; tries++) {
        int fx = (int)((platform_tick_ms() + tries * 97) % SNAKE_COLS);
        int fy = (int)((platform_tick_ms() / 7 + tries * 131) % SNAKE_ROWS);
        bool on_snake = false;
        for (int i = 0; i < s_snake_len; i++) {
            if (s_snake_x[i] == fx && s_snake_y[i] == fy) { on_snake = true; break; }
        }
        if (!on_snake) { s_food_x = fx; s_food_y = fy; return; }
    }
    s_food_x = 0; s_food_y = 0;
}

static void snake_step() {
    if (s_snake_dead) return;
    int hx = s_snake_x[0], hy = s_snake_y[0];
    switch (s_snake_dir) {
        case 0: hy--; break;
        case 1: hx++; break;
        case 2: hy++; break;
        default: hx--; break;
    }
    // wall collision
    if (hx < 0 || hx >= SNAKE_COLS || hy < 0 || hy >= SNAKE_ROWS) {
        s_snake_dead = true;
        return;
    }
    // self collision (excluding tail which will move away unless growing)
    bool will_grow = (hx == s_food_x && hy == s_food_y);
    int check_len = will_grow ? s_snake_len : s_snake_len - 1;
    for (int i = 0; i < check_len; i++) {
        if (s_snake_x[i] == hx && s_snake_y[i] == hy) { s_snake_dead = true; return; }
    }
    // shift body
    for (int i = s_snake_len; i > 0; i--) {
        s_snake_x[i] = s_snake_x[i - 1];
        s_snake_y[i] = s_snake_y[i - 1];
    }
    s_snake_x[0] = hx; s_snake_y[0] = hy;
    if (will_grow) {
        if (s_snake_len < SNAKE_MAX) s_snake_len++;
        s_snake_score += 10;
        snake_spawn_food();
    }
}

static void snake_paint(Window* w) {
    Surface& s = w->back;
    // background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x0A0A1A);
    // board area
    int ox = (w->content_w - SNAKE_COLS * SNAKE_CELL) / 2;
    int oy = 50;
    gfx::fillrect(s, ox - 2, oy - 2, SNAKE_COLS * SNAKE_CELL + 4, SNAKE_ROWS * SNAKE_CELL + 4, 0x3498DB);
    gfx::fillrect(s, ox, oy, SNAKE_COLS * SNAKE_CELL, SNAKE_ROWS * SNAKE_CELL, 0x141428);
    // food
    gfx::fillrect(s, ox + s_food_x * SNAKE_CELL, oy + s_food_y * SNAKE_CELL, SNAKE_CELL, SNAKE_CELL, 0xE74C3C);
    // snake
    for (int i = 0; i < s_snake_len; i++) {
        uint32_t c = (i == 0) ? 0x2ECC71 : 0x27AE60;
        gfx::fillrect(s, ox + s_snake_x[i] * SNAKE_CELL, oy + s_snake_y[i] * SNAKE_CELL,
                      SNAKE_CELL - 1, SNAKE_CELL - 1, c);
    }
    // HUD
    char hud[64];
    ksprintf(hud, sizeof(hud), "Snake  Score: %d", s_snake_score);
    gfx::text(s, 12, 8, hud, 0x2ECC71, 0x0A0A1A);
    if (s_snake_dead) {
        gfx::text(s, ox, oy + SNAKE_ROWS * SNAKE_CELL / 2 - 10, "GAME OVER - press R to restart",
                  0xE74C3C, 0x141428);
    } else {
        gfx::text(s, ox, oy + SNAKE_ROWS * SNAKE_CELL + 8, "Arrows: move   R: restart",
                  0x95A5A6, 0x0A0A1A);
    }
}

static void snake_tick_wm(Window* w) {
    if (s_snake_dead) return;
    uint32_t now = platform_tick_ms();
    uint32_t delay = (s_snake_len < 6) ? 180u : (s_snake_len < 12 ? 140u : 100u);
    if (now - s_snake_last_move >= delay) {
        s_snake_last_move = now;
        snake_step();
        snake_paint(w);
    }
}

static void snake_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    if (e->ascii == 'r' || e->ascii == 'R') {
        snake_reset();
        snake_paint(w);
        return;
    }
    int nd = -1;
    switch (e->keycode) {
        case KEY_UP: nd = 0; break;
        case KEY_RIGHT: nd = 1; break;
        case KEY_DOWN: nd = 2; break;
        case KEY_LEFT: nd = 3; break;
        default: break;
    }
    if (nd >= 0 && (nd + 2) % 4 != s_snake_dir) {
        s_snake_dir = nd;  // cannot reverse into itself
    }
}

// ============================================================
// launch_game: Snake is fully playable; others show a preview
// ============================================================
static const char* k_game_names[] = {
    "Snake", "Tetris", "Pong", "Breakout",
    "Space Invaders", "Memory", "Calculator", "Paint"
};

// Full repaint of a preview game window. IMPORTANT: WM calls on_paint on
// EVERY frame, so the callback must redraw the whole scene. A callback that
// only fills the background would wipe the content to black (the bug that
// made every game except Snake show a black screen).
static void preview_paint(Window* w, int game_id) {
    Surface& s = w->back;
    int cw = w->content_w;
    int ch = w->content_h;
    if (cw < 40) cw = 40;
    if (ch < 40) ch = 40;

    // Background
    gfx::fillrect(s, 0, 0, cw, ch, 0x0A0A1A);

    // Game title bar
    gfx::fillrect(s, 0, 0, cw, 28, 0x2C3E50);
    gfx::text(s, 10, 6, k_game_names[game_id], 0xFFFFFF, 0x2C3E50);
    gfx::text(s, 380, 6, "Preview", 0x2ECC71, 0x2C3E50);

    // Game canvas
    int canvas_x = 10;
    int canvas_y = 36;
    int canvas_w = cw - 20;
    int canvas_h = ch - 76;
    if (canvas_w < 40) canvas_w = 40;
    if (canvas_h < 40) canvas_h = 40;

    gfx::fillrect(s, canvas_x, canvas_y, canvas_w, canvas_h, 0x1A1A2E);
    gfx::rect(s, canvas_x, canvas_y, canvas_w, canvas_h, 0x3498DB);

    if (game_id == 1) { // Tetris
        gfx::fillrect(s, 200, 100, 20, 20, 0x9B59B6);
        gfx::fillrect(s, 220, 100, 20, 20, 0x9B59B6);
        gfx::fillrect(s, 200, 120, 20, 20, 0x9B59B6);
        gfx::fillrect(s, 220, 120, 20, 20, 0x9B59B6);
        gfx::text(s, canvas_x + 10, canvas_y + 10, "Tetris - Blocks falling!", 0xECF0F1, 0x1A1A2E);
    } else if (game_id == 2) { // Pong
        gfx::fillrect(s, 200, canvas_y + canvas_h - 20, 60, 8, 0x3498DB);
        gfx::fillrect(s, 200, canvas_y + 10, 60, 8, 0xE74C3C);
        gfx::fillrect(s, 240, 160, 8, 8, 0xFFFFFF);
        gfx::text(s, canvas_x + 10, canvas_y + 10, "Pong - Hit the ball!", 0xECF0F1, 0x1A1A2E);
    } else if (game_id == 3) { // Breakout
        for (int i = 0; i < 12; i++) {
            int bx = canvas_x + 10 + (i % 6) * 60;
            int by = canvas_y + 10 + (i / 6) * 20;
            gfx::fillrect(s, bx, by, 50, 12, 0xE74C3C);
        }
        gfx::fillrect(s, 200, canvas_y + canvas_h - 20, 60, 8, 0xF39C12);
        gfx::fillrect(s, 240, 180, 8, 8, 0xFFFFFF);
        gfx::text(s, canvas_x + 10, canvas_y + 60, "Breakout - Break all bricks!", 0xECF0F1, 0x1A1A2E);
    } else {
        gfx::text(s, canvas_x + 10, canvas_y + 10, "Coming soon...", 0xECF0F1, 0x1A1A2E);
        gfx::text(s, canvas_x + 10, canvas_y + 40, "This game is under construction.", 0x95A5A6, 0x1A1A2E);
    }

    // Game status bar
    gfx::fillrect(s, 0, ch - 36, cw, 36, 0x2C3E50);
    gfx::text(s, 10, ch - 28, "Preview build", 0x2ECC71, 0x2C3E50);
    gfx::text(s, 200, ch - 28, "Press ESC to exit", 0x95A5A6, 0x2C3E50);
}

static void launch_game(int game_id) {
    if (game_id == 0) {
        // Playable Snake
        Window* gw = g_wm->create_window("Snake", 120, 80, 440, 380);
        if (!gw) return;
        snake_reset();
        snake_paint(gw);
        gw->on_paint = snake_paint;
        gw->on_tick = snake_tick_wm;
        gw->on_key = snake_key;
        g_wm->raise(gw);
        return;
    }

    // Preview window for the other games
    Window* gw = g_wm->create_window(k_game_names[game_id], 120, 80, 480, 400);
    if (!gw) return;

    int* pid = new int(game_id);
    gw->userdata = pid;
    preview_paint(gw, game_id);
    // on_paint must be a plain function pointer (no capture); the game id is
    // carried in userdata.
    gw->on_paint = [](Window* w) {
        int* pid = (int*)w->userdata;
        if (!pid) return;
        preview_paint(w, *pid);
    };
    gw->on_close = [](Window* w) {
        delete (int*)w->userdata;
        w->userdata = 0;
    };
    g_wm->raise(gw);
}

static void games_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    
    GameCenterState* st = (GameCenterState*)w->userdata;
    if (!st) return;
    
    int grid_x = 20;
    int grid_y = 70;
    int card_w = 150;
    int card_h = 180;
    int gap = 16;
    
    for (int i = 0; i < 8; i++) {
        int row = i / 4;
        int col = i % 4;
        int x = grid_x + col * (card_w + gap);
        int y = grid_y + row * (card_h + gap);
        
        if (mx >= x && mx < x + card_w && my >= y && my < y + card_h) {
            st->selected_game = i;
            games_paint(w);
            launch_game(i);
            return;
        }
    }
}

Window* open_game_center() {
    Window* w = g_wm->create_window("Game Center", 80, 80, 700, 500);
    if (!w) return 0;
    
    GameCenterState* st = new GameCenterState();
    w->userdata = st;
    w->on_paint = games_paint;
    w->on_mouse = games_mouse;
    
    return w;
}

} // namespace apps
} // namespace nefu
