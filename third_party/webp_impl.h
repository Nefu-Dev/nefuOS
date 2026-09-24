// Memory hooks for libwebp compiled into nefuOS (host + bare).
#ifndef NEFU_WEBP_IMPL_H
#define NEFU_WEBP_IMPL_H
extern void* nefu_kalloc_x(unsigned);
extern void* nefu_kcalloc_x(unsigned, unsigned);
extern void* nefu_krealloc_x(void*, unsigned);
extern void nefu_kfree_x(void*);
#endif
