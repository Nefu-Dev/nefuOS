/* nefuOS nanosvg (SVG parser + rasterizer) implementation unit.
 * Compiled as C so the bare build never pulls C++ <math.h> hosted templates.
 * Bare: malloc/free/realloc route through nefu kalloc hooks defined in
 * stb_image_wrap.cpp (NEFU_BARE). Host: CRT as usual. */
#ifdef NEFU_BARE
extern void* nefu_kalloc_x(unsigned long long sz);
extern void  nefu_kfree_x(void* p);
extern void* nefu_krealloc_x(void* p, unsigned long long sz);
void* malloc(unsigned long long sz) { return nefu_kalloc_x(sz); }
void free(void* p) { nefu_kfree_x(p); }
void* realloc(void* p, unsigned long long sz) { return nefu_krealloc_x(p, sz); }
#endif

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg_rast.h"
