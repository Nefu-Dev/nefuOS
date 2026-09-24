// nefuOS DEFLATE / zlib library — implementation
#include "deflate.h"
#include "hash.h"

namespace nefu {
namespace deflate {

// =====================================================================
//  Decompressor (RFC 1951)
// =====================================================================

namespace {

struct BitReader {
    const uint8_t* p;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcnt;

    BitReader(const uint8_t* data, size_t n) : p(data), size(n), pos(0), bitbuf(0), bitcnt(0) {}

    int read_bit() {
        if (bitcnt == 0) {
            if (pos >= size) return -1;
            bitbuf = p[pos++];
            bitcnt = 8;
        }
        int b = (int)(bitbuf & 1);
        bitbuf >>= 1;
        bitcnt--;
        return b;
    }

    int read_bits(int n) {
        int v = 0;
        for (int i = 0; i < n; i++) {
            int b = read_bit();
            if (b < 0) return -1;
            v |= (b << i);
        }
        return v;
    }

    void align_byte() { bitcnt = 0; bitbuf = 0; }
};

// Huffman decoder trie (canonical codes, MSB-first traversal)
struct HuffTrie {
    int* left;
    int* right;
    int* sym;
    int cap;
    int nodes;

    HuffTrie() : left(0), right(0), sym(0), cap(0), nodes(0) {}
    ~HuffTrie() { if (left) delete[] left; if (right) delete[] right; if (sym) delete[] sym; }

    void reset() { nodes = 1; left[0] = right[0] = -1; sym[0] = -1; }

    bool alloc(int max_nodes) {
        cap = max_nodes;
        left = new int[cap];
        right = new int[cap];
        sym = new int[cap];
        if (!left || !right || !sym) return false;
        reset();
        return true;
    }

    // insert symbol with canonical code 'code' of 'len' bits
    void insert(int code, int len, int symbol) {
        int node = 0;
        for (int i = len - 1; i >= 0; i--) {
            int bit = (code >> i) & 1;
            int& child = bit ? right[node] : left[node];
            if (child < 0) {
                if (nodes >= cap) return;
                child = nodes++;
                left[child] = right[child] = -1;
                sym[child] = -1;
            }
            node = child;
        }
        sym[node] = symbol;
    }

