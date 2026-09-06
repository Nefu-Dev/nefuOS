// nefuOS （）
// 8x16 ASCII(32..127) ， core/gui/font.h
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

int main() {
    const int CW = 8, CH = 16;
    unsigned char font[96][CH];
    memset(font, 0, sizeof(font));

    HDC dc = CreateCompatibleDC(NULL);
    HFONT hf = CreateFontA(CH, CW, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Terminal");
    if (!hf) {
        fprintf(stderr, "Terminal font not found, fallback to DEFAULT_GUI_FONT\n");
        hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    SelectObject(dc, hf);

    for (int c = 0; c < 96; c++) {
        char ch = (char)(c + 32);
        // 32bpp DIB， = ，line 0
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = CW;
        bi.bmiHeader.biHeight = -CH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!bmp) { fprintf(stderr, "DIB create failed\n"); return 1; }
        HGDIOBJ old = SelectObject(dc, bmp);
        RECT r = { 0, 0, CW, CH };
        HBRUSH wh = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &r, wh);
        DeleteObject(wh);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, RGB(0, 0, 0));
        SetBkMode(dc, OPAQUE);
        TextOutA(dc, 0, 0, &ch, 1);
        // ：32bpp DIB BGRA，=0x00FFFFFF，=0x00000000；（）
        const uint32_t* px = (const uint32_t*)bits;
        for (int y = 0; y < CH; y++) {
            unsigned char row = 0;
            for (int x = 0; x < CW; x++) {
                uint32_t p = px[y * CW + x];
                int lum = (int)((p >> 16) & 0xFF) + (int)((p >> 8) & 0xFF) + (int)(p & 0xFF);
                bool black = (lum < 384);
                if (black) row |= (unsigned char)(0x80 >> x);
            }
            font[c][y] = row;
        }
        SelectObject(dc, old);
        DeleteObject(bmp);
    }
    DeleteObject(hf);
    DeleteDC(dc);

    FILE* f = fopen("../core/gui/font.h", "wb");
    if (!f) { fprintf(stderr, "cannot open ../core/gui/font.h\n"); return 1; }
    fprintf(f, "// auto-generated：8x16 ASCII bitmap font（32..127），per char 16 bytes，one row per byte 8 pixel，MSB=left side\n");
    fprintf(f, "// generator tool：tools/fontgen.cpp，do not edit manually\n");
    fprintf(f, "#pragma once\n");
    fprintf(f, "namespace nefu { extern const unsigned char font8x16[96][16]; }\n");
    fprintf(f, "const unsigned char nefu::font8x16[96][16] = {\n");
    for (int c = 0; c < 96; c++) {
        fprintf(f, "  { ");
        for (int y = 0; y < CH; y++) {
            if (y) fprintf(f, ", ");
            fprintf(f, "0x%02X", font[c][y]);
        }
        char cc = (c + 32 >= 32 && c + 32 < 127) ? (char)(c + 32) : ' ';
        fprintf(f, " }, // %d '%c'\n", c + 32, cc);
    }
    fprintf(f, "};\n");
    fclose(f);
    printf("fontgen OK: 96 glyphs -> font.h\n");
    return 0;
}
