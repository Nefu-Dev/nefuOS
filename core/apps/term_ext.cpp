// nefuOS terminal extension commands (batch 1)
// ~45 Unix-style commands implemented on top of the VFS + nefuOS libraries.
#include "term_ext.h"
#include "../vfs/vfs.h"
#include "../klib/klib.h"
#include "../platform.h"
#include "../lib/hash.h"
#include "../lib/prime.h"
#include "../lib/json.h"
#include "../lib/regex.h"
#include "../lib/deflate.h"
#include "../lib/bigint.h"
#include "../lib/softmath.h"
#include "../algo/sort.h"
#include "../algo/search.h"
#include "../algo/graph.h"
#include "../algo/ds.h"
#ifndef NEFU_BARE
// the double-based numeric library is host-only: the bare kernel has no
// soft-double, and its <math.h> pulls in libstdc++ tr1 headers that do not
// compile under -fno-exceptions.
#include "../algo/numeric.h"
#endif
#include "../gfxlib/gfxlib_all.h"
#include "../datlib/dat_all.h"
#include "../cryptlib/crypt_all.h"
#include "../complib/comp_all.h"
#include "../mathlib/math_all.h"
#include "../simlib/sim_all.h"
#include "../gfxmath/gfx_all.h"
#include "../audlib/aud_all.h"
#include "../dblib/db_all.h"
#include "../textlib/text_all.h"
#include "../textproc/textproc_all.h"
#include "../crypto/crypto_all.h"
#include "../compress/compress_all.h"
#include "../serialize/serialize_all.h"
#include "../audsp/audsp_all.h"
#include "../gfx3d/gfx3d_all.h"
#include "../simulate/simulate_all.h"
#include "../mathext/mathext_all.h"
#include "../uiwidgets/uiwidgets_all.h"
#include "../database/database_all.h"
#include "../ml/ml_all.h"
#include "../termcmds/termcmds_all.h"
#include "../minilang/minilang_all.h"
#include "../sysutil/sysutil_all.h"
#include "../netproto/netproto_all.h"
#include "../filesystem/filesystem_all.h"
#include "../raytrace/raytrace_all.h"
#include "../compiler/compiler_all.h"
#include "../sys/sha256.h"

namespace nefu {

// ===================== helpers =====================

static bool cmd_read_file(const char* path, uint8_t*& data, uint32_t& size) {
    data = 0; size = 0;
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir || f->size == 0) return false;
    data = (uint8_t*)kalloc(f->size + 1);
    if (!data) return false;
    memcpy(data, f->data, f->size);
    data[f->size] = 0;
    size = f->size;
    return true;
}

static void cmd_write_file(const char* path, const uint8_t* data, uint32_t size) {
    FSNode* f = g_vfs->resolve(path);
    if (!f) f = g_vfs->create_file(path);
    if (f) g_vfs->write_file(f, data, size);
}

static String cmd_abs_path(const char* path) {
    String out;
    FSNode* n = g_vfs->resolve(path);
    if (!n) {
        out = path;
        return out;
    }
    // walk up to root
    List<String> parts;
    FSNode* cur = n;
    while (cur && cur->parent && cur != g_vfs->root()) {
        parts.push(cur->name);
        cur = cur->parent;
    }
    if (parts.size() == 0) { out = "/"; return out; }
    for (int i = parts.size() - 1; i >= 0; i--) {
        out += '/';
        out += parts[i];
    }
    return out;
}

// ===================== command implementations =====================

static void cmd_cut(TermState* t, int argc, const char** argv) {
    // cut -d<delim> -f<fields> <file>
    char delim = '\t';
    int field = 1;
    const char* file = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-d", 2) == 0) delim = argv[i][2] ? argv[i][2] : '\t';
        else if (strncmp(argv[i], "-f", 2) == 0) field = atoi(argv[i] + 2);
        else file = argv[i];
    }
    if (!file || field < 1) { term_ext_print(t, "usage: cut -d<d> -f<n> <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "cut: no such file"); return; }
    char* p = (char*)d;
    char* start = p;
    for (uint32_t i = 0; i <= n; i++) {
        if (i == n || p[i] == '\n') {
            char save = p[i];
            p[i] = 0;
            // split the line by delim, print field
            int f = 1;
            char* tok = start;
            char* q = start;
            for (;;) {
                if (*q == delim) {
                    if (f == field) {
                        char old = *q;
                        *q = 0;
                        term_ext_print(t, tok);
                        *q = old;
                        break;
                    }
                    f++;
                    tok = q + 1;
                }
                if (!*q) {
                    if (f == field) term_ext_print(t, tok);
                    break;
                }
                q++;
            }
            if (i == n) break;
            p[i] = save;
            start = p + i + 1;
        }
    }
    kfree(d);
}

static void cmd_paste(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: paste <file1> <file2>"); return; }
    // read both files into line lists
    const char* f1 = argv[1];
    const char* f2 = argc > 2 ? argv[2] : 0;
    uint8_t* d1; uint32_t n1;
    if (!cmd_read_file(f1, d1, n1)) { term_ext_print(t, "paste: no such file"); return; }
    uint8_t* d2 = 0; uint32_t n2 = 0;
    if (f2) cmd_read_file(f2, d2, n2);
    // tokenize lines
    char* l1[256]; int c1 = 0;
    char* p = (char*)d1; char* st = p;
    for (uint32_t i = 0; i <= n1 && c1 < 256; i++) {
        if (i == n1 || p[i] == '\n') { p[i] = 0; l1[c1++] = st; st = p + i + 1; }
    }
    char* l2[256]; int c2 = 0;
    if (d2) {
        p = (char*)d2; st = p;
        for (uint32_t i = 0; i <= n2 && c2 < 256; i++) {
            if (i == n2 || p[i] == '\n') { p[i] = 0; l2[c2++] = st; st = p + i + 1; }
        }
    }
    int max = c1 > c2 ? c1 : c2;
    for (int i = 0; i < max; i++) {
        char buf[512];
        if (i < c1 && i < c2) ksprintf(buf, sizeof(buf), "%s\t%s", l1[i], l2 ? l2[i] : "");
        else if (i < c1) ksprintf(buf, sizeof(buf), "%s", l1[i]);
        else ksprintf(buf, sizeof(buf), "\t%s", l2 ? l2[i] : "");
        term_ext_print(t, buf);
    }
    kfree(d1);
    if (d2) kfree(d2);
}

static void cmd_tr(TermState* t, int argc, const char** argv) {
    // tr <from> <to> [file]  — character translation on stdin-like arg
    if (argc < 3) { term_ext_print(t, "usage: tr <from> <to> <file>"); return; }
    const char* from = argv[1];
    const char* to = argv[2];
    const char* file = argc > 3 ? argv[3] : 0;
    if (!file) { term_ext_print(t, "usage: tr <from> <to> <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "tr: no such file"); return; }
    for (uint32_t i = 0; i < n; i++) {
        const char* f = from;
        const char* t2 = to;
        int idx = 0;
        while (*f) {
            if (*f == d[i]) { d[i] = *t2; break; }
            f++; t2++; idx++;
        }
    }
    cmd_write_file(file, d, n);
    char msg[64];
    ksprintf(msg, sizeof(msg), "tr: translated %u chars", (unsigned)n);
    term_ext_print(t, msg);
    kfree(d);
}

static void cmd_expand(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: expand <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "expand: no such file"); return; }
    String out;
    int col = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (d[i] == '\t') {
            int sp = 8 - (col % 8);
            for (int k = 0; k < sp; k++) { out += ' '; col++; }
        } else {
            out += (char)d[i];
            col = (d[i] == '\n') ? 0 : col + 1;
        }
    }
    cmd_write_file(file, (const uint8_t*)out.c_str(), (uint32_t)out.len());
    term_ext_print(t, "expand: done");
    kfree(d);
}

static void cmd_fold(TermState* t, int argc, const char** argv) {
    int width = 80;
    const char* file = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-w", 2) == 0) width = atoi(argv[i] + 2);
        else file = argv[i];
    }
    if (!file || width < 1) { term_ext_print(t, "usage: fold -w<width> <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "fold: no such file"); return; }
    String out;
    int col = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (d[i] == '\n') { out += '\n'; col = 0; continue; }
        out += (char)d[i];
        col++;
        if (col >= width) { out += '\n'; col = 0; }
    }
    cmd_write_file(file, (const uint8_t*)out.c_str(), (uint32_t)out.len());
    term_ext_print(t, "fold: done");
    kfree(d);
}

static void cmd_nl(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: nl <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "nl: no such file"); return; }
    char* p = (char*)d;
    char* st = p;
    int ln = 1;
    for (uint32_t i = 0; i <= n; i++) {
        if (i == n || p[i] == '\n') {
            char save = p[i];
            p[i] = 0;
            char buf[512];
            ksprintf(buf, sizeof(buf), "%6d\t%s", ln++, st);
            term_ext_print(t, buf);
            if (i == n) break;
            p[i] = save;
            st = p + i + 1;
        }
    }
    kfree(d);
}