    int decode(BitReader& br) const {
        int node = 0;
        for (int i = 0; i < 15; i++) {
            int b = br.read_bit();
            if (b < 0) return -1;
            node = b ? right[node] : left[node];
            if (node < 0) return -1;
            if (sym[node] >= 0) return sym[node];
        }
        return -1;
    }
};

// build canonical trie from code lengths (nsyms entries). Returns true if ok.
bool build_trie(HuffTrie& trie, const uint8_t* lengths, int nsyms) {
    int count[16];
    for (int i = 0; i < 16; i++) count[i] = 0;
    int maxlen = 0;
    for (int i = 0; i < nsyms; i++) {
        int l = lengths[i];
        if (l > 0) {
            if (l > maxlen) maxlen = l;
            if (l < 16) count[l]++;
        }
    }
    if (maxlen > 15) return false;
    int next[16];
    int code = 0;
    for (int l = 1; l < 16; l++) {
        code = (code + count[l - 1]) << 1;
        next[l] = code;
    }
    trie.reset();
    for (int i = 0; i < nsyms; i++) {
        int l = lengths[i];
        if (l > 0) trie.insert(next[l]++, l, i);
    }
    return true;
}

// decode code-length stream (dynamic huffman), producing 'n' lengths
bool decode_lengths(BitReader& br, const HuffTrie& cl_trie, uint8_t* out, int n) {
    int i = 0;
    while (i < n) {
        int sym = cl_trie.decode(br);
        if (sym < 0) return false;
        if (sym <= 15) {
            out[i++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (i == 0) return false;
            int prev = out[i - 1];
            int rep = 3 + br.read_bits(2);
            if (rep < 0) return false;
            for (int k = 0; k < rep && i < n; k++) out[i++] = (uint8_t)prev;
        } else if (sym == 17) {
            int rep = 3 + br.read_bits(3);
            if (rep < 0) return false;
            for (int k = 0; k < rep && i < n; k++) out[i++] = 0;
        } else { // 18
            int rep = 11 + br.read_bits(7);
            if (rep < 0) return false;
            for (int k = 0; k < rep && i < n; k++) out[i++] = 0;
        }
    }
    return true;
}

// length base and extra bits (symbols 257..285)
static const uint16_t LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

// fixed-huffman code lengths for literals/lengths
static const uint8_t FIXED_LL[288] = {
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
};
static const uint8_t FIXED_DIST[30] = {
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};

int inflate_block(BitReader& br, uint8_t* dst, size_t dst_cap, size_t& out_len) {
    for (;;) {
        int bfinal = br.read_bit();
        int btype = br.read_bits(2);
        if (bfinal < 0 || btype < 0) return -1;
        if (btype == 0) {
            // stored block
            br.align_byte();
            int len = br.read_bits(16);
            int nlen = br.read_bits(16);
            if (len < 0 || nlen < 0) return -1;
            if (((len ^ 0xFFFF) & 0xFFFF) != (nlen & 0xFFFF)) return -1;
            for (int i = 0; i < len; i++) {
                int b = br.read_bit();
                if (b < 0) return -1;
                // read remaining 7 bits of the byte
                int v = b;
                for (int k = 1; k < 8; k++) {
                    int bb = br.read_bit();
                    if (bb < 0) return -1;
                    v |= (bb << k);
                }
                if (out_len >= dst_cap) return -1;
                dst[out_len++] = (uint8_t)v;
            }
        } else if (btype == 1 || btype == 2) {
            HuffTrie ll, dist;
            if (btype == 1) {
                if (!ll.alloc(288 * 2 + 8)) return -1;
                if (!dist.alloc(30 * 2 + 8)) return -1;
                build_trie(ll, FIXED_LL, 288);
                build_trie(dist, FIXED_DIST, 30);
            } else {
                int hlit = br.read_bits(5) + 257;
                int hdist = br.read_bits(5) + 1;
                int hclen = br.read_bits(4) + 4;
                if (hlit < 0 || hdist < 0 || hclen < 0) return -1;
                static const uint8_t CL_ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t cl_lengths[19];
                for (int i = 0; i < 19; i++) cl_lengths[i] = 0;
                for (int i = 0; i < hclen; i++) {
                    int v = br.read_bits(3);
                    if (v < 0) return -1;
                    cl_lengths[CL_ORDER[i]] = (uint8_t)v;
                }
                HuffTrie cl;
                if (!cl.alloc(19 * 2 + 8)) return -1;
                if (!build_trie(cl, cl_lengths, 19)) return -1;
                if (hlit > 288 || hdist > 32) return -1;
                uint8_t ll_lengths[288];
                uint8_t dist_lengths[32];
                if (!decode_lengths(br, cl, ll_lengths, hlit)) return -1;
                if (!decode_lengths(br, cl, dist_lengths, hdist)) return -1;
                if (!ll.alloc(hlit * 2 + 8)) return -1;
                if (!dist.alloc(hdist * 2 + 8)) return -1;
                if (!build_trie(ll, ll_lengths, hlit)) return -1;
                if (!build_trie(dist, dist_lengths, hdist)) return -1;
            }
            // decode symbols
            for (;;) {
                int sym = ll.decode(br);
                if (sym < 0) return -1;
                if (sym < 256) {
                    if (out_len >= dst_cap) return -1;
                    dst[out_len++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break; // end of block
                } else if (sym <= 285) {
                    int li = sym - 257;
                    int len = LEN_BASE[li] + br.read_bits(LEN_EXTRA[li]);
                    int dsym = dist.decode(br);
                    if (dsym < 0 || dsym > 29) return -1;
                    int dbase = DIST_BASE[dsym] + br.read_bits(DIST_EXTRA[dsym]);
                    if (len < 0 || dbase < 0) return -1;
                    if ((size_t)dbase > out_len) return -1;
                    for (int i = 0; i < len; i++) {
                        if (out_len >= dst_cap) return -1;
                        dst[out_len] = dst[out_len - dbase];
                        out_len++;
                    }
                } else {
                    return -1;
                }
            }
        } else {
            return -1; // reserved block type
        }
        if (bfinal) break;
    }
    return 0;
}

} // namespace

int raw_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
    BitReader br(src, src_len);
    size_t out_len = 0;
    if (inflate_block(br, dst, dst_cap, out_len) != 0) return -1;
    return (int)out_len;
}

int zlib_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
    if (src_len < 6) return -1;
    if ((src[0] & 0x0F) != 8) return -1;      // method = deflate
    if (((src[0] << 8) | src[1]) % 31 != 0) return -1; // header check
    if (src[1] & 0x20) return -1;             // preset dictionary unsupported
    BitReader br(src + 2, src_len - 2);
    size_t out_len = 0;
    if (inflate_block(br, dst, dst_cap, out_len) != 0) return -1;
    // adler32 trailer follows the compressed bits (byte-aligned end)
    br.align_byte();
    size_t used = br.pos;
    if (src_len < 2 + used + 4) return -1;
    const uint8_t* tr = src + 2 + used;
    uint32_t want = (uint32_t)tr[0] << 24 | (uint32_t)tr[1] << 16 | (uint32_t)tr[2] << 8 | (uint32_t)tr[3];
    uint32_t got = hash::adler32(dst, out_len, 1);
    if (want != got) return -1;
    return (int)out_len;
}

// =====================================================================
//  Compressor (LZ77 + fixed Huffman)
// =====================================================================

namespace {

struct BitWriter {
    uint8_t* p;
    size_t cap;
    size_t pos;
    uint32_t bitbuf;
    int bitcnt;

