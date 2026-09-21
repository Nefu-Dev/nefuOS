// nefuOS download manager
// - Tracks all downloads (active, completed, failed)
// - Shows download list UI
// - Saves downloads to /usr/downloads
// SPDX-License-Identifier: MIT

#include "../vfs/vfs.h"
#include "../platform.h"
#include "../klib/klib.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include <cstring>

namespace nefu {

// Download status
enum DownloadStatus {
    DL_PENDING = 0,
    DL_ACTIVE,
    DL_COMPLETE,
    DL_FAILED,
    DL_PAUSED
};

// Download item
struct DownloadItem {
    String url;
    String filename;
    String local_path;
    uint32_t total_bytes;
    uint32_t received_bytes;
    DownloadStatus status;
    void* thread;
    volatile bool done;
    volatile bool error;
    uint8_t* data;
    
    DownloadItem() : total_bytes(0), received_bytes(0), status(DL_PENDING), thread(0), done(false), error(false), data(0) {}
};

// Download manager (use simple array to avoid static constructor issues on bare metal)
static const int MAX_DOWNLOADS = 32;
static DownloadItem* g_downloads_arr[MAX_DOWNLOADS];
static int g_downloads_count = 0;

// Get download count
int download_count() {
    return g_downloads_count;
}

// Get download by index
DownloadItem* download_get(int idx) {
    if (idx < 0 || idx >= g_downloads_count) return 0;
    return g_downloads_arr[idx];
}

// Add a new download
DownloadItem* download_add(const char* url, const char* filename) {
    DownloadItem* item = new DownloadItem();
    item->url = url;
    
    // Extract filename from URL if not provided
    if (!filename || !*filename) {
        const char* last_slash = strrchr(url, '/');
        if (last_slash && *(last_slash + 1)) {
            item->filename = last_slash + 1;
        } else {
            item->filename = "download.bin";
        }
    } else {
        item->filename = filename;
    }
    
    // Build local path
    g_vfs->mkdir("/usr/downloads");
    item->local_path = "/usr/downloads/";
    item->local_path += item->filename;
    
    if (g_downloads_count < MAX_DOWNLOADS) {
        g_downloads_arr[g_downloads_count++] = item;
    }
    return item;
}

// Start download thread
struct DlThreadCtx {
    DownloadItem* item;
};

static void download_thread_fn(void* arg) {
    DlThreadCtx* ctx = (DlThreadCtx*)arg;
    DownloadItem* item = ctx->item;
    
    item->status = DL_ACTIVE;
    uint8_t* body = 0;
    uint32_t body_len = 0;
    
    if (platform_http_get(item->url.c_str(), &body, &body_len) && body) {
        item->data = body;
        item->received_bytes = body_len;
        item->total_bytes = body_len;
        item->status = DL_COMPLETE;
        
        // Save to VFS
        FSNode* f = g_vfs->create_file(item->local_path.c_str());
        if (f) {
            g_vfs->write_file(f, body, body_len);
        }
    } else {
        item->status = DL_FAILED;
        item->error = true;
    }
    
    item->done = true;
    delete ctx;
}

void download_start(DownloadItem* item) {
    if (!item || item->status == DL_ACTIVE) return;
    
    DlThreadCtx* ctx = new DlThreadCtx();
    ctx->item = item;
    
    item->status = DL_ACTIVE;
    item->done = false;
    item->error = false;
    item->thread = platform_thread_create(download_thread_fn, ctx);
}

// Cancel download
void download_cancel(DownloadItem* item) {
    if (!item) return;
    item->status = DL_FAILED;
    item->done = true;
    item->error = true;
}

// Clear completed downloads
void download_clear_completed() {
    for (int i = 0; i < g_downloads_count; i++) {
        DownloadItem* item = g_downloads_arr[i];
        if (item && (item->status == DL_COMPLETE || item->status == DL_FAILED)) {
            if (item->data) {
                kfree(item->data);
                item->data = 0;
            }
        }
    }
}

// =====================================================================
// Download list window
// =====================================================================
struct DlListState {
    Window* w;
    int scroll;
    uint8_t last_buttons;
};

static void dl_list_paint(Window* w) {
    DlListState* st = (DlListState*)w->userdata;
    Surface& s = w->back;
    
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, color::WHITE);
    
