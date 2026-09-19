// stubs for ttf repro
#include "../../core/klib/klib.h"
#include "../../core/platform.h"
#include <cstdlib>
#include <cstdio>
namespace nefu {
void* kalloc(size_t n) { return malloc(n); }
void kfree(void* p) { free(p); }
void platform_dbg(const char* s) { fputs(s, stderr); }
bool platform_ttf_text(const char* s, int size, int& w, int& h, uint8_t*& extra) {
    (void)s; (void)size; w = 0; h = 0; extra = 0; return false;
}
void platform_ttf_free(uint8_t* p) { (void)p; }
}
