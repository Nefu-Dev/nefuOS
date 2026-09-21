// nefuOS built-in dictionary app
// Simple English-Chinese dictionary
#include "apps.h"
#include <cstdio>
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct DictEntry {
    const char* en;
    const char* zh;
};

// Small built-in dictionary
static DictEntry g_dict[] = {
    {"hello", "你好"},
    {"world", "世界"},
    {"computer", "电脑"},
    {"system", "系统"},
    {"file", "文件"},
    {"directory", "目录"},
    {"window", "窗口"},
    {"desktop", "桌面"},
    {"keyboard", "键盘"},
    {"mouse", "鼠标"},
    {"network", "网络"},
    {"browser", "浏览器"},
    {"software", "软件"},
    {"hardware", "硬件"},
    {"memory", "内存"},
    {"processor", "处理器"},
    {"display", "显示器"},
    {"text", "文本"},
    {"image", "图片"},
    {"audio", "音频"},
    {"video", "视频"},
    {"program", "程序"},
    {"code", "代码"},
    {"function", "函数"},
    {"variable", "变量"},
    {"class", "类"},
    {"object", "对象"},
    {"pointer", "指针"},
    {"array", "数组"},
};
static const int g_dict_count = sizeof(g_dict) / sizeof(g_dict[0]);

struct DictState {
    int w, h;
    int scroll;
};

static void dict_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    int y = 30;
    gfx::text(s, 15, y, "=== Dictionary ===", 0x89b4fa, 0);
    y += 30;

    // Show first 12 entries
    int show = g_dict_count < 12 ? g_dict_count : 12;
    for (int i = 0; i < show; i++) {
        char line[64];
        ksprintf(line, sizeof(line), "%-15s = %s", g_dict[i].en, g_dict[i].zh);
        gfx::text(s, 15, y, line, 0xcdd6f4, 0);
        y += 22;
    }

    y += 10;
    gfx::text(s, 15, y, "Built-in dictionary (30 words)", 0x6c7086, 0);
}

void app_dictionary_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Dictionary", x, y, 320, 350);
    if (!w) return;
    DictState* st = new DictState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->scroll = 0;
    w->userdata = st;
    w->on_paint = dict_paint;
    g_wm->raise(w);
}

} // namespace nefu