static void cmd_hexdump(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: hexdump <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "hexdump: no such file"); return; }
    for (uint32_t off = 0; off < n; off += 16) {
        char line[128];
        char* q = line;
        q += ksprintf(q, 16, "%08x  ", (unsigned)off);
        for (int i = 0; i < 16; i++) {
            if (off + (uint32_t)i < n) q += ksprintf(q, 8, "%02x ", d[off + i]);
            else q += ksprintf(q, 8, "   ");
            if (i == 7) q += ksprintf(q, 4, " ");
        }
        q += ksprintf(q, 4, " |");
        for (int i = 0; i < 16 && off + (uint32_t)i < n; i++) {
            char c = (char)d[off + i];
            *q++ = (c >= 32 && c < 127) ? c : '.';
        }
        *q++ = '|'; *q = 0;
        term_ext_print(t, line);
    }
    kfree(d);
}

static void cmd_xxd(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: xxd <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "xxd: no such file"); return; }
    for (uint32_t off = 0; off < n; off += 16) {
        char line[128];
        char* q = line;
        q += ksprintf(q, 16, "%08x: ", (unsigned)off);
        for (int i = 0; i < 16; i++) {
            if (off + (uint32_t)i < n) q += ksprintf(q, 8, "%02x ", d[off + i]);
            else q += ksprintf(q, 8, "   ");
        }
        q += ksprintf(q, 4, " ");
        for (int i = 0; i < 16 && off + (uint32_t)i < n; i++) {
            char c = (char)d[off + i];
            *q++ = (c >= 32 && c < 127) ? c : '.';
        }
        *q = 0;
        term_ext_print(t, line);
    }
    kfree(d);
}

static void cmd_strings(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: strings <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "strings: no such file"); return; }
    char buf[512];
    int bl = 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = (char)d[i];
        if (c >= 32 && c < 127) {
            if (bl < 511) buf[bl++] = c;
        } else {
            if (bl >= 4) { buf[bl] = 0; term_ext_print(t, buf); }
            bl = 0;
        }
    }
    if (bl >= 4) { buf[bl] = 0; term_ext_print(t, buf); }
    kfree(d);
}

static void cmd_base64(TermState* t, int argc, const char** argv) {
    bool decode = false;
    const char* file = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) decode = true;
        else file = argv[i];
    }
    if (!file) { term_ext_print(t, "usage: base64 [-d] <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "base64: no such file"); return; }
    if (!decode) {
        int cap = (int)((n + 2) / 3) * 4 + 1;
        char* out = (char*)kalloc((size_t)cap);
        if (!out) { kfree(d); return; }
        int olen = hash::base64_encode(d, n, out, (size_t)cap);
        if (olen > 0) term_ext_print(t, out);
        kfree(out);
    } else {
        uint8_t* out = (uint8_t*)kalloc(n + 1);
        if (!out) { kfree(d); return; }
        int olen = hash::base64_decode((const char*)d, out, n);
        if (olen >= 0) cmd_write_file(file, out, (uint32_t)olen);
        char msg[64];
        ksprintf(msg, sizeof(msg), "decoded %d bytes", olen);
        term_ext_print(t, msg);
        kfree(out);
    }
    kfree(d);
}

static void cmd_md5sum(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: md5sum <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "md5sum: no such file"); return; }
    hash::MD5 m;
    m.update(d, n);
    char hex[33];
    m.hex_final(hex);
    char out[512];
    ksprintf(out, sizeof(out), "%s  %s", hex, file);
    term_ext_print(t, out);
    kfree(d);
}

static void cmd_sha1sum(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: sha1sum <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "sha1sum: no such file"); return; }
    hash::SHA1 s;
    s.update(d, n);
    char hex[41];
    s.hex_final(hex);
    char out[512];
    ksprintf(out, sizeof(out), "%s  %s", hex, file);
    term_ext_print(t, out);
    kfree(d);
}

static void cmd_sha256sum(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: sha256sum <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "sha256sum: no such file"); return; }
    uint8_t digest[32];
    nefu_sha256(d, n, digest);
    char hex[65];
    nefu_sha256_hex(digest, hex);
    char out[512];
    ksprintf(out, sizeof(out), "%s  %s", hex, file);
    term_ext_print(t, out);
    kfree(d);
}

static void cmd_crc32(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: crc32 <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "crc32: no such file"); return; }
    uint32_t c = hash::crc32(d, n);
    char out[512];
    ksprintf(out, sizeof(out), "%08x  %u  %s", (unsigned)c, (unsigned)n, file);
    term_ext_print(t, out);
    kfree(d);
}

static void cmd_cksum(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: cksum <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "cksum: no such file"); return; }
    uint32_t c = hash::crc32(d, n);
    char out[512];
    ksprintf(out, sizeof(out), "%u %u %s", (unsigned)c, (unsigned)n, file);
    term_ext_print(t, out);
    kfree(d);
}

static void cmd_cmp(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: cmp <file1> <file2>"); return; }
    uint8_t* d1; uint32_t n1;
    uint8_t* d2; uint32_t n2;
    if (!cmd_read_file(argv[1], d1, n1)) { term_ext_print(t, "cmp: no such file"); return; }
    if (!cmd_read_file(argv[2], d2, n2)) { kfree(d1); term_ext_print(t, "cmp: no such file"); return; }
    uint32_t m = n1 < n2 ? n1 : n2;
    bool same = true;
    uint32_t diff_at = 0;
    for (uint32_t i = 0; i < m; i++) {
        if (d1[i] != d2[i]) { same = false; diff_at = i; break; }
    }
    if (same && n1 == n2) term_ext_print(t, "files are identical");
    else if (same) term_ext_print(t, "files differ: one is a prefix of the other");
    else {
        char out[128];
        ksprintf(out, sizeof(out), "files differ at byte %u (0x%x)", (unsigned)diff_at, (unsigned)diff_at);
        term_ext_print(t, out);
    }
    kfree(d1); kfree(d2);
}

static void cmd_comm(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: comm <file1> <file2>"); return; }
    uint8_t* d1; uint32_t n1;
    uint8_t* d2; uint32_t n2;
    if (!cmd_read_file(argv[1], d1, n1)) { term_ext_print(t, "comm: no such file"); return; }
    if (!cmd_read_file(argv[2], d2, n2)) { kfree(d1); term_ext_print(t, "comm: no such file"); return; }
    // simple: lines of file1 not in file2 (col1), file2 not in file1 (col2), common (col3)
    char* l1[512]; int c1 = 0;
    char* p = (char*)d1; char* st = p;
    for (uint32_t i = 0; i <= n1 && c1 < 512; i++) {
        if (i == n1 || p[i] == '\n') { p[i] = 0; l1[c1++] = st; st = p + i + 1; }
    }
    char* l2[512]; int c2 = 0;
    p = (char*)d2; st = p;
    for (uint32_t i = 0; i <= n2 && c2 < 512; i++) {
        if (i == n2 || p[i] == '\n') { p[i] = 0; l2[c2++] = st; st = p + i + 1; }
    }
    bool* used1 = new bool[c1]; bool* used2 = new bool[c2];
    for (int i = 0; i < c1; i++) used1[i] = false;
    for (int i = 0; i < c2; i++) used2[i] = false;
    for (int i = 0; i < c1; i++) {
        for (int j = 0; j < c2; j++) {
            if (!used2[j] && strcmp(l1[i], l2[j]) == 0) { used1[i] = true; used2[j] = true; break; }
        }
    }
    for (int i = 0; i < c1; i++) {
        if (!used1[i]) { char b[600]; ksprintf(b, sizeof(b), "%s", l1[i]); term_ext_print(t, b); }
    }
    for (int i = 0; i < c2; i++) {
        if (!used2[i]) { char b[600]; ksprintf(b, sizeof(b), "\t%s", l2[i]); term_ext_print(t, b); }
    }
    term_ext_print(t, "--- common lines ---");
    for (int i = 0; i < c1; i++) {
        if (used1[i]) { char b[600]; ksprintf(b, sizeof(b), "\t\t%s", l1[i]); term_ext_print(t, b); }
    }
    delete[] used1; delete[] used2;
    kfree(d1); kfree(d2);
}

static void cmd_diff(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: diff <file1> <file2>"); return; }
    uint8_t* d1; uint32_t n1;
    uint8_t* d2; uint32_t n2;
    if (!cmd_read_file(argv[1], d1, n1)) { term_ext_print(t, "diff: no such file"); return; }
    if (!cmd_read_file(argv[2], d2, n2)) { kfree(d1); term_ext_print(t, "diff: no such file"); return; }
    char* l1[512]; int c1 = 0;
    char* p = (char*)d1; char* st = p;
    for (uint32_t i = 0; i <= n1 && c1 < 512; i++) {
        if (i == n1 || p[i] == '\n') { p[i] = 0; l1[c1++] = st; st = p + i + 1; }
    }
    char* l2[512]; int c2 = 0;
    p = (char*)d2; st = p;
    for (uint32_t i = 0; i <= n2 && c2 < 512; i++) {
        if (i == n2 || p[i] == '\n') { p[i] = 0; l2[c2++] = st; st = p + i + 1; }
    }
    // LCS table (bounded)
    const int MAXN = 512;
    static int16_t dp[MAXN * MAXN];
    for (int i = 0; i <= c1; i++) dp[i * (MAXN + 1) + 0] = 0;
    for (int j = 0; j <= c2; j++) dp[0 * (MAXN + 1) + j] = 0;
    for (int i = 1; i <= c1; i++) {
        for (int j = 1; j <= c2; j++) {
            if (strcmp(l1[i - 1], l2[j - 1]) == 0) dp[i * (MAXN + 1) + j] = (int16_t)(dp[(i - 1) * (MAXN + 1) + (j - 1)] + 1);
            else {
                int16_t a = dp[(i - 1) * (MAXN + 1) + j];
                int16_t b = dp[i * (MAXN + 1) + (j - 1)];
                dp[i * (MAXN + 1) + j] = a > b ? a : b;
            }
        }
    }
    // walk back
    List<const char*> out;
    int i = c1, j = c2;
    while (i > 0 && j > 0) {
        if (strcmp(l1[i - 1], l2[j - 1]) == 0) {
            char* b = (char*)kalloc(600);
            ksprintf(b, 600, "  %s", l1[i - 1]);
            out.push(b);
            i--; j--;
        } else if (dp[(i - 1) * (MAXN + 1) + j] >= dp[i * (MAXN + 1) + (j - 1)]) {
            char* b = (char*)kalloc(600);
            ksprintf(b, 600, "- %s", l1[i - 1]);
            out.push(b);
            i--;
        } else {
            char* b = (char*)kalloc(600);
            ksprintf(b, 600, "+ %s", l2[j - 1]);
            out.push(b);
            j--;
        }
    }
    while (i > 0) {
        char* b = (char*)kalloc(600);
        ksprintf(b, 600, "- %s", l1[i - 1]);
        out.push(b);
        i--;
    }
    while (j > 0) {
        char* b = (char*)kalloc(600);
        ksprintf(b, 600, "+ %s", l2[j - 1]);
        out.push(b);
        j--;
    }
    char hdr[64];
    ksprintf(hdr, sizeof(hdr), "%d,%d vs %d,%d", 1, c1, 1, c2);
    term_ext_print(t, hdr);
    for (int k = out.size() - 1; k >= 0; k--) {
        term_ext_print(t, out[k]);
        kfree((void*)out[k]);
    }
    kfree(d1); kfree(d2);
}

