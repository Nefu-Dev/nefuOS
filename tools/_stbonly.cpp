#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <cstdio>
#include <malloc.h>
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("open fail\n"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* d = (unsigned char*)malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) { printf("read fail\n"); return 1; }
    fclose(f);
    int w = 0, h = 0, c = 0;
    unsigned char* p = stbi_load_from_memory(d, (int)n, &w, &h, &c, 4);
    printf("size=%ld ok=%d %dx%d c=%d\n", n, p != 0, w, h, c);
    if (p) {
        // sample a few pixels and the alpha range
        int mn = 255, mx = 0;
        for (int i = 3; i < w * h * 4; i += 4) { if (p[i] < mn) mn = p[i]; if (p[i] > mx) mx = p[i]; }
        printf("alpha min=%d max=%d px0=(%d,%d,%d,%d)\n", mn, mx, p[0], p[1], p[2], p[3]);
        free(p);
    }
    return 0;
}