    BitWriter(uint8_t* buf, size_t c) : p(buf), cap(c), pos(0), bitbuf(0), bitcnt(0) {}

    void write_bit(int b) {
        bitbuf |= (uint32_t)(b & 1) << bitcnt;
        if (++bitcnt == 8) {
            if (pos < cap) p[pos++] = (uint8_t)bitbuf;
            bitbuf = 0; bitcnt = 0;
        }
    }

    void write_bits(int v, int n) {
        for (int i = 0; i < n; i++) write_bit((v >> i) & 1);
    }

    void align_byte() {
        if (bitcnt) {
            if (pos < cap) p[pos++] = (uint8_t)bitbuf;
            bitbuf = 0; bitcnt = 0;
        }
    }
};

// fixed huffman codes (RFC 1951 3.2.6):
//   0-143   : 8 bits, codes 0x30..0xBF
//   144-255 : 9 bits, codes 0x190..0x1FF
//   256-279 : 7 bits, codes 0x00..0x17
//   280-287 : 8 bits, codes 0xC0..0xC7
static uint16_t FIXED_LL_CODE[288];

static void init_fixed_codes() {
    static bool done = false;
    if (done) return;
    for (int i = 0; i < 144; i++) FIXED_LL_CODE[i] = (uint16_t)(0x30 + i);
    for (int i = 144; i < 256; i++) FIXED_LL_CODE[i] = (uint16_t)(0x190 + (i - 144));
    for (int i = 256; i < 280; i++) FIXED_LL_CODE[i] = (uint16_t)(i - 256);
    for (int i = 280; i < 288; i++) FIXED_LL_CODE[i] = (uint16_t)(0xC0 + (i - 280));
    done = true;
}

// hash-chain match finder state (works in local window coordinates)
struct LZState {
    int* head;        // hash -> local position
    int* prev;        // local position -> previous candidate
    int window_start; // absolute start of the window

