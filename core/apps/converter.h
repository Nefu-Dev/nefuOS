// nefuOS Unit Converter
// Convert between different units (length, weight, temperature, etc.)
#pragma once
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu { namespace apps {

enum class ConvCategory {
    Length,
    Weight,
    Temperature,
    Volume,
    Area
};

struct UnitOption {
    const char* name;
    double factor;  // relative to base unit
};

static ConvCategory s_conv_cat = ConvCategory::Length;
static int s_from_unit = 0;
static int s_to_unit = 1;
static char s_input[32] = "1.0";
static int s_input_len = 3;
static bool s_input_focus = false;   // typing in the value box
static int  s_drop_open = 0;         // 0 = closed, 1 = from list, 2 = to list

static UnitOption length_units[] = {
    { "mm", 0.001 },
    { "cm", 0.01 },
    { "m", 1.0 },
    { "km", 1000.0 },
    { "inch", 0.0254 },
    { "foot", 0.3048 },
    { "mile", 1609.344 },
};

static UnitOption weight_units[] = {
    { "g", 0.001 },
    { "kg", 1.0 },
    { "lb", 0.453592 },
    { "oz", 0.0283495 },
    { "ton", 1000.0 },
};

static UnitOption temperature_units[] = {
    { "C", 1.0 },
    { "F", 1.0 },
    { "K", 1.0 },
};

static UnitOption volume_units[] = {
    { "ml", 0.001 },
    { "liter", 1.0 },
    { "gallon", 3.78541 },
    { "pint", 0.473176 },
    { "cup", 0.236588 },
};

static UnitOption area_units[] = {
    { "m2", 1.0 },
    { "km2", 1000000.0 },
    { "ft2", 0.092903 },
    { "acre", 4046.86 },
    { "hectare", 10000.0 },
};

static UnitOption* get_units(ConvCategory cat, int& count) {
    switch (cat) {
        case ConvCategory::Length: count = 7; return length_units;
        case ConvCategory::Weight: count = 5; return weight_units;
        case ConvCategory::Temperature: count = 3; return temperature_units;
        case ConvCategory::Volume: count = 5; return volume_units;
        case ConvCategory::Area: count = 5; return area_units;
        default: count = 7; return length_units;
    }
}

static double parse_input(const char* str) {
    int i = 0;
    double val = 0.0;
    double frac = 0.1;
    bool in_frac = false;
    bool neg = false;
    if (str[0] == '-') { neg = true; i = 1; }
    while (str[i]) {
        if (str[i] == '.') {
            in_frac = true;
        } else if (str[i] >= '0' && str[i] <= '9') {
            if (in_frac) {
                val += (str[i] - '0') * frac;
                frac *= 0.1;
            } else {
                val = val * 10 + (str[i] - '0');
            }
        }
        i++;
    }
    return neg ? -val : val;
}

static void paint_converter(Window* w) {
    Surface& s = w->back;
    
    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xFFFFFF);
    
    // Title
    gfx::fillrect(s, 0, 0, w->content_w, 32, 0x8E44AD);
    gfx::text(s, 12, 8, "Unit Converter", 0xFFFFFF, 0x8E44AD);
    
    // Category tabs
    const char* cats[] = { "Length", "Weight", "Temp", "Volume", "Area" };
    for (int i = 0; i < 5; i++) {
        int bx = 12 + i * 70;
        int by = 42;
        int bw = 64;
        int bh = 26;
        
        bool active = (int)s_conv_cat == i;
        gfx::fillrect(s, bx, by, bw, bh, active ? 0x9B59B6 : 0xECF0F1);
        gfx::text(s, bx + 8, by + 7, cats[i], active ? 0xFFFFFF : 0x2C3E50,
                  active ? 0x9B59B6 : 0xECF0F1);
    }
    
    int count;
    UnitOption* units = get_units(s_conv_cat, count);
    if (s_from_unit >= count) s_from_unit = count - 1;
    if (s_to_unit >= count) s_to_unit = count - 1;
    
    // Input value
    gfx::text(s, 20, 90, "Value:", 0x2C3E50, 0xFFFFFF);
    gfx::fillrect(s, 80, 84, 120, 28, s_input_focus ? 0xFFF9E6 : 0xF8F9FA);
    gfx::rect(s, 80, 84, 120, 28, s_input_focus ? 0xF39C12 : 0xBDC3C7);
    gfx::text(s, 88, 91, s_input, 0x2C3E50, s_input_focus ? 0xFFF9E6 : 0xF8F9FA);
    if (s_input_focus) {
        // blinking cursor
        static uint32_t cursor_phase = 0;
        if ((platform_tick_ms() / 400) & 1) {
            int cw = gfx::text_width(s_input);
            gfx::fillrect(s, 88 + cw + 2, 89, 2, 16, 0x2C3E50);
        }
        (void)cursor_phase;
    }
    
    // From unit dropdown
    gfx::text(s, 20, 130, "From:", 0x2C3E50, 0xFFFFFF);
    gfx::fillrect(s, 80, 124, 120, 28, 0xEBF5FB);
    gfx::rect(s, 80, 124, 120, 28, 0x3498DB);
    gfx::text(s, 88, 131, units[s_from_unit].name, 0x2C3E50, 0xEBF5FB);
    gfx::text(s, 180, 131, "v", 0x3498DB, 0xEBF5FB);
    
    // To unit dropdown
    gfx::text(s, 20, 170, "To:", 0x2C3E50, 0xFFFFFF);
    gfx::fillrect(s, 80, 164, 120, 28, 0xEAFAF1);
    gfx::rect(s, 80, 164, 120, 28, 0x27AE60);
    gfx::text(s, 88, 171, units[s_to_unit].name, 0x2C3E50, 0xEAFAF1);
    gfx::text(s, 180, 171, "v", 0x27AE60, 0xEAFAF1);
    
    // Dropdown lists
    if (s_drop_open != 0) {
        int dy = (s_drop_open == 1) ? 152 : 192;  // below the box
        int dh = count * 20 + 6;
        if (dy + dh > w->content_h - 10) dh = w->content_h - 10 - dy;
        gfx::fillrect(s, 80, dy, 120, dh, 0xFFFFFF);
        gfx::rect(s, 80, dy, 120, dh, 0x7F8C8D);
        for (int i = 0; i < count; i++) {
            int iy = dy + 3 + i * 20;
            bool sel = (s_drop_open == 1) ? (i == s_from_unit) : (i == s_to_unit);
            gfx::fillrect(s, 82, iy, 116, 18, sel ? 0xDFE8F6 : 0xFFFFFF);
            gfx::text(s, 88, iy + 3, units[i].name, sel ? 0x2C3E50 : 0x34495E,
                      sel ? 0xDFE8F6 : 0xFFFFFF);
        }
    }
    
    // Result
    gfx::fillrect(s, 20, 210, w->content_w - 40, 50, 0xF8F9FA);
    gfx::rect(s, 20, 210, w->content_w - 40, 50, 0xDEE2E6);
    
    double input_val = parse_input(s_input);
    
    double result;
    if (s_conv_cat == ConvCategory::Temperature) {
        // Special temperature conversion
        double celsius;
        if (s_from_unit == 0) celsius = input_val; // C
        else if (s_from_unit == 1) celsius = (input_val - 32) * 5.0 / 9.0; // F
        else celsius = input_val + 273.15; // K
        
        if (s_to_unit == 0) result = celsius;
        else if (s_to_unit == 1) result = celsius * 9.0 / 5.0 + 32;
        else result = celsius - 273.15;
    } else {
        double base_val = input_val * units[s_from_unit].factor;
        result = base_val / units[s_to_unit].factor;
    }
    
    char result_str[32];
    ksprintf(result_str, sizeof(result_str), "%.4f %s", result, units[s_to_unit].name);
    
    gfx::text(s, 35, 225, result_str, 0x2C3E50, 0xF8F9FA);
    
    // Hint
    gfx::text(s, 20, w->content_h - 30, "Click a box, type a value, pick units", 0x95A5A6, 0xFFFFFF);
}

static void on_converter_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    int count;
    get_units(s_conv_cat, count);
    
    // Dropdown item selection (highest priority: click inside an open list)
    if (s_drop_open != 0) {
        int dy = (s_drop_open == 1) ? 152 : 192;
        if (mx >= 80 && mx <= 200 && my >= dy && my <= dy + count * 20 + 3) {
            int idx = (my - dy - 3) / 20;
            if (idx >= 0 && idx < count) {
                if (s_drop_open == 1) s_from_unit = idx;
                else s_to_unit = idx;
                s_drop_open = 0;
                paint_converter(w);
                return;
            }
        }
        // click outside the list closes it
        s_drop_open = 0;
        paint_converter(w);
        return;
    }
    
    // Category tabs
    for (int i = 0; i < 5; i++) {
        int bx = 12 + i * 70;
        int by = 42;
        int bw = 64;
        int bh = 26;
        
        if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
            s_conv_cat = (ConvCategory)i;
            s_from_unit = 0;
            s_to_unit = 1;
            s_input_focus = false;
            s_drop_open = 0;
            paint_converter(w);
            return;
        }
    }
    
    // Input box focus
    if (mx >= 80 && mx <= 200 && my >= 84 && my <= 112) {
        s_input_focus = true;
        s_drop_open = 0;
        paint_converter(w);
        return;
    }
    
    // From dropdown
    if (mx >= 80 && mx <= 200 && my >= 124 && my <= 152) {
        s_drop_open = (s_drop_open == 1) ? 0 : 1;
        s_input_focus = false;
        paint_converter(w);
        return;
    }
    
    // To dropdown
    if (mx >= 80 && mx <= 200 && my >= 164 && my <= 192) {
        s_drop_open = (s_drop_open == 2) ? 0 : 2;
        s_input_focus = false;
        paint_converter(w);
        return;
    }
}

static void on_converter_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down || !s_input_focus) return;
    
    if (e->keycode == KEY_BACKSPACE) {
        if (s_input_len > 0) {
            s_input[--s_input_len] = 0;
            paint_converter(w);
        }
        return;
    }
    if (e->keycode == KEY_ENTER || e->keycode == KEY_ESC) {
        s_input_focus = false;
        paint_converter(w);
        return;
    }
    
    char ch = e->ascii;
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-') {
        if (s_input_len < 31) {
            s_input[s_input_len++] = ch;
            s_input[s_input_len] = 0;
            paint_converter(w);
        }
    }
}

static void converter_launch() {
    Window* w = g_wm->create_window("Unit Converter", 100, 100, 280, 290);
    if (!w) return;
    
    s_input_focus = false;
    s_drop_open = 0;
    paint_converter(w);
    w->on_mouse = on_converter_mouse;
    w->on_key = on_converter_key;
    w->on_paint = paint_converter;
    
    g_wm->raise(w);
}

}} // namespace
