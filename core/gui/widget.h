// nefuOS GUI Widget Library
// Provides reusable UI controls: Button, Label, TextBox, ListBox, CheckBox, RadioButton, Slider, ProgressBar
#pragma once

#include "../gfx/gfx.h"
#include "../wm.h"

namespace nefu {
namespace ui {

// ============================================
// Base Widget
// ============================================

enum WidgetType {
    WIDGET_BUTTON,
    WIDGET_LABEL,
    WIDGET_TEXTBOX,
    WIDGET_LISTBOX,
    WIDGET_CHECKBOX,
    WIDGET_RADIO,
    WIDGET_SLIDER,
    WIDGET_PROGRESSBAR,
    WIDGET_PANEL,
    WIDGET_TABVIEW
};

class Widget {
public:
    int x, y, w, h;
    const char* text;
    bool visible;
    bool enabled;
    WidgetType type;
    void* userdata;
    
    Widget(WidgetType t, int x, int y, int w, int h, const char* text)
        : x(x), y(y), w(w), h(h), text(text), visible(true), enabled(true), type(t), userdata(0) {}
    
    virtual ~Widget() {}
    
    virtual void on_paint(Surface& s) = 0;
    virtual void on_click(int mx, int my) = 0;
    virtual void on_key(int keycode, char ascii) {}
    
    bool contains(int mx, int my) {
        return mx >= x && mx < x + w && my >= y && my < y + h;
    }
};

// ============================================
// Button Widget
// ============================================

class Button : public Widget {
public:
    bool pressed;
    void (*on_click)(void* userdata);
    
    Button(int x, int y, int w, int h, const char* text)
        : Widget(WIDGET_BUTTON, x, y, w, h, text), pressed(false), on_click(0) {}
    
    void on_paint(Surface& s) override {
        uint32_t bg = pressed ? 0x0099FF : 0xCCCCCC;
        uint32_t fg = pressed ? 0xFFFFFF : 0x000000;
        
        gfx::fillrect(s, x, y, w, h, bg);
        gfx::rect(s, x, y, w, h, 0x666666);
        
        int tw = gfx::text_width(text);
        gfx::text(s, x + (w - tw) / 2, y + (h - 16) / 2, text, fg, bg);
    }
    
    void on_click(int mx, int my) override {
        pressed = !pressed;
        if (on_click) on_click(userdata);
    }
};

// ============================================
// Label Widget
// ============================================

class Label : public Widget {
public:
    uint32_t text_color;
    
    Label(int x, int y, int w, int h, const char* text)
        : Widget(WIDGET_LABEL, x, y, w, h, text), text_color(0x000000) {}
    
    void on_paint(Surface& s) override {
        gfx::text(s, x, y + (h - 16) / 2, text, text_color, 0xFFFFFF);
    }
    
    void on_click(int mx, int my) override {}
};

// ============================================
// TextBox Widget
// ============================================

class TextBox : public Widget {
public:
    char buffer[256];
    int cursor;
    bool focused;
    
    TextBox(int x, int y, int w, int h, const char* placeholder = "")
        : Widget(WIDGET_TEXTBOX, x, y, w, h, placeholder), cursor(0), focused(false) {
        buffer[0] = 0;
    }
    
    void on_paint(Surface& s) override {
        gfx::fillrect(s, x, y, w, h, 0xFFFFFF);
        gfx::rect(s, x, y, w, h, focused ? 0x0099FF : 0x999999);
        
        if (cursor == 0 && buffer[0] == 0) {
            gfx::text(s, x + 4, y + (h - 16) / 2, text, 0x999999, 0xFFFFFF);
        } else {
            gfx::text(s, x + 4, y + (h - 16) / 2, buffer, 0x000000, 0xFFFFFF);
        }
        
        if (focused) {
            gfx::char8x16(s, x + 4 + cursor * 8, y + (h - 16) / 2, '|', 0x666666, 0xFFFFFF);
        }
    }
    
    void on_click(int mx, int my) override {
        focused = true;
    }
    
