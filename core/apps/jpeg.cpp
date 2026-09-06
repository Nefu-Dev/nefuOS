// nefuOS baseline-JPEG decoder implementation (integer only).
#include "jpeg.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {



// ===================== bit reader =====================
struct BitReader {
    const uint8_t* data;
    uint32_t size;
    uint32_t pos;       // byte position
    int bitbuf;         // current byte (0xFF filtered)
    int bitcnt;         // bits left in bitbuf
    bool marker;        // a restart / EOI marker was hit
    uint8_t marker_byte;
};

static bool br_init(BitReader* r, const uint8_t* d, uint32_t n) {
    r->data = d; r->size = n; r->pos = 0; r->bitbuf = 0; r->bitcnt = 0;
    r->marker = false; r->marker_byte = 0;
    // advance to the byte after the SOS header (caller positions pos)
    return true;
}

// fetch next byte, skipping 0xFF 0x00 stuffing; non-00 after FF is a marker
static bool br_byte(BitReader* r, uint8_t* out) {
    for (;;) {
        if (r->pos >= r->size) return false;
        uint8_t b = r->data[r->pos++];
        if (b == 0xFF) {
            if (r->pos >= r->size) return false;
            uint8_t n = r->data[r->pos];
            if (n == 0x00) { r->pos++; *out = 0xFF; return true; }
            r->marker = true; r->marker_byte = n; return false;
        }
        *out = b;
        return true;
    }
}

static int get_bit(BitReader* r) {
    if (r->marker) return 0;
    if (r->bitcnt == 0) {
        uint8_t b;
        if (!br_byte(r, &b)) { r->marker = true; return 0; }
        r->bitbuf = b; r->bitcnt = 8;
    }
    r->bitcnt--;
    return (r->bitbuf >> r->bitcnt) & 1;
}

static int get_bits(BitReader* r, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | get_bit(r);
    return v;
}

// ===================== huffman =====================
struct Huff {
    int count[17];
    int symbol[256];
    int mincode[17];
    int maxcode[17];
    int valptr[17];
    int nsym;
};

static void huff_build(Huff* h) {
    int code = 0, k = 0;
    for (int i = 1; i <= 16; i++) {
        if (h->count[i] == 0) {
            h->mincode[i] = 0; h->maxcode[i] = -1; continue;
        }
        h->valptr[i] = k;
        h->mincode[i] = code;
        h->maxcode[i] = code + h->count[i] - 1; // last code of this length
        k += h->count[i];
        code += h->count[i];
        code <<= 1; // codes of the next length start here
    }
    h->nsym = k;
}

static int huff_decode(Huff* h, BitReader* r) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | get_bit(r);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l]) {
            int idx = h->valptr[l] + (code - h->mincode[l]);
            if (idx >= 0 && idx < 256) return h->symbol[idx];
            return 0;
        }
    }
    return 0;
}

// ===================== IDCT =====================
static const int IDCT_T[8][8] = {
    {11585,16069,15137,13623,11585,9102,6270,3196},
    {11585,13623,6270,-3196,-11585,-16069,-15137,-9102},
    {11585,9102,-6270,-16069,-11585,3196,15137,13623},
    {11585,3196,-15137,-9102,11585,13623,-6270,-16069},
    {11585,-3196,-15137,9102,11585,-13623,-6270,16069},
    {11585,-9102,-6270,16069,-11585,-3196,15137,-13623},
    {11585,-13623,6270,3196,-11585,16069,-15137,9102},
    {11585,-16069,15137,-13623,11585,-9102,6270,-3196}
};

// 1D inverse DCT of an 8-vector, result scaled back to 1x
static void idct_1d(const int* in, int* out) {
    for (int k = 0; k < 8; k++) {
        int s = 0;
        const int* t = IDCT_T[k];
        for (int n = 0; n < 8; n++) s += t[n] * in[n];
        // divide by 16384*2 = 32768 with rounding
        out[k] = (s + 16384) >> 15;
    }
}

