// nefuOS Calculator Application
#pragma once

#include "../gfx/gfx.h"
#include "../wm.h"
#include "../klib/klib.h"

namespace nefu {
namespace apps {

struct CalcState {
    char display[64];
    double current;
    double previous;
    char operation;
    bool new_number;
    
    CalcState() : current(0), previous(0), operation(0), new_number(true) {
        strcpy(display, "0");
    }
    
    void input_digit(int d) {
        if (new_number) {
            ksprintf(display, sizeof(display), "%d", d);
            new_number = false;
        } else {
            int len = strlen(display);
            if (len < 15) {
                char buf[2];
                ksprintf(buf, sizeof(buf), "%d", d);
                strcat(display, buf);
            }
        }
        current = atof(display);
    }
    
    void input_dot() {
        if (new_number) {
            strcpy(display, "0.");
            new_number = false;
        } else if (!strchr(display, '.')) {
            strcat(display, ".");
        }
    }
    
    void set_op(char op) {
        if (!new_number) {
            if (operation) compute();
            previous = current;
            current = 0;
            new_number = true;
        }
        operation = op;
    }
    
    void compute() {
        if (!operation) return;
        
        double result = 0;
        switch (operation) {
            case '+': result = previous + current; break;
            case '-': result = previous - current; break;
            case '*': result = previous * current; break;
            case '/': 
                if (current != 0) result = previous / current; 
                else result = 0;
                break;
        }
        
        ksprintf(display, sizeof(display), "%g", result);
        current = result;
        previous = 0;
        operation = 0;
        new_number = true;
    }
    
    void clear() {
        current = 0;
        previous = 0;
        operation = 0;
        new_number = true;
        strcpy(display, "0");
    }
};

static void calc_paint(Window* w) {
    CalcState* st = (CalcState*)w->userdata;
    if (!st) return;
    
    Surface& s = w->back;
    
    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x2C3E50);
    
    // Display
    gfx::fillrect(s, 8, 8, w->content_w - 16, 50, 0xECF0F1);
    gfx::text(s, 16, 24, st->display, 0x2C3E50, 0xECF0F1);
    
    // Buttons
    const char* labels[] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", ".", "=", "+"
    };
    
    int btn_w = (w->content_w - 32) / 4;
    int btn_h = 40;
    int start_y = 70;
    
    for (int i = 0; i < 16; i++) {
        int row = i / 4;
        int col = i % 4;
        int x = 8 + col * (btn_w + 4);
        int y = start_y + row * (btn_h + 4);
        
        uint32_t color = 0x34495E;
        if (strchr("/*-+=", labels[i])) color = 0xE67E22;
        if (labels[i][0] == '=') color = 0x27AE60;
        
        gfx::fillrect(s, x, y, btn_w, btn_h, color);
        gfx::text(s, x + btn_w/2 - 4, y + 14, labels[i], 0xFFFFFF, color);
    }
}

static void calc_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    
    CalcState* st = (CalcState*)w->userdata;
    if (!st) return;
    
    int btn_w = (w->content_w - 32) / 4;
    int btn_h = 40;
    int start_y = 70;
    
    const char* labels[] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", ".", "=", "+"
    };
    
    for (int i = 0; i < 16; i++) {
        int row = i / 4;
        int col = i % 4;
        int x = 8 + col * (btn_w + 4);
        int y = start_y + row * (btn_h + 4);
        
        if (mx >= x && mx < x + btn_w && my >= y && my < y + btn_h) {
            char c = labels[i][0];
            if (c >= '0' && c <= '9') {
                st->input_digit(c - '0');
            } else if (c == '.') {
                st->input_dot();
            } else if (c == '=') {
                st->compute();
            } else {
                st->set_op(c);
            }
            return;
        }
    }
}

Window* open_calculator() {
    Window* w = g_wm->create_window("Calculator", 200, 200, 220, 280);
    if (!w) return 0;
    
    CalcState* st = new CalcState();
    w->userdata = st;
    w->on_paint = calc_paint;
    w->on_mouse = calc_mouse;
    
    return w;
}

} // namespace apps
} // namespace nefu
