// ：print 'A'
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

static void dump(const char* name, const uint32_t* px, int W, int CH) {
    printf("== %s ==\n", name);
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t p = px[y * W + x] & 0x00FFFFFF;
            int lum = ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
            putchar(lum < 384 ? '#' : '.');
        }
        putchar('\n');
    }
}

int main() {
    const int CW = 8, CH = 16;
    const char* tests = "A5xW.";

    HDC dc = CreateCompatibleDC(NULL);
    HFONT hf = CreateFontA(CH, CW, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Terminal");
    if (!hf) hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject(dc, hf);

    for (int t = 0; t < (int)strlen(tests); t++) {
        char ch = tests[t];
        // path1：32bpp DIB top-down
        {
            BITMAPINFO bi; memset(&bi, 0, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = CW; bi.bmiHeader.biHeight = -CH;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = NULL;
            HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
            HGDIOBJ old = SelectObject(dc, bmp);
            RECT r = { 0, 0, CW, CH };
            HBRUSH wh = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(dc, &r, wh);
            DeleteObject(wh);
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, RGB(0, 0, 0));
            SetBkMode(dc, OPAQUE);
            TextOutA(dc, 0, 0, &ch, 1);
            printf("== DIB32 '%c' ==\n", ch);
            dump("", (const uint32_t*)bits, CW, CH);
            SelectObject(dc, old);
            DeleteObject(bmp);
        }
        // path2：1bpp
        {
            HBITMAP bmp = CreateBitmap(CW, CH, 1, 1, NULL);
            HGDIOBJ old = SelectObject(dc, bmp);
            RECT r = { 0, 0, CW, CH };
            HBRUSH wh = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(dc, &r, wh);
            DeleteObject(wh);
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, RGB(0, 0, 0));
            SetBkMode(dc, OPAQUE);
            TextOutA(dc, 0, 0, &ch, 1);
            LONG bits[CH * 2];
            memset(bits, 0, sizeof(bits));
            GetBitmapBits(bmp, sizeof(bits), bits);
            printf("== MONO1 '%c' (raw dwords) ==\n", ch);
            for (int y = 0; y < CH; y++) {
                uint32_t d = (uint32_t)bits[y];
                for (int x = 0; x < CW; x++) {
                    putchar((d & (1u << x)) ? '#' : '.');
                }
                putchar(' ');
                for (int x = 0; x < CW; x++) {
                    putchar((d & (0x80u >> x)) ? '#' : '.');
                }
                putchar('\n');
            }
            SelectObject(dc, old);
            DeleteObject(bmp);
        }
    }
    DeleteObject(hf);
    DeleteDC(dc);
    return 0;
}