    void on_key(int keycode, char ascii) override {
        if (!focused) return;
        
        if (ascii >= 32 && ascii < 127 && cursor < 255) {
            buffer[cursor++] = ascii;
            buffer[cursor] = 0;
        } else if (keycode == 8 && cursor > 0) {
            buffer[--cursor] = 0;
        }
    }
};

// ============================================
// ListBox Widget
// ============================================

class ListBox : public Widget {
public:
    const char** items;
    int item_count;
    int selected;
    int scroll;
    
    ListBox(int x, int y, int w, int h, const char** items, int count)
        : Widget(WIDGET_LISTBOX, x, y, w, h, ""), items(items), item_count(count), selected(-1), scroll(0) {}
    
    void on_paint(Surface& s) override {
        gfx::fillrect(s, x, y, w, h, 0xFFFFFF);
        gfx::rect(s, x, y, w, h, 0x999999);
        
        int visible = h / 20;
        for (int i = 0; i < visible && i + scroll < item_count; i++) {
            int iy = y + i * 20;
            if (i + scroll == selected) {
                gfx::fillrect(s, x + 1, iy + 1, w - 2, 18, 0x0099FF);
                gfx::text(s, x + 4, iy + 2, items[i + scroll], 0xFFFFFF, 0x0099FF);
            } else {
                gfx::text(s, x + 4, iy + 2, items[i + scroll], 0x000000, 0xFFFFFF);
            }
        }
    }
    
    void on_click(int mx, int my) override {
        int idx = (my - y) / 20 + scroll;
        if (idx >= 0 && idx < item_count) {
            selected = idx;
        }
    }
};

// ============================================
// CheckBox Widget
// ============================================

class CheckBox : public Widget {
public:
    bool checked;
    
    CheckBox(int x, int y, int w, int h, const char* text)
        : Widget(WIDGET_CHECKBOX, x, y, w, h, text), checked(false) {}
    
    void on_paint(Surface& s) override {
        gfx::rect(s, x, y + 2, 14, 14, 0x666666);
        if (checked) {
            gfx::fillrect(s, x + 2, y + 4, 10, 10, 0x0099FF);
        }
        gfx::text(s, x + 20, y + 2, text, 0x000000, 0xFFFFFF);
    }
    
    void on_click(int mx, int my) override {
        checked = !checked;
    }
};

// ============================================
// RadioButton Widget
// ============================================

class RadioButton : public Widget {
public:
    bool selected;
    int group;
    
    RadioButton(int x, int y, int w, int h, const char* text, int group)
        : Widget(WIDGET_RADIO, x, y, w, h, text), selected(false), group(group) {}
    
    void on_paint(Surface& s) override {
        gfx::rect(s, x, y + 2, 14, 14, 0x666666);
        if (selected) {
            gfx::fillrect(s, x + 3, y + 5, 8, 8, 0x0099FF);
        }
        gfx::text(s, x + 20, y + 2, text, 0x000000, 0xFFFFFF);
    }
    
    void on_click(int mx, int my) override {
        selected = true;
    }
};

// ============================================
// Slider Widget
// ============================================

class Slider : public Widget {
public:
    int value;
    int min_val;
    int max_val;
    
    Slider(int x, int y, int w, int h, int min_val, int max_val, int initial)
        : Widget(WIDGET_SLIDER, x, y, w, h, ""), value(initial), min_val(min_val), max_val(max_val) {}
    
    void on_paint(Surface& s) override {
        gfx::fillrect(s, x, y + h / 2 - 2, w, 4, 0xCCCCCC);
        int knob_x = x + (w - 12) * (value - min_val) / (max_val - min_val);
        gfx::rect(s, knob_x, y, 12, h, 0x0099FF);
    }
    
    void on_click(int mx, int my) override {
        value = min_val + (mx - x) * (max_val - min_val) / w;
        if (value < min_val) value = min_val;
        if (value > max_val) value = max_val;
    }
};

// ============================================
// ProgressBar Widget
// ============================================

class ProgressBar : public Widget {
public:
    int value;
    int max_value;
    