static void cmd_expr(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: expr <int-expr>   ( + - * / % ( ) )"); return; }
    String expr;
    for (int i = 1; i < argc; i++) {
        expr += argv[i];
    }
    const char* s = expr.c_str();
    const char* p = s;
    // minimal: evaluate with precedence via nested function (implemented below)
    // Use a tiny state machine instead
    struct ExprEval {
        const char* p;
        bool ok;
        ExprEval(const char* s) : p(s), ok(true) {}
        void skip() { while (*p == ' ') p++; }
        long long num() {
            skip();
            bool neg = false;
            if (*p == '-') { neg = true; p++; }
            long long v = 0;
            if (*p >= '0' && *p <= '9') {
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            } else ok = false;
            return neg ? -v : v;
        }
        long long factor() {
            skip();
            if (*p == '(') {
                p++;
                long long v = addsub();
                skip();
                if (*p == ')') p++; else ok = false;
                return v;
            }
            return num();
        }
        long long muldiv() {
            long long v = factor();
            for (;;) {
                skip();
                char c = *p;
                if (c == '*') { p++; v *= factor(); }
                else if (c == '/') { p++; long long d = factor(); if (d == 0) { ok = false; return 0; } v /= d; }
                else if (c == '%') { p++; long long d = factor(); if (d == 0) { ok = false; return 0; } v %= d; }
                else break;
            }
            return v;
        }
        long long addsub() {
            long long v = muldiv();
            for (;;) {
                skip();
                char c = *p;
                if (c == '+') { p++; v += muldiv(); }
                else if (c == '-') { p++; v -= muldiv(); }
                else break;
            }
            return v;
        }
    };
    ExprEval ev(p);
    long long r = ev.addsub();
    if (!ev.ok) { term_ext_print(t, "expr: syntax error"); return; }
    char out[32];
    // manual int64 -> string (ksprintf has no %lld)
    if (r == 0) { term_ext_print(t, "0"); return; }
    char tmp[24];
    int tn = 0;
    bool neg = r < 0;
    unsigned long long u = neg ? (unsigned long long)(0 - r) : (unsigned long long)r;
    while (u) { tmp[tn++] = (char)('0' + u % 10); u /= 10; }
    int o = 0;
    if (neg) out[o++] = '-';
    while (tn) out[o++] = tmp[--tn];
    out[o] = 0;
    term_ext_print(t, out);
}

static void cmd_factor(TermState* t, int argc, const char** argv) {
    const char* ns = argc > 1 ? argv[1] : 0;
    if (!ns) { term_ext_print(t, "usage: factor <number>"); return; }
    uint64_t n = 0;
    const char* p = ns;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (uint64_t)(*p - '0'); p++; }
    if (n < 2) { term_ext_print(t, "factor: number too small"); return; }
    List<prime::Factor> facs;
    prime::factorize_u64(n, facs);
    char out[256];
    char* q = out;
    q += ksprintf(q, 32, "%llu =", (unsigned long long)n);
    for (int i = 0; i < facs.size(); i++) {
        if (facs[i].e > 1) q += ksprintf(q, 32, " %llu^%d", (unsigned long long)facs[i].p, facs[i].e);
        else q += ksprintf(q, 32, " %llu", (unsigned long long)facs[i].p);
    }
    if (facs.size() == 0) q += ksprintf(q, 16, " (prime?)");
    term_ext_print(t, out);
}

static void cmd_primes(TermState* t, int argc, const char** argv) {
    int limit = argc > 1 ? atoi(argv[1]) : 100;
    if (limit < 2) limit = 2;
    if (limit > 1000000) limit = 1000000;
    List<int> primes;
    prime::sieve(limit, primes);
    char buf[256];
    int n = 0;
    buf[0] = 0;
    for (int i = 0; i < primes.size(); i++) {
        char nb[16];
        ksprintf(nb, sizeof(nb), "%d ", primes[i]);
        for (char* q = nb; *q && n < 250; q++) buf[n++] = *q;
        if (n >= 248) { buf[n] = 0; term_ext_print(t, buf); n = 0; }
    }
    if (n) { buf[n] = 0; term_ext_print(t, buf); }
    char sum[64];
    ksprintf(sum, sizeof(sum), "%d primes <= %d", primes.size(), limit);
    term_ext_print(t, sum);
}

static void cmd_split(TermState* t, int argc, const char** argv) {
    int bytes = 1000;
    const char* file = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-b", 2) == 0) bytes = atoi(argv[i] + 2);
        else file = argv[i];
    }
    if (!file || bytes < 1) { term_ext_print(t, "usage: split -b<bytes> <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "split: no such file"); return; }
    int parts = (int)((n + (uint32_t)bytes - 1) / (uint32_t)bytes);
    if (parts > 100) parts = 100;
    char out[256];
    for (int i = 0; i < parts; i++) {
        ksprintf(out, sizeof(out), "%s.%03d", file, i);
        uint32_t off = (uint32_t)(i * bytes);
        uint32_t len = n - off;
        if (len > (uint32_t)bytes) len = (uint32_t)bytes;
        cmd_write_file(out, d + off, len);
        char msg[128];
        ksprintf(msg, sizeof(msg), "wrote %s (%u bytes)", out, (unsigned)len);
        term_ext_print(t, msg);
    }
    kfree(d);
}

static void cmd_realpath(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: realpath <path>"); return; }
    term_ext_print(t, cmd_abs_path(argv[1]).c_str());
}

static void cmd_chmod(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: chmod <mode> <file>  (simulated)"); return; }
    term_ext_print(t, "chmod: simulated (VFS has no permission bits)");
}

static void cmd_chown(TermState* t, int argc, const char** argv) {
    term_ext_print(t, "chown: simulated (single-user VFS)");
}

static void cmd_chgrp(TermState* t, int argc, const char** argv) {
    term_ext_print(t, "chgrp: simulated (single-user VFS)");
}

static void cmd_nproc(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    term_ext_print(t, "1 (single-core nefuOS)");
}

static void cmd_arch(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    term_ext_print(t, "x86_64");
}

static void cmd_logname(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    term_ext_print(t, "user");
}

static void cmd_groups(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    term_ext_print(t, "users wheel");
}

static void cmd_hostid(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    char out[64];
    ksprintf(out, sizeof(out), "%08x", (unsigned)(platform_tick_ms() ^ 0x6E654675));
    term_ext_print(t, out);
}

static void cmd_tty(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    term_ext_print(t, "/dev/console");
}

static void cmd_getconf(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: getconf <key>  (PAGE_SIZE|LONG_BIT|WORD_BIT|NAME_MAX|PATH_MAX)"); return; }
    if (strcmp(argv[1], "PAGE_SIZE") == 0) term_ext_print(t, "4096");
    else if (strcmp(argv[1], "LONG_BIT") == 0) term_ext_print(t, "64");
    else if (strcmp(argv[1], "WORD_BIT") == 0) term_ext_print(t, "32");
    else if (strcmp(argv[1], "NAME_MAX") == 0) term_ext_print(t, "255");
    else if (strcmp(argv[1], "PATH_MAX") == 0) term_ext_print(t, "4096");
    else if (strcmp(argv[1], "ARG_MAX") == 0) term_ext_print(t, "65536");
    else term_ext_print(t, "getconf: unknown key");
}

static void cmd_tar(TermState* t, int argc, const char** argv) {
    // tar -t <file> | tar -x <file> <destdir>  (ustar subset)
    bool list = true;
    const char* file = 0;
    const char* dest = "/tmp";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0) list = false;
        else if (strcmp(argv[i], "-t") == 0) list = true;
        else if (!file) file = argv[i];
        else dest = argv[i];
    }
    if (!file) { term_ext_print(t, "usage: tar -t <file> | tar -x <file> [destdir]"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "tar: no such file"); return; }
    uint32_t off = 0;
    int count = 0;
    while (off + 512 <= n) {
        const uint8_t* h = d + off;
        if (h[0] == 0) break; // end marker
        // name (0..99) or prefix(345..499)+name
        char name[256];
        int nl = 0;
        while (nl < 100 && h[nl]) name[nl] = (char)h[nl], nl++;
        if (nl == 100 || h[nl] == 0) {
            if (nl >= 100) {
                // try prefix
                int pl = 0;
                while (pl < 155 && h[345 + pl]) pl++;
                if (pl > 0) {
                    char prefix[156];
                    for (int k = 0; k < pl; k++) prefix[k] = (char)h[345 + k];
                    prefix[pl] = 0;
                    name[nl] = 0;
                    char tmp[400];
                    ksprintf(tmp, sizeof(tmp), "%s/%s", prefix, name);
                    nl = (int)strlen(tmp);
                    for (int k = 0; k < nl; k++) name[k] = tmp[k];
                }
            }
        }
        name[nl] = 0;
        // size octal at 124..135
        unsigned long sz = 0;
        for (int k = 0; k < 12; k++) {
            char c = (char)h[124 + k];
            if (c >= '0' && c <= '7') sz = sz * 8 + (unsigned long)(c - '0');
        }
        char type = (char)h[156];
        if (list) {
            char out[300];
            if (type == '5') ksprintf(out, sizeof(out), "drwxr-xr-x  %s/", name);
            else ksprintf(out, sizeof(out), "-rw-r--r--  %lu  %s", sz, name);
            term_ext_print(t, out);
        } else {
            // extract
            char full[512];
            if (dest[0] == '/') ksprintf(full, sizeof(full), "%s/%s", dest, name);
            else ksprintf(full, sizeof(full), "/tmp/%s", name);
            if (type == '5') {
                g_vfs->mkdir(full);
            } else {
                cmd_write_file(full, d + off + 512, (uint32_t)sz);
                char msg[256];
                ksprintf(msg, sizeof(msg), "extracted %s (%lu bytes)", full, sz);
                term_ext_print(t, msg);
            }
        }
        count++;
        off += 512 + (uint32_t)((sz + 511) / 512 * 512);
    }
    char sum[64];
    ksprintf(sum, sizeof(sum), "tar: %d entries", count);
    term_ext_print(t, sum);
    kfree(d);
}

