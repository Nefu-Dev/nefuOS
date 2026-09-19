// temporary fontview crash repro v6 - step printf
#include "../../core/gui/gfx.h"
#include "../../core/gui/ttfont.h"
#include "../../core/klib/klib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace nefu;
static int step = 0;
int main() {
    bool ok = ttf_init();
    printf("step %d ttf_init=%d\n", ++step, ok); fflush(stdout);
    if (!ok) return 1;
    int w = 402, h = 302;
    uint8_t* buf = (uint8_t*)malloc((size_t)w * h * 4);
    printf("step %d buf=%p\n", ++step, buf); fflush(stdout);
    Surface s;
    s.addr = buf; s.width = w; s.height = h; s.pitch = w * 4;
    s.fill(0xFFFFFFFF);
    int y = 40;
    char sbuf[160];
    printf("step %d draw t1\n", ++step); fflush(stdout);
    ttf_draw_text(&s, 14, y, "Font    : VT323 (SIL OFL 1.1, Google Fonts)", 16, 0xFF808088, 0xFFFFFFFF); y += 16;
    printf("step %d draw t2\n", ++step); fflush(stdout);
    ttf_draw_text(&s, 14, y, "Engine  : stb_truetype (MIT, nothings/stb)", 16, 0xFF808088, 0xFFFFFFFF); y += 16;
    printf("step %d draw t3\n", ++step); fflush(stdout);
    ttf_draw_text(&s, 14, y, "Render  : vector outlines, anti-aliased", 16, 0xFF808088, 0xFFFFFFFF); y += 24;
    printf("step %d samples\n", ++step); fflush(stdout);
    static const int sizes[] = { 12, 16, 24, 32, 48 };
    for (int i = 0; i < 5; i++) {
        ksprintf(sbuf, sizeof(sbuf), "Sample %dpx - nefuOS", sizes[i]);
        ttf_draw_text(&s, 14, y, sbuf, sizes[i], 0xFF101018, 0xFFFFFFFF);
        y += sizes[i] + 10;
        if (y > h + 0) break;
    }
    printf("step %d gallery\n", ++step); fflush(stdout);
    y += 6;
    ttf_draw_text(&s, 14, y, "ASCII gallery (24px):", 16, 0xFF808088, 0xFFFFFFFF); y += 26;
    char row[64];
    int idx = 0;
    for (int c = 32; c <= 126; c++) {
        row[idx++] = (char)c;
        if (idx == 32) {
            row[idx] = 0;
            ttf_draw_text(&s, 14, y, row, 24, 0xFF101018, 0xFFFFFFFF);
            y += 26;
            idx = 0;
        }
    }
    if (idx > 0) {
        row[idx] = 0;
        ttf_draw_text(&s, 14, y, row, 24, 0xFF101018, 0xFFFFFFFF);
    }
    printf("step %d done\n", ++step); fflush(stdout);
    free(buf);
    return 0;
}
