# -*- coding: utf-8 -*-
# bare.cpp: kernel arena 64MB -> 512MB, add browser arena (2GB) + balloc/bfree
p = r'D:\mycppos1\nefuOS\backends\bare\bare.cpp'
s = open(p, encoding='utf-8').read()

old = '''// Arena starts at 0x920000: above the kernel image (linked at 0x100000,
// ends < 0x620000, enforced at build time) and above the entry/stub stacks
// (0x900000) and the stub staging region (0x620000..~0x8A0000).
#define ARENA_BASE 0x920000u
#define ARENA_SIZE (64u * 1024u * 1024u)
struct Blk { uint32_t size; Blk* next; };   // 8 bytes, 16
static uint32_t s_arena_next = ARENA_BASE;
static Blk* s_free = 0;'''
new = '''// Arena starts at 0x920000: above the kernel image (linked at 0x100000,
// ends < 0x620000, enforced at build time) and above the entry/stub stacks
// (0x900000) and the stub staging region (0x620000..~0x8A0000).
// Kernel arena: 512 MB budget (kernel + NVFS + network). Browser arena:
// a separate 2 GB region (BROWSER_ARENA_BASE..+2GB) so the browser can grow
// up to its own 2 GB budget without starving the kernel.
#define ARENA_BASE 0x920000u
#define ARENA_SIZE (512u * 1024u * 1024u)      // kernel + NVFS + network
#define BROWSER_ARENA_BASE 0x40000000u
#define BROWSER_ARENA_SIZE (2048u * 1024u * 1024u)  // browser, 2 GB
struct Blk { uint32_t size; Blk* next; };   // 8 bytes, 16
static uint32_t s_arena_next = ARENA_BASE;
static Blk* s_free = 0;
// ---- browser arena (independent bump + free list) ----
static uint32_t s_bnext = BROWSER_ARENA_BASE;
static Blk* s_bfree = 0;
uint64_t browser_mem_used_bytes = 0;

static void* b_alloc_from_bump(uint32_t sz) {
    uint32_t need = (sz + 15) & ~15u;
    uint32_t cur = s_bnext;
    if (cur + need + 16 > BROWSER_ARENA_BASE + BROWSER_ARENA_SIZE) return 0;
    s_bnext = cur + need + 16;
    Blk* b = (Blk*)cur;
    b->size = need;
    return (void*)(b + 1);
}'''
print('arena block count:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# add balloc/bfree/brealloc after kfree
old2 = '''void kfree(void* p) {
    if (!p) return;
    Blk* b = (Blk*)p - 1;
    b->next = s_free;
    s_free = b;
}'''
new2 = '''void kfree(void* p) {
    if (!p) return;
    Blk* b = (Blk*)p - 1;
    b->next = s_free;
    s_free = b;
}

void* balloc(size_t sz) {
    if (sz == 0) sz = 8;
    uint32_t need = ((uint32_t)sz + 15) & ~15u;
    Blk** pp = &s_bfree;
    while (*pp) {
        if ((*pp)->size >= need) {
            Blk* b = *pp;
            *pp = b->next;
            browser_mem_used_bytes += b->size;
            return (void*)(b + 1);
        }
        pp = &(*pp)->next;
    }
    void* r = b_alloc_from_bump(need);
    if (r) browser_mem_used_bytes += need;
    return r;
}

void bfree(void* p) {
    if (!p) return;
    Blk* b = (Blk*)p - 1;
    if (browser_mem_used_bytes >= b->size) browser_mem_used_bytes -= b->size;
    b->next = s_bfree;
    s_bfree = b;
}

void* brealloc(void* p, size_t sz) {
    if (!p) return balloc(sz);
    Blk* b = (Blk*)p - 1;
    uint32_t old = b->size;
    if ((uint32_t)sz <= old) return p;
    void* np = balloc(sz);
    if (!np) return 0;
    memcpy(np, p, old);
    bfree(p);
    return np;
}'''
print('kfree block count:', s.count(old2))
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)

# mem_total reports the 4 GB logical cap
old3 = '''    // Read memory size from CMOS (extended memory above the low 16 MB)
    uint16_t mem_kb = cmos_read(0x17) | (cmos_read(0x18) << 8);
    out->mem_total_mb = (ARENA_SIZE / (1024*1024)) + (mem_kb / 1024);'''
new3 = '''    // Read memory size from CMOS (extended memory above the low 16 MB)
    uint16_t mem_kb = cmos_read(0x17) | (cmos_read(0x18) << 8);
    (void)mem_kb;
    // Logical system cap: 4 GB (kernel 512MB + browser 2GB + reserve 1.5GB)
    out->mem_total_mb = (uint64_t)SYS_MEM_TOTAL_MB;'''
print('hw_info block count:', s.count(old3))
assert s.count(old3) == 1
s = s.replace(old3, new3, 1)

open(p, 'w', encoding='utf-8', newline='').write(s)
print('bare.cpp done')