static void cmd_gzip(TermState* t, int argc, const char** argv) {
    bool decompress = false;
    const char* file = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) decompress = true;
        else file = argv[i];
    }
    if (!file) { term_ext_print(t, "usage: gzip [-d] <file>"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "gzip: no such file"); return; }
    if (!decompress) {
        // gzip header: 1f 8b 08 00 mtime(4) 00 02 ff
        int cap = n + n / 8 + 128;
        uint8_t* out = (uint8_t*)kalloc((size_t)cap);
        if (!out) { kfree(d); return; }
        out[0] = 0x1F; out[1] = 0x8B; out[2] = 0x08; out[3] = 0x00;
        out[4] = out[5] = out[6] = out[7] = 0;
        out[8] = 0x02; out[9] = 0xFF;
        int rl = deflate::raw_compress(d, n, out + 10, (size_t)(cap - 10 - 8));
        if (rl < 0) { kfree(d); kfree(out); term_ext_print(t, "gzip: compression failed"); return; }
        uint32_t crc = hash::crc32(d, n);
        int total = 10 + rl;
        out[total + 0] = (uint8_t)crc;
        out[total + 1] = (uint8_t)(crc >> 8);
        out[total + 2] = (uint8_t)(crc >> 16);
        out[total + 3] = (uint8_t)(crc >> 24);
        out[total + 4] = (uint8_t)n;
        out[total + 5] = (uint8_t)(n >> 8);
        out[total + 6] = (uint8_t)(n >> 16);
        out[total + 7] = (uint8_t)(n >> 24);
        char dst[512];
        ksprintf(dst, sizeof(dst), "%s.gz", file);
        cmd_write_file(dst, out, (uint32_t)(total + 8));
        char msg[128];
        ksprintf(msg, sizeof(msg), "gzip: %u -> %d bytes (%s)", (unsigned)n, total + 8, dst);
        term_ext_print(t, msg);
        kfree(out);
    } else {
        // decompress: support gzip and zlib magic
        int rl = -1;
        if (n >= 2 && d[0] == 0x1F && d[1] == 0x8B) {
            // gzip: 10-byte header + deflate + crc32 + isize
            if (n >= 18) {
                uint8_t* out = (uint8_t*)kalloc(n * 4 + 1024);
                if (out) {
                    rl = deflate::raw_decompress(d + 10, n - 18, out, n * 4 + 1024);
                    if (rl >= 0) {
                        uint32_t crc = hash::crc32(out, (size_t)rl);
                        uint32_t want = (uint32_t)d[n - 8] | ((uint32_t)d[n - 7] << 8) | ((uint32_t)d[n - 6] << 16) | ((uint32_t)d[n - 5] << 24);
                        if (crc != want) { term_ext_print(t, "gzip: crc mismatch"); kfree(out); kfree(d); return; }
                        char dst[512];
                        if (strlen(file) > 3 && strcmp(file + strlen(file) - 3, ".gz") == 0) {
                            int l = (int)strlen(file) - 3;
                            for (int k = 0; k < l; k++) dst[k] = file[k];
                            dst[l] = 0;
                        } else ksprintf(dst, sizeof(dst), "%s.out", file);
                        cmd_write_file(dst, out, (uint32_t)rl);
                        char msg[128];
                        ksprintf(msg, sizeof(msg), "gunzip: %d bytes -> %s", rl, dst);
                        term_ext_print(t, msg);
                    }
                    kfree(out);
                }
            }
        } else {
            // try zlib
            uint8_t* out = (uint8_t*)kalloc(n * 4 + 1024);
            if (out) {
                rl = deflate::zlib_decompress(d, n, out, n * 4 + 1024);
                if (rl >= 0) {
                    char dst[512];
                    if (strlen(file) > 2 && strcmp(file + strlen(file) - 2, ".z") == 0) {
                        int l = (int)strlen(file) - 2;
                        for (int k = 0; k < l; k++) dst[k] = file[k];
                        dst[l] = 0;
                    } else ksprintf(dst, sizeof(dst), "%s.out", file);
                    cmd_write_file(dst, out, (uint32_t)rl);
                    char msg[128];
                    ksprintf(msg, sizeof(msg), "gunzip: %d bytes -> %s", rl, dst);
                    term_ext_print(t, msg);
                }
                kfree(out);
            }
        }
        if (rl < 0) term_ext_print(t, "gzip -d: cannot decompress");
    }
    kfree(d);
}

static void cmd_json(TermState* t, int argc, const char** argv) {
    const char* file = argc > 1 ? argv[1] : 0;
    if (!file) { term_ext_print(t, "usage: json <file>   (validate + pretty print)"); return; }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(file, d, n)) { term_ext_print(t, "json: no such file"); return; }
    json::Value v;
    const char* err = 0;
    if (!json::parse((const char*)d, v, &err)) {
        char out[256];
        ksprintf(out, sizeof(out), "json: parse error: %s", err ? err : "unknown");
        term_ext_print(t, out);
        kfree(d);
        return;
    }
    String pretty;
    json::to_string(v, pretty, true);
    cmd_write_file(file, (const uint8_t*)pretty.c_str(), (uint32_t)pretty.len());
    char out[128];
    ksprintf(out, sizeof(out), "json: valid, %d bytes pretty-printed", (unsigned)pretty.len());
    term_ext_print(t, out);
    // print first 20 lines
    int lines = 0;
    char* p = (char*)pretty.data();
    char* st = p;
    for (int i = 0; i < pretty.len() && lines < 20; i++) {
        if (p[i] == '\n') {
            p[i] = 0;
            term_ext_print(t, st);
            p[i] = '\n';
            st = p + i + 1;
            lines++;
        }
    }
    if (lines < 20 && pretty.len()) term_ext_print(t, st);
    kfree(d);
}

static void cmd_match(TermState* t, int argc, const char** argv) {
    // match <regex> <file>  — print lines matching (regex grep)
    if (argc < 3) { term_ext_print(t, "usage: match <regex> <file>"); return; }
    const char* pat = argv[1];
    if (!regex::compile(pat)) {
        term_ext_print(t, "match: bad regex");
        return;
    }
    uint8_t* d; uint32_t n;
    if (!cmd_read_file(argv[2], d, n)) { term_ext_print(t, "match: no such file"); return; }
    char* p = (char*)d;
    char* st = p;
    int hits = 0;
    for (uint32_t i = 0; i <= n; i++) {
        if (i == n || p[i] == '\n') {
            char save = p[i];
            p[i] = 0;
            if (regex::contains(st)) { term_ext_print(t, st); hits++; }
            if (i == n) break;
            p[i] = save;
            st = p + i + 1;
        }
    }
    char msg[64];
    ksprintf(msg, sizeof(msg), "match: %d lines", hits);
    term_ext_print(t, msg);
    kfree(d);
}