    ProgressBar(int x, int y, int w, int h, int max_value = 100)
        : Widget(WIDGET_PROGRESSBAR, x, y, w, h, ""), value(0), max_value(max_value) {}
    
    void on_paint(Surface& s) override {
        gfx::fillrect(s, x, y, w, h, 0xEEEEEE);
        gfx::rect(s, x, y, w, h, 0x999999);
        
        int fill_w = (w - 2) * value / max_value;
        gfx::fillrect(s, x + 1, y + 1, fill_w, h - 2, 0x00CC00);
    }
    
    void on_click(int mx, int my) override {}
};

// ============================================
// Panel Widget (container)
// ============================================

class Panel : public Widget {
public:
    Widget** children;
    int child_count;
    int child_capacity;
    
    Panel(int x, int y, int w, int h)
        : Widget(WIDGET_PANEL, x, y, w, h, ""), children(0), child_count(0), child_capacity(0) {}
    
    ~Panel() {
        if (children) {
            for (int i = 0; i < child_count; i++) delete children[i];
            kfree(children);
        }
    }
    
    void add(Widget* w) {
        if (child_count >= child_capacity) {
            child_capacity = child_capacity ? child_capacity * 2 : 8;
            Widget** new_children = (Widget**)kalloc(child_capacity * sizeof(Widget*));
            if (children) {
                memcpy(new_children, children, child_count * sizeof(Widget*));
                kfree(children);
            }
            children = new_children;
        }
        children[child_count++] = w;
    }
    
    void on_paint(Surface& s) override {
        gfx::fillrect(s, x, y, w, h, 0xF0F0F0);
        gfx::rect(s, x, y, w, h, 0xCCCCCC);
        
        for (int i = 0; i < child_count; i++) {
            if (children[i]->visible) {
                children[i]->on_paint(s);
            }
        }
    }
    
    void on_click(int mx, int my) override {
        for (int i = child_count - 1; i >= 0; i--) {
            if (children[i]->visible && children[i]->contains(mx, my)) {
                children[i]->on_click(mx, my);
                return;
            }
        }
    }
};

// ============================================
// TabView Widget
// ============================================

class TabView : public Widget {
public:
    const char** tab_names;
    Panel** tabs;
    int tab_count;
    int active_tab;
    
    TabView(int x, int y, int w, int h, const char** names, int count)
        : Widget(WIDGET_TABVIEW, x, y, w, h, ""), tab_names(names), tabs(0), tab_count(count), active_tab(0) {
        tabs = (Panel**)kalloc(count * sizeof(Panel*));
        for (int i = 0; i < count; i++) {
            tabs[i] = new Panel(x, y + 24, w, h - 24);
        }
    }
    
    ~TabView() {
        for (int i = 0; i < tab_count; i++) delete tabs[i];
        kfree(tabs);
    }
    
    void on_paint(Surface& s) override {
        // Tab headers
        int tx = x;
        for (int i = 0; i < tab_count; i++) {
            int tw = 80;
            if (i == active_tab) {
                gfx::fillrect(s, tx, y, tw, 20, 0xFFFFFF);
                gfx::text(s, tx + 6, y + 2, tab_names[i], 0x000000, 0xFFFFFF);
            } else {
                gfx::text(s, tx + 6, y + 2, tab_names[i], 0x666666, 0xF0F0F0);
            }
            tx += tw + 2;
        }
        
        // Active tab content
        tabs[active_tab]->on_paint(s);
    }
    
    void on_click(int mx, int my) override {
        int tx = x;
        for (int i = 0; i < tab_count; i++) {
            int tw = 80;
            if (mx >= tx && mx < tx + tw && my >= y && my < y + 20) {
                active_tab = i;
                return;
            }
            tx += tw + 2;
        }
        tabs[active_tab]->on_click(mx, my);
    }
};

} // namespace ui
} // namespace nefu
