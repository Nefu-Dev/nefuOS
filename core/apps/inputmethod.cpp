// nefuOS Input Method - Character Palette
// Provides special characters, Greek letters, math symbols, etc.
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

// Character categories
struct CharCategory {
    const char* name;
    const char* chars;  // string of UTF-8 characters
};

static CharCategory categories[] = {
    { "Basic Latin", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" },
    { "Punctuation", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~" },
    { "Chinese Common", "你好谢谢再见请问现在时间今天明天我们他们这个那个什么为什么哪里怎么可以能够喜欢想要觉得知道问题回答学习工作生活娱乐游戏音乐电影书籍朋友家人电脑手机网络系统软件硬件浏览器文件文件夹图片文档音乐视频游戏设置控制面板开始菜单搜索运行注销关机重启睡眠休眠" },
    { "Chinese Numbers", "一二三四五六七八九十百千万亿零壹贰叁肆伍陆柒捌玖拾" },
    { "Chinese Days", "星期一星期二星期三星期四星期五星期六星期日月火水木金土天日春夏秋冬东南西北中" },
    { "Greek Uppercase", "ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩ" },
    { "Greek Lowercase", "αβγδεζηθικλμνξοπρστυφχψω" },
    { "Math Operators", "±×÷≈≠≤≥∞∂∆∏∑√∛∜" },
    { "Arrows", "←↑→↓↔↕↖↗↘↙⇐⇒⇑⇓" },
    { "Currency", "$€£¥₹₽₩₪₡₱₴₵" },
    { "Misc", "©®™°±µ¶§†‡•…′″€" },
};

static int current_cat = 0;
static int selected_char = -1;

static void draw_character_grid(Surface& s, int x, int y, int w, int h, const char* chars) {
    int cols = 10;
    int cell_w = w / cols;
    int cell_h = 24;
    
    int i = 0;
    for (int row = 0; row < h / cell_h; row++) {
        for (int col = 0; col < cols; col++) {
            int idx = row * cols + col;
            if (!chars[idx] || chars[idx] == 0) break;
            
            int cx = x + col * cell_w;
            int cy = y + row * cell_h;
            
            // Cell background
            if (i == selected_char) {
                gfx::fillrect(s, cx + 2, cy + 2, cell_w - 4, cell_h - 4, 0x4A90D9);
                gfx::char8x16(s, cx + 8, cy + 6, chars[idx], 0xFFFFFF, 0x4A90D9);
            } else {
                gfx::rect(s, cx + 2, cy + 2, cell_w - 4, cell_h - 4, 0xCCCCCC);
                gfx::char8x16(s, cx + 8, cy + 6, chars[idx], 0x333333, 0xFFFFFF);
            }
            i++;
        }
    }
}

void inputmethod_launch() {
    Window* w = g_wm->create_window("Input Method", 100, 100, 400, 360);
    if (!w) return;
    
    // We use a simple immediate-mode approach for this app
    // Draw category buttons
    int cat_y = 8;
    int cat_h = 24;
    
    for (int i = 0; i < 11; i++) {
        int bx = 8;
        int by = cat_y + i * (cat_h + 2);
        if (i == current_cat) {
            gfx::fillrect(w->back, bx, by, 100, cat_h, 0x4A90D9);
            gfx::text(w->back, bx + 6, by + 5, categories[i].name, 0xFFFFFF, 0x4A90D9);
        } else {
            gfx::rect(w->back, bx, by, 100, cat_h, 0xCCCCCC);
            gfx::text(w->back, bx + 6, by + 5, categories[i].name, 0x333333, 0xFFFFFF);
        }
    }
    
    // Character grid
    int grid_x = 116;
    int grid_y = 8;
    int grid_w = w->content_w - 124;
    int grid_h = w->content_h - 40;
    
    gfx::rect(w->back, grid_x, grid_y, grid_w, grid_h, 0x999999);
    draw_character_grid(w->back, grid_x, grid_y, grid_w, grid_h, categories[current_cat].chars);
    
    // Bottom hint
    gfx::fillrect(w->back, 0, w->content_h - 24, w->content_w, 24, 0xF0F0F0);
    gfx::text(w->back, 8, w->content_h - 18, "Click a character to select it.", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
    g_wm->raise(w); // Force on top
}

} // namespace nefu