// full 8x8 2D IDCT; input already dequantized, output -128..127 range
static void idct_2d(const int* in, int* out) {
    int tmp[8][8];
    for (int y = 0; y < 8; y++) {
        int row[8], res[8];
        for (int x = 0; x < 8; x++) row[x] = in[y * 8 + x];
        idct_1d(row, res);
        for (int x = 0; x < 8; x++) tmp[y][x] = res[x];
    }
    for (int x = 0; x < 8; x++) {
        int col[8], res[8];
        for (int y = 0; y < 8; y++) col[y] = tmp[y][x];
        idct_1d(col, res);
        for (int y = 0; y < 8; y++) out[y * 8 + x] = res[x];
    }
}

// ===================== main decoder =====================
struct JpegCtx {
    BitReader br;
    int width, height;
    int ncomp;
    int hsf[4], vsf[4];          // sampling factors per component
    int comp_id[4];
    int tq[4];                   // quant table select per component
    int quant[4][64];            // quant tables
    Huff huff_dc[4], huff_ac[4];
    int mcus_x, mcus_y;
    int mcu_w, mcu_h;            // MCU size in pixels
    int max_h, max_v;
    int rst_interval;            // DRI
    int seldc[4], selac[4];      // huffman table selectors from SOS
};

static bool jpeg_parse(JpegCtx* c, const uint8_t* data, uint32_t size) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false; // SOI
    uint32_t p = 2;
    bool saw_sos = false;
    c->width = 0; c->height = 0; c->ncomp = 0;
    c->rst_interval = 0;
    memset(c->quant, 0, sizeof(c->quant));
    for (int i = 0; i < 4; i++) { c->huff_dc[i].nsym = 0; memset(c->huff_dc[i].count, 0, sizeof(c->huff_dc[i].count)); c->huff_ac[i].nsym = 0; memset(c->huff_ac[i].count, 0, sizeof(c->huff_ac[i].count)); }
    while (p + 3 < size) {
        if (data[p] != 0xFF) { p++; continue; }
        uint8_t m = data[p + 1];
        if (m == 0xD9) break; // EOI
        if (m == 0x01) { p += 2; continue; } // TEM
        if (m >= 0xD0 && m <= 0xD7) { p += 2; continue; } // RSTn (outside scan)
        if (m == 0x00 || m == 0xD8) { p += 2; continue; } // stuffing byte or SOI only
        uint32_t seglen = ((uint32_t)data[p + 2] << 8) | data[p + 3];
        if (seglen < 2) return false;
        const uint8_t* seg = data + p + 4;
        uint32_t sl = seglen - 2;
        if (m == 0xDB) { // DQT
            uint32_t i = 0;
            while (i + 65 <= sl) {
                uint8_t pq = seg[i] >> 4, tq = seg[i] & 0x0F;
                i++;
                if (pq != 0) return false; // only 8-bit precision
                for (int k = 0; k < 64; k++) c->quant[tq][k] = seg[i + k];
                i += 64;
            }
        } else if (m == 0xC0 || m == 0xC1) { // SOF0/SOF1 (baseline/extended-sequential)
            uint8_t prec = seg[0];
            c->height = ((uint32_t)seg[1] << 8) | seg[2];
            c->width = ((uint32_t)seg[3] << 8) | seg[4];
            c->ncomp = seg[5];
            if (prec != 8) return false;
            if (c->ncomp < 1 || c->ncomp > 3) return false;
            for (int i = 0; i < c->ncomp; i++) {
                c->comp_id[i] = seg[6 + i * 3];
                c->hsf[i] = seg[7 + i * 3] >> 4;
                c->vsf[i] = seg[7 + i * 3] & 0x0F;
                c->tq[i] = seg[8 + i * 3];
            }
        } else if (m == 0xC4) { // DHT
            uint32_t i = 0;
            while (i + 17 <= sl) {
                uint8_t tc = seg[i] >> 4, th = seg[i] & 0x0F;
                i++;
                Huff* h = tc == 0 ? &c->huff_dc[th] : &c->huff_ac[th];
                h->nsym = 0;
                memset(h->count, 0, sizeof(h->count));
                int total = 0;
                for (int k = 0; k < 16; k++) { h->count[k + 1] = seg[i + k]; total += seg[i + k]; }
                i += 16;
                if (i + (uint32_t)total > sl) return false;
                for (int k = 0; k < total && k < 256; k++) h->symbol[k] = seg[i + k];
                h->nsym = total;
                i += (uint32_t)total;
                huff_build(h);
            }
        } else if (m == 0xDD) { // DRI
            if (sl >= 2) c->rst_interval = ((uint32_t)seg[0] << 8) | seg[1];
        } else if (m == 0xDA) { // SOS - scan data follows
            uint8_t ns = seg[0];
            if (ns < 1 || ns > 4) return false;
            for (int i = 0; i < ns; i++) {
            c->seldc[i] = seg[2 + i * 2] >> 4;
            c->selac[i] = seg[2 + i * 2] & 0x0F;
        saw_sos = true;
            }
            // decode all MCUs here
            uint32_t sp = p + 2 + seglen;
            br_init(&c->br, data, size);
            c->br.pos = sp;
            // MCU geometry
            c->max_h = 1; c->max_v = 1;
            for (int i = 0; i < c->ncomp; i++) {
                if (c->hsf[i] > c->max_h) c->max_h = c->hsf[i];
                if (c->vsf[i] > c->max_v) c->max_v = c->vsf[i];
            }
            c->mcu_w = 8 * c->max_h;
            c->mcu_h = 8 * c->max_v;
            c->mcus_x = (c->width + c->mcu_w - 1) / c->mcu_w;
            c->mcus_y = (c->height + c->mcu_h - 1) / c->mcu_h;
            return true; // caller decodes scan
        }
        p += 2 + seglen;
    }
    return c->width > 0 && c->height > 0 && c->ncomp >= 1 && c->ncomp <= 3 && saw_sos;
}

