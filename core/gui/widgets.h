// nefuOS widgets
#pragma once
#include "gfx.h"

namespace nefu {

struct Button {
    int x, y, w, h;
    const char* label;
    int id;
    bool pressed;
    void (*on_click)(void* ud);
    void* ud;
    Button() : x(0), y(0), w(0), h(0), label(0), id(0), pressed(false), on_click(0), ud(0) {}
};

namespace ui {

void draw_button(Surface& s, Button& b);
// return true event consumed by button
bool button_event(Button& b, int mx, int my, uint8_t buttons, bool pressed, bool released);

// Clock widget
struct ClockWidget {
    int x, y;
    int hour, minute, second;
    ClockWidget(int x_, int y_) : x(x_), y(y_), hour(12), minute(0), second(0) {}
};

void draw_clock(Surface& s, ClockWidget& w);

// CPU usage widget
struct CPUWidget {
    int x, y;
    int percent;
    CPUWidget(int x_, int y_) : x(x_), y(y_), percent(45) {}
};

void draw_cpu(Surface& s, CPUWidget& w);

// Memory widget
struct MemoryWidget {
    int x, y;
    int percent;
    MemoryWidget(int x_, int y_) : x(x_), y(y_), percent(60) {}
};

void draw_memory(Surface& s, MemoryWidget& w);

// Weather widget
struct WeatherWidget {
    int x, y;
    int temp;
    const char* condition;
    WeatherWidget(int x_, int y_) : x(x_), y(y_), temp(22), condition("Sunny") {}
};

void draw_weather(Surface& s, WeatherWidget& w);

} // namespace ui
} // namespace nefu
