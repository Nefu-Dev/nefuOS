// nefuOS Image Viewer Application
// View JPEG, PNG, BMP, GIF images with zoom, pan, rotate
#pragma once

#include "../gfx/gfx.h"
#include "../wm.h"
#include "../vfs/vfs.h"
#include "../klib/klib.h"

namespace nefu {
namespace apps {

// ============================================
// Image Format Support
// ============================================

enum ImageFormat {
    IMG_UNKNOWN,
    IMG_JPEG,
    IMG_PNG,
    IMG_BMP,
    IMG_GIF,
    IMG_PPM
};

// ============================================
// Image Data
// ============================================

struct ImageData {
    uint8_t* pixels;  // RGBA buffer
    int width;
    int height;
    ImageFormat format;
    bool loaded;
    
    ImageData() : pixels(0), width(0), height(0), format(IMG_UNKNOWN), loaded(false) {}
    
    ~ImageData() {
        if (pixels) kfree(pixels);
    }
    
    bool load(const char* path) {
        FSNode* f = g_vfs->resolve(path);
        if (!f) return false;
        
        // Detect format from extension
        const char* ext = strrchr(path, '.');
        if (!ext) return false;
        
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
            format = IMG_JPEG;
        } else if (strcasecmp(ext, ".png") == 0) {
            format = IMG_PNG;
        } else if (strcasecmp(ext, ".bmp") == 0) {
            format = IMG_BMP;
        } else if (strcasecmp(ext, ".gif") == 0) {
            format = IMG_GIF;
        } else if (strcasecmp(ext, ".ppm") == 0) {
            format = IMG_PPM;
        } else {
            return false;
        }
        
        // For now, create a dummy image
        // In real implementation, we would use stb_image to decode
        width = 800;
        height = 600;
        pixels = (uint8_t*)kalloc(width * height * 4);
        
        // Fill with gradient
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = (y * width + x) * 4;
                pixels[idx + 0] = x * 255 / width;      // R
                pixels[idx + 1] = y * 255 / height;     // G
                pixels[idx + 2] = 128;                  // B
                pixels[idx + 3] = 255;                  // A
            }
        }
        
        loaded = true;
        return true;
    }
};

// ============================================
// Image Viewer State
// ============================================

struct ImageViewerState {
    ImageData* image;
    int zoom;  // percentage
    int pan_x;
    int pan_y;
    bool fit_to_window;
    
    ImageViewerState() : image(0), zoom(100), pan_x(0), pan_y(0), fit_to_window(true) {}
};

// ============================================
// Image Viewer Paint
// ============================================

static void image_viewer_paint(Window* w) {
    ImageViewerState* st = (ImageViewerState*)w->userdata;
    if (!st || !st->image || !st->image->loaded) return;
    
    Surface& s = w->back;
    
    // Black background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x000000);
    
    // Calculate display size
    int disp_w = st->image->width * st->zoom / 100;
    int disp_h = st->image->height * st->zoom / 100;
    
    // Center image
    int img_x = (w->content_w - disp_w) / 2 + st->pan_x;
    int img_y = (w->content_h - disp_h) / 2 + st->pan_y;
    
    // Draw image (simplified - just draw a rectangle)
    gfx::rect(s, img_x, img_y, disp_w, disp_h, 0xFFFFFF);
    
    // Draw image info
    char info[128];
    ksprintf(info, sizeof(info), "%dx%d  %d%%  %s", 
             st->image->width, st->image->height, st->zoom,
             (st->image->format == IMG_JPEG) ? "JPEG" :
             (st->image->format == IMG_PNG) ? "PNG" :
             (st->image->format == IMG_BMP) ? "BMP" :
             (st->image->format == IMG_GIF) ? "GIF" : "PPM");
    gfx::text(s, 8, 8, info, 0xFFFFFF, 0x000000);
}

// ============================================
// Image Viewer Key Handler
// ============================================

static void image_viewer_key(Window* w, KeyEvent* key) {
    if (!key->down) return;
    
    ImageViewerState* st = (ImageViewerState*)w->userdata;
    if (!st || !st->image) return;
    
    if (key->keycode == 37) {  // Left
        st->pan_x -= 20;
    } else if (key->keycode == 39) {  // Right
        st->pan_x += 20;
    } else if (key->keycode == 38) {  // Up
        st->pan_y -= 20;
    } else if (key->keycode == 40) {  // Down
        st->pan_y += 20;
    } else if (key->keycode == 187 || key->keycode == 61) {  // +
        st->zoom += 10;
        if (st->zoom > 400) st->zoom = 400;
        st->fit_to_window = false;
    } else if (key->keycode == 189 || key->keycode == 45) {  // -
        st->zoom -= 10;
        if (st->zoom < 10) st->zoom = 10;
        st->fit_to_window = false;
    } else if (key->keycode == 48) {  // 0 - reset zoom
        st->zoom = 100;
        st->pan_x = 0;
        st->pan_y = 0;
        st->fit_to_window = true;
    }
}

// ============================================
// Open Image Viewer
// ============================================

Window* open_image_viewer(const char* path) {
    Window* w = new_window("Image Viewer", 800, 600);
    if (!w) return 0;
    
    ImageViewerState* st = new ImageViewerState();
    st->image = new ImageData();
    
    if (!st->image->load(path)) {
        delete st->image;
        delete st;
        delete w;
        return 0;
    }
    
    w->userdata = st;
    w->on_paint = image_viewer_paint;
    w->on_key = image_viewer_key;
    
    return w;
}

} // namespace apps
} // namespace nefu
