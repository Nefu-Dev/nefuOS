// 字库预览工具：把 font.h 渲染成大图 BMP，用于检查字模是否正确
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "../core/gui/font.h"

#pragma pack(push,1)
struct BMPHDR {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfRes1, bfRes2;
    uint32_t bfOffBits;
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter, biYPelsPerMeter;
    uint32_t biClrUsed, biClrImportant;
};
#pragma pack(pop)

int main() {
    const int SCALE = 4, COLS = 16, ROWS = 6;
    const int cellW = 8 * SCALE + 4, cellH = 16 * SCALE + 6;
    const int W = COLS * cellW + 8, H = ROWS * cellH + 8;
    uint32_t* px = new uint32_t[(size_t)W * H];
    memset(px, 0, (size_t)W * H * 4);
    // 白底黑字
    for (int i = 0; i < W * H; i++) px[i] = 0x00FFFFFF;
    for (int c = 0; c < 96; c++) {
        int gx = 4 + (c % COLS) * cellW;
        int gy = 4 + (c / COLS) * cellH;
        for (int y = 0; y < 16; y++) {
            unsigned char b = nefu::font8x16[c][y];
            for (int x = 0; x < 8; x++) {
                if (b & (0x80 >> x)) {
                    for (int sy = 0; sy < SCALE; sy++)
                        for (int sx = 0; sx < SCALE; sx++)
                            px[(size_t)(gy + y * SCALE + sy) * W + (gx + x * SCALE + sx)] = 0x00000000;
                }
            }
        }
    }
    // 写 BMP（自下而上）
    int rowSize = ((W * 4 + 3) / 4) * 4;
    int imgSize = rowSize * H;
    FILE* f = fopen("font_preview.bmp", "wb");
    if (!f) { printf("cannot write\n"); return 1; }
    BMPHDR h;
    memset(&h, 0, sizeof(h));
    h.bfType = 0x4D42;
    h.bfSize = 54 + imgSize;
    h.bfOffBits = 54;
    h.biSize = 40;
    h.biWidth = W;
    h.biHeight = H;
    h.biPlanes = 1;
    h.biBitCount = 32;
    h.biSizeImage = imgSize;
    fwrite(&h, 1, sizeof(h), f);
    for (int y = H - 1; y >= 0; y--) {
        fwrite(px + (size_t)y * W, 1, (size_t)W * 4, f);
        for (int i = W * 4; i < rowSize; i++) fputc(0, f);
    }
    fclose(f);
    printf("font_preview.bmp written %dx%d\n", W, H);
    return 0;
}