static void cmd_bigcalc(TermState* t, int argc, const char** argv) {
    // bigcalc <a> <op> <b>   op = + - * / % ^
    if (argc < 4) { term_ext_print(t, "usage: bigcalc <a> <op> <b>   (arbitrary precision, op=+ - * / % ^)"); return; }
    bignum::BigInt a, b;
    a.from_string(argv[1], 10);
    b.from_string(argv[3], 10);
    bignum::BigInt r;
    char op = argv[2][0];
    switch (op) {
    case '+': r = bignum::big_add(a, b); break;
    case '-': r = bignum::big_sub(a, b); break;
    case '*': r = bignum::big_mul(a, b); break;
    case '/': { bignum::BigInt q, rem; if (!bignum::big_divmod(a, b, q, rem)) { term_ext_print(t, "bigcalc: div by zero"); return; } r = q; break; }
    case '%': { bignum::BigInt q, rem; if (!bignum::big_divmod(a, b, q, rem)) { term_ext_print(t, "bigcalc: mod by zero"); return; } r = rem; break; }
    case '^': r = bignum::big_pow(a, b); break;
    default: term_ext_print(t, "bigcalc: bad operator"); return;
    }
    char out[2048];
    r.to_string(out, sizeof(out), 10);
    term_ext_print(t, out);
}

static void cmd_math(TermState* t, int argc, const char** argv) {
    // math <op> <value>  — fixed-point math demo
    if (argc < 3) {
        term_ext_print(t, "usage: math <op> <value>   op = sqrt|sin|cos|tan|exp|ln|log10");
        term_ext_print(t, "  values in radians (or plain integers for sqrt/ln)");
        return;
    }
    const char* op = argv[1];
    fx::fix x = fx::fxf(atoi(argv[2]), 1);
    // support decimals "1.5" -> 1 + 0.5
    const char* dot = strchr(argv[2], '.');
    if (dot) {
        int ip = atoi(argv[2]);
        int frac = atoi(dot + 1);
        int fdigits = 0;
        const char* q = dot + 1;
        while (*q >= '0' && *q <= '9') { fdigits++; q++; }
        fx::fix f = 0;
        if (fdigits > 0 && frac > 0) {
            int den = 1;
            for (int i = 0; i < fdigits; i++) den *= 10;
            f = fx::fxf(frac, den);
        }
        x = fx::itofix(ip) + (ip < 0 ? -f : f);
    }
    fx::fix r = 0;
    if (strcmp(op, "sqrt") == 0) r = fx::fx_sqrt(x);
    else if (strcmp(op, "sin") == 0) r = fx::fx_sin(x);
    else if (strcmp(op, "cos") == 0) r = fx::fx_cos(x);
    else if (strcmp(op, "tan") == 0) r = fx::fx_tan(x);
    else if (strcmp(op, "exp") == 0) r = fx::fx_exp(x);
    else if (strcmp(op, "ln") == 0) r = fx::fx_ln(x);
    else if (strcmp(op, "log10") == 0) r = fx::fx_log10(x);
    else if (strcmp(op, "atan") == 0) r = fx::fx_atan(x);
    else { term_ext_print(t, "math: unknown op"); return; }
    // print as fixed point with 4 decimals
    bool neg = r < 0;
    int ip = (int)(r >> 16);
    int fp = (int)((r & 0xFFFF) * 10000 / 65536);
    if (neg) { ip = -ip; }
    char out[64];
    if (neg) ksprintf(out, sizeof(out), "-%d.%04d", ip, fp);
    else ksprintf(out, sizeof(out), "%d.%04d", ip, fp);
    term_ext_print(t, out);
}

static void cmd_hashstr(TermState* t, int argc, const char** argv) {
    // hash <algo> <string>  algo = md5|sha1|sha256|crc32
    if (argc < 3) { term_ext_print(t, "usage: hash <md5|sha1|sha256|crc32> <string>"); return; }
    const char* algo = argv[1];
    const char* s = argv[2];
    char out[200];
    if (strcmp(algo, "md5") == 0) {
        hash::MD5 m;
        m.update(s, strlen(s));
        char hex[33];
        m.hex_final(hex);
        ksprintf(out, sizeof(out), "%s  %s", hex, s);
    } else if (strcmp(algo, "sha1") == 0) {
        hash::SHA1 sh;
        sh.update(s, strlen(s));
        char hex[41];
        sh.hex_final(hex);
        ksprintf(out, sizeof(out), "%s  %s", hex, s);
    } else if (strcmp(algo, "sha256") == 0) {
        uint8_t digest[32];
        nefu_sha256(s, (uint32_t)strlen(s), digest);
        char hex[65];
        nefu_sha256_hex(digest, hex);
        ksprintf(out, sizeof(out), "%s  %s", hex, s);
    } else if (strcmp(algo, "crc32") == 0) {
        uint32_t c = hash::crc32(s, strlen(s));
        ksprintf(out, sizeof(out), "%08x  %s", (unsigned)c, s);
    } else {
        term_ext_print(t, "hash: unknown algorithm");
        return;
    }
    term_ext_print(t, out);
}

static void cmd_prime_check(TermState* t, int argc, const char** argv) {
    const char* ns = argc > 1 ? argv[1] : 0;
    if (!ns) { term_ext_print(t, "usage: isprime <number>"); return; }
    uint64_t n = 0;
    const char* p = ns;
    while (*p >= '0' && *p <= '9') n = n * 10 + (uint64_t)(*p - '0');
    char out[128];
    if (prime::is_prime_u64(n)) ksprintf(out, sizeof(out), "%llu is prime", (unsigned long long)n);
    else ksprintf(out, sizeof(out), "%llu is NOT prime", (unsigned long long)n);
    term_ext_print(t, out);
}

static void cmd_nextprime(TermState* t, int argc, const char** argv) {
    const char* ns = argc > 1 ? argv[1] : 0;
    if (!ns) { term_ext_print(t, "usage: nextprime <number>"); return; }
    uint64_t n = 0;
    const char* p = ns;
    while (*p >= '0' && *p <= '9') n = n * 10 + (uint64_t)(*p - '0');
    char out[128];
    ksprintf(out, sizeof(out), "%llu", (unsigned long long)prime::next_prime_u64(n));
    term_ext_print(t, out);
}