// decode one huffman-encoded 8x8 block into out[] (dequantized DCT coefs)
static void decode_block(JpegCtx* c, Huff* dc_huff, Huff* ac_huff, int q, int* out, int* dc_pred) {
    for (int i = 0; i < 64; i++) out[i] = 0;
    // DC
    int s = huff_decode(dc_huff, &c->br);
    if (s > 0) {
        int bits = get_bits(&c->br, s);
        if (bits < (1 << (s - 1))) bits -= (1 << s) - 1;
        *dc_pred += bits;
    }
    out[0] = *dc_pred * c->quant[q][0];
    // AC
    int k = 1;
    while (k < 64) {
        int sym = huff_decode(ac_huff, &c->br);
        int r = sym >> 4, s = sym & 0x0F;
        if (s == 0) {
            if (r == 0) break;         // EOB
            if (r == 15) { k += 16; continue; } // ZRL
        }
        k += r;
                if (k >= 64) break; // run too far: stop AC decoding
        if (k > 63) break;
        int bits = get_bits(&c->br, s);
        if (bits < (1 << (s - 1))) bits -= (1 << s) - 1;
        static const int ZIG[64] = {
            0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
            35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
        };
        if (k < 64) out[ZIG[k]] = bits * c->quant[q][k];
        k++;
    }
    // dequantized DCT coefficients -> spatial samples (in place, safe)
    idct_2d(out, out);
}

