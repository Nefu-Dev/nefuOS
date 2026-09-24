// nefuOS Notification Center
// Shows system notifications and alerts
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

#define MAX_NOTIFICATIONS 15
#define NOTIF_TITLE_LEN 64
#define NOTIF_BODY_LEN 128

struct Notification {
    char title[NOTIF_TITLE_LEN];
    char body[NOTIF_BODY_LEN];
    uint32_t time;  // timestamp
    bool read;
};

static Notification s_notifs[MAX_NOTIFICATIONS];
static int s_notif_count = 0;

// Add a new notification
void notif_add(const char* title, const char* body) {
    if (!title || !*title) return;
    
    // Shift notifications down
    for (int i = s_notif_count - 1; i > 0; i--) {
        strncpy(s_notifs[i].title, s_notifs[i-1].title, NOTIF_TITLE_LEN - 1);
        strncpy(s_notifs[i].body, s_notifs[i-1].body, NOTIF_BODY_LEN - 1);
        s_notifs[i].time = s_notifs[i-1].time;
        s_notifs[i].read = s_notifs[i-1].read;
    }
    
    // Add new notification at top
    strncpy(s_notifs[0].title, title, NOTIF_TITLE_LEN - 1);
    s_notifs[0].title[NOTIF_TITLE_LEN - 1] = 0;
    strncpy(s_notifs[0].body, body ? body : "", NOTIF_BODY_LEN - 1);
    s_notifs[0].body[NOTIF_BODY_LEN - 1] = 0;
    s_notifs[0].time = platform_tick_ms();
    s_notifs[0].read = false;
    
    if (s_notif_count < MAX_NOTIFICATIONS) s_notif_count++;
}

void notifcenter_launch() {
    Window* w = g_wm->create_window("Notification Center", 120, 80, 420, 400);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x323232);
    gfx::text(w->back, 12, 10, "Notifications", 0xFFFFFF, 0x323232);
    
    // Unread count badge
    int unread = 0;
    for (int i = 0; i < s_notif_count; i++) {
        if (!s_notifs[i].read) unread++;
    }
    if (unread > 0) {
        char badge[16];
        ksprintf(badge, 16, "(%d)", unread);
        gfx::text(w->back, w->content_w - 40, 10, badge, 0xFFD700, 0x323232);
    }
    
    // Notification list
    int list_y = 44;
    int card_h = 64;
    
    for (int i = 0; i < s_notif_count && i < 8; i++) {
        int y = list_y + i * (card_h + 4);
        
        // Card background
        uint32_t bg = s_notifs[i].read ? 0xF5F5F5 : 0xFFFFFF;
        gfx::fillrect(w->back, 6, y, w->content_w - 12, card_h, bg);
        gfx::rect(w->back, 6, y, w->content_w - 12, card_h, 0xDDDDDD);
        
        // Unread indicator
        if (!s_notifs[i].read) {
            gfx::fillrect(w->back, 8, y + 8, 4, card_h - 16, 0x4A90D9);
        }
        
        // Title
        gfx::text(w->back, 20, y + 8, s_notifs[i].title, 0x1A1B1C, bg);
        
        // Body
        gfx::text(w->back, 20, y + 28, s_notifs[i].body, 0x6B7280, bg);
        
        // Time (simplified)
        char time_str[32];
        ksprintf(time_str, 32, "%ds ago", (platform_tick_ms() - s_notifs[i].time) / 1000);
        gfx::text(w->back, w->content_w - 80, y + 8, time_str, 0x999999, bg);
        
        // Mark as read
        s_notifs[i].read = true;
    }
    
    // Empty state
    if (s_notif_count == 0) {
        gfx::text(w->back, 140, 180, "No notifications", 0x999999, 0xFFFFFF);
        gfx::text(w->back, 110, 200, "You're all caught up!", 0x999999, 0xFFFFFF);
    }
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 30, w->content_w, 30, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 22, "System notifications appear here", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
}

} // namespace nefu
