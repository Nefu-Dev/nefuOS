// nefuOS Graphics System - Full Implementation
#pragma once

#include "../klib/klib.h"
#include "gfx.h"

namespace nefu {
namespace gfx {

// Graphics modes
enum GraphicsMode {
    GRAPHICS_MODE_TEXT,
    GRAPHICS_MODE_320x200x256,
    GRAPHICS_MODE_640x480x256,
    GRAPHICS_MODE_800x600x256,
    GRAPHICS_MODE_1024x768x256,
    GRAPHICS_MODE_800x600x16,
    GRAPHICS_MODE_800x600x32
};

// Color formats
enum ColorFormat {
    COLOR_FORMAT_RGB888,
    COLOR_FORMAT_RGB565,
    COLOR_FORMAT_RGB332,
    COLOR_FORMAT_RGBA8888
};

// Graphics context
struct GraphicsContext {
    Surface* surface;
    int width;
    int height;
    ColorFormat format;
    uint32_t foreground;
    uint32_t background;
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
    bool clip_enabled;
};

// Font
struct Font {
    const uint8_t* data;
    int width;
    int height;
    int first_char;
    int char_count;
    bool monospace;
};

// Graphics Manager
class GraphicsManager {
private:
    GraphicsContext current_context;
    Font fonts[8];
    int font_count;
    GraphicsMode current_mode;
    
public:
    GraphicsManager() : font_count(0), current_mode(GRAPHICS_MODE_800x600x32) {
        memset(&current_context, 0, sizeof(current_context));
    }
    
    // Initialize graphics
    bool init(GraphicsMode mode) {
        current_mode = mode;
        
        // Set graphics mode
        switch (mode) {
            case GRAPHICS_MODE_320x200x256:
                current_context.width = 320;
                current_context.height = 200;
                current_context.format = COLOR_FORMAT_RGB332;
                break;
            case GRAPHICS_MODE_640x480x256:
                current_context.width = 640;
                current_context.height = 480;
                current_context.format = COLOR_FORMAT_RGB332;
                break;
            case GRAPHICS_MODE_800x600x256:
                current_context.width = 800;
                current_context.height = 600;
                current_context.format = COLOR_FORMAT_RGB332;
                break;
            case GRAPHICS_MODE_1024x768x256:
                current_context.width = 1024;
                current_context.height = 768;
                current_context.format = COLOR_FORMAT_RGB332;
                break;
            case GRAPHICS_MODE_800x600x16:
                current_context.width = 800;
                current_context.height = 600;
                current_context.format = COLOR_FORMAT_RGB565;
                break;
            case GRAPHICS_MODE_800x600x32:
                current_context.width = 800;
                current_context.height = 600;
                current_context.format = COLOR_FORMAT_RGB888;
                break;
        }
        
        // Set default colors
        current_context.foreground = 0xFFFFFF;
        current_context.background = 0x000000;
        
        return true;
    }
    
    // Shutdown graphics
    void shutdown() {
        // Restore text mode
    }
    
    // Get current mode
    GraphicsMode get_mode() const { return current_mode; }
    
    // Get screen width
    int get_width() const { return current_context.width; }
    
    // Get screen height
    int get_height() const { return current_context.height; }
    
    // Set foreground color
    void set_foreground(uint32_t color) {
        current_context.foreground = color;
    }
    
    // Set background color
    void set_background(uint32_t color) {
        current_context.background = color;
    }
    
    // Set clip rectangle
    void set_clip(int x, int y, int w, int h) {
        current_context.clip_x = x;
        current_context.clip_y = y;
        current_context.clip_w = w;
        current_context.clip_h = h;
        current_context.clip_enabled = true;
    }
    
    // Disable clip
    void disable_clip() {
        current_context.clip_enabled = false;
    }
    
    // Draw pixel
    void put_pixel(int x, int y, uint32_t color) {
        if (current_context.clip_enabled) {
            if (x < current_context.clip_x || x >= current_context.clip_x + current_context.clip_w ||
                y < current_context.clip_y || y >= current_context.clip_y + current_context.clip_h) {
                return;
            }
        }
        
        if (current_context.surface) {
            current_context.surface->setpx(x, y, color);
        }
    }
    
    // Get pixel
    uint32_t get_pixel(int x, int y) {
        if (current_context.surface) {
            return current_context.surface->getpx(x, y);
        }
        return 0;
    }
    
