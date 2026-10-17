// nefuOS application package viewer / launcher.
// Handles two formats:
// .nefud - text manifest that binds to a built-in app.
//   Its payload may be raw bytes or a ZIP archive carrying a Linux .deb layout
//   (debian-binary + DEBIAN/control + DEBIAN/* + data/...), built by
//   tools/nefupack.py deb.  The manifest keys, NEFUD1 magic, @@PAYLOAD@@
//   delimiter and trailing CRC32 are unchanged either way.
// .bin - real binary package (NEFBIN01) that either binds to a built-in
// app or carries NEFVM bytecode that actually executes here
#include "apps.h"
#include "nefvm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"
#include "../lib/zip.h"   // PK-zip payload (Linux .deb layout) reader
#include "../lib/deflate.h" // raw inflate + gzip (RFC 1952) for data.tar.gz
#include "../cryptlib/hmac.h"   // HMAC-SHA256 signature verify (Runnable .deb)
#include "../cryptlib/sha256.h" // ELF integrity digest

namespace nefu {

struct NefudState {
    FSNode* file;
    char name[48];
    char desc[96];
    char author[48];
    char ver[16];
    int app_id;
    bool is_bin;          // NEFBIN01 package
    bool is_zip;          // .nefud payload is a PK zip (Linux .deb layout)
    int zip_count;        // entries in the zip payload
    bool is_ar;           // .nefud payload is a Runnable .deb (ar) archive
    int  sig_status;      // HMAC signature: -1 no key/unknown, 0 fail, 1 ok
    bool elf_ok;          // ELF sha256 matches DEBIAN/sha256sums
    uint32_t elf_size;
    uint64_t elf_entry;
    char elf_name[64];
    char elf_sha_expected[65];
    uint32_t payload_off; // .nefud: offset of the @@PAYLOAD@@ bytes
    uint32_t payload_size;
    bool crc_ok;
    char outbuf[600];     // VM / install output buffer
    int outlen;
    Button btn;           // main action (Launch / Run / Install)
};

// standard CRC-32 (IEEE 802.3), table-free bitwise version (small kernel)
static uint32_t crc32_bytes(const uint8_t* d, uint32_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (uint32_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return ~c;
}

// VM output callback: append chunks into the window buffer
static void vm_out(const char* s, void* ud) {
    NefudState* st = (NefudState*)ud;
    if (!s || !st) return;
    int n = 0;
    while (s[n] && st->outlen + n < (int)sizeof(st->outbuf) - 1) { n++; }
    if (n > 0) {
        memcpy(st->outbuf + st->outlen, s, (size_t)n);
        st->outlen += n;
        st->outbuf[st->outlen] = 0;
    }
}

// Fill any empty manifest fields (name/desc/author/ver) from DEBIAN/control.
// ctl is a NUL-terminated buffer of len bytes (parsed in place).
static void nefud_control_fill(NefudState* st, char* ctl, long len) {
    long i = 0;
    char* p = ctl;
    while (i <= len) {
        if (i == len || ctl[i] == '\n') {
            ctl[i] = 0;
            char* colon = p;
            while (*colon && *colon != ':') colon++;
            if (*colon == ':') {
                *colon = 0;
                const char* val = colon + 1;
                while (*val == ' ') val++;
                if (strcmp(p, "Package") == 0 && !st->name[0])
                    strncpy(st->name, val, sizeof(st->name) - 1);
                else if (strcmp(p, "Maintainer") == 0 && !st->author[0])
                    strncpy(st->author, val, sizeof(st->author) - 1);
                else if (strcmp(p, "Version") == 0 && !st->ver[0])
                    strncpy(st->ver, val, sizeof(st->ver) - 1);
                else if (strcmp(p, "Description") == 0 && !st->desc[0]) {
                    const char* e = val;
                    while (*e && *e != '\n' && *e != '\r') e++;
                    int nn = (int)(e - val);
                    if (nn > (int)sizeof(st->desc) - 1) nn = (int)sizeof(st->desc) - 1;
                    memcpy(st->desc, val, (size_t)nn);
                    st->desc[nn] = 0;
                }
            }
            p = ctl + i + 1;
        }
        i++;
    }
}

// Create every leading directory component of `path` (an absolute "/..." path)
// that is missing. VFS::mkdir only creates one level, so walk component-wise.
static void nefud_mkdir_p(const char* path) {
    if (!path || !*path) return;
    size_t n = strlen(path);
    if (n > 298) n = 298;
    size_t i = 0;
    while (i < n) {
        while (i < n && path[i] == '/') i++;
        size_t j = i;
        while (j < n && path[j] != '/') j++;
        if (j > i) {
            char buf[300];
            memcpy(buf, path, j);
            buf[j] = 0;
            if (!g_vfs->resolve(buf)) g_vfs->mkdir(buf);
        }
        i = j;
    }
}

// ---- minimal ar + ustar tar parsing for Runnable .deb (ELF static) payloads ----

typedef void (*nefud_ar_cb)(const char* name, const uint8_t* data, uint32_t len, void* ud);
// Iterate the members of an ar archive (.deb).  `data` points into the payload.
static int nefud_ar_for_each(const uint8_t* p, uint32_t n, nefud_ar_cb cb, void* ud) {
    if (n < 8 || memcmp(p, "!<arch>\n", 8) != 0) return 0;
    uint32_t pos = 8;
    int count = 0;
    while (pos + 60 <= n) {
        const uint8_t* h = p + pos;
        char name[20];
        uint32_t nl = 0;
        while (nl < 16 && h[nl] && h[nl] != ' ') { name[nl] = (char)h[nl]; nl++; }
        if (nl && name[nl - 1] == '/') nl--;
        name[nl] = 0;
        uint32_t size = 0;
        for (int i = 0; i < 10; i++) {
            char c = (char)h[48 + i];
            if (c >= '0' && c <= '9') size = size * 10 + (uint32_t)(c - '0');
        }
        if (size > n - pos - 60) break;
        if (nl) { count++; if (cb) cb(name, p + pos + 60, size, ud); }
        pos = pos + 60 + size + ((2 - (size & 1)) & 1);
    }
    return count;
}

typedef void (*nefud_tar_cb)(const char* name, const uint8_t* data, uint32_t len,
                             uint8_t type, const char* link, void* ud);
// Iterate the entries of a ustar tar (data.tar/control.tar).  Stops at the
// two-block end-of-archive marker (all-zero header).  Regular files
// (typeflag '0'/0) call cb with data; directories ('5') and symlinks ('2')
// call cb with data==0 (link = symlink target); everything else is skipped.
static int nefud_tar_for_each(const uint8_t* p, uint32_t n, nefud_tar_cb cb, void* ud) {
    uint32_t pos = 0;
    int count = 0;
    while (pos + 512 <= n) {
        const uint8_t* h = p + pos;
        bool allzero = true;
        for (int i = 0; i < 512; i++) if (h[i]) { allzero = false; break; }
        if (allzero) break;                       // end-of-archive
        char name[128];
        uint32_t nl = 0;
        while (nl < 100 && h[nl]) { name[nl] = (char)h[nl]; nl++; }
        name[nl] = 0;
        uint32_t size = 0;
        for (int i = 0; i < 12; i++) {
            char c = (char)h[124 + i];
            if (c >= '0' && c <= '7') size = size * 8 + (uint32_t)(c - '0');
        }
        char typeflag = (char)h[156];
        uint32_t padded = (size + 511) & ~511u;
        if (pos + 512 + padded > n) break;
        if (typeflag == '0' || typeflag == 0) {   // regular file
            count++;
            if (cb) cb(name, p + pos + 512, size, typeflag, 0, ud);
        } else if (typeflag == '5') {             // directory
            if (cb) cb(name, 0, 0, typeflag, 0, ud);
        } else if (typeflag == '2') {             // symlink -> target in h[157..256]
            char link[128];
            uint32_t ll = 0;
            while (ll < 100 && h[157 + ll]) { link[ll] = (char)h[157 + ll]; ll++; }
            link[ll] = 0;
            if (cb) cb(name, 0, 0, typeflag, link, ud);
        }
        pos += 512 + padded;
    }
    return count;
}

// Run nefud_tar_for_each over an ar member that may be a plain ustar tar or a
// gzip-compressed one (control.tar.gz / data.tar.gz).  `name` is the ar member
// name; the compressed case is inflated into a temporary buffer first.
static int nefud_tar_maybe_gzip(const char* name, const uint8_t* data, uint32_t len,
                                nefud_tar_cb cb, void* ud) {
    if (!name || !data) return 0;
    size_t nl = strlen(name);
    if (nl >= 4 && name[nl - 3] == '.' && name[nl - 2] == 'g' && name[nl - 1] == 'z') {
        // gzip-compressed tar: inflate into a bounded buffer then walk it.
        if (len < 18) return 0;
        const uint32_t MAX_TAR = 16u * 1024u * 1024u;
        uint8_t* tmp = (uint8_t*)kalloc(MAX_TAR);
        if (!tmp) return 0;
        int sz = deflate::gzip_decompress(data, len, tmp, MAX_TAR);
        if (sz < 0) { kfree(tmp); return 0; }
        int cnt = nefud_tar_for_each(tmp, (uint32_t)sz, cb, ud);
        kfree(tmp);
        return cnt;
    }
    return nefud_tar_for_each(data, len, cb, ud);
}

static bool nefud_hex_eq(const uint8_t* a, const char* b, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        uint8_t x = a[k], y = (uint8_t)b[k];
        if (x >= 'A' && x <= 'F') x += 32;
        if (y >= 'A' && y <= 'F') y += 32;
        if (x != y) return false;
    }
    return true;
}

// Load the pack key from /etc/nefu_pack.key (binary, >=32 bytes).  Returns the
// key length, or -1 if absent / too small (signature then reported as "no key").
static int nefud_load_key(uint8_t* keyout, int maxlen) {
    FSNode* kf = g_vfs->resolve("/etc/nefu_pack.key");
    if (!kf || kf->is_dir || kf->size == 0) return -1;
    uint32_t sz = kf->size < (uint32_t)maxlen ? kf->size : (uint32_t)maxlen;
    memcpy(keyout, kf->data, sz);
    return (int)sz;
}

// Verify the HMAC-SHA256 signature in the _sig member of an ar payload.
// The signed region is the exact bytes preceding the _sig header.
// Returns 1 ok, 0 mismatch, -1 not verifiable (no _sig / no key passed).
static int nefud_sig_verify(const uint8_t* p, uint32_t n, const uint8_t* key, int klen) {
    if (n < 8 || memcmp(p, "!<arch>\n", 8) != 0) return -1;
    uint32_t pos = 8;
    uint32_t sig_off = 0, sig_size = 0;
    int found = 0;
    while (pos + 60 <= n) {
        const uint8_t* h = p + pos;
        char name[20];
        uint32_t nl = 0;
        while (nl < 16 && h[nl] && h[nl] != ' ') { name[nl] = (char)h[nl]; nl++; }
        if (nl && name[nl - 1] == '/') nl--;
        name[nl] = 0;
        uint32_t size = 0;
        for (int i = 0; i < 10; i++) {
            char c = (char)h[48 + i];
            if (c >= '0' && c <= '9') size = size * 10 + (uint32_t)(c - '0');
        }
        if (size > n - pos - 60) break;
        if (nl == 4 && name[0] == '_' && name[1] == 's' && name[2] == 'i' && name[3] == 'g') {
            sig_off = pos; sig_size = size; found = 1; break;
        }
        pos = pos + 60 + size + ((2 - (size & 1)) & 1);
    }
    if (!found || sig_size == 0) return -1;
    // kernel hmac_sha256 caps the message at 4096 bytes, so sign the SHA-256
    // digest of the ar bytes instead of the whole ar (HMAC-of-hash; the digest
    // still binds every byte).  This matches the packer.
    unsigned char dig[32];
    nefu::crypt::sha256(p, (int)sig_off, dig);
    unsigned char mac[32];
    nefu::crypt::hmac_sha256(key, klen, dig, 32, mac);
    char hex[65];
    nefu::crypt::hmac_sha256_hex(mac, hex);
    const uint8_t* s = p + sig_off + 60;
    uint32_t i = 0;
    while (i + 4 < sig_size && !(s[i] == 's' && s[i+1] == 'i' && s[i+2] == 'g' && s[i+3] == '=')) i++;
    if (i + 4 >= sig_size) return 0;
    const uint8_t* hv = s + i + 4;
    uint32_t hl = sig_size - (i + 4);
    while (hl && (hv[hl - 1] == '\n' || hv[hl - 1] == '\r' || hv[hl - 1] == ' ')) hl--;
    if (hl != 64) return 0;
    return nefud_hex_eq(hv, hex, 64) ? 1 : 0;
}

static uint64_t nefud_elf_entry(const uint8_t* e, uint32_t n) {
    if (n < 32) return 0;
    uint64_t v = 0;
    int w = (e[4] == 2) ? 8 : 4;   // ELF64 vs ELF32
    for (int i = 0; i < w && i < 8; i++) v |= (uint64_t)e[24 + i] << (8 * i);
    return v;
}

// ---- ar-deb parse callbacks (control / sha256sums / ELF) ----

static void nefud_ctrl_scan(const char* name, const uint8_t* data, uint32_t len,
                            uint8_t type, const char* link, void* ud) {
    (void)type; (void)link;
    NefudState* st = (NefudState*)ud;
    if (!data) return;   // only regular files carry control content
    if (strcmp(name, "DEBIAN/control") == 0 || strcmp(name, "control") == 0) {
        char* ctl = (char*)kalloc((size_t)len + 1);
        if (ctl) {
            memcpy(ctl, data, len); ctl[len] = 0;
            nefud_control_fill(st, ctl, (long)len);
            kfree(ctl);
        }
    } else if (strcmp(name, "DEBIAN/sha256sums") == 0) {
        char* sums = (char*)kalloc((size_t)len + 1);
        if (sums) {
            memcpy(sums, data, len); sums[len] = 0;
            // keep the first 64 hex chars = sha256 of the ELF
            strncpy(st->elf_sha_expected, sums, 64);
            st->elf_sha_expected[64] = 0;
            kfree(sums);
        }
    }
}

static void nefud_data_scan(const char* name, const uint8_t* data, uint32_t len,
                            uint8_t type, const char* link, void* ud) {
    (void)type; (void)link;
    NefudState* st = (NefudState*)ud;
    if (!data || len < 4 || data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') return;
    st->elf_size = len;
    st->elf_entry = nefud_elf_entry(data, len);
    strncpy(st->elf_name, name, sizeof(st->elf_name) - 1);
    if (st->elf_sha_expected[0]) {
        unsigned char dig[32];
        nefu::crypt::sha256(data, (int)len, dig);
        char hex[65];
        nefu::crypt::sha256_hex(dig, hex);
        st->elf_ok = (strncmp(st->elf_sha_expected, hex, 64) == 0);
    }
}

static void nefud_ar_scan(const char* name, const uint8_t* data, uint32_t len, void* ud) {
    NefudState* st = (NefudState*)ud;
    if (strcmp(name, "control.tar") == 0 || strcmp(name, "control.tar.gz") == 0)
        nefud_tar_maybe_gzip(name, data, len, nefud_ctrl_scan, st);
    else if (strcmp(name, "data.tar") == 0 || strcmp(name, "data.tar.gz") == 0)
        nefud_tar_maybe_gzip(name, data, len, nefud_data_scan, st);
}

// ---- ar-deb install: write every file in data.tar (rooted at "/") into VFS ----

static void nefud_data_install(const char* name, const uint8_t* data, uint32_t len,
                               uint8_t type, const char* link, void* ud) {
    int* done = (int*)ud;
    char path[300];
    int pl = 0;
    path[pl++] = '/';
    for (const char* c = name; *c && pl < 298; c++) path[pl++] = *c;
    path[pl] = 0;
    if (type == '5') {                    // directory: create every level
        nefud_mkdir_p(path);
        return;
    }
    if (type == '2') {                    // symlink: no VFS symlink support, skip
        if (done) (*done)++;
        return;
    }
    if (!data) return;
    char parent[300];
    memcpy(parent, path, (size_t)pl);
    int last = -1;
    for (int k = 0; k < pl; k++) if (path[k] == '/') last = k;
    if (last > 0) { parent[last] = 0; nefud_mkdir_p(parent); }
    FSNode* nf = g_vfs->create_file(path);
    if (nf) {
        g_vfs->write_file(nf, data, len);
        (*done)++;
    }
}

static void nefud_ar_install_scan(const char* name, const uint8_t* data, uint32_t len, void* ud) {
    if (strcmp(name, "data.tar") == 0 || strcmp(name, "data.tar.gz") == 0)
        nefud_tar_maybe_gzip(name, data, len, nefud_data_install, ud);
}

static int nefud_install_ar(NefudState* st) {
    if (!st->file || !st->is_ar || st->payload_size == 0) return 0;
    int done = 0;
    nefud_ar_for_each(st->file->data + st->payload_off, st->payload_size,
                      nefud_ar_install_scan, &done);
    return done;
}

// ---- public .deb environment API (terminal `deb` command + boot self-test) ----

// Append s at out[pos], keeping out NUL-terminated. Returns the new pos.
static int nefud_out_append(char* out, int outsz, int pos, const char* s) {
    if (pos < 0 || !out || outsz <= 0) return -1;
    while (*s && pos + 1 < outsz) out[pos++] = *s++;
    if (pos < outsz) out[pos] = 0;
    return pos;
}

struct NefudDebCtx { char* out; int outsz; int pos; };

static void nefud_deb_list_members(const char* name, const uint8_t* data, uint32_t len, void* ud) {
    NefudDebCtx* c = (NefudDebCtx*)ud;
    (void)data;
    char b[80];
    ksprintf(b, sizeof(b), "  member: %s (%u B)\n", name, (unsigned)len);
    c->pos = nefud_out_append(c->out, c->outsz, c->pos, b);
}

// Summarize a real Debian .deb (ar) file: member list + control fields.
// Returns 0 on success (out filled), -1 when `f` is not an ar archive.
int nefud_deb_probe(FSNode* f, char* out, int outsz) {
    if (!out || outsz <= 0) return -1;
    out[0] = 0;
    if (!f || f->is_dir || f->size < 8 || memcmp(f->data, "!<arch>\n", 8) != 0) {
        strncpy(out, "not a .deb (ar archive)", (size_t)outsz - 1);
        return -1;
    }
    NefudState st;
    memset(&st, 0, sizeof(st));
    st.file = f;
    st.is_ar = true;
    nefud_ar_for_each(f->data, f->size, nefud_ar_scan, &st);
    int pos = 0;
    pos = nefud_out_append(out, outsz, pos, "format: Debian .deb (ar)\n");
    pos = nefud_out_append(out, outsz, pos, "members:\n");
    NefudDebCtx c; c.out = out; c.outsz = outsz; c.pos = pos;
    nefud_ar_for_each(f->data, f->size, nefud_deb_list_members, &c);
    pos = c.pos;
    if (st.name[0])   { pos = nefud_out_append(out, outsz, pos, "Package: ");   pos = nefud_out_append(out, outsz, pos, st.name);   pos = nefud_out_append(out, outsz, pos, "\n"); }
    if (st.ver[0])    { pos = nefud_out_append(out, outsz, pos, "Version: ");   pos = nefud_out_append(out, outsz, pos, st.ver);    pos = nefud_out_append(out, outsz, pos, "\n"); }
    if (st.author[0]) { pos = nefud_out_append(out, outsz, pos, "Maintainer: "); pos = nefud_out_append(out, outsz, pos, st.author); pos = nefud_out_append(out, outsz, pos, "\n"); }
    if (st.desc[0])   { pos = nefud_out_append(out, outsz, pos, "Description: "); pos = nefud_out_append(out, outsz, pos, st.desc); pos = nefud_out_append(out, outsz, pos, "\n"); }
    return 0;
}

// Install every file of data.tar(.gz) into the VFS. Returns the number of
// entries installed (>=0), or -1 when `f` is not an ar archive.
int nefud_deb_install(FSNode* f, char* out, int outsz) {
    if (!out || outsz <= 0) return -1;
    out[0] = 0;
    if (!f || f->is_dir || f->size < 8 || memcmp(f->data, "!<arch>\n", 8) != 0) {
        strncpy(out, "not a .deb (ar archive)", (size_t)outsz - 1);
        return -1;
    }
    NefudState st;
    memset(&st, 0, sizeof(st));
    st.file = f;
    st.is_ar = true;
    st.payload_off = 0;
    st.payload_size = f->size;
    int done = nefud_install_ar(&st);
    char b[96];
    ksprintf(b, sizeof(b), "installed %d file(s) from data.tar to VFS", done);
    strncpy(out, b, (size_t)outsz - 1);
    return done;
}

// Find the first ELF inside data.tar(.gz); fill vfs_path with "/" + tar name.
// Returns 1 when found, 0 otherwise.
struct NefudElfFindCtx { char* vfs; int cap; int found; };
static void nefud_deb_find_elf_cb(const char* name, const uint8_t* data, uint32_t len,
                                  uint8_t type, const char* link, void* ud) {
    NefudElfFindCtx* c = (NefudElfFindCtx*)ud;
    (void)type; (void)link;
    if (c->found) return;
    if (!data || len < 4 || data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') return;
    int n = 0;
    if (c->cap > 1) c->vfs[n++] = '/';
    for (const char* s = name; *s && n + 1 < c->cap; s++) c->vfs[n++] = *s;
    c->vfs[n] = 0;
    c->found = 1;
}
static void nefud_deb_find_elf_ar(const char* name, const uint8_t* data, uint32_t len, void* ud) {
    if (strcmp(name, "data.tar") == 0 || strcmp(name, "data.tar.gz") == 0)
        nefud_tar_maybe_gzip(name, data, len, nefud_deb_find_elf_cb, ud);
}
int nefud_deb_find_elf(FSNode* f, char* vfs_path, int path_sz) {
    if (!f || f->is_dir || f->size < 8 || memcmp(f->data, "!<arch>\n", 8) != 0) return 0;
    if (!vfs_path || path_sz <= 0) return 0;
    vfs_path[0] = 0;
    NefudElfFindCtx c; c.vfs = vfs_path; c.cap = path_sz; c.found = 0;
    nefud_ar_for_each(f->data, f->size, nefud_deb_find_elf_ar, &c);
    return c.found;
}

// Install a PK-zip .deb payload: write every "data/<rel>" file into the VFS
// under "/<rel>", creating parent dirs. Returns the number of files written.
static int nefud_install(NefudState* st) {
    if (!st->file || !st->is_zip || st->payload_size == 0) return 0;
    const uint8_t* z = st->file->data + st->payload_off;
    size_t zn = st->payload_size;
    int cnt = zip::count(z, zn);
    if (cnt <= 0) return 0;
    int done = 0;
    for (int i = 0; i < cnt; i++) {
        zip::Entry e;
        if (zip::entry(z, zn, i, &e) != 1) continue;
        int nl = (int)strlen(e.name);
        if (nl < 5 || strncmp(e.name, "data/", 5) != 0) continue;
        // directory entry ("data/foo/") -> mkdir the dir itself
        if (e.name[nl - 1] == '/') {
            char dir[300];
            int pl = 0;
            dir[pl++] = '/';
            for (int k = 5; k < nl - 1 && pl < 298; k++) dir[pl++] = e.name[k];
            dir[pl] = 0;
            nefud_mkdir_p(dir);
            continue;
        }
        // file entry: cap uncompressed size to keep the kernel bounded
        if (e.uncomp_size == 0 || e.uncomp_size >= 0x100000) continue;
        uint8_t* tmp = (uint8_t*)kalloc(e.uncomp_size);
        if (!tmp) continue;
        long r = zip::read(z, zn, e, tmp, e.uncomp_size);
        if (r >= 0) {
            char path[300];
            int pl = 0;
            path[pl++] = '/';
            for (int k = 5; k < nl && pl < 298; k++) path[pl++] = e.name[k];
            path[pl] = 0;
            // create the parent directory (everything but the file name)
            char parent[300];
            memcpy(parent, path, (size_t)pl);
            int last = -1;
            for (int k = 0; k < pl; k++) if (path[k] == '/') last = k;
            if (last > 0) { parent[last] = 0; nefud_mkdir_p(parent); }
            FSNode* nf = g_vfs->create_file(path);
            if (nf) {
                g_vfs->write_file(nf, tmp, (uint32_t)r);
                done++;
            }
        }
        kfree(tmp);
    }
    return done;
}

static void nefud_parse(NefudState* st) {
    FSNode* f = st->file;
    st->name[0] = 0; st->desc[0] = 0; st->author[0] = 0; st->ver[0] = 0;
    st->app_id = -1; st->is_bin = false; st->is_zip = false; st->zip_count = 0;
    st->is_ar = false; st->sig_status = -1; st->elf_ok = false;
    st->elf_size = 0; st->elf_entry = 0; st->elf_name[0] = 0; st->elf_sha_expected[0] = 0;
    st->payload_off = 0; st->payload_size = 0; st->crc_ok = true;
    if (!f || f->is_dir || f->size == 0) return;
    const uint8_t* d = f->data;
    // .bin package: fixed binary header
    if (f->size >= 64 &&
        d[0] == 'N' && d[1] == 'E' && d[2] == 'F' && d[3] == 'B' &&
        d[4] == 'I' && d[5] == 'N' && d[6] == '0' && d[7] == '1') {
        st->is_bin = true;
        for (int i = 0; i < 24 && d[8 + i]; i++) if (i < 47) st->name[i] = (char)d[8 + i];
        for (int i = 0; i < 32 && d[32 + i]; i++) if (i < 95) st->desc[i] = (char)d[32 + i];
        strcpy(st->author, "nefuOS");
        strcpy(st->ver, "1.0");
        if (st->name[0]) st->app_id = nefud_name_to_app_id(st->name);
        return;
    }
    // .nefud text manifest
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) return;
    memcpy(buf, d, f->size);
    buf[f->size] = 0;
    char* line = buf;
    for (uint32_t i = 0; i < f->size; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            if (strncmp(line, "name=", 5) == 0) strncpy(st->name, line + 5, sizeof(st->name) - 1);
            else if (strncmp(line, "desc=", 5) == 0) strncpy(st->desc, line + 5, sizeof(st->desc) - 1);
            else if (strncmp(line, "author=", 7) == 0) strncpy(st->author, line + 7, sizeof(st->author) - 1);
            else if (strncmp(line, "ver=", 4) == 0) strncpy(st->ver, line + 4, sizeof(st->ver) - 1);
            line = buf + i + 1;
        }
    }
    if (strncmp(line, "name=", 5) == 0) strncpy(st->name, line + 5, sizeof(st->name) - 1);
    else if (strncmp(line, "desc=", 5) == 0) strncpy(st->desc, line + 5, sizeof(st->desc) - 1);
    else if (strncmp(line, "author=", 7) == 0) strncpy(st->author, line + 7, sizeof(st->author) - 1);
    else if (strncmp(line, "ver=", 4) == 0) strncpy(st->ver, line + 4, sizeof(st->ver) - 1);
    // payload section: "@@PAYLOAD@@\n<bytes><crc32be>" (nefupack format)
    const char* mark = strstr(buf, "@@PAYLOAD@@\n");
    if (mark && f->size >= 8) {
        uint32_t po = (uint32_t)(mark - buf) + 12;   // strlen("@@PAYLOAD@@\n")
        if (po + 4 <= f->size) {
            uint32_t psz = f->size - po - 4;         // trailing 4-byte CRC32
            st->payload_off = po;
            st->payload_size = psz;
            uint32_t calc = crc32_bytes(f->data + po, psz);
            uint32_t stored = ((uint32_t)f->data[f->size - 4] << 24) |
                              ((uint32_t)f->data[f->size - 3] << 16) |
                              ((uint32_t)f->data[f->size - 2] << 8) |
                              ((uint32_t)f->data[f->size - 1]);
            st->crc_ok = (calc == stored);
        }
    }
    kfree(buf);
    // ZIP payload (Linux .deb layout): count entries and, if the manifest left
    // any field empty, fall back to DEBIAN/control.
    if (st->payload_size > 0) {
        const uint8_t* z = f->data + st->payload_off;
        if (zip::is_zip(z, st->payload_size)) {
            st->is_zip = true;
            st->zip_count = zip::count(z, st->payload_size);
            zip::Entry ce;
            if (zip::find(z, st->payload_size, "DEBIAN/control", &ce) == 1 &&
                ce.uncomp_size > 0 && ce.uncomp_size < 4096) {
                char* ctl = (char*)kalloc((size_t)ce.uncomp_size + 1);
                if (ctl) {
                    long r = zip::read(z, st->payload_size, ce, (uint8_t*)ctl,
                                       (size_t)ce.uncomp_size + 1);
                    if (r >= 0) nefud_control_fill(st, ctl, r);
                    kfree(ctl);
                }
            }
        }
    }
    // ar payload (Runnable .deb: ar header + static ELF + HMAC signature)
    if (st->payload_size > 0) {
        const uint8_t* z = f->data + st->payload_off;
        if (st->payload_size >= 8 && memcmp(z, "!<arch>\n", 8) == 0) {
            st->is_ar = true;
            // read DEBIAN/control (+ fill empty manifest fields), DEBIAN/sha256sums,
            // and the usr/bin/* ELF from the control.tar / data.tar members
            nefud_ar_for_each(z, st->payload_size, nefud_ar_scan, st);
            // verify the HMAC-SHA256 signature with the pack key (if installed)
            uint8_t key[32];
            int klen = nefud_load_key(key, 32);
            if (klen > 0)
                st->sig_status = nefud_sig_verify(z, st->payload_size, key, klen);
            else
                st->sig_status = -1;   // no key -> not verifiable here
        }
    }
    if (st->name[0]) st->app_id = nefud_name_to_app_id(st->name);
}

static void nefud_paint(Window* w) {
    NefudState* st = (NefudState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    gfx::text(s, 14, 12, st->is_bin ? "nefuOS Binary Application (.bin)" : "nefuOS Application Package (.nefud)",
              color::BLUE, color::WHITE);
    int y = 42;
    char buf[160];
    gfx::text(s, 14, y, "Name   :", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->name[0] ? st->name : "(none)", color::TEXT, color::WHITE); y += 20;
    gfx::text(s, 14, y, "Desc   :", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->desc[0] ? st->desc : "(none)", color::TEXT, color::WHITE); y += 20;
    gfx::text(s, 14, y, "Author :", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->author[0] ? st->author : "(unknown)", color::TEXT, color::WHITE); y += 20;
    gfx::text(s, 14, y, "Version:", color::TEXT2, color::WHITE);
    gfx::text(s, 90, y, st->ver[0] ? st->ver : "(?)", color::TEXT, color::WHITE); y += 20;
    if (st->payload_size > 0) {
        if (st->is_ar)
            ksprintf(buf, sizeof(buf), "payload: %u B  CRC:%s  sig:%s  ELF:%s",
                     (unsigned)st->payload_size, st->crc_ok ? "OK" : "BAD",
                     st->sig_status == 1 ? "OK" : (st->sig_status == 0 ? "FAIL" : "nokey"),
                     st->elf_ok ? "sha256 OK" : (st->elf_size ? "sha MISMATCH" : "-"));
        else if (st->is_zip)
            ksprintf(buf, sizeof(buf), "payload: %u B  CRC: %s  [ZIP .deb, %d entries]",
                     (unsigned)st->payload_size, st->crc_ok ? "OK" : "MISMATCH", st->zip_count);
        else
            ksprintf(buf, sizeof(buf), "payload: %u bytes  CRC: %s",
                     (unsigned)st->payload_size, st->crc_ok ? "OK" : "MISMATCH");
        gfx::text(s, 14, y, buf, st->crc_ok ? color::GREEN : color::RED, color::WHITE);
        y += 6;
        if (st->is_ar && st->elf_size) {
            ksprintf(buf, sizeof(buf), "ELF: %s (%u B)  entry=0x%llx", st->elf_name,
                     (unsigned)st->elf_size, (unsigned long long)st->elf_entry);
            gfx::text(s, 14, y, buf, color::TEXT, color::WHITE);
            y += 6;
        }
    }
    y += 20;

    if (st->app_id >= 0) {
        st->btn.x = 14; st->btn.y = y; st->btn.w = 120; st->btn.h = 28;
        st->btn.label = "Launch";
        ui::draw_button(s, st->btn);
        gfx::text(s, 150, y + 8, "bound to a built-in app - click to run", color::BLUE_LT, color::WHITE);
    } else if (st->is_ar) {
        // Real Linux .deb (ar archive, optionally signed): Install button
        st->btn.x = 14; st->btn.y = y; st->btn.w = 120; st->btn.h = 28;
        st->btn.label = "Install";
        ui::draw_button(s, st->btn);
        gfx::text(s, 150, y + 8, "Linux .deb (ar) - install data.tar to VFS",
                  color::BLUE_LT, color::WHITE);
        y += 44;
        gfx::fillrect(s, 14, y, w->content_w - 28, 66, 0x00F2F2F0);
        gfx::rect(s, 14, y, w->content_w - 28, 66, color::BORDER);
        gfx::text(s, 20, y + 8, st->outlen > 0 ? st->outbuf : "(no output yet - press Install)", color::TEXT, 0x00F2F2F0);
    } else if (st->is_zip) {
        // Linux .deb zip payload: Install button + output box
        st->btn.x = 14; st->btn.y = y; st->btn.w = 120; st->btn.h = 28;
        st->btn.label = "Install";
        ui::draw_button(s, st->btn);
        gfx::text(s, 150, y + 8, "Linux .deb zip - install data/ to VFS", color::BLUE_LT, color::WHITE);
        y += 44;
        gfx::fillrect(s, 14, y, w->content_w - 28, 66, 0x00F2F2F0);
        gfx::rect(s, 14, y, w->content_w - 28, 66, color::BORDER);
        gfx::text(s, 20, y + 8, st->outlen > 0 ? st->outbuf : "(no output yet - press Install)", color::TEXT, 0x00F2F2F0);
    } else if (st->is_bin) {
        // executable bytecode: Run button + output box
        st->btn.x = 14; st->btn.y = y; st->btn.w = 120; st->btn.h = 28;
        st->btn.label = "Run";
        ui::draw_button(s, st->btn);
        gfx::text(s, 150, y + 8, "NEFVM bytecode - click to execute", color::BLUE_LT, color::WHITE);
        y += 44;
        // output box
        gfx::fillrect(s, 14, y, w->content_w - 28, 66, 0x00F2F2F0);
        gfx::rect(s, 14, y, w->content_w - 28, 66, color::BORDER);
        gfx::text(s, 20, y + 8, st->outlen > 0 ? st->outbuf : "(no output yet - press Run)", color::TEXT, 0x00F2F2F0);
    } else {
        st->btn.x = 0; st->btn.y = 0; st->btn.w = 0; st->btn.h = 0;
        st->btn.label = 0;
        gfx::text(s, 14, y, "No built-in app bound - showing raw text.", color::RED, color::WHITE);
        y += 22;
        if (st->file && !st->file->is_dir && st->file->size > 0) {
            char* raw = (char*)kalloc((size_t)st->file->size + 1);
            if (raw) {
                memcpy(raw, st->file->data, st->file->size);
                raw[st->file->size] = 0;
                gfx::text(s, 14, y, raw, color::TEXT, color::WHITE);
                kfree(raw);
            }
        }
    }
    ksprintf(buf, sizeof(buf), "path: %s", node_path(st->file).c_str());
    gfx::text(s, 14, 200, buf, color::TEXT2, color::WHITE);
}

static void nefud_mouse(Window* w, int mx, int my, uint8_t buttons) {
    NefudState* st = (NefudState*)w->userdata;
    if (!st->btn.label) return;
    static uint8_t last = 0;
    bool pressed = buttons && !last;
    bool released = !buttons && last;
    last = buttons;
    if (ui::button_event(st->btn, mx, my, buttons, pressed, released)) {
        if (st->app_id >= 0) {
            app_launch(st->app_id);
        } else if (st->is_ar && st->file) {
            st->outlen = 0;
            st->outbuf[0] = 0;
            int installed = nefud_deb_install(st->file, st->outbuf, (int)sizeof(st->outbuf));
            if (installed < 0) {  // fall back to the old ar path message
                st->outlen = ksprintf(st->outbuf, sizeof(st->outbuf) - 1,
                                      "install failed: not a .deb");
            } else {
                st->outlen = (int)strlen(st->outbuf);
            }
            if (st->outlen < 0) st->outlen = 0;
        } else if (st->is_zip && st->file) {
            st->outlen = 0;
            st->outbuf[0] = 0;
            int installed = nefud_install(st);
            int n = ksprintf(st->outbuf, sizeof(st->outbuf) - 1,
                             "installed %d file(s) from data/ to VFS", installed);
            if (n < 0) n = 0;
            st->outlen = n;
        } else if (st->is_bin && st->file) {
            st->outlen = 0;
            st->outbuf[0] = 0;
            nefvm_run(st->file->data, st->file->size, vm_out, st);
        }
    }
}

static void nefud_close(Window* w) {
    if (w->userdata) {
        NefudState* st = (NefudState*)w->userdata;
        delete st;
        w->userdata = 0;
    }
}

// ensure the sample .bin packages exist under /usr/share/apps
static void ensure_bin_samples() {
    g_vfs->mkdir("/usr/share/apps");
    FSNode* f = g_vfs->resolve("/usr/share/apps/hello.bin");
    if (!f) {
        f = g_vfs->create_file("/usr/share/apps/hello.bin");
        if (f) {
            // header: magic + empty name + desc, then bytecode
            uint8_t pk[128];
            memset(pk, 0, sizeof(pk));
            memcpy(pk, "NEFBIN01", 8);
            const char* desc = "NEFVM demo: hello + arithmetic";
            for (int i = 0; desc[i] && i < 32; i++) pk[32 + i] = (uint8_t)desc[i];
            const char* msg = "Hello, NEFUOS!";
            int p = 64;
            for (const char* c = msg; *c; c++) { pk[p++] = 0x01; pk[p++] = (uint8_t)*c; pk[p++] = 0x09; }
            pk[p++] = 0x01; pk[p++] = 10; pk[p++] = 0x09;      // newline
            pk[p++] = 0x01; pk[p++] = 6;  pk[p++] = 0x01; pk[p++] = 7; pk[p++] = 0x06; pk[p++] = 0x08; pk[p++] = 0x0A; // 6*7
            pk[p++] = 0x01; pk[p++] = 100; pk[p++] = 0x01; pk[p++] = 8; pk[p++] = 0x05; pk[p++] = 0x08; pk[p++] = 0x0A; // 100-8
            pk[p++] = 0x00; // HALT
            g_vfs->write_file(f, pk, (uint32_t)p);
        }
    }
    FSNode* g = g_vfs->resolve("/usr/share/apps/calculator.nefud");
    if (!g) {
        g = g_vfs->create_file("/usr/share/apps/calculator.nefud");
        if (g) {
            const char* t = "NEFUD1\nname=Calculator\ndesc=Arithmetic calculator\nauthor=nefuOS\nver=1.0\n";
            g_vfs->write_file(g, (const uint8_t*)t, (uint32_t)strlen(t));
        }
    }
}

void app_show_nefud(FSNode* f) {
    if (!f) return;
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("App Package", x, y, 480, 250);
    if (!w) return;
    NefudState* st = new NefudState();
    st->file = f;
    st->btn.label = 0;
    st->outlen = 0;
    st->outbuf[0] = 0;
    nefud_parse(st);
    w->userdata = st;
    w->on_paint = nefud_paint;
    w->on_mouse = nefud_mouse;
    w->on_close = nefud_close;
}

void nefud_launch() {
    ensure_bin_samples();
    FSNode* f = g_vfs->resolve("/usr/share/apps/hello.bin");
    if (!f) f = g_vfs->resolve("/usr/share/apps/calculator.nefud");
    if (f) app_show_nefud(f);
}

} // namespace nefu
