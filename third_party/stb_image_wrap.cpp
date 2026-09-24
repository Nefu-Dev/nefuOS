// nefuOS integration for stb_image.h (public domain, Sean Barrett / RAD Game Tools).
// Bare-metal friendly: memory via kalloc/kfree, no stdio, no floating point
// (HDR/LINEAR disabled; JPEG uses the built-in integer IDCT, PNG/BMP/GIF/TGA
// are integer paths). Decodes from memory only.
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz)   nefu::kalloc((size_t)(sz))
#define STBI_FREE(p)      nefu::kfree((p))
#define STBI_REALLOC(p,sz) nefu::krealloc((p),(size_t)(sz))
#include <stddef.h>
#include <stdint.h>
// declare libc hooks before <stdlib.h> so mm_malloc.h finds them
extern "C" {
void* malloc(size_t sz);
void free(void* p);
int abs(int x);
}
#include <stdlib.h>
extern "C" int abs(int x) { return x < 0 ? -x : x; }
namespace nefu { void* kalloc(size_t sz); void kfree(void* p); void* krealloc(void* p, size_t sz); }
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Decode any stb-supported image (JPG/PNG/GIF/BMP/TGA/...) from memory.
// Returns an RGBA8 buffer allocated with kalloc; caller must kfree() it.
// Allocator hooks shared by nanosvg_impl.c and webp_impl.c (both host and
// bare routes go through the kernel allocator so free() always matches).
extern "C" void* nefu_kalloc_x(size_t sz) { return nefu::kalloc(sz); }
extern "C" void nefu_kfree_x(void* p) { nefu::kfree(p); }
extern "C" void* nefu_krealloc_x(void* p, size_t sz) { return nefu::krealloc(p, sz); }

namespace nefu {
bool stbi_decode_mem(const uint8_t* data, int len, int* w, int* h, uint8_t** out) {
    int c = 0;
    uint8_t* p = stbi_load_from_memory((const stbi_uc*)data, len, w, h, &c, 4);
    if (!p) return false;
    *out = p;
    return true;
}

// Probe image dimensions without decoding (safe for huge files).
bool stbi_info_mem(const uint8_t* data, int len, int* w, int* h) {
    int c = 0;
    return stbi_info_from_memory((const stbi_uc*)data, len, w, h, &c) == 1;
}

// Format sniff: PNG / JPEG / BMP / GIF / TGA / PSD / PIC / PNM ... (stb family).
enum ImgFmt { IMG_UNKNOWN = 0, IMG_PNG = 1, IMG_JPEG = 2, IMG_BMP = 3,
              IMG_GIF = 4, IMG_TGA = 5, IMG_SVG = 6, IMG_WEBP = 7 };
ImgFmt stbi_sniff(const uint8_t* d, int len) {
    if (!d || len < 16) return IMG_UNKNOWN;
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    static const uint8_t png[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    if (memcmp(d, png, 8) == 0) return IMG_PNG;
    // JPEG: FF D8
    if (d[0] == 0xFF && d[1] == 0xD8) return IMG_JPEG;
    // BMP: 'BM'
    if (d[0] == 'B' && d[1] == 'M') return IMG_BMP;
    // GIF: 'GIF87a' / 'GIF89a'
    if (d[0] == 'G' && d[1] == 'I' && d[2] == 'F') return IMG_GIF;
    // WebP: RIFF....WEBP (bytes 0-3 "RIFF", 4-7 size, 8-11 "WEBP")
    if (d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
        d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P') return IMG_WEBP;
    // TGA: 0..2 first byte, colormap 0/1, image type 1/2/3/9/10/11, 18 bytes+
    if (d[0] <= 2 && d[1] <= 1 && d[2] <= 11 && d[2] >= 1) return IMG_TGA;
    // SVG: starts with '<' and looks like an XML/SVG document
    if (d[0] == '<') return IMG_SVG;
    return IMG_UNKNOWN;
}
} // namespace nefu

// ======================== SVG via nanosvg ========================
// Implementation lives in nanosvg_impl.c (C translation unit, so the bare
// build never drags in C++ <math.h>/tr1 hosted templates). Here we only need
// the declarations; nanosvg.h wraps everything in extern "C".
#include "nanosvg.h"
#include "nanosvg_rast.h"   // rasterizer API declarations

namespace nefu {
// Rasterize an SVG document from memory into an RGBA8 buffer (kalloc'd).
// Scales down to fit max_px on the longer side; caller must kfree() the buffer.
bool nsvg_decode_mem(const uint8_t* data, int len, int max_px, int* w, int* h, uint8_t** out) {
    char* doc = (char*)kalloc((size_t)len + 1);
    if (!doc) return false;
    memcpy(doc, data, (size_t)len);
    doc[len] = 0;
    NSVGimage* img = nsvgParse(doc, "px", 96.0f);
    kfree(doc);
    if (!img || img->width <= 0 || img->height <= 0) {
        if (img) nsvgDelete(img);
        return false;
    }
    float scale = 1.0f;
    float longer = img->width > img->height ? img->width : img->height;
    if (longer > (float)max_px) scale = (float)max_px / longer;
    int W = (int)(img->width * scale + 0.5f);
    int H = (int)(img->height * scale + 0.5f);
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (W > max_px) W = max_px;
    if (H > max_px) H = max_px;
    size_t bufn = (size_t)W * (size_t)H * 4;
    uint8_t* buf = (uint8_t*)kalloc(bufn);
    if (!buf) { nsvgDelete(img); return false; }
    memset(buf, 0, bufn);   // transparent background
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) { kfree(buf); nsvgDelete(img); return false; }
    nsvgRasterize(rast, img, 0, 0, scale, buf, W, H, W * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    *w = W; *h = H; *out = buf;
    return true;
}
} // namespace nefu
