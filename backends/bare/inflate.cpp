// nefuOS boot stub inflate — minimal raw-DEFLATE (RFC 1951) decompressor.
// Converted from C to C++ (extern "C" preserves ABI for assembly callers).
extern "C" {

/* nefuOS boot stub inflate — minimal raw-DEFLATE (RFC 1951) decompressor.
 * Compile: gcc -m32 -ffreestanding -fno-builtin -Os -c inflate.c
 * No libc, no globals, no static mutable state.  Handles stored / fixed /
 * dynamic Huffman blocks.  Returns decompressed byte count (0 on error).
 * Callable from 32-bit protected mode assembly (cdecl).
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#ifdef NEFU_DBG
static void dbg(u8 c) { __asm__ volatile("outb %0, %1" :: "a"(c), "d"((u16)0xE9)); }
static void dbg32(u32 v) {
    dbg((u8)(v >> 24)); dbg((u8)(v >> 16)); dbg((u8)(v >> 8)); dbg((u8)v);
}
#endif
#ifdef NEFU_DBG_HOST
#include <stdio.h>
static void dbg(u8 c) { putchar((int)c); fflush(stdout); }
static void dbg32(u32 v) {
    dbg((u8)(v >> 24)); dbg((u8)(v >> 16)); dbg((u8)(v >> 8)); dbg((u8)v);
}
#endif

typedef struct {
    const u8 *in;
    u32 inleft;
    u8 *out;
    u32 outleft;
    const u8 *outstart;
    u32 bitbuf;
    int bitcnt;
} st;

/* ---- bit reader (LSB-first) ---- */
static int bits(st *s, int need, u32 *val) {
    while (s->bitcnt < need) {
        if (s->inleft == 0) return -1;
        s->bitbuf |= (u32)(*s->in++) << s->bitcnt;
        s->inleft--;
        s->bitcnt += 8;
    }
    *val = s->bitbuf & ((1u << need) - 1);
    s->bitbuf >>= need;
    s->bitcnt -= need;
    return 0;
}
/* ---- Huffman table ---- */
typedef struct {
    short count[16];      /* number of codes of each length 1..15 */
    short symbol[288];    /* symbols sorted by (length, symbol) */
} huff;

static int build_huff(huff *h, const u8 *lens, int n) {
    int i, len, left;
    short offs[16];
    for (len = 0; len < 16; len++) h->count[len] = 0;
    for (i = 0; i < n; i++) h->count[lens[i]]++;
    if (h->count[0] == n) {   /* degenerate: all symbols zero length */
        return 0;
    }
    /* oversubscription check */
    left = 1;
    for (len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    /* build symbol index offsets */
    offs[1] = 0;
    for (len = 1; len < 15; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (i = 0; i < n; i++)
        if (lens[i]) h->symbol[offs[lens[i]]++] = (short)i;
    return 0;
}

static int decode(st *s, const huff *h, int *sym) {
    int len, code, first, count, index;
    u32 bit;
    code = first = index = 0;
    for (len = 1; len <= 15; len++) {
        if (bits(s, 1, &bit)) {
#ifdef NEFU_DBG
            dbg('e'); dbg((u8)len); dbg32((u32)(s->out - s->outstart)); dbg32(s->inleft);
#endif
            return -1;
        }
        code |= (int)bit;
        count = h->count[len];
        if (code - count < first) {
            *sym = h->symbol[index + (code - first)];
            return 0;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
#ifdef NEFU_DBG
    dbg('r'); dbg((u8)len); dbg32((u32)(s->out - s->outstart)); dbg32(s->inleft);
#endif
    return -1;   /* ran out of codes */
}

/* length/distance base + extra tables (RFC 1951 3.2.5) */
static const short length_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const short length_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const short dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
    4097,6145,8193,12289,16385,24577
};
static const short dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static u32 xsum(const u8 *p, u32 n) {
    u32 h = 0x811c9dc5u;
    u32 i;
    for (i = 0; i < n; i++) { h ^= (u32)p[i]; h *= 0x01000193u; }
    return h;
}

/* ---- stored block (byte aligned) ---- */
static int stored(st *s) {
    u32 len, i;
    /* discard leftover bits: in already points at the next unread byte */
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inleft < 4) return -1;
    len = (u32)s->in[0] | ((u32)s->in[1] << 8);
    if ((u16)len != (u16)~(u16)((u32)s->in[2] | ((u32)s->in[3] << 8))) return -1;
    s->in += 4;
    s->inleft -= 4;
    if (s->inleft < len) return -1;
    if (s->outleft < len) return -1;
    for (i = 0; i < len; i++) s->out[i] = s->in[i];
    s->in += len;
    s->inleft -= len;
    s->out += len;
    s->outleft -= len;
    return 0;
}

/* ---- decode a block's literal/length + distance stream ---- */
static int codes(st *s, const huff *l, const huff *d) {
    int sym;
    u32 len, dist, extra;
    for (;;) {
#ifdef NEFU_DBG
        if ((((u32)(s->out - s->outstart)) & 0xFFFFu) == 0) {
            dbg('.'); dbg32((u32)(s->out - s->outstart)); dbg32(s->inleft);
            dbg('#'); dbg32(xsum((const u8*)s->outstart, (u32)(s->out - s->outstart)));
            dbg('%'); dbg32(s->bitbuf); dbg((u8)s->bitcnt);
        }
        if (((u32)(s->out - s->outstart)) >= 0x1CE80 && ((u32)(s->out - s->outstart)) <= 0x1CFA0) {
            dbg('s'); dbg32((u32)(s->out - s->outstart)); dbg((u8)sym);
        }
#endif
#ifdef NEFU_DBG_HOST
        if ((((u32)(s->out - s->outstart)) & 0xFFFFu) == 0) {
            dbg('.'); dbg32((u32)(s->out - s->outstart)); dbg32(s->inleft);
            dbg('#'); dbg32(xsum((const u8*)s->outstart, (u32)(s->out - s->outstart)));
            dbg('%'); dbg32(s->bitbuf); dbg((u8)s->bitcnt);
        }
        if (((u32)(s->out - s->outstart)) >= 0x1CE80 && ((u32)(s->out - s->outstart)) <= 0x1CFA0) {
            dbg('s'); dbg32((u32)(s->out - s->outstart)); dbg((u8)sym);
        }
#endif
        sym = 0;
        if (decode(s, l, &sym)) {
#ifdef NEFU_DBG
            dbg('L');
            dbg32((u32)(s->out - s->outstart));
            dbg32(s->inleft);
            dbg32(s->bitbuf);
            dbg((u8)s->bitcnt);
#endif
            return -21;      /* lit/len decode fail */
        }
        if (sym < 256) {                 /* literal */
            if (s->outleft == 0) return -27;     /* literal output overflow */
            *s->out++ = (u8)sym;
            s->outleft--;
        } else if (sym > 256) {          /* length */
            sym -= 257;
            if (sym >= 29) return -25;           /* bad length symbol */
            len = (u32)length_base[sym];
            if (bits(s, length_extra[sym], &extra)) return -22;  /* len extra bits fail */
            len += extra;
#ifdef NEFU_DBG
            if (((u32)(s->out - s->outstart)) >= 0x1CEA0 && ((u32)(s->out - s->outstart)) <= 0x1CEE0) {
                dbg('L'); dbg32((u32)(s->out - s->outstart)); dbg((u8)(sym+257)); dbg((u8)len);
            }
#endif
#ifdef NEFU_DBG_HOST
            if (((u32)(s->out - s->outstart)) >= 0x1CEA0 && ((u32)(s->out - s->outstart)) <= 0x1CEE0) {
                dbg('L'); dbg32((u32)(s->out - s->outstart)); dbg((u8)(sym+257)); dbg((u8)len);
            }
#endif
            sym = 0;
            if (decode(s, d, &sym)) {
#ifdef NEFU_DBG
                dbg('D');
                dbg32((u32)(s->out - s->outstart));
                dbg32(s->inleft);
                dbg32(s->bitbuf);
                dbg((u8)s->bitcnt);
#endif
                return -23;  /* dist decode fail */
            }
            if (sym >= 30) return -26;           /* bad dist symbol */
#ifdef NEFU_DBG
            if (((u32)(s->out - s->outstart)) >= 0x1CEA0 && ((u32)(s->out - s->outstart)) <= 0x1CEE0) {
                dbg('d'); dbg32((u32)(s->out - s->outstart)); dbg((u8)sym);
            }
#endif
#ifdef NEFU_DBG_HOST
            if (((u32)(s->out - s->outstart)) >= 0x1CEA0 && ((u32)(s->out - s->outstart)) <= 0x1CEE0) {
                dbg('d'); dbg32((u32)(s->out - s->outstart)); dbg((u8)sym);
            }
#endif
            dist = (u32)dist_base[sym];
            if (bits(s, dist_extra[sym], &extra)) return -24;  /* dist extra bits fail */
            dist += extra;
            /* copy len bytes from dist bytes back (may overlap) */
            {
                unsigned long out_sofar = (unsigned long)(s->out - s->outstart);
                if (out_sofar < dist) return -11;
                if (s->outleft < len) return -12;
                {
                    u8 *src = s->out - dist;
                    while (len--) {
                        *s->out = *src;
                        s->out++;
                        src++;
                        s->outleft--;
                    }
                }
            }
        } else {                         /* end of block */
            return 0;
        }
    }
}

/* ---- fixed Huffman tables ---- */
static void fixed(huff *l, huff *d) {
    u8 lens[288];
    int i;
    for (i = 0; i < 144; i++) lens[i] = 8;
    for (; i < 256; i++) lens[i] = 9;
    for (; i < 280; i++) lens[i] = 7;
    for (; i < 288; i++) lens[i] = 8;
    build_huff(l, lens, 288);
    for (i = 0; i < 30; i++) lens[i] = 5;
    build_huff(d, lens, 30);
}

/* ---- dynamic Huffman block ---- */
static const short order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

static int dynamic(st *s, huff *l, huff *d) {
    u32 hlit, hdist, hclen, i, v;
    u8 lens[320];
    u8 lengths[19];
    huff h;
    int n, rep, val, sym;
    if (bits(s, 5, &hlit) || bits(s, 5, &hdist) || bits(s, 4, &hclen)) return -1;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    for (i = 0; i < 19; i++) lengths[i] = 0;
    for (i = 0; i < hclen; i++) {
        if (bits(s, 3, &v)) return -1;
        lengths[order[i]] = (u8)v;
    }
    if (build_huff(&h, lengths, 19)) return -1;
    n = 0;
    while (n < (int)(hlit + hdist)) {
        sym = 0;
        if (decode(s, &h, &sym)) return -1;
        if (sym < 16) {
            lens[n++] = (u8)sym;
        } else {
            val = 0;
            rep = 0;
            if (sym == 16) {
                if (n == 0) return -1;
                val = lens[n - 1];
                if (bits(s, 2, &v)) return -1;
                rep = 3 + (int)v;
            } else if (sym == 17) {
                if (bits(s, 3, &v)) return -1;
                rep = 3 + (int)v;
            } else {                     /* 18 */
                if (bits(s, 7, &v)) return -1;
                rep = 11 + (int)v;
            }
            while (rep--) {
                if (n >= (int)(hlit + hdist)) return -1;
                lens[n++] = (u8)val;
            }
        }
    }
    if (build_huff(l, lens, (int)hlit)) return -1;
    if (build_huff(d, lens + hlit, (int)hdist)) return -1;
    return 0;
}

/* ---- raw deflate stream driver ----
 * Distinct error codes for QEMU debugging (low byte observed on debugcon):
 *   -1 input exhausted in bits()
 *   -2 stored block corrupt / short
 *   -3 codes() failed (decode / length / distance)
 *   -4 dynamic() failed (header / table)
 *   -5 invalid block type 3
 */
static int inflate_raw(st *s) {
    u32 last, type;
    huff l, d;
    for (;;) {
        if (bits(s, 1, &last) || bits(s, 2, &type)) return -1;
#ifdef NEFU_DBG
        dbg('B'); dbg((u8)type); dbg32((u32)(s->out - s->outstart)); dbg32(s->inleft);
#endif
        if (type == 0) {
            if (stored(s)) return -2;
        } else if (type == 1) {
            fixed(&l, &d);
            { int rc = codes(s, &l, &d); if (rc) return rc; }
        } else if (type == 2) {
            if (dynamic(s, &l, &d)) return -4;
            { int rc = codes(s, &l, &d); if (rc) return rc; }
        } else {
            return -5;
        }
        if (last) return 0;
    }
}

/* ---- public entry ----
 * Returns decompressed byte count on success, or a negative error code:
 *   -1 input exhausted / bad block header
 *   -2 oversubscribed or invalid Huffman table
 *   -3 decode ran out of codes / ran past end
 *   -4 stored block corrupt
 *   -5 output overflow (literal / copy)
 *   -11 length copy distance out of range
 *   -12 output buffer would overflow
 */
long nefu_inflate(u8 *dst, unsigned long dst_cap,
                  const u8 *src, unsigned long src_len) {
    st s;
    int rc;
#ifdef NEFU_DBG
    dbg('I'); dbg32((u32)src_len); dbg(src[0]); dbg(src[1]); dbg(src[2]); dbg(src[3]);
#endif
    s.in = src;
    s.inleft = (u32)src_len;
    s.out = dst;
    s.outleft = (u32)dst_cap;
    s.outstart = dst;
    s.bitbuf = 0;
    s.bitcnt = 0;
    rc = inflate_raw(&s);
    if (rc) return (long)rc;
    return (long)(s.out - dst);
}


} // extern C