static void cmd_selftest_ext(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int pass = 0, fail = 0;
    // json
    {
        json::Value v;
        if (json::parse("{\"a\":1,\"b\":[true,null,\"x\"],\"c\":{\"d\":2.5}}", v) && v.get_int("a") == 1) pass++; else fail++;
        String s;
        json::to_string(v, s, false);
        json::Value v2;
        if (json::parse(s, v2) && v2.size() == 3) pass++; else fail++;
    }
    // regex
    {
        if (regex::match_full("^a[0-9]+$", "a12345")) pass++; else fail++;
        if (regex::match_anywhere("colou?r", "the color is red")) pass++; else fail++;
        if (!regex::match_anywhere("^b", "abc")) pass++; else fail++;
        if (regex::match_anywhere("(ab|cd)+", "xycdabz")) pass++; else fail++;
        if (regex::match_full("[a-z]{3}", "xyz")) pass++; else fail++;
    }
    // hash
    {
        char hex[33];
        hash::MD5 m;
        m.update("abc", 3);
        m.hex_final(hex);
        if (strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0) pass++; else fail++;
        hash::SHA1 sh;
        sh.update("abc", 3);
        char hex2[41];
        sh.hex_final(hex2);
        if (strcmp(hex2, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0) pass++; else fail++;
        if (hash::crc32("123456789", 9) == 0xCBF43926u) pass++; else fail++;
        if (hash::adler32("Wikipedia", 9, 1) == 0x11E60398u) pass++; else fail++;
    }
    // deflate round trip
    {
        const char* txt = "nefuOS deflate round-trip test: ";
        char big[4096];
        int n = 0;
        for (int i = 0; i < 40; i++) {
            const char* w = "The quick brown fox jumps over the lazy dog. ";
            for (const char* p = w; *p && n < 4000; p++) big[n++] = *p;
        }
        uint8_t comp[8192];
        uint8_t decomp[8192];
        int cl = deflate::zlib_compress((const uint8_t*)big, (size_t)n, comp, sizeof(comp));
        if (cl > 0) {
            int dl = deflate::zlib_decompress(comp, (size_t)cl, decomp, sizeof(decomp));
            if (dl == n && memcmp(big, decomp, (size_t)n) == 0) pass++;
            else fail++;
        } else fail++;
        (void)txt;
    }
    // bigint
    {
        bignum::BigInt a, b, r;
        a.from_string("123456789012345678901234567890", 10);
        b.from_string("987654321098765432109876543210", 10);
        r = bignum::big_mul(a, b);
        char out[256];
        r.to_string(out, sizeof(out), 10);
        if (strcmp(out, "121932631137021795226185032733622923332237463801111263526900") == 0) pass++;
        else fail++;
        bignum::BigInt p(97);
        if (bignum::big_is_prime(p, 4)) pass++; else fail++;
        bignum::BigInt np(91);
        if (!bignum::big_is_prime(np, 4)) pass++; else fail++;
    }
    // fixed point
    {
        if (fx::fixtoi(fx::fx_sqrt(fx::itofix(144))) == 12) pass++; else fail++;
        fx::fix s1 = fx::fx_sin(fx::FX_PI_2);
        if (fx::fixtoi(s1) == 1) pass++; else fail++;
    }
    char out[128];
    ksprintf(out, sizeof(out), "ext selftest: %d passed, %d failed", pass, fail);
    term_ext_print(t, out);
}

static void cmd_algotest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    // run every algorithm library self test and report a combined verdict
    int sort_f  = algo::sort_self_test();
    int search_f = algo::search_self_test();
    int graph_f = algo::graph_self_test();
    int ds_f    = algo::ds_self_test();
#ifdef NEFU_BARE
    // the bare kernel has no FPU: the double-based numeric library is
    // host-only, so its self test is skipped there
    char out[160];
    ksprintf(out, sizeof(out),
             "algo self tests: sort=%d search=%d graph=%d ds=%d (numeric skipped on bare)",
             sort_f, search_f, graph_f, ds_f);
    term_ext_print(t, out);
    if (sort_f + search_f + graph_f + ds_f == 0)
        term_ext_print(t, "ALL ALGORITHM TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
#else
    int num_f   = num::numeric_self_test();
    char out[160];
    ksprintf(out, sizeof(out),
             "algo self tests: sort=%d search=%d graph=%d ds=%d numeric=%d failures",
             sort_f, search_f, graph_f, ds_f, num_f);
    term_ext_print(t, out);
    if (sort_f + search_f + graph_f + ds_f + num_f == 0)
        term_ext_print(t, "ALL ALGORITHM TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
#endif
}

// ---- 浠跨湡寮曟搸鍛戒护 ----
static void cmd_life(TermState* t, int argc, const char** argv) {
    int steps = 10;
    if (argc > 1) steps = atoi(argv[1]);
    if (steps < 1) steps = 1; if (steps > 500) steps = 500;
    simulate::GameOfLife life;
    life.init(40, 24, simulate::Boundary::Torus);
    life.glider();
    for (int i = 0; i < steps; i++) life.step();
    char out[96];
    ksprintf(out, sizeof(out), "life: ran %d generations, live cells=%d", steps, life.count_live());
    term_ext_print(t, out);
    life.shutdown();
}
static void cmd_particle(TermState* t, int argc, const char** argv) {
    int n = 200; if (argc > 1) n = atoi(argv[1]);
    if (n < 1) n = 1; if (n > 5000) n = 5000;
    simulate::ParticleSystem sys;
    sys.init(n, 2, 2, 1, 640, 480);
    simulate::Emitter e; e.type = simulate::EmitterType::Point;
    e.origin = simulate::Vec2(320, 50); e.rate = 500;
    e.speed_min = 40; e.speed_max = 120; e.life_min = 1; e.life_max = 2;
    sys.add_emitter(e);
    simulate::ForceField g; g.type = simulate::ForceType::Gravity; g.vector = simulate::Vec2(0, 150);
    sys.add_field(g); sys.update(1.0);
    char out[96];
    ksprintf(out, sizeof(out), "particle: after 1.0s alive=%d", sys.alive_count());
    term_ext_print(t, out); sys.shutdown();
}
static void cmd_pendulum(TermState* t, int argc, const char** argv) {
    double len = 1.0; int steps = 200;
    if (argc > 1) len = atof(argv[1]);
    if (argc > 2) steps = atoi(argv[2]);
    if (len < 0.1) len = 0.1; if (len > 5) len = 5;
    simulate::Pendulum p; p.init(len, 0.5);
    for (int i = 0; i < steps; i++) p.step(0.01);
    char out[96];
    ksprintf(out, sizeof(out), "pendulum: L=%.2f after %d steps angle=%.3f", len, steps, p.angle);
    term_ext_print(t, out);
}
static void cmd_lsystem(TermState* t, int argc, const char** argv) {
    int it = 2; if (argc > 1) it = atoi(argv[1]);
    if (it < 1) it = 1; if (it > 6) it = 6;
    simulate::LSystem ls; ls.init(); ls.preset_koch(); ls.iterate(it);
    char out[96];
    ksprintf(out, sizeof(out), "lsystem: Koch %d iters, F segments=%d, len=%d", it, ls.count_symbol('F'), ls.out_len);
    term_ext_print(t, out); ls.shutdown();
}
static void cmd_boids(TermState* t, int argc, const char** argv) {
    int n = 30; int steps = 100;
    if (argc > 1) n = atoi(argv[1]);
    if (argc > 2) steps = atoi(argv[2]);
    if (n < 2) n = 2; if (n > 200) n = 200;
    simulate::Flocking f; f.init(256, 4, 640, 480);
    for (int i = 0; i < n; i++) {
        simulate::Boid b;
        b.pos = simulate::Vec2(100 + (i * 37) % 400, 100 + (i * 53) % 280);
        b.vel = simulate::Vec2(40, (i % 3 - 1) * 20);
        f.add_boid(b);
    }
    for (int i = 0; i < steps; i++) f.step(0.016);
    char out[128];
    ksprintf(out, sizeof(out), "boids: %d agents, avg_speed=%.1f dispersion=%.1f", n, f.avg_speed(), f.dispersion());
    term_ext_print(t, out); f.shutdown();
}
static void cmd_simtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int ca = simulate::ca_self_test();
    int p  = simulate::particle_self_test();
    int ph = simulate::physics_self_test();
    int fl = simulate::fluid_self_test();
    int ls = simulate::lsystem_self_test();
    int fp = simulate::flocking_self_test();
    char out[192];
    ksprintf(out, sizeof(out), "simulate: ca=%d particle=%d physics=%d fluid=%d lsystem=%d flocking=%d fails", ca, p, ph, fl, ls, fp);
    term_ext_print(t, out);
    if (ca+p+ph+fl+ls+fp == 0) term_ext_print(t, "ALL SIMULATE TESTS PASSED");
    else term_ext_print(t, "FAILURES DETECTED");
}

static void cmd_gfxtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    // run every graphics library self test and report a combined verdict
    int rast_f = gfxlib::raster_self_test();
    int geo_f  = gfxlib::geo_self_test();
    int tra_f  = gfxlib::transform_self_test();
    int noi_f  = gfxlib::noise_self_test();
    int col_f  = gfxlib::color_self_test();
    char out[160];
    ksprintf(out, sizeof(out),
             "gfxlib self tests: raster=%d geo=%d transform=%d noise=%d color=%d failures",
             rast_f, geo_f, tra_f, noi_f, col_f);
    term_ext_print(t, out);
    if (rast_f + geo_f + tra_f + noi_f + col_f == 0)
        term_ext_print(t, "ALL GRAPHICS TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_texttest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    // run every text library self test and report a combined verdict
    int lev_f = text::levenshtein_self_test();
    int lcs_f = text::lcs_self_test();
    int sea_f = text::search_self_test();
    int aho_f = text::aho_self_test();
    int tok_f = text::token_self_test();
    int ngr_f = text::ngram_self_test();
    int reg_f = text::regex_self_test();
    int dif_f = text::diff_self_test();
    char out[200];
    ksprintf(out, sizeof(out),
             "textlib self tests: lev=%d lcs=%d search=%d aho=%d token=%d ngram=%d regex=%d diff=%d failures",
             lev_f, lcs_f, sea_f, aho_f, tok_f, ngr_f, reg_f, dif_f);
    term_ext_print(t, out);
    if (lev_f + lcs_f + sea_f + aho_f + tok_f + ngr_f + reg_f + dif_f == 0)
        term_ext_print(t, "ALL TEXT TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_datlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    // run every data-structure library self test and report a combined verdict
    int v_f  = dt::vec_self_test();
    int r_f  = dt::ringbuf_self_test();
    int b_f  = dt::bitset_self_test();
    int h_f  = dt::hashtab_self_test();
    int rb_f = dt::rbtree_self_test();
    int tp_f = dt::treap_self_test();
    int bt_f = dt::btree_self_test();
    int sg_f = dt::segtree_self_test();
    int fw_f = dt::fenwick_self_test();
    int bl_f = dt::bloom_self_test();
    int lr_f = dt::lru_self_test();
    int sl_f = dt::sortedlist_self_test();
    int iq_f = dt::ipq_self_test();
    int op_f = dt::objpool_self_test();
    int rx_f = dt::radix_self_test();
    int tw_f = dt::timerwheel_self_test();
    char out[240];
    ksprintf(out, sizeof(out),
             "datlib self tests: vec=%d ring=%d bits=%d hash=%d rb=%d treap=%d btree=%d seg=%d fw=%d bloom=%d lru=%d sl=%d ipq=%d pool=%d radix=%d tw=%d failures",
             v_f, r_f, b_f, h_f, rb_f, tp_f, bt_f, sg_f, fw_f, bl_f, lr_f, sl_f, iq_f, op_f, rx_f, tw_f);
    term_ext_print(t, out);
    int total = v_f + r_f + b_f + h_f + rb_f + tp_f + bt_f + sg_f +
                fw_f + bl_f + lr_f + sl_f + iq_f + op_f + rx_f + tw_f;
    if (total == 0)
        term_ext_print(t, "ALL DATA TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_dblibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every data-storage library self test and report a combined verdict
    int total = nefu::dbx::db_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "dblib self tests: csv+ini+json+bptree+page+table = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL DATA TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_audlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every audio library self test and report a combined verdict
    int total = nefu::audx::aud_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "audlib self tests: wave+synth+env+filter+seq+mixer+delay+reverb+mod+pitch = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL AUDIO TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_gfxmathtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every geometry math library self test and report a combined verdict
    int total = nefu::gfx::gfx_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "gfxmath self tests: vec3+mat4+quat+ray+plane+aabb+sphere+camera+mesh+proj+render = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL GFX MATH TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_simlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every simulation library self test and report a combined verdict
    int total = nefu::simx::sim_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "simlib self tests: life+queue+world+boids+epidemic+traffic+perlin+langton = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL SIM TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_mathlibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    (void)t;
    // run every math library self test and report a combined verdict
    int total = nefu::mathx::math_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "mathlib self tests: bigint+rational+complex+matrix+fft+poly+stat+regress+prime+rand+vec2+gcd = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL MATH TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_complibtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    // run every compression library self test and report a combined verdict
    int total = nefu::comp::comp_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "complib self tests: bitio+rle+huffman+lz77+lzw+arithmetic+bwt = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL COMPRESSION TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}

static void cmd_crypttest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    // run every crypto library self test and report a combined verdict
    int total = nefu::crypt::crypt_all_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "cryptlib self tests: sha256+sha1+md5+crc+b64+hmac+pbkdf2+aes+rc4+xor = %d failures", total);
    term_ext_print(t, out);
    if (total == 0)
        term_ext_print(t, "ALL CRYPTO TESTS PASSED");
    else
        term_ext_print(t, "FAILURES DETECTED - see numbers above");
}


// ===================== textproc 命令 =====================
static void cmd_editdist(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: editdist <s1> <s2>"); return; }
    int d = nefu::textproc::levenshtein(argv[1], argv[2]);
    char out[64];
    ksprintf(out, sizeof(out), "levenshtein(%s,%s) = %d", argv[1], argv[2], d);
    term_ext_print(t, out);
}

static void cmd_lcs(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: lcs <s1> <s2>"); return; }
    int len = nefu::textproc::lcs_length(argv[1], argv[2]);
    char* s = nefu::textproc::lcs_string(argv[1], argv[2]);
    char out[200];
    ksprintf(out, sizeof(out), "lcs len=%d  lcs=%s", len, s);
    delete[] s;
    term_ext_print(t, out);
}

static void cmd_kmp(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: kmp <pattern> <text>"); return; }
    int c;
    int* hits = nefu::textproc::kmp_search(argv[1], argv[2], &c);
    char out[256];
    ksprintf(out, sizeof(out), "kmp '%s' in '%s': %d hit(s)", argv[1], argv[2], c);
    term_ext_print(t, out);
    for (int i = 0; i < c && i < 10; i++) {
        ksprintf(out, sizeof(out), "  @ %d", hits[i]);
        term_ext_print(t, out);
    }
    delete[] hits;
}

static void cmd_wordfreq(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: wordfreq <file>"); return; }
    uint8_t* d = 0; uint32_t n = 0;
    if (!cmd_read_file(argv[1], d, n)) { term_ext_print(t, "wordfreq: no such file"); return; }
    char* txt = new char[n + 1];
    for (uint32_t i = 0; i < n; i++) txt[i] = (char)d[i];
    txt[n] = 0;
    nefu::textproc::WordFreq* wf = nefu::textproc::word_freq(txt);
    char* rep = nefu::textproc::word_freq_report(wf);
    term_ext_print(t, rep);
    delete[] rep;
    nefu::textproc::word_freq_free(wf);
    delete[] txt;
    kfree(d);
}

static void cmd_ngram(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: ngram <n> <text>"); return; }
    int nn = atoi(argv[1]);
    int c;
    char** g = nefu::textproc::ngram_words(argv[2], nn, &c);
    char out[128];
    ksprintf(out, sizeof(out), "%d %d-grams:", c, nn);
    term_ext_print(t, out);
    for (int i = 0; i < c && i < 20; i++) {
        ksprintf(out, sizeof(out), "  %s", g[i]);
        term_ext_print(t, out);
    }
    nefu::textproc::ngram_free(g, c);
}

static void cmd_soundex(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: soundex <word>"); return; }
    char code[5];
    nefu::textproc::soundex(argv[1], code);
    char out[64];
    ksprintf(out, sizeof(out), "soundex(%s) = %s", argv[1], code);
    term_ext_print(t, out);
}

static void cmd_tptest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::textproc::textproc_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "textproc self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL TEXTPROC TESTS PASSED" : "TEXTPROC FAILURES DETECTED");
}