    LZState() : head(0), prev(0), window_start(0) {}
    bool init(size_t win_len) {
        head = new int[1 << 15];
        prev = new int[win_len + 1];
        if (!head || !prev) return false;
        for (int i = 0; i < (1 << 15); i++) head[i] = -1;
        for (size_t i = 0; i <= win_len; i++) prev[i] = -1;
        return true;
    }
    ~LZState() { if (head) delete[] head; if (prev) delete[] prev; }
};

inline uint32_t hash3(const uint8_t* p) {
    return (uint32_t)(((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ p[2]) & 0x7FFF;
}

// find longest match for src[pos] inside the window; returns match length
// and sets dist (bytes back). src_start = start of the local window in src.
int find_match(const uint8_t* src, size_t len, size_t pos, int window_start, LZState& st, int& dist) {
    if (pos + 2 >= len) return 0;
    uint32_t h = hash3(src + pos);
    int best = 0, best_dist = 0;
    int lpos = (int)(pos - window_start);
    int cand_local = st.head[h];
    int chain = 0;
    const int MAX_CHAIN = 48;
    int limit = (int)pos - 32768;
    if (limit < window_start) limit = window_start;
    while (cand_local >= 0 && chain < MAX_CHAIN) {
        int cand = cand_local + window_start;
        if (cand >= limit && cand < (int)pos &&
            src[cand] == src[pos] && src[cand + 1] == src[pos + 1]) {
            int m = 2;
            while (m < 258 && pos + (size_t)m < len && src[cand + m] == src[pos + m]) m++;
            if (m > best) {
                best = m;
                best_dist = (int)(pos - cand);
                if (best >= 258) break;
            }
        }
        cand_local = st.prev[cand_local];
        chain++;
    }
    // update hash chain
    st.prev[lpos] = st.head[h];
    st.head[h] = lpos;
    dist = best_dist;
    return best;
}

// emit one fixed-huffman deflate block for the window [start, end)
bool emit_fixed_block(BitWriter& bw, const uint8_t* src, size_t start, size_t end, bool final) {
    init_fixed_codes();
    bw.write_bit(final ? 1 : 0);
    bw.write_bits(1, 2); // fixed huffman
    LZState st;
    if (!st.init(end - start + 1)) return false;
    st.window_start = (int)start;
    size_t pos = start;
    while (pos < end) {
        int dist = 0;
        int m = find_match(src, end, pos, (int)start, st, dist);
        if (m >= 3 && dist > 0) {
            // length symbol: largest li with LEN_BASE[li] <= m
            int li = 28;
            while (li > 0 && LEN_BASE[li] > m) li--;
            int sym = 257 + li;
            bw.write_bits(FIXED_LL_CODE[sym], FIXED_LL[sym]);
            bw.write_bits(m - LEN_BASE[li], LEN_EXTRA[li]);
            // distance symbol: largest di with DIST_BASE[di] <= dist
            int di = 29;
            while (di > 0 && DIST_BASE[di] > dist) di--;
            bw.write_bits(di, 5);
            bw.write_bits(dist - DIST_BASE[di], DIST_EXTRA[di]);
            pos += (size_t)m;
        } else {
            bw.write_bits(FIXED_LL_CODE[src[pos]], 8);
            pos++;
        }
    }
    bw.write_bits(FIXED_LL_CODE[256], 7); // end of block
    return true;
}

} // namespace

int raw_compress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
    BitWriter bw(dst, dst_cap);
    size_t chunk = 32768;
    size_t start = 0;
    while (start < src_len) {
        size_t end = start + chunk;
        if (end > src_len) end = src_len;
        bool final = (end == src_len);
        if (end - start < 64) {
            bw.write_bit(final ? 1 : 0);
            bw.write_bits(0, 2); // BTYPE=00 stored
            bw.align_byte();
            int len = (int)(end - start);
            bw.write_bits(len, 16);
            bw.write_bits((~len) & 0xFFFF, 16);
            for (size_t i = start; i < end; i++) bw.write_bits(src[i], 8);
        } else {
            emit_fixed_block(bw, src, start, end, final);
        }
        start = end;
    }
    bw.align_byte();
    return (int)bw.pos;
}

int zlib_compress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
    if (dst_cap < 6) return -1;
    dst[0] = 0x78;
    dst[1] = (src_len > 0xFFFF) ? 0xDA : 0x9C; // 32K window
    int deflate_len = raw_compress(src, src_len, dst + 2, dst_cap - 2);
    if (deflate_len < 0 || (size_t)(2 + deflate_len + 4) > dst_cap) return -1;
    size_t out_pos = 2 + (size_t)deflate_len;
    uint32_t ad = hash::adler32(src, src_len, 1);
    dst[out_pos + 0] = (uint8_t)(ad >> 24);
    dst[out_pos + 1] = (uint8_t)(ad >> 16);
    dst[out_pos + 2] = (uint8_t)(ad >> 8);
    dst[out_pos + 3] = (uint8_t)ad;
    return (int)(out_pos + 4);
}

} // namespace deflate
} // namespace nefu
