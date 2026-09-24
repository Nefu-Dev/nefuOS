// nefuOS Input System - Full Implementation
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace input {

// Key codes
enum KeyCode {
    KEY_NONE = 0,
    KEY_ESCAPE = 1,
    KEY_1 = 2, KEY_2 = 3, KEY_3 = 4, KEY_4 = 5, KEY_5 = 6,
    KEY_6 = 7, KEY_7 = 8, KEY_8 = 9, KEY_9 = 10, KEY_0 = 11,
    KEY_MINUS = 12, KEY_EQUALS = 13, KEY_BACKSPACE = 14,
    KEY_TAB = 15,
    KEY_Q = 16, KEY_W = 17, KEY_E = 18, KEY_R = 19, KEY_T = 20,
    KEY_Y = 21, KEY_U = 22, KEY_I = 23, KEY_O = 24, KEY_P = 25,
    KEY_LEFTBRACKET = 26, KEY_RIGHTBRACKET = 27, KEY_ENTER = 28,
    KEY_LEFTCTRL = 29,
    KEY_A = 30, KEY_S = 31, KEY_D = 32, KEY_F = 33, KEY_G = 34,
    KEY_H = 35, KEY_J = 36, KEY_K = 37, KEY_L = 38,
    KEY_SEMICOLON = 39, KEY_APOSTROPHE = 40, KEY_GRAVE = 41,
    KEY_LEFTSHIFT = 42, KEY_BACKSLASH = 43,
    KEY_Z = 44, KEY_X = 45, KEY_C = 46, KEY_V = 47, KEY_B = 48,
    KEY_N = 49, KEY_M = 50,
    KEY_COMMA = 51, KEY_DOT = 52, KEY_SLASH = 53,
    KEY_RIGHTSHIFT = 54,
    KEY_KPASTERISK = 55,
    KEY_LEFTALT = 56, KEY_SPACE = 57, KEY_CAPSLOCK = 58,
    KEY_F1 = 59, KEY_F2 = 60, KEY_F3 = 61, KEY_F4 = 62, KEY_F5 = 63,
    KEY_F6 = 64, KEY_F7 = 65, KEY_F8 = 66, KEY_F9 = 67, KEY_F10 = 68,
    KEY_NUMLOCK = 69, KEY_SCROLLLOCK = 70,
    KEY_KP7 = 71, KEY_KP8 = 72, KEY_KP9 = 73,
    KEY_KPMINUS = 74, KEY_KP4 = 75, KEY_KP5 = 76, KEY_KP6 = 77,
    KEY_KPPLUS = 78, KEY_KP1 = 79, KEY_KP2 = 80, KEY_KP3 = 81,
    KEY_KP0 = 82, KEY_KPDOT = 83,
    KEY_F11 = 87, KEY_F12 = 88,
    KEY_RIGHTCTRL = 97, KEY_KPSLASH = 98, KEY_RIGHTALT = 99,
    KEY_HOME = 102, KEY_UP = 103, KEY_PAGEUP = 104,
    KEY_LEFT = 105, KEY_RIGHT = 106,
    KEY_END = 107, KEY_DOWN = 108, KEY_PAGEDOWN = 109,
    KEY_INSERT = 110, KEY_DELETE = 111
};

// Mouse buttons
enum MouseButton {
    MOUSE_BUTTON_LEFT = 1,
    MOUSE_BUTTON_RIGHT = 2,
    MOUSE_BUTTON_MIDDLE = 4,
    MOUSE_BUTTON_X1 = 8,
    MOUSE_BUTTON_X2 = 16
};

// Key event
struct KeyEvent {
    KeyCode key;
    bool down;
    bool shift;
    bool ctrl;
    bool alt;
    char ascii;
    uint32_t time;
};

// Mouse event
struct MouseEvent {
    int x;
    int y;
    int dx;
    int dy;
    MouseButton buttons;
    int wheel;
    uint32_t time;
};

// Input event
struct InputEvent {
    enum { KEY_EVENT, MOUSE_EVENT } type;
    union {
        KeyEvent key;
        MouseEvent mouse;
    };
};

// Input handler callback
typedef void (*InputHandler)(const InputEvent* event, void* userdata);

// Input manager
class InputManager {
private:
    InputHandler handlers[8];
    void* handler_userdata[8];
    int handler_count;
    
    // Keyboard state
    bool key_down[256];
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
    
    // Mouse state
    int mouse_x;
    int mouse_y;
    MouseButton mouse_buttons;
    int mouse_wheel;
    
    // Event queue
    InputEvent event_queue[256];
    int queue_head;
    int queue_tail;
    
public:
    InputManager() : handler_count(0), mouse_x(0), mouse_y(0), 
                     mouse_buttons(MOUSE_BUTTON_LEFT), mouse_wheel(0),
                     queue_head(0), queue_tail(0) {
        memset(key_down, 0, sizeof(key_down));
        shift_pressed = false;
        ctrl_pressed = false;
        alt_pressed = false;
    }
    
    // Initialize input
    bool init() {
        // Initialize input devices
        return true;
    }
    
    // Shutdown input
    void shutdown() {
        handler_count = 0;
        queue_head = 0;
        queue_tail = 0;
    }
    
    // Register input handler
    int register_handler(InputHandler handler, void* userdata) {
        if (handler_count >= 8) return -1;
        
        handlers[handler_count] = handler;
        handler_userdata[handler_count] = userdata;
        return handler_count++;
    }
    
    // Unregister input handler
    void unregister_handler(int id) {
        if (id < 0 || id >= handler_count) return;
        
        // Shift handlers
        for (int i = id; i < handler_count - 1; i++) {
            handlers[i] = handlers[i + 1];
            handler_userdata[i] = handler_userdata[i + 1];
        }
        handler_count--;
    }
    
    // Push key event
    void push_key_event(KeyCode key, bool down) {
        InputEvent event;
        event.type = InputEvent::KEY_EVENT;
        event.key.key = key;
        event.key.down = down;
        event.key.shift = shift_pressed;
        event.key.ctrl = ctrl_pressed;
        event.key.alt = alt_pressed;
        event.key.time = 0;
        
        // Update key state
        key_down[key] = down;
        
        // Update modifier keys
        if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) shift_pressed = down;
        if (key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL) ctrl_pressed = down;
        if (key == KEY_LEFTALT || key == KEY_RIGHTALT) alt_pressed = down;
        
        // Convert to ASCII
        event.key.ascii = key_to_ascii(key, shift_pressed);
        
        // Push to queue
        push_event(&event);
    }
    
    // Push mouse event
    void push_mouse_event(int x, int y, MouseButton buttons, int wheel) {
        InputEvent event;
        event.type = InputEvent::MOUSE_EVENT;
        event.mouse.x = x;
        event.mouse.y = y;
        event.mouse.dx = x - mouse_x;
        event.mouse.dy = y - mouse_y;
        event.mouse.buttons = buttons;
        event.mouse.wheel = wheel;
        event.mouse.time = 0;
        
        mouse_x = x;
        mouse_y = y;
        mouse_buttons = buttons;
        mouse_wheel += wheel;
        
        push_event(&event);
    }
    
    // Poll events
    bool poll_event(InputEvent* event) {
        if (queue_head == queue_tail) return false;
        
        *event = event_queue[queue_head];
        queue_head = (queue_head + 1) % 256;
        
        // Dispatch to handlers
        dispatch_event(event);
        
        return true;
    }
    
    // Check if key is down
    bool is_key_down(KeyCode key) const {
        return key_down[key];
    }
    
    // Check if shift is pressed
    bool is_shift_down() const {
        return shift_pressed;
    }
    
    // Check if ctrl is pressed
    bool is_ctrl_down() const {
        return ctrl_pressed;
    }
    
    // Check if alt is pressed
    bool is_alt_down() const {
        return alt_pressed;
    }
    
    // Get mouse position
    void get_mouse_position(int& x, int& y) const {
        x = mouse_x;
        y = mouse_y;
    }
    
    // Get mouse buttons
    MouseButton get_mouse_buttons() const {
        return mouse_buttons;
    }
    
    // Get mouse wheel
    int get_mouse_wheel() const {
        return mouse_wheel;
    }
    
private:
    void push_event(const InputEvent* event) {
        int next_tail = (queue_tail + 1) % 256;
        if (next_tail == queue_head) return;  // Queue full
        
        event_queue[queue_tail] = *event;
        queue_tail = next_tail;
    }
    
    void dispatch_event(const InputEvent* event) {
        for (int i = 0; i < handler_count; i++) {
            handlers[i](event, handler_userdata[i]);
        }
    }
    
    char key_to_ascii(KeyCode key, bool shift) {
        if (key >= KEY_1 && key <= KEY_0) {
            const char* normal = "1234567890";
            const char* shifted = "!@#$%^&*()";
            return shift ? shifted[key - KEY_1] : normal[key - KEY_1];
        }
        
        if (key >= KEY_A && key <= KEY_Z) {
            char c = 'a' + (key - KEY_A);
            return shift ? c - 32 : c;
        }
        
        if (key == KEY_SPACE) return ' ';
        if (key == KEY_ENTER) return '\n';
        if (key == KEY_TAB) return '\t';
        
        if (key == KEY_MINUS) return shift ? '_' : '-';
        if (key == KEY_EQUALS) return shift ? '+' : '=';
        if (key == KEY_LEFTBRACKET) return shift ? '{' : '[';
        if (key == KEY_RIGHTBRACKET) return shift ? '}' : ']';
        if (key == KEY_SEMICOLON) return shift ? ':' : ';';
        if (key == KEY_APOSTROPHE) return shift ? '"' : '\'';
        if (key == KEY_GRAVE) return shift ? '~' : '`';
        if (key == KEY_BACKSLASH) return shift ? '|' : '\\';
        if (key == KEY_COMMA) return shift ? '<' : ',';
        if (key == KEY_DOT) return shift ? '>' : '.';
        if (key == KEY_SLASH) return shift ? '?' : '/';
        
        return 0;
    }
};

// Global input manager
InputManager g_input;

} // namespace input
} // namespace nefu