// ===================== batch1 新库自检命令 =====================
static void cmd_cryptotest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::crypto::crypto_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "crypto self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL CRYPTO TESTS PASSED" : "CRYPTO FAILURES DETECTED");
}

static void cmd_comptest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::compress::compress_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "compress self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL COMPRESS TESTS PASSED" : "COMPRESS FAILURES DETECTED");
}

static void cmd_serialtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::serialize::serialize_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "serialize self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL SERIALIZE TESTS PASSED" : "SERIALIZE FAILURES DETECTED");
}

static void cmd_audtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::audsp::audsp_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "audsp self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL AUDSP TESTS PASSED" : "AUDSP FAILURES DETECTED");
}

static void cmd_gfx3dtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::gfx3d::gfx3d_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "gfx3d self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL GFX3D TESTS PASSED" : "GFX3D FAILURES DETECTED");
}

static void cmd_mathtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::mathext::mathext_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "mathext self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL MATHEXT TESTS PASSED" : "MATHEXT FAILURES DETECTED");
}

// ===================== uiwidgets 自检 =====================
static void cmd_uitest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::ui::uiwidgets_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "uiwidgets self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL UIWIDGETS TESTS PASSED" : "UIWIDGETS FAILURES DETECTED");
}

// ===================== database 命令 =====================
static nefu::database::KVStore* g_term_kv = 0;
static nefu::database::Database* g_term_db = 0;
static nefu::database::KVStore* term_kv() { if(!g_term_kv) g_term_kv = new nefu::database::KVStore(); return g_term_kv; }
static nefu::database::Database* term_db() { if(!g_term_db) g_term_db = new nefu::database::Database(); return g_term_db; }

static void cmd_kvput(TermState* t, int argc, const char** argv) {
    if (argc < 3) { term_ext_print(t, "usage: kvput <key> <value>"); return; }
    term_kv()->put(nefu::String(argv[1]), nefu::String(argv[2]));
    term_ext_print(t, "ok");
}
static void cmd_kvget(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: kvget <key>"); return; }
    nefu::String v;
    if (term_kv()->get(nefu::String(argv[1]), &v)) term_ext_print(t, v.c_str());
    else term_ext_print(t, "(not found)");
}
static void cmd_kvdel(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: kvdel <key>"); return; }
    term_ext_print(t, term_kv()->del(nefu::String(argv[1])) ? "ok" : "(not found)");
}
static void cmd_sql(TermState* t, int argc, const char** argv) {
    if (argc < 2) { term_ext_print(t, "usage: sql \"<statement>\""); return; }
    nefu::database::SqlResult r = term_db()->exec(argv[1]);
    if (!r.ok) { term_ext_print(t, r.error.c_str()); return; }
    nefu::String hdr;
    for (int i=0;i<r.col_names.size();i++){ if(i) hdr += " | "; hdr += r.col_names[i]; }
    term_ext_print(t, hdr.c_str());
    for (int i=0;i<r.rows.size();i++){
        nefu::String line;
        for (int c=0;c<r.rows[i].cells.size();c++){ if(c) line+=" | "; line += r.rows[i].cells[c]; }
        term_ext_print(t, line.c_str());
    }
}
static void cmd_dbtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::database::database_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "database self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL DATABASE TESTS PASSED" : "DATABASE FAILURES DETECTED");
}

static void cmd_mltest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::ml::ml_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "ml self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL ML TESTS PASSED" : "ML FAILURES DETECTED");
}

static void cmd_termcmdstest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::termcmds::termcmds_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "termcmds self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL TERMCMDS TESTS PASSED" : "TERMCMDS FAILURES");
}

static void cmd_minilangtest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::minilang::minilang_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "minilang self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL MINILANG TESTS PASSED" : "MINILANG FAILURES");
}

static void cmd_sysutiltest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::sysutil::sysutil_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "sysutil self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL SYSUTIL TESTS PASSED" : "SYSUTIL FAILURES");
}

static void cmd_netprototest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::netproto::netproto_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "netproto self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL NETPROTO TESTS PASSED" : "NETPROTO FAILURES");
}

static void cmd_fstest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::filesystem::filesystem_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "filesystem self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL FILESYSTEM TESTS PASSED" : "FILESYSTEM FAILURES");
}

static void cmd_raytest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::raytrace::raytrace_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "raytrace self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL RAYTRACE TESTS PASSED" : "RAYTRACE FAILURES");
}

static void cmd_compilertest(TermState* t, int argc, const char** argv) {
    (void)argc; (void)argv;
    int f = nefu::compiler::compiler_self_test();
    char out[160];
    ksprintf(out, sizeof(out), "compiler self test failures = %d", f);
    term_ext_print(t, out);
    term_ext_print(t, f == 0 ? "ALL COMPILER TESTS PASSED" : "COMPILER FAILURES");
}









// ===================== registry =====================

static const char* EXT_COMMANDS[] = {
    "cut", "paste", "tr", "expand", "fold", "nl", "hexdump", "xxd", "strings",
    "base64", "md5sum", "sha1sum", "sha256sum", "crc32", "cksum", "cmp", "comm", "diff",
    "expr", "factor", "primes", "split", "realpath", "chmod", "chown", "chgrp",
    "nproc", "arch", "logname", "groups", "hostid", "tty", "getconf",
    "tar", "gzip", "json", "match", "bigcalc", "math", "hash", "isprime",
    "nextprime", "extselftest", "algotest", "gfxtest", "texttest", "datlibtest", "crypttest", "comptest", "complibtest", "mathlibtest", "simlibtest", "gfxmathtest", "audlibtest", "dblibtest",
    "cryptotest", "comptest", "serialtest", "audtest", "gfx3dtest",
    "life", "particle", "pendulum", "lsystem", "boids", "simtest",
    "mathtest", "uitest", "kvput", "kvget", "kvdel", "sql", "dbtest", "mltest", "termcmdstest", "minilangtest", "sysutiltest", "netprototest", "fstest", "raytest", "compilertest"
};
static const int EXT_COUNT = (int)(sizeof(EXT_COMMANDS) / sizeof(EXT_COMMANDS[0]));

