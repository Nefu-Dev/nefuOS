// nefuOS Calculator App
// Simple 4-function calculator with basic operations

#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include <cstring>

namespace nefu {

namespace {

struct CalcState {
    char display[32];
    double current;
    double stored;
    char op;
    bool new_number;
    Button btns[20];

    CalcState() : current(0), stored(0), op(0), new_number(true) {
        display[0] = '0';
        display[1] = 0;
    }
};

static void calc_paint(Window* w) {
    CalcState* st = (CalcState*)w->userdata;
    Surface& s = w->back;

    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x202020);

    // Display
    gfx::fillrect(s, 10, 10, w->content_w - 20, 50, 0x000000);
    gfx::text(s, 20, 25, st->display, 0x00FF00, 0x000000);

    // Buttons
    const char* labels[20] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", ".", "=", "+",
        "C", "CE", "√", "^2"
    };

    int bw = (w->content_w - 30) / 4;
    int bh = 40;
    int start_y = 70;

    for (int i = 0; i < 20; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = 10 + col * (bw + 5);
        int y = start_y + row * (bh + 5);

        Button& b = st->btns[i];
        b.x = x;
        b.y = y;
        b.w = bw;
        b.h = bh;
        b.label = labels[i];
        b.id = i;
        ui::draw_button(s, b);
    }
}

static void calc_key(CalcState* st, int btn_id) {
    const char* labels[20] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", ".", "=", "+",
        "C", "CE", "√", "^2"
    };

    const char* lbl = labels[btn_id];

    if (lbl[0] >= '0' && lbl[0] <= '9') {
        if (st->new_number) {
            st->display[0] = 0;
            st->new_number = false;
        }
        int len = strlen(st->display);
        if (len < 30) {
            strcat(st->display, lbl);
        }
    } else if (lbl[0] == '.') {
        if (st->new_number) {
            strcpy(st->display, "0.");
            st->new_number = false;
        } else {
            if (!strchr(st->display, '.')) {
                strcat(st->display, ".");
            }
        }
    } else if (lbl[0] == '+' || lbl[0] == '-' || lbl[0] == '*' || lbl[0] == '/') {
        st->stored = atof(st->display);
        st->op = lbl[0];
        st->new_number = true;
    } else if (lbl[0] == '=') {
        double b = atof(st->display);
        double result = 0;
        switch (st->op) {
            case '+': result = st->stored + b; break;
            case '-': result = st->stored - b; break;
            case '*': result = st->stored * b; break;
            case '/': result = (b != 0) ? st->stored / b : 0; break;
        }
        ksprintf(st->display, 32, "%g", result);
        st->op = 0;
        st->new_number = true;
    } else if (strcmp(lbl, "C") == 0) {
        st->display[0] = '0';
        st->display[1] = 0;
        st->current = 0;
        st->stored = 0;
        st->op = 0;
        st->new_number = true;
    } else if (strcmp(lbl, "CE") == 0) {
        st->display[0] = '0';
        st->display[1] = 0;
        st->new_number = true;
    } else if (strcmp(lbl, "√") == 0) {
        double val = atof(st->display);
        if (val > 0) {
            double result = val;
            // Simple sqrt approximation
            for (int i = 0; i < 10; i++) {
                result = (result + val / result) / 2;
            }
            ksprintf(st->display, 32, "%g", result);
        }
        st->new_number = true;
    } else if (strcmp(lbl, "^2") == 0) {
        double val = atof(st->display);
        double result = val * val;
        ksprintf(st->display, 32, "%g", result);
        st->new_number = true;
    }
}

static void calc_mouse(Window* w, int mx, int my, uint8_t buttons) {
    CalcState* st = (CalcState*)w->userdata;
    bool pressed = buttons;

    int bw = (w->content_w - 30) / 4;
    int bh = 40;
    int start_y = 70;

    for (int i = 0; i < 20; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = 10 + col * (bw + 5);
        int y = start_y + row * (bh + 5);

        if (pressed && mx >= x && mx < x + bw && my >= y && my < y + bh) {
            calc_key(st, i);
        }
    }
}

static void calc_close(Window* w) {
    CalcState* st = (CalcState*)w->userdata;
    delete st;
}

} // namespace

void calculator_launch() {
    CalcState* st = new CalcState();
    Window* w = g_wm->create_window("Calculator", 100, 100, 240, 320);
    w->userdata = st;
    w->on_paint = calc_paint;
    w->on_mouse = calc_mouse;
    w->on_close = calc_close;
    g_wm->raise(w);
}

} // namespace nefu