    // Draw line
    void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
        int dx = x1 > x0 ? x1 - x0 : x0 - x1;
        int dy = y1 > y0 ? y1 - y0 : y0 - y1;
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        
        while (true) {
            put_pixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
    
    // Draw rectangle outline
    void draw_rect(int x, int y, int w, int h, uint32_t color) {
        draw_line(x, y, x + w - 1, y, color);
        draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
        draw_line(x, y, x, y + h - 1, color);
        draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    }
    
    // Draw filled rectangle
    void fill_rect(int x, int y, int w, int h, uint32_t color) {
        for (int dy = 0; dy < h; dy++) {
            for (int dx = 0; dx < w; dx++) {
                put_pixel(x + dx, y + dy, color);
            }
        }
    }
    
    // Draw circle outline
    void draw_circle(int cx, int cy, int radius, uint32_t color) {
        int x = radius;
        int y = 0;
        int err = 0;
        
        while (x >= y) {
            put_pixel(cx + x, cy + y, color);
            put_pixel(cx + y, cy + x, color);
            put_pixel(cx - y, cy + x, color);
            put_pixel(cx - x, cy + y, color);
            put_pixel(cx - x, cy - y, color);
            put_pixel(cx - y, cy - x, color);
            put_pixel(cx + y, cy - x, color);
            put_pixel(cx + x, cy - y, color);
            
            if (err <= 0) {
                y++;
                err += 2 * y + 1;
            }
            if (err > 0) {
                x--;
                err -= 2 * x + 1;
            }
        }
    }
    
    // Draw filled circle
    void fill_circle(int cx, int cy, int radius, uint32_t color) {
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                if (x * x + y * y <= radius * radius) {
                    put_pixel(cx + x, cy + y, color);
                }
            }
        }
    }
    
    // Draw ellipse outline
    void draw_ellipse(int cx, int cy, int rx, int ry, uint32_t color) {
        int x = 0;
        int y = ry;
        int rx2 = rx * rx;
        int ry2 = ry * ry;
        int err = ry2 - (2 * ry - 1) * rx2;
        
        while (x <= rx) {
            put_pixel(cx + x, cy + y, color);
            put_pixel(cx - x, cy + y, color);
            put_pixel(cx + x, cy - y, color);
            put_pixel(cx - x, cy - y, color);
            
            int e2 = 2 * err;
            if (e2 <= (2 * y - 1) * rx2) {
                y--;
                err -= (2 * y - 1) * rx2;
            }
            if (e2 >= -(2 * x + 1) * ry2) {
                x++;
                err += (2 * x + 1) * ry2;
            }
        }
    }
    
    // Draw triangle outline
    void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
        draw_line(x0, y0, x1, y1, color);
        draw_line(x1, y1, x2, y2, color);
        draw_line(x2, y2, x0, y0, color);
    }
    
    // Draw filled triangle
    void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
        // Barycentric triangle fill
        int min_y = y0;
        if (y1 < min_y) min_y = y1;
        if (y2 < min_y) min_y = y2;
        
        int max_y = y0;
        if (y1 > max_y) max_y = y1;
        if (y2 > max_y) max_y = y2;
        