    // Title bar area
    gfx::fillrect(s, 0, 0, w->content_w, 24, color::PANEL);
    gfx::text(s, 8, 4, "Downloads", color::TEXT, color::PANEL);
    
    int y = 30;
    int line_h = 40;
    
    for (int i = st->scroll; i < g_downloads_count && y < w->content_h - 40; i++) {
        DownloadItem* item = g_downloads_arr[i];
        
        // Status color
        uint32_t bg = color::WHITE;
        if (item->status == DL_ACTIVE) bg = 0xFFE8F5E9;
        else if (item->status == DL_COMPLETE) bg = 0xFFE3F2FD;
        else if (item->status == DL_FAILED) bg = 0xFFFFEBEE;
        
        gfx::fillrect(s, 4, y, w->content_w - 8, line_h - 4, bg);
        gfx::rect(s, 4, y, w->content_w - 8, line_h - 4, color::BORDER);
        
        // Filename
        gfx::text(s, 12, y + 4, item->filename.c_str(), color::TEXT, bg);
        
        // URL (truncated)
        char url_buf[60];
        ksprintf(url_buf, sizeof(url_buf), "%s", item->url.c_str());
        if (strlen(url_buf) > 50) {
            url_buf[47] = '.'; url_buf[48] = '.'; url_buf[49] = '.'; url_buf[50] = 0;
        }
        gfx::text(s, 12, y + 18, url_buf, color::TEXT2, bg);
        
        // Status and progress
        char st_buf[64];
        const char* st_text = "";
        switch (item->status) {
            case DL_ACTIVE: st_text = "Downloading..."; break;
            case DL_COMPLETE: st_text = "Complete"; break;
            case DL_FAILED: st_text = "Failed"; break;
            case DL_PAUSED: st_text = "Paused"; break;
            default: st_text = "Pending"; break;
        }
        
        if (item->total_bytes > 0) {
            ksprintf(st_buf, sizeof(st_buf), "%s (%u/%u KB)", st_text, 
                     item->received_bytes / 1024, item->total_bytes / 1024);
        } else {
            ksprintf(st_buf, sizeof(st_buf), "%s", st_text);
        }
        gfx::text(s, w->content_w - 160, y + 14, st_buf, color::TEXT2, bg);
        
        // Progress bar
        if (item->status == DL_ACTIVE && item->total_bytes > 0) {
            int bar_w = 140;
            int filled = bar_w * item->received_bytes / item->total_bytes;
            gfx::fillrect(s, w->content_w - 160, y + 28, bar_w, 6, 0xFFE0E0E0);
            gfx::fillrect(s, w->content_w - 160, y + 28, filled, 6, 0xFF2196F3);
        }
        
        y += line_h;
    }
    
    // Bottom status
    gfx::fillrect(s, 0, w->content_h - 24, w->content_w, 24, color::PANEL);
    char cnt[64];
    ksprintf(cnt, sizeof(cnt), "%d download(s)", g_downloads_count);
    gfx::text(s, 8, w->content_h - 20, cnt, color::TEXT2, color::PANEL);
}

static void dl_list_close(Window* w) {
    DlListState* st = (DlListState*)w->userdata;
    delete st;
    w->userdata = 0;
}

// Launch download list window
void download_list_launch() {
    Window* w = g_wm->create_window("Downloads", 100, 80, 480, 360);
    if (!w) return;
    
    DlListState* st = new DlListState();
    st->w = w;
    st->scroll = 0;
    st->last_buttons = 0;
    
    w->userdata = st;
    w->on_paint = dl_list_paint;
    w->on_close = dl_list_close;
    
    g_wm->raise(w);
}

} // namespace nefu