bool term_ext_handles(const char* cmd) {
    if (!cmd) return false;
    for (int i = 0; i < EXT_COUNT; i++) {
        if (strcmp(cmd, EXT_COMMANDS[i]) == 0) return true;
    }
    return false;
}

int term_ext_count() { return EXT_COUNT; }
const char* term_ext_name(int i) {
    return (i >= 0 && i < EXT_COUNT) ? EXT_COMMANDS[i] : "";
}

void term_ext_dispatch(TermState* t, int argc, const char** argv) {
    if (argc < 1 || !argv[0]) return;
    const char* a0 = argv[0];
    if (strcmp(a0, "cut") == 0) cmd_cut(t, argc, argv);
    else if (strcmp(a0, "paste") == 0) cmd_paste(t, argc, argv);
    else if (strcmp(a0, "tr") == 0) cmd_tr(t, argc, argv);
    else if (strcmp(a0, "expand") == 0) cmd_expand(t, argc, argv);
    else if (strcmp(a0, "fold") == 0) cmd_fold(t, argc, argv);
    else if (strcmp(a0, "nl") == 0) cmd_nl(t, argc, argv);
    else if (strcmp(a0, "hexdump") == 0) cmd_hexdump(t, argc, argv);
    else if (strcmp(a0, "xxd") == 0) cmd_xxd(t, argc, argv);
    else if (strcmp(a0, "strings") == 0) cmd_strings(t, argc, argv);
    else if (strcmp(a0, "base64") == 0) cmd_base64(t, argc, argv);
    else if (strcmp(a0, "md5sum") == 0) cmd_md5sum(t, argc, argv);
    else if (strcmp(a0, "sha1sum") == 0) cmd_sha1sum(t, argc, argv);
    else if (strcmp(a0, "sha256sum") == 0) cmd_sha256sum(t, argc, argv);
    else if (strcmp(a0, "crc32") == 0) cmd_crc32(t, argc, argv);
    else if (strcmp(a0, "cksum") == 0) cmd_cksum(t, argc, argv);
    else if (strcmp(a0, "cmp") == 0) cmd_cmp(t, argc, argv);
    else if (strcmp(a0, "comm") == 0) cmd_comm(t, argc, argv);
    else if (strcmp(a0, "diff") == 0) cmd_diff(t, argc, argv);
    else if (strcmp(a0, "expr") == 0) cmd_expr(t, argc, argv);
    else if (strcmp(a0, "factor") == 0) cmd_factor(t, argc, argv);
    else if (strcmp(a0, "primes") == 0) cmd_primes(t, argc, argv);
    else if (strcmp(a0, "split") == 0) cmd_split(t, argc, argv);
    else if (strcmp(a0, "realpath") == 0) cmd_realpath(t, argc, argv);
    else if (strcmp(a0, "chmod") == 0) cmd_chmod(t, argc, argv);
    else if (strcmp(a0, "chown") == 0) cmd_chown(t, argc, argv);
    else if (strcmp(a0, "chgrp") == 0) cmd_chgrp(t, argc, argv);
    else if (strcmp(a0, "nproc") == 0) cmd_nproc(t, argc, argv);
    else if (strcmp(a0, "arch") == 0) cmd_arch(t, argc, argv);
    else if (strcmp(a0, "logname") == 0) cmd_logname(t, argc, argv);
    else if (strcmp(a0, "groups") == 0) cmd_groups(t, argc, argv);
    else if (strcmp(a0, "hostid") == 0) cmd_hostid(t, argc, argv);
    else if (strcmp(a0, "tty") == 0) cmd_tty(t, argc, argv);
    else if (strcmp(a0, "getconf") == 0) cmd_getconf(t, argc, argv);
    else if (strcmp(a0, "tar") == 0) cmd_tar(t, argc, argv);
    else if (strcmp(a0, "gzip") == 0) cmd_gzip(t, argc, argv);
    else if (strcmp(a0, "json") == 0) cmd_json(t, argc, argv);
    else if (strcmp(a0, "match") == 0) cmd_match(t, argc, argv);
    else if (strcmp(a0, "bigcalc") == 0) cmd_bigcalc(t, argc, argv);
    else if (strcmp(a0, "math") == 0) cmd_math(t, argc, argv);
    else if (strcmp(a0, "hash") == 0) cmd_hashstr(t, argc, argv);
    else if (strcmp(a0, "isprime") == 0) cmd_prime_check(t, argc, argv);
    else if (strcmp(a0, "nextprime") == 0) cmd_nextprime(t, argc, argv);
    else if (strcmp(a0, "extselftest") == 0) cmd_selftest_ext(t, argc, argv);
    else if (strcmp(a0, "algotest") == 0) cmd_algotest(t, argc, argv);
    else if (strcmp(a0, "gfxtest") == 0) cmd_gfxtest(t, argc, argv);
    else if (strcmp(a0, "texttest") == 0) cmd_texttest(t, argc, argv);
    else if (strcmp(a0, "datlibtest") == 0) cmd_datlibtest(t, argc, argv);
    else if (strcmp(a0, "editdist") == 0) cmd_editdist(t, argc, argv);
    else if (strcmp(a0, "lcs") == 0) cmd_lcs(t, argc, argv);
    else if (strcmp(a0, "kmp") == 0) cmd_kmp(t, argc, argv);
    else if (strcmp(a0, "wordfreq") == 0) cmd_wordfreq(t, argc, argv);
    else if (strcmp(a0, "ngram") == 0) cmd_ngram(t, argc, argv);
    else if (strcmp(a0, "soundex") == 0) cmd_soundex(t, argc, argv);
    else if (strcmp(a0, "tptest") == 0) cmd_tptest(t, argc, argv);
    else if (strcmp(a0, "crypttest") == 0) cmd_crypttest(t, argc, argv);
    else if (strcmp(a0, "comptest") == 0) cmd_comptest(t, argc, argv);
    else if (strcmp(a0, "complibtest") == 0) cmd_complibtest(t, argc, argv);
    else if (strcmp(a0, "mathlibtest") == 0) cmd_mathlibtest(t, argc, argv);
    else if (strcmp(a0, "simlibtest") == 0) cmd_simlibtest(t, argc, argv);
    else if (strcmp(a0, "gfxmathtest") == 0) cmd_gfxmathtest(t, argc, argv);
    else if (strcmp(a0, "audlibtest") == 0) cmd_audlibtest(t, argc, argv);
    else if (strcmp(a0, "dblibtest") == 0) cmd_dblibtest(t, argc, argv);
    else if (strcmp(a0, "cryptotest") == 0) cmd_cryptotest(t, argc, argv);
    else if (strcmp(a0, "comptest") == 0) cmd_comptest(t, argc, argv);
    else if (strcmp(a0, "serialtest") == 0) cmd_serialtest(t, argc, argv);
    else if (strcmp(a0, "audtest") == 0) cmd_audtest(t, argc, argv);
    else if (strcmp(a0, "gfx3dtest") == 0) cmd_gfx3dtest(t, argc, argv);
    else if (strcmp(a0, "life") == 0) cmd_life(t, argc, argv);
    else if (strcmp(a0, "particle") == 0) cmd_particle(t, argc, argv);
    else if (strcmp(a0, "pendulum") == 0) cmd_pendulum(t, argc, argv);
    else if (strcmp(a0, "lsystem") == 0) cmd_lsystem(t, argc, argv);
    else if (strcmp(a0, "boids") == 0) cmd_boids(t, argc, argv);
    else if (strcmp(a0, "simtest") == 0) cmd_simtest(t, argc, argv);
    else if (strcmp(a0, "mathtest") == 0) cmd_mathtest(t, argc, argv);
    else if (strcmp(a0, "uitest") == 0) cmd_uitest(t, argc, argv);
    else if (strcmp(a0, "kvput") == 0) cmd_kvput(t, argc, argv);
    else if (strcmp(a0, "kvget") == 0) cmd_kvget(t, argc, argv);
    else if (strcmp(a0, "kvdel") == 0) cmd_kvdel(t, argc, argv);
    else if (strcmp(a0, "sql") == 0) cmd_sql(t, argc, argv);
    else if (strcmp(a0, "dbtest") == 0) cmd_dbtest(t, argc, argv);
    else if (strcmp(a0, "mltest") == 0) cmd_mltest(t, argc, argv);
    else if (strcmp(a0, "termcmdstest") == 0) cmd_termcmdstest(t, argc, argv);
    else if (strcmp(a0, "minilangtest") == 0) cmd_minilangtest(t, argc, argv);
    else if (strcmp(a0, "sysutiltest") == 0) cmd_sysutiltest(t, argc, argv);
    else if (strcmp(a0, "netprototest") == 0) cmd_netprototest(t, argc, argv);
    else if (strcmp(a0, "fstest") == 0) cmd_fstest(t, argc, argv);
    else if (strcmp(a0, "raytest") == 0) cmd_raytest(t, argc, argv);
    else if (strcmp(a0, "compilertest") == 0) cmd_compilertest(t, argc, argv);
    else term_ext_print(t, "ext: unknown command");
}

} // namespace nefu
