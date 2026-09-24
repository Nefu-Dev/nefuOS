// nefuOS 压缩工具 —— 窗口应用
//
// 功能：
//   选择算法 (RLE / Huffman / LZW / LZ77 / BWT+MTF / Arithmetic)
//   对内置示例数据进行压缩 / 解压，显示原始大小、压缩后大小、压缩率。
//   1..6 选算法，C 压缩，D 解压，R 换示例，Esc 关闭。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../compress/compress_all.h"

namespace nefu {

namespace {

const int TOOL_W = 560, TOOL_H = 380;
const int MAXDAT = 8192;

const char* ALGO_NAMES[6] = {
    "RLE", "Huffman", "LZW", "LZ77", "BWT+MTF", "Arithmetic"
};

struct CompressTool {
    uint8_t src[MAXDAT];
    uint8_t dst[MAXDAT * 2];
    uint8_t back[MAXDAT];
    int srclen;
    int comp_len;      // 压缩后长度
    int decomp_len;
    int algo;          // 0..5
    int phase;         // 0=未压缩 1=已压缩 2=已解压
    bool ok;

    void make_sample() {
        // 构造一段有冗余的文本 + 少量随机，便于演示压缩
        const char* t = "the quick brown fox jumps over the lazy dog. "
                        "nefuOS is a tiny teaching operating system. ";
        int tl = 82;
        int k = 0;
        for (int i = 0; i < MAXDAT; i++) src[i] = (uint8_t)t[k++ % tl];
        srclen = MAXDAT;
        comp_len = 0; decomp_len = 0; phase = 0; ok = true;
    }

    int run_compress() {
        switch (algo) {
        case 0: return rle_byte_encode(src, srclen, dst, sizeof(dst));
        case 1: return huffman_static_encode(src, srclen, dst, sizeof(dst));
        case 2: return lzw_encode(src, srclen, dst, sizeof(dst));
        case 3: return lz77_encode(src, srclen, dst, sizeof(dst));
        case 4: return bwt_encode(src, srclen, dst, sizeof(dst));
        case 5: return arith_encode(src, srclen, dst, sizeof(dst));
        }
        return -1;
    }
    int run_decompress() {
        switch (algo) {
        case 0: return rle_byte_decode(dst, comp_len, back, sizeof(back));
        case 1: return huffman_static_decode(dst, comp_len, back, sizeof(back));
        case 2: return lzw_decode(dst, comp_len, back, sizeof(back));
        case 3: return lz77_decode(dst, comp_len, back, sizeof(back));
        case 4: return bwt_decode(dst, comp_len, back, sizeof(back));
        case 5: return arith_decode(dst, comp_len, back, sizeof(back));
        }
        return -1;
    }

    void compress() {
        comp_len = run_compress();
        phase = 1; ok = (comp_len > 0);
    }
    void decompress() {
        decomp_len = run_decompress();
        phase = 2;
        ok = (decomp_len == srclen);
    }

    void paint(Surface& s) {
        s.fill(0x00FAF8EF);
        int W = s.width;
        gfx::text_scale(s, 10, 8, "Compression Tool", 0x00776756, 0x00FAF8EF, 2);
        char buf[128];
        ksprintf(buf, sizeof(buf), "Algorithm [1-6]: %s", ALGO_NAMES[algo]);
        gfx::text(s, 10, 44, buf, 0x00505050, 0x00FAF8EF);
        ksprintf(buf, sizeof(buf), "Original size : %d bytes", srclen);
        gfx::text(s, 10, 74, buf, 0x00505050, 0x00FAF8EF);
        ksprintf(buf, sizeof(buf), "Compressed    : %d bytes", comp_len);
        gfx::text(s, 10, 96, buf, 0x00505050, 0x00FAF8EF);
        ksprintf(buf, sizeof(buf), "Decompressed  : %d bytes", decomp_len);
        gfx::text(s, 10, 118, buf, 0x00505050, 0x00FAF8EF);
        if (comp_len > 0 && srclen > 0) {
            int pct = (int)((long long)comp_len * 100 / srclen);
            ksprintf(buf, sizeof(buf), "Ratio: %d%%  (%.2fx)", pct,
                     (double)srclen / comp_len);
            gfx::text(s, 10, 148, buf, 0x0027AE60, 0x00FAF8EF);
        }
        if (phase == 2) {
            gfx::text(s, 10, 180, ok ? "Round-trip OK!" : "Round-trip MISMATCH",
                      ok ? 0x0027AE60 : 0x00E74C3C, 0x00FAF8EF);
        }
        gfx::text(s, 10, H2() - 40,
                  "C: compress   D: decompress   R: new sample   Esc: close",
                  0x00909090, 0x00FAF8EF);
    }
    int H2() { return 380; }
};

} // namespace

static CompressTool* tool_of(Window* w) { return (CompressTool*)w->userdata; }
static void tool_paint(Window* w) { tool_of(w)->paint(w->back); }
static void tool_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    CompressTool* t = tool_of(w);
    if (e->ascii >= '1' && e->ascii <= '6') { t->algo = e->ascii - '1'; return; }
    if (e->ascii == 'c' || e->ascii == 'C') { t->compress(); return; }
    if (e->ascii == 'd' || e->ascii == 'D') { t->decompress(); return; }
    if (e->ascii == 'r' || e->ascii == 'R') { t->make_sample(); return; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}
static void tool_close(Window* w) {
    if (w->userdata) delete (CompressTool*)w->userdata;
    w->userdata = 0;
}

void compresstool_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Compression Tool", x, y, TOOL_W, TOOL_H);
    if (!w) return;
    CompressTool* t = new CompressTool();
    t->algo = 0;
    t->make_sample();
    w->userdata = t;
    w->on_paint = tool_paint;
    w->on_key = tool_key;
    w->on_close = tool_close;
    g_wm->raise(w);
}

} // namespace nefu
