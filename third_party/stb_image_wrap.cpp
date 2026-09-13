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
namespace nefu {
bool stbi_decode_mem(const uint8_t* data, int len, int* w, int* h, uint8_t** out) {
    int c = 0;
    uint8_t* p = stbi_load_from_memory((const stbi_uc*)data, len, w, h, &c, 4);
    if (!p) return false;
    *out = p;
    return true;
}
} // namespace nefu
