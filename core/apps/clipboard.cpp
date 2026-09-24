// nefuOS Clipboard Manager
// Manages clipboard history and allows pasting previous items
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

#define MAX_CLIPBOARD_HISTORY 20
#define CLIPBOARD_ITEM_LEN 256

static char s_clip_history[MAX_CLIPBOARD_HISTORY][CLIPBOARD_ITEM_LEN];
static int s_clip_count = 0;
static int s_selected = -1;

// Add item to clipboard history
void clipboard_add(const char* text) {
    if (!text || !*text) return;
    
    // Shift history down
    for (int i = s_clip_count - 1; i > 0; i--) {
        strncpy(s_clip_history[i], s_clip_history[i-1], CLIPBOARD_ITEM_LEN - 1);
    }
    
    // Add new item at top
    strncpy(s_clip_history[0], text, CLIPBOARD_ITEM_LEN - 1);
    s_clip_history[0][CLIPBOARD_ITEM_LEN - 1] = 0;
    
    if (s_clip_count < MAX_CLIPBOARD_HISTORY) s_clip_count++;
    s_selected = 0;
}

// Get clipboard item
const char* clipboard_get(int idx) {
    if (idx < 0 || idx >= s_clip_count) return "";
    return s_clip_history[idx];
}

// Get count
int clipboard_count() {
    return s_clip_count;
}

void clipboard_launch() {
    Window* w = g_wm->create_window("Clipboard Manager", 80, 80, 480, 360);
    if (!w) return;
    
    // Title
    gfx::fillrect(w->back, 0, 0, w->content_w, 32, 0x4A90D9);
    gfx::text(w->back, 10, 8, "Clipboard History", 0xFFFFFF, 0x4A90D9);
    
    // List items
    int list_y = 40;
    int item_h = 28;
    
    for (int i = 0; i < s_clip_count && i < 10; i++) {
        int y = list_y + i * item_h;
        
        // Background
        if (i == s_selected) {
            gfx::fillrect(w->back, 4, y, w->content_w - 8, item_h - 2, 0xE3F2FD);
            gfx::rect(w->back, 4, y, w->content_w - 8, item_h - 2, 0x4A90D9);
        } else {
            gfx::rect(w->back, 4, y, w->content_w - 8, item_h - 2, 0xDDDDDD);
        }
        
        // Item text
        char buf[CLIPBOARD_ITEM_LEN + 4];
        ksprintf(buf, CLIPBOARD_ITEM_LEN + 4, "%d. %s", i + 1, s_clip_history[i]);
        gfx::text(w->back, 12, y + 6, buf, 0x333333, i == s_selected ? 0xE3F2FD : 0xFFFFFF);
    }
    
    // Empty state
    if (s_clip_count == 0) {
        gfx::text(w->back, 100, 160, "Clipboard is empty", 0x999999, 0xFFFFFF);
        gfx::text(w->back, 80, 180, "Copy some text first!", 0x999999, 0xFFFFFF);
    }
    
    // Bottom hint
    gfx::fillrect(w->back, 0, w->content_h - 28, w->content_w, 28, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 20, "Click an item to select it", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
}

} // namespace nefu
