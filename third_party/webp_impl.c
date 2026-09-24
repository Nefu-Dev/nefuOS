// nefuOS WebP decode bridge: wraps libwebp's WebPDecodeRGBA.
// Host builds use the CRT; bare builds route through the kernel allocator
// (declared in webp_impl.h, definition in stb_image_wrap.cpp NEFU_BARE block).
#include "src/webp/decode.h"
// Memory is always kernel-allocated (host + bare) so browser's kfree matches.

// Zeroed allocation (libwebp calloc path). Kernel kalloc does not zero.
void* nefu_kcalloc_x(unsigned n, unsigned sz) {
    unsigned long long tot = (unsigned long long)n * sz;
    if (tot == 0) return 0;
    void* p = nefu_kalloc_x((unsigned)tot);
    if (p) {
        unsigned char* b = (unsigned char*)p;
        for (unsigned long long i = 0; i < tot; i++) b[i] = 0;
    }
    return p;
}

// Decode a WebP image into a freshly allocated RGBA buffer (caller frees with
// nefu_kfree_x). Returns 0 on failure. Pixels are 4 bytes each, RGBA order.
int nefu_webp_decode(const unsigned char* data, unsigned len,
                     int* w, int* h, unsigned char** rgba) {
    if (!data || !len || !w || !h || !rgba) return 0;
    WebPBitstreamFeatures feat;
    if (WebPGetFeatures(data, (size_t)len, &feat) != VP8_STATUS_OK) return 0;
    if (feat.width <= 0 || feat.height <= 0) return 0;
    long long pix = (long long)feat.width * feat.height;
    if (pix <= 0 || pix > 4194304LL) return 0;   // safety cap (2048x2048)
    unsigned char* out = WebPDecodeRGBA(data, (size_t)len, w, h);
    if (!out) return 0;
    *rgba = out;
    return 1;
}
