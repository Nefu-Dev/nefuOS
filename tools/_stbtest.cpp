#include <cstdio>
#include <cstdlib>
#include "stb_image_wrap.h"
namespace nefu {
void* kalloc(size_t sz) { return malloc(sz); }
void kfree(void* p) { free(p); }
void* krealloc(void* p, size_t sz) { return realloc(p, sz); }
}
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("open fail\n"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* d = (unsigned char*)malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) { printf("read fail\n"); return 1; }
    fclose(f);
    int w = 0, h = 0; unsigned char* out = 0;
    nefu::ImgFmt fmt = nefu::stbi_sniff(d, (int)n);
    printf("fmt=%d size=%ld\n", (int)fmt, n);
    bool ok = nefu::stbi_decode_mem(d, (int)n, &w, &h, &out);
    printf("decode ok=%d %dx%d\n", (int)ok, w, h);
    return 0;
}