        for (int y = min_y; y <= max_y; y++) {
            int xs[32];
            int count = 0;
            
            // Find intersections with edges
            int edges[3][4] = {
                {x0, y0, x1, y1},
                {x1, y1, x2, y2},
                {x2, y2, x0, y0}
            };
            
            for (int i = 0; i < 3; i++) {
                int ex0 = edges[i][0], ey0 = edges[i][1];
                int ex1 = edges[i][2], ey1 = edges[i][3];
                
                if (y < ey0 || y > ey1) continue;
                if (ey0 == ey1) continue;
                
                int x = ex0 + (y - ey0) * (ex1 - ex0) / (ey1 - ey0);
                xs[count++] = x;
            }
            
            // Sort and draw horizontal lines
            for (int i = 0; i < count - 1; i++) {
                for (int j = i + 1; j < count; j++) {
                    if (xs[i] > xs[j]) {
                        int tmp = xs[i];
                        xs[i] = xs[j];
                        xs[j] = tmp;
                    }
                }
            }
            
            for (int i = 0; i < count - 1; i += 2) {
                for (int x = xs[i]; x <= xs[i + 1]; x++) {
                    put_pixel(x, y, color);
                }
            }
        }
    }
    
    // Draw rounded rectangle
    void draw_round_rect(int x, int y, int w, int h, int radius, uint32_t color) {
        draw_line(x + radius, y, x + w - radius - 1, y, color);
        draw_line(x + radius, y + h - 1, x + w - radius - 1, y + h - 1, color);
        draw_line(x, y + radius, x, y + h - radius - 1, color);
        draw_line(x + w - 1, y + radius, x + w - 1, y + h - radius - 1, color);
        
        // Corners
        draw_ellipse(x + radius, y + radius, radius, radius, color);
        draw_ellipse(x + w - radius - 1, y + radius, radius, radius, color);
        draw_ellipse(x + radius, y + h - radius - 1, radius, radius, color);
        draw_ellipse(x + w - radius - 1, y + h - radius - 1, radius, radius, color);
    }
    
    // Draw filled rounded rectangle
    void fill_round_rect(int x, int y, int w, int h, int radius, uint32_t color) {
        fill_rect(x, y + radius, w, h - 2 * radius, color);
        fill_rect(x + radius, y, w - 2 * radius, radius, color);
        fill_rect(x + radius, y + h - radius, w - 2 * radius, radius, color);
        
        // Corners
        for (int dy = -radius; dy <= 0; dy++) {
            for (int dx = -radius; dx <= 0; dx++) {
                if (dx * dx + dy * dy <= radius * radius) {
                    put_pixel(x + radius + dx, y + radius + dy, color);
                    put_pixel(x + w - radius - 1 + dx, y + radius + dy, color);
                    put_pixel(x + radius + dx, y + h - radius - 1 + dy, color);
                    put_pixel(x + w - radius - 1 + dx, y + h - radius - 1 + dy, color);
                }
            }
        }
    }
    
    // Draw text
    void draw_text(int x, int y, const char* text, uint32_t color, Font* font) {
        if (!font) return;
        
        int cx = x;
        int cy = y;
        
        while (*text) {
            int ch = (unsigned char)*text;
            if (ch == '\n') {
                cy += font->height;
                cx = x;
            } else if (ch >= font->first_char && ch < font->first_char + font->char_count) {
                int idx = ch - font->first_char;
                const uint8_t* glyph = font->data + idx * font->height;
                
                for (int row = 0; row < font->height; row++) {
                    for (int col = 0; col < font->width; col++) {
                        if (glyph[row] & (0x80 >> col)) {
                            put_pixel(cx + col, cy + row, color);
                        }
                    }
                }
                
                cx += font->width;
                if (!font->monospace) {
                    // Variable width font
                }
            }
            text++;
        }
    }
    
    // Draw surface
    void draw_surface(int x, int y, Surface* src) {
        for (int dy = 0; dy < src->height; dy++) {
            for (int dx = 0; dx < src->width; dx++) {
                uint32_t color = src->getpx(dx, dy);
                put_pixel(x + dx, y + dy, color);
            }
        }
    }
    
    // Draw surface with transparency
    void draw_surface_alpha(int x, int y, Surface* src, uint8_t alpha) {
        for (int dy = 0; dy < src->height; dy++) {
            for (int dx = 0; dx < src->width; dx++) {
                uint32_t src_color = src->getpx(dx, dy);
                uint32_t dst_color = get_pixel(x + dx, y + dy);
                
                int sr = (src_color >> 16) & 0xFF;
                int sg = (src_color >> 8) & 0xFF;
                int sb = src_color & 0xFF;
                
                int dr = (dst_color >> 16) & 0xFF;
                int dg = (dst_color >> 8) & 0xFF;
                int db = dst_color & 0xFF;
                
                int r = (sr * alpha + dr * (255 - alpha)) / 255;
                int g = (sg * alpha + dg * (255 - alpha)) / 255;
                int b = (sb * alpha + db * (255 - alpha)) / 255;
                
                put_pixel(x + dx, y + dy, (r << 16) | (g << 8) | b);
            }
        }
    }
    
    // Clear screen
    void clear(uint32_t color) {
        fill_rect(0, 0, current_context.width, current_context.height, color);
    }
    
    // Scroll screen
    void scroll(int dx, int dy, uint32_t fill_color) {
        if (dy > 0) {
            // Scroll up
            for (int y = 0; y < current_context.height - dy; y++) {
                for (int x = 0; x < current_context.width; x++) {
                    uint32_t color = get_pixel(x, y + dy);
                    put_pixel(x, y, color);
                }
            }
            // Fill bottom
            fill_rect(0, current_context.height - dy, current_context.width, dy, fill_color);
        } else if (dy < 0) {
            // Scroll down
            for (int y = current_context.height - 1; y >= -dy; y--) {
                for (int x = 0; x < current_context.width; x++) {
                    uint32_t color = get_pixel(x, y + dy);
                    put_pixel(x, y, color);
                }
            }
            // Fill top
            fill_rect(0, 0, current_context.width, -dy, fill_color);
        }
    }
    
    // Register font
    int register_font(const Font& font) {
        if (font_count >= 8) return -1;
        fonts[font_count++] = font;
        return font_count - 1;
    }
    
    // Get font
    Font* get_font(int idx) {
        if (idx < 0 || idx >= font_count) return 0;
        return &fonts[idx];
    }
};

// Global graphics manager
GraphicsManager g_graphics;

} // namespace gfx
} // namespace nefu