bool jpeg_decode(const uint8_t* data, uint32_t size, Surface& out) {
    JpegCtx c;
    if (!jpeg_parse(&c, data, size)) return false;
    if (c.width <= 0 || c.height <= 0 || c.width > 2048 || c.height > 2048) return false;

    out.addr = (uint8_t*)kalloc((size_t)c.width * (size_t)c.height * 4);
    if (!out.addr) return false;
    out.width = c.width;
    out.height = c.height;
    out.pitch = c.width * 4;
    // per-component block buffers (max 4:2:0 => 6 blocks per MCU); static to
    // keep the bare kernel stack small
    static int blocks[4][6][64];
    int dc_pred[4] = { 0, 0, 0, 0 };
    int mcus_x = c.mcus_x, mcus_y = c.mcus_y;

    for (int my = 0; my < mcus_y; my++) {
        for (int mx = 0; mx < mcus_x; mx++) {
            int mcu_index = my * mcus_x + mx;
            if (c.rst_interval > 0 && mcu_index > 0 && (mcu_index % c.rst_interval) == 0) {
                // restart: align to byte, discard pending bits, reset DC predictors
                c.br.bitcnt = 0;
                dc_pred[0] = dc_pred[1] = dc_pred[2] = 0;
                // consume the RSTn marker
                if (c.br.marker) { c.br.marker = false; }
            }
            if (c.br.marker) goto done_scan;
            // decode component blocks
            for (int ci = 0; ci < c.ncomp; ci++) {
                int nb = c.hsf[ci] * c.vsf[ci];
                int q = c.tq[ci];
                Huff* dh = &c.huff_dc[c.seldc[ci]];
                Huff* ah = &c.huff_ac[c.selac[ci]];
                for (int i = 0; i < nb && i < 6; i++) {
                    decode_block(&c, dh, ah, q, blocks[ci][i], &dc_pred[ci]);
                }
            }
            // compose pixels for this MCU
            if (c.br.marker) goto done_scan;
            for (int py = 0; py < c.mcu_h; py++) {
                int gy = my * c.mcu_h + py;
                if (gy >= c.height) continue;
                for (int px = 0; px < c.mcu_w; px++) {
                    int gx = mx * c.mcu_w + px;
                    if (gx >= c.width) continue;
                    int y, cb = 128, cr = 128;
                    // Y
                    {
                        int bx = px / 8, by = py / 8;
                        if (c.hsf[0] == 1 && c.vsf[0] == 1) { bx = 0; by = 0; }
                        int lx = px - bx * 8, ly = py - by * 8;
                        int id = blocks[0][by * c.hsf[0] + bx][ly * 8 + lx];
                        y = id + 128;
                    }
                    // chroma: nearest sample
                    for (int ci = 1; ci < c.ncomp; ci++) {
                        int hf = c.hsf[ci], vf = c.vsf[ci];
                        int sx = px * hf / c.max_h;
                        int sy = py * vf / c.max_v;
                        int bx = sx / 8, by = sy / 8;
                        int lx = sx - bx * 8, ly = sy - by * 8;
                        int id = blocks[ci][by * hf + bx][ly * 8 + lx];
                        if (ci == 1) cb = id + 128; else cr = id + 128;
                    }
                    // clip sample values to 0..255
                    if (y < 0) y = 0; else if (y > 255) y = 255;
                    if (cb < 0) cb = 0; else if (cb > 255) cb = 255;
                    if (cr < 0) cr = 0; else if (cr > 255) cr = 255;
                    // YCbCr -> RGB (full-range JPEG, fixed point BT.601)
                    int cbd = cb - 128, crd = cr - 128;
                    int R = (y * 1024 + 1436 * crd) >> 10;
                    int G = (y * 1024 - 352 * cbd - 731 * crd) >> 10;
                    int B = (y * 1024 + 1815 * cbd) >> 10;
                    if (R < 0) R = 0; if (R > 255) R = 255;
                    if (G < 0) G = 0; if (G > 255) G = 255;
                    if (B < 0) B = 0; if (B > 255) B = 255;
                    out.px(gx, gy) = ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
                }
            }
        }
    }
done_scan:
    return true;
}

} // namespace nefu
