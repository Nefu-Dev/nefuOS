// =============================================================================
//  update.cpp — nefuOS OTA update: manifest fetch, streaming download with
//  HTTP-302 + resume (Range) + SHA-256 verify, user-space backup, install,
//  and a non-blocking GUI progress window.
//
//  Constraints: no STL, no exceptions; kalloc/kfree + nefu::String/List.
//  The long download runs on a worker thread; progress lives in a volatile
//  struct that a lightweight GUI window paints every frame, so the main
//  thread never blocks on I/O.
// =============================================================================
#include "update.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include "../platform.h"
#include "../lib/json.h"
#include "../klib/aes128.h"
#include "../sys/sha256.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "term_ext.h"
#include "npkg.h"
#include <stdarg.h>

namespace nefu {

// ---- current OS version (mirrors the v3.0 release tag) ---------------------
static const char* UPDATE_CURRENT_VERSION = "3.0.0";
// default manifest (InfinityFree host answers every path with a JS challenge)
static const char* UPDATE_MANIFEST_DEFAULT = "https://nefuos.site.je/update.json";

// ---- VFS layout ------------------------------------------------------------
static const char* UPDATE_DIR      = "/user/updates";
static const char* UPDATE_INCOMING = "/user/updates/incoming";
static const char* UPDATE_STATE    = "/user/updates/status.json";

static const char* ARTIFACT_ISO_NAME = "nefuOS.iso";
static const char* ARTIFACT_LLM_NAME = "llm_model.img";

// =============================================================================
// Small helpers
// =============================================================================
static void uoutf(const UpdateOut& o, const char* fmt, ...) {
    if (!o.print) return;
    char b[320];
    va_list ap; va_start(ap, fmt); kvsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    o.print(o.ud, b);
}
// adapter: terminal print sink (UpdateOut expects void* ud)
static void update_term_print(void* ud, const char* s) {
    term_ext_print((TermState*)ud, s);
}

// mkdir -p (like npkg)
static FSNode* update_mkdirs(const char* path) {
    if (!path || !*path) return 0;
    if (g_vfs->resolve(path)) return g_vfs->resolve(path);
    String acc;
    const char* p = path;
    if (*p == '/') { acc += "/"; p++; }
    while (*p) {
        const char* slash = strchr(p, '/');
        int len = slash ? (int)(slash - p) : (int)nefu::strlen(p);
        if (len > 0) {
            String part = acc;
            if (part.len() > 0 && part[part.len() - 1] != '/') part += "/";
            part += String(p, len);
            if (!g_vfs->resolve(part.c_str())) {
                FSNode* d = g_vfs->mkdir(part.c_str());
                if (!d) return 0;
            }
            acc = part;
        }
        if (!slash) break;
        p = slash + 1;
    }
    return g_vfs->resolve(path);
}

// stream-append a chunk into an FSNode, growing the buffer geometrically so we
// never hold a second copy of the whole file (streams straight into the VFS).
static bool vfs_stream_append(FSNode* f, uint64_t& cap,
                              const uint8_t* data, uint32_t len) {
    uint64_t need = (uint64_t)f->size + len;
    if (need > cap) {
        uint64_t ncap = cap ? cap : (1u << 20);
        while (ncap < need) ncap *= 2;
        if (ncap > 0x7FFFFFFF) ncap = need;
        uint8_t* nb = (uint8_t*)kalloc((uint32_t)ncap);
        if (!nb) return false;
        if (f->data) { memcpy(nb, f->data, f->size); kfree(f->data); }
        f->data = nb;
        cap = ncap;
    }
    memcpy(f->data + f->size, data, len);
    f->size += len;
    f->mtime = platform_tick_ms() / 1000;
    return true;
}

// basename of a path (last component)
static const char* update_basename(const char* path) {
    const char* b = path;
    for (const char* p = path; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

// semver compare "3.0.0" vs "2.5.1"; returns <0 / 0 / >0
static int update_cmp_version(const char* a, const char* b) {
    while (*a || *b) {
        while (*a == '.' ) a++;
        while (*b == '.') b++;
        int va = 0, vb = 0;
        while (*a >= '0' && *a <= '9') { va = va * 10 + (*a - '0'); a++; }
        while (*b >= '0' && *b <= '9') { vb = vb * 10 + (*b - '0'); b++; }
        if (va != vb) return va < vb ? -1 : 1;
        // skip trailing non-digit junk
        while (*a && *a != '.') a++;
        while (*b && *b != '.') b++;
    }
    return 0;
}

static double update_atof(const char* s) {
    if (!s) return 0;
    double v = 0; bool neg = false;
    if (*s == '-') { neg = true; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') { s++; double d = 0.1; while (*s >= '0' && *s <= '9') { v += d * (*s - '0'); d *= 0.1; s++; } }
    return neg ? -v : v;
}

// extract host from a URL ("https://host[:port]/path")
static void update_url_host(const char* url, char* host, int hostsz) {
    host[0] = 0;
    const char* p = strstr(url, "://");
    if (!p) return;
    p += 3;
    const char* e = p;
    while (*e && *e != '/' && *e != ':' && *e != '?') e++;
    int n = (int)(e - p);
    if (n >= hostsz) n = hostsz - 1;
    memcpy(host, p, n); host[n] = 0;
}

// =============================================================================
// InfinityFree JS `__test` challenge bypass (mirrors npkg.cpp; per-host cookie)
// =============================================================================
static int update_hexval(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static const char* update_find(const char* d, uint32_t len, const char* sub) {
    uint32_t sl = nefu::strlen(sub);
    for (uint32_t i = 0; i + sl <= len; i++) {
        uint32_t j;
        for (j = 0; j < sl; j++) if (d[i + j] != sub[j]) break;
        if (j == sl) return d + i;
    }
    return 0;
}
static bool update_parse_challenge(const uint8_t* body, uint32_t blen,
                                   uint8_t key[16], uint8_t iv[16], uint8_t c[16]) {
    if (!update_find((const char*)body, blen, "slowAES")) return false;
    uint32_t pos = 0;
    for (int n = 0; n < 3; n++) {
        const char* m = update_find((const char*)body + pos, blen - pos, "toNumbers(\"");
        if (!m) return false;
        uint32_t off = (uint32_t)(m - (const char*)body) + (uint32_t)nefu::strlen("toNumbers(\"");
        if (off + 32 > blen) return false;
        uint8_t tmp[16];
        for (int k = 0; k < 16; k++) {
            int hi = update_hexval((char)body[off + k * 2]);
            int lo = update_hexval((char)body[off + k * 2 + 1]);
            if (hi < 0 || lo < 0) return false;
            tmp[k] = (uint8_t)((hi << 4) | lo);
        }
        if (n == 0) { for (int i = 0; i < 16; i++) key[i] = tmp[i]; }
        else if (n == 1) { for (int i = 0; i < 16; i++) iv[i] = tmp[i]; }
        else { for (int i = 0; i < 16; i++) c[i] = tmp[i]; }
        pos = off + 32;
    }
    return true;
}

// GET url transparently solving the challenge. On success *out is a kalloc'd
// buffer (kfree), *out_len its length. *cookie is the hex __test value ("" when
// no challenge), *eff_url the URL that returned the real body (challenged hosts
// need "?i=1"). Returns true when a real body was received.
static bool update_http_get_challenge(const char* url, uint8_t** out, uint32_t* out_len,
                                      String& cookie, String& eff_url) {
    *out = 0; *out_len = 0; cookie.clear();
    eff_url = url;
    uint8_t* body = 0; uint32_t blen = 0;
    // Prefer the generic WinINet request path (host): the native TLS13 streamer
    // can return a truncated handshake for some hosts, hiding the InfinityFree
    // challenge page from the parser below. On bare metal platform_http_request
    // is a stub, so fall back to platform_http_get which supports plain
    // http://<ip> (no TLS/DNS) there.
    int status = 0;
    bool got = platform_http_request("GET", url, 0, 0, 0, &status, &body, &blen);
    if (!got && strncmp(url, "http://", 7) == 0)
        got = platform_http_get(url, &body, &blen);
    if (!got || !body) {
        if (body) kfree(body);
        return false;
    }
    uint8_t key[16], iv[16], c[16];
    if (update_parse_challenge(body, blen, key, iv, c)) {
        kfree(body);                       // discard the challenge page
        uint8_t plain[16];
        nefu::aes128_cbc_decrypt(key, iv, c, 16, plain);
        static const char* HEX = "0123456789abcdef";
        char cookie_hex[33];
        for (int i = 0; i < 16; i++) {
            cookie_hex[i * 2]     = HEX[(plain[i] >> 4) & 0xf];
            cookie_hex[i * 2 + 1] = HEX[plain[i] & 0xf];
        }
        cookie_hex[32] = 0;
        cookie = cookie_hex;
        char hdr[64];
        int w = 0;
        static const char* P = "Cookie: __test=";
        for (int i = 0; P[i]; i++) hdr[w++] = P[i];
        for (int i = 0; i < 32; i++) hdr[w++] = cookie_hex[i];
        hdr[w++] = '\r'; hdr[w++] = '\n'; hdr[w] = 0;
        // challenged hosts re-serve the real file on "<url>?i=1"
        eff_url = url;
        eff_url += (strchr(url, '?') ? "&i=1" : "?i=1");
        int status = 0;
        uint8_t* rb = 0; uint32_t rlen = 0;
        bool ok = platform_http_request("GET", eff_url.c_str(), hdr, 0, 0, &status, &rb, &rlen);
        if (ok && rb && rlen > 0) {
            *out = rb; *out_len = rlen;
            return true;
        }
        if (rb) kfree(rb);
        return false;
    }
    // no challenge
    *out = body; *out_len = blen;
    return true;
}

// =============================================================================
// Manifest
// =============================================================================
struct UpdateManifest {
    String version, min_version, channel, release_date;
    String iso_url, iso_sha256;
    double iso_size_mb;
    String llm_url, llm_sha256;
    double llm_size_mb;
    bool force;
};
// read a numeric manifest field: JSON numbers keep their raw text in Value.s
// for fractional values and the integer in Value.i for whole numbers.
static double update_manifest_size(const nefu::json::Value& root, const char* key) {
    const nefu::json::Value* v = root.get(key);
    if (!v) return 0;
    if (v->is_string()) return update_atof(v->s.c_str());
    if (v->is_int()) {
        if (v->s.len() > 0) return update_atof(v->s.c_str());  // "4.05"
        return (double)v->i;
    }
    return 0;
}
static bool update_parse_manifest(const String& text, UpdateManifest& m) {
    nefu::json::Value root;
    if (!nefu::json::parse(text, root)) return false;
    if (!root.is_object()) return false;
    m.version     = root.get_str("latest_version");
    m.min_version = root.get_str("min_supported_version");
    m.channel     = root.get_str("channel");
    m.release_date= root.get_str("release_date");
    m.iso_url     = root.get_str("iso_url");
    m.iso_sha256  = root.get_str("iso_sha256");
    m.iso_size_mb = update_manifest_size(root, "iso_size_mb");
    m.llm_url     = root.get_str("llm_model_url");
    m.llm_sha256  = root.get_str("llm_model_sha256");
    m.llm_size_mb = update_manifest_size(root, "llm_model_size_mb");
    m.force       = root.get_bool("force_update", false);
    return !m.version.empty() && !m.iso_url.empty() && !m.llm_url.empty();
}

// =============================================================================
// Progress (volatile; painted by a non-blocking GUI window)
// =============================================================================
enum UpdateState {
    UPD_IDLE = 0, UPD_FETCH, UPD_DL_ISO, UPD_DL_LLM,
    UPD_VERIFY, UPD_INSTALL, UPD_DONE, UPD_FAILED, UPD_ABORTED
};
struct UpdateProgress {
    volatile int  state;
    volatile uint64_t received;
    volatile uint64_t total;
    volatile int  pct;
    volatile bool abort;
    char current[64];
    char message[160];
    UpdateProgress() : state(UPD_IDLE), received(0), total(0), pct(0), abort(false) {
        current[0] = 0; message[0] = 0;
    }
};
static UpdateProgress g_prog;

static void prog_set(int st, const char* msg, const char* cur = 0) {
    g_prog.state = st;
    if (msg) { nefu::strncpy(g_prog.message, msg, sizeof(g_prog.message) - 1); g_prog.message[sizeof(g_prog.message) - 1] = 0; }
    if (cur)  { nefu::strncpy(g_prog.current, cur, sizeof(g_prog.current) - 1); g_prog.current[sizeof(g_prog.current) - 1] = 0; }
}

// =============================================================================
// Streaming download + SHA-256 verify + resume
// =============================================================================
struct DlCtx {
    FSNode* f;
    uint64_t cap;
    uint32_t offset;          // resume base (bytes already present)
    int status;               // HTTP status, set by platform BEFORE first chunk
    bool first;
    volatile bool* abort;
    UpdateProgress* prog;
};

static bool dl_cb(void* ud, const uint8_t* data, uint32_t len) {
    DlCtx* c = (DlCtx*)ud;
    if (c->abort && *c->abort) return false;
    if (c->first) {
        c->first = false;
        if (c->status == 200 && c->offset > 0) {
            // server ignored Range -> restart from scratch
            c->offset = 0;
            if (c->f->data) { kfree(c->f->data); c->f->data = 0; }
            c->f->size = 0; c->cap = 0;
        }
    }
    if (!vfs_stream_append(c->f, c->cap, data, len)) return false;
    if (c->prog) {
        uint64_t got = (uint64_t)c->f->size;      // includes resume base
        c->prog->received = got;
        if (c->prog->total > 0) {
            uint64_t p = (got * 100ull) / c->prog->total;
            c->prog->pct = p > 100 ? 100 : (int)p;
        } else c->prog->pct = 0;
    }
    return true;
}

// Compute the SHA-256 of a complete VFS file and compare (lowercase hex).
static bool update_hash_matches(FSNode* f, const char* expected) {
    if (!f || f->is_dir || f->size == 0) return false;
    uint8_t digest[32];
    nefu_sha256(f->data, f->size, digest);
    char hex[65];
    nefu_sha256_hex(digest, hex);
    return nefu::strcmp(hex, expected) == 0;
}

// Download `url` to `dest_part` (streaming, resumable), verify the COMPLETE
// file against `expected_sha`, then move the verified file to `dest_final`
// (overwriting). A mismatch deletes the partial file and returns false, so a
// corrupted image never reaches the install step.
static bool update_download_verify_install(const UpdateOut& out,
                                           const char* url,
                                           const char* cookie, bool add_suffix,
                                           const char* dest_part, const char* dest_final,
                                           double size_mb, const char* expected_sha,
                                           const char* label,
                                           UpdateProgress* prog, volatile bool* abort) {
    // If the installed copy already matches, skip (idempotent re-run).
    FSNode* installed = g_vfs->resolve(dest_final);
    if (installed && !installed->is_dir && update_hash_matches(installed, expected_sha)) {
        uoutf(out, "update: %s already installed & verified (SHA-256 OK), skipping\n", label);
        return true;
    }

    update_mkdirs(UPDATE_INCOMING);
    FSNode* part = g_vfs->resolve(dest_part);
    uint32_t offset = (part && !part->is_dir) ? part->size : 0;
    if (!part) part = g_vfs->create_file(dest_part);
    if (!part) { uoutf(out, "update: cannot create %s\n", dest_part); return false; }

    // effective URL: challenged hosts need "?i=1"
    String eff = url;
    if (add_suffix) eff += (strchr(url, '?') ? "&i=1" : "?i=1");

    // headers: cookie + resume Range
    String hdrs;
    if (cookie && cookie[0]) { hdrs += "Cookie: __test="; hdrs += cookie; hdrs += "\r\n"; }
    if (offset > 0) {
        // NOTE: String has no integer operator+= (a long long would silently
        // narrow to a char), so the Range header must be formatted via ksprintf.
        char rbuf[48];
        ksprintf(rbuf, sizeof(rbuf), "Range: bytes=%u-\r\n", offset);
        hdrs += rbuf;
    }

    DlCtx ctx;
    ctx.f = part; ctx.cap = part->size; ctx.offset = offset;
    ctx.status = 0; ctx.first = true; ctx.abort = abort; ctx.prog = prog;
    if (prog) {
        prog->total = (uint64_t)(size_mb * 1024.0 * 1024.0);
        prog->received = offset;
        prog->pct = prog->total ? (int)((uint64_t)offset * 100ull / prog->total) : 0;
        nefu::strncpy(prog->current, label, sizeof(prog->current) - 1);
        prog->current[sizeof(prog->current) - 1] = 0;
    }

    if (offset > 0) uoutf(out, "update: resuming %s from byte %u (Range: bytes=%u-)\n", label, offset, offset);
    else            uoutf(out, "update: downloading %s (%u MB)...\n", label, (unsigned)(size_mb + 0.5));

    bool ok = platform_http_stream_request("GET", eff.c_str(), hdrs.c_str(),
                                           &ctx, dl_cb, &ctx.status);
    if (!ok) {
        if (abort && *abort) {
            prog_set(UPD_ABORTED, "cancelled; partial kept for resume");
            uoutf(out, "update: %s cancelled (partial kept for resume)\n", label);
        } else {
            prog_set(UPD_FAILED, "network failure; partial kept for resume");
            uoutf(out, "update: %s download failed (partial kept at %s)\n", label, dest_part);
        }
        return false;
    }

    // ---- verify the COMPLETE file before install ----
    // Integrity is decided by SHA-256 only. The manifest's "*_size_mb" is a
    // rounded display value (e.g. 4.05 MB vs the real 4247552 B), so an exact
    // byte-count check would spuriously reject a valid image.
    prog_set(UPD_VERIFY, "verifying SHA-256", label);
    if (!update_hash_matches(part, expected_sha)) {
        // corrupt image: delete the bad file, do NOT let it into the update
        uint8_t digest[32];
        nefu_sha256(part->data, part->size, digest);
        char hex[65];
        nefu_sha256_hex(digest, hex);
        g_vfs->remove_node(part);
        char m[200];
        ksprintf(m, sizeof(m),
                 "update: SHA-256 MISMATCH for %s (got %u bytes, sha=%s, expected=%s) -> bad file deleted, update aborted",
                 label, part->size, hex, expected_sha);
        uoutf(out, "%s\n", m);
        prog_set(UPD_FAILED, "SHA-256 mismatch; bad file deleted");
        return false;
    }
    uoutf(out, "update: %s verified (SHA-256 OK, %u bytes)\n", label, part->size);

    // ---- back up the current version, then overwrite ----
    prog_set(UPD_INSTALL, "backing up + installing", label);
    char bdir[160];
    ksprintf(bdir, sizeof(bdir), "%s/backup-%s", UPDATE_DIR, UPDATE_CURRENT_VERSION);
    FSNode* old = g_vfs->resolve(dest_final);
    if (old && !old->is_dir) {
        update_mkdirs(bdir);
        String bpath = String(bdir) + "/" + update_basename(dest_final);
        FSNode* bf = g_vfs->create_file(bpath.c_str());
        if (bf) {
            g_vfs->write_file(bf, old->data, old->size);
            uoutf(out, "update: backed up previous %s -> %s\n", label, bpath.c_str());
        } else {
            uoutf(out, "update: WARNING backup write failed for %s\n", label);
        }
        // remove old before move so move_node overwrites cleanly
        g_vfs->remove_node(old);
    }

    // move verified part into place
    String par = dest_final;
    int ls = -1;
    for (int i = 0; dest_final[i]; i++) if (dest_final[i] == '/') ls = i;
    String parent = (ls > 0) ? String(dest_final, ls) : String("/");
    update_mkdirs(parent.c_str());
    FSNode* pdir = g_vfs->resolve(parent.c_str());
    if (!pdir || !g_vfs->move_node(part, pdir, update_basename(dest_final))) {
        uoutf(out, "update: install failed (move %s -> %s)\n", dest_part, dest_final);
        prog_set(UPD_FAILED, "install (move) failed");
        return false;
    }
    uoutf(out, "update: %s installed -> %s\n", label, dest_final);
    return true;
}

// =============================================================================
// Pipeline
// =============================================================================
static int update_run_pipeline_impl(const UpdateOut& out, const char* manifest_url,
                                    bool force, UpdateProgress* prog, volatile bool* abort) {
    const char* man = (manifest_url && manifest_url[0]) ? manifest_url : UPDATE_MANIFEST_DEFAULT;
    uoutf(out, "update: manifest %s\n", man);
    prog_set(UPD_FETCH, "fetching update manifest");

    // fetch manifest (transparently solves the InfinityFree challenge)
    uint8_t* body = 0; uint32_t blen = 0;
    String cookie, eff;
    if (!update_http_get_challenge(man, &body, &blen, cookie, eff)) {
        uoutf(out, "update: cannot fetch update manifest from %s\n", man);
        prog_set(UPD_FAILED, "manifest fetch failed");
        if (body) kfree(body);
        return 1;
    }
    String text((const char*)body, (int)blen);
    kfree(body);
    UpdateManifest m;
    if (!update_parse_manifest(text, m)) {
        uoutf(out, "update: bad update manifest (expected latest_version/iso_url/llm_model_url)\n");
        prog_set(UPD_FAILED, "bad manifest");
        return 2;
    }
    uoutf(out, "update: latest=%s channel=%s release=%s\n",
          m.version.c_str(), m.channel.c_str(), m.release_date.c_str());

    // version gate
    int cmp = update_cmp_version(m.version.c_str(), UPDATE_CURRENT_VERSION);
    if (cmp < 0) {
        uoutf(out, "update: server %s is older than current %s, nothing to do\n",
              m.version.c_str(), UPDATE_CURRENT_VERSION);
        prog_set(UPD_DONE, "no update (server older)");
        return 0;
    }
    if (cmp == 0 && !force && !m.force) {
        uoutf(out, "update: already at latest version %s, nothing to do (use 'update force')\n",
              m.version.c_str());
        prog_set(UPD_DONE, "already up to date");
        return 0;
    }
    uoutf(out, "update: installing %s (force=%d)\n", m.version.c_str(), force ? 1 : m.force ? 1 : 0);

    // Determine the challenge cookie for file downloads. The "?i=1" suffix and
    // the __test cookie only apply to the manifest host when it actually
    // presented an InfinityFree challenge (cookie non-empty). Other hosts
    // (GitHub, local mirrors) get a plain request with Range only.
    char man_host[96]; update_url_host(man, man_host, sizeof(man_host));
    String file_cookie = cookie;             // "" unless the manifest host was challenged
    bool use_suffix = false;
    if (!cookie.empty()) {
        char h[96]; update_url_host(m.iso_url.c_str(), h, sizeof(h));
        use_suffix = (nefu::strcmp(h, man_host) == 0);
    }

    // ---- download iso ----
    char iso_part[160], iso_final[160], llm_part[160], llm_final[160];
    ksprintf(iso_part,  sizeof(iso_part),  "%s/%s.part", UPDATE_INCOMING, ARTIFACT_ISO_NAME);
    ksprintf(iso_final, sizeof(iso_final), "%s/%s",      UPDATE_DIR,      ARTIFACT_ISO_NAME);
    ksprintf(llm_part,  sizeof(llm_part),  "%s/%s.part", UPDATE_INCOMING, ARTIFACT_LLM_NAME);
    ksprintf(llm_final, sizeof(llm_final), "%s/%s",      UPDATE_DIR,      ARTIFACT_LLM_NAME);

    if (abort && *abort) { prog_set(UPD_ABORTED, "cancelled"); return 3; }
    prog_set(UPD_DL_ISO, "downloading ISO", ARTIFACT_ISO_NAME);
    if (!update_download_verify_install(out, m.iso_url.c_str(),
                                        file_cookie.c_str(), use_suffix,
                                        iso_part, iso_final, m.iso_size_mb,
                                        m.iso_sha256.c_str(), "ISO",
                                        prog, abort)) {
        return 4;
    }
    if (abort && *abort) { prog_set(UPD_ABORTED, "cancelled"); return 3; }

    // ---- download llm model (GitHub: 302 redirect + Range) ----
    prog_set(UPD_DL_LLM, "downloading LLM model", ARTIFACT_LLM_NAME);
    if (!update_download_verify_install(out, m.llm_url.c_str(),
                                        "", false,   // GitHub: no cookie, no suffix
                                        llm_part, llm_final, m.llm_size_mb,
                                        m.llm_sha256.c_str(), "LLM model",
                                        prog, abort)) {
        return 5;
    }

    // ---- persist a state marker ----
    {
        String st = "{ \"installed_version\": \"";
        st += m.version; st += "\", \"release_date\": \""; st += m.release_date;
        st += "\", \"channel\": \""; st += m.channel;
        st += "\", \"installed_at\": \"now\" }";
        FSNode* sf = g_vfs->create_file(UPDATE_STATE);
        if (sf) g_vfs->write_file(sf, (const uint8_t*)st.c_str(), (uint32_t)st.len());
    }

    uoutf(out, "update: DONE — nefuOS %s installed\n", m.version.c_str());
    prog_set(UPD_DONE, "update complete");
    return 0;
}

// synchronous full-pipeline runner (headless host verification)
int update_run_pipeline(const UpdateOut& out, const char* manifest_url, bool force) {
    volatile bool abort = false;
    return update_run_pipeline_impl(out, manifest_url, force, 0, &abort);
}

// =============================================================================
// Non-blocking worker (terminal "update apply")
// =============================================================================
struct UpdateJob {
    String manifest_url;
    bool force;
};
static void update_worker(void* arg) {
    UpdateJob* job = (UpdateJob*)arg;
    UpdateOut silent; silent.print = 0; silent.ud = 0;
    volatile bool* abort = &g_prog.abort;
    int rc = update_run_pipeline_impl(silent, job->manifest_url.c_str(),
                                      job->force, &g_prog, abort);
    if (g_prog.abort) prog_set(UPD_ABORTED, "cancelled");
    else if (rc == 0) prog_set(UPD_DONE, "update complete");
    else if (g_prog.state != UPD_FAILED) prog_set(UPD_FAILED, "update failed");
    delete job;
}

// =============================================================================
// GUI progress window (non-blocking; re-painted every frame by the WM)
// =============================================================================
struct ProgWinState { Window* w; };
static void prog_win_paint(Window* w) {
    ProgWinState* st = (ProgWinState*)w->userdata;
    (void)st;
    Surface& s = w->back;
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, color::WHITE);
    gfx::fillrect(s, 0, 0, w->content_w, 24, color::PANEL);
    gfx::text(s, 8, 4, "nefuOS Update", color::TEXT, color::PANEL);

    const char* st_text = "Idle";
    switch (g_prog.state) {
        case UPD_FETCH:   st_text = "Checking for updates..."; break;
        case UPD_DL_ISO:  st_text = "Downloading ISO"; break;
        case UPD_DL_LLM:  st_text = "Downloading LLM model"; break;
        case UPD_VERIFY:  st_text = "Verifying SHA-256"; break;
        case UPD_INSTALL: st_text = "Backing up + installing"; break;
        case UPD_DONE:    st_text = "Done"; break;
        case UPD_FAILED:  st_text = "Failed"; break;
        case UPD_ABORTED: st_text = "Cancelled"; break;
    }
    gfx::text(s, 12, 34, st_text, color::TEXT, color::WHITE);

    if (g_prog.current[0]) gfx::text(s, 12, 52, g_prog.current, color::TEXT2, color::WHITE);

    // progress bar
    int bw = w->content_w - 24;
    gfx::fillrect(s, 12, 76, bw, 18, 0xFFE0E0E0);
    int fill = bw * g_prog.pct / 100;
    if (fill < 0) fill = 0; if (fill > bw) fill = bw;
    gfx::fillrect(s, 12, 76, fill, 18, 0xFF2196F3);
    char pct[32];
    ksprintf(pct, sizeof(pct), "%d%%", g_prog.pct);
    gfx::text(s, 20, 78, pct, color::WHITE, 0xFF2196F3);

    if (g_prog.total > 0) {
        char b[80];
        ksprintf(b, sizeof(b), "%llu / %llu bytes", (unsigned long long)g_prog.received,
                 (unsigned long long)g_prog.total);
        gfx::text(s, 12, 100, b, color::TEXT2, color::WHITE);
    }
    if (g_prog.message[0]) gfx::text(s, 12, 118, g_prog.message, color::TEXT2, color::WHITE);
}
static void prog_win_close(Window* w) {
    ProgWinState* st = (ProgWinState*)w->userdata;
    delete st;
    w->userdata = 0;
    g_prog.abort = true;   // closing the window cancels the download
}
static void prog_win_launch() {
    Window* w = g_wm->create_window("nefuOS Update", 120, 90, 360, 150);
    if (!w) return;
    ProgWinState* st = new ProgWinState();
    st->w = w;
    w->userdata = st;
    w->on_paint = prog_win_paint;
    w->on_close = prog_win_close;
    g_wm->raise(w);
}

// =============================================================================
// Terminal command
// =============================================================================
static void update_status(TermState* t) {
    term_ext_print(t, "update: current version ");
    term_ext_print(t, UPDATE_CURRENT_VERSION);
    String line = "update: manifest ";
    line += UPDATE_MANIFEST_DEFAULT;
    term_ext_print(t, line.c_str());
    FSNode* d = g_vfs->resolve(UPDATE_DIR);
    if (!d) { term_ext_print(t, "update: no artifacts installed yet (/user/updates absent)"); return; }
    FSNode* iso = g_vfs->resolve((String(UPDATE_DIR) + "/" + ARTIFACT_ISO_NAME).c_str());
    FSNode* llm = g_vfs->resolve((String(UPDATE_DIR) + "/" + ARTIFACT_LLM_NAME).c_str());
    FSNode* ip  = g_vfs->resolve((String(UPDATE_INCOMING) + "/" + ARTIFACT_ISO_NAME + ".part").c_str());
    FSNode* lp  = g_vfs->resolve((String(UPDATE_INCOMING) + "/" + ARTIFACT_LLM_NAME + ".part").c_str());
    char b[128];
    if (iso) { ksprintf(b, sizeof(b), "update: %s %u bytes (installed)", ARTIFACT_ISO_NAME, iso->size); term_ext_print(t, b); }
    else      term_ext_print(t, "update: ISO not installed");
    if (llm) { ksprintf(b, sizeof(b), "update: %s %u bytes (installed)", ARTIFACT_LLM_NAME, llm->size); term_ext_print(t, b); }
    else      term_ext_print(t, "update: LLM model not installed");
    if (ip) { ksprintf(b, sizeof(b), "update: resume point: ISO partial %u bytes", ip->size); term_ext_print(t, b); }
    if (lp) { ksprintf(b, sizeof(b), "update: resume point: LLM partial %u bytes", lp->size); term_ext_print(t, b); }
}

static void update_usage(TermState* t) {
    term_ext_print(t, "update - nefuOS OTA updater");
    term_ext_print(t, "usage:");
    term_ext_print(t, "  update                     check for an update");
    term_ext_print(t, "  update check [url]         check for an update (custom manifest)");
    term_ext_print(t, "  update apply [url]         download + verify + install (non-blocking)");
    term_ext_print(t, "  update force  [url]        apply even if already latest");
    term_ext_print(t, "  update fast   [url]        FAST: sync incremental update (only missing/dirty");
    term_ext_print(t, "                             artifacts are downloaded; no GUI; idempotent)");
    term_ext_print(t, "  update system              update the OS through the npkg channel (nefuos-system)");
    term_ext_print(t, "  update status              show installed artifacts + resume points");
    term_ext_print(t, "  update cancel              cancel a running download");
    term_ext_print(t, "shortcut: Ctrl+Shift+U       quick update with progress window");
    term_ext_print(t, "manifest: ");
    term_ext_print(t, UPDATE_MANIFEST_DEFAULT);
}

// "check": fetch manifest + compare, print to the terminal (fast, ~1s)
static void update_run_check(TermState* t, const char* url) {
    UpdateOut out; out.print = update_term_print; out.ud = t;
    update_run_pipeline(out, url, false);
}

// "apply/force": run on a worker thread, show GUI progress, never block the UI
static void update_run_apply(TermState* t, const char* url, bool force) {
    if (g_prog.state == UPD_DL_ISO || g_prog.state == UPD_DL_LLM ||
        g_prog.state == UPD_FETCH || g_prog.state == UPD_INSTALL ||
        g_prog.state == UPD_VERIFY) {
        term_ext_print(t, "update: a download is already running (use 'update cancel' to stop)");
        return;
    }
    g_prog.abort = false;
    g_prog.received = 0; g_prog.total = 0; g_prog.pct = 0;
    g_prog.state = UPD_FETCH;
    prog_set(UPD_FETCH, "starting update...");
    prog_win_launch();
    UpdateJob* job = new UpdateJob();
    job->manifest_url = url ? url : "";
    job->force = force;
    platform_thread_create_stk(update_worker, job, 256 * 1024);
    term_ext_print(t, force ? "update: applying (forced)... see the progress window"
                            : "update: applying... see the progress window");
}

// "fast": synchronous incremental update. update_download_verify_install is
// idempotent - any artifact already installed whose SHA-256 matches is skipped,
// so a second run is near-instant and only missing/corrupt files are fetched.
static void update_run_fast(TermState* t, const char* url) {
    UpdateOut out; out.print = update_term_print; out.ud = t;
    volatile bool abort = false;
    term_ext_print(t, "update: fast incremental update (sync, no GUI)...");
    int rc = update_run_pipeline_impl(out, url, false, 0, &abort);
    term_ext_print(t, rc == 0 ? "update: fast update finished"
                              : "update: fast update failed (see log above)");
}

// "system": update the OS itself through the npkg channel. The system image is
// published in the npkg registry as a normal package named "nefuos-system"; we
// refresh the registry and let npkg install it (same code path as any app).
static void update_run_system(TermState* t) {
    term_ext_print(t, "update: system update via npkg channel...");
    // 1) refresh the npkg registry
    {
        const char* a[2] = { "npkg", "update" };
        npkg_cmd(t, 2, a);
    }
    // 2) find "nefuos-system" in the local registry
    FSNode* rf = g_vfs->resolve("/pkg/package.json");
    if (!rf || rf->is_dir || rf->size == 0) {
        term_ext_print(t, "update: npkg registry missing (npkg update failed?)");
        return;
    }
    String reg((const char*)rf->data, (int)rf->size);
    nefu::json::Value root;
    if (!nefu::json::parse(reg, root) || !root.is_object() || !root.has("app")) {
        term_ext_print(t, "update: bad npkg registry");
        return;
    }
    const nefu::json::Value* app = root.get("app");
    const nefu::json::Member* sys = 0;
    for (int i = 0; app && i < app->obj.size(); i++) {
        if (app->obj[i].key == "nefuos-system") { sys = &app->obj[i]; break; }
    }
    if (!sys) {
        term_ext_print(t, "update: no 'nefuos-system' package in npkg registry");
        term_ext_print(t, "update: (publish the OS image as nefuos-system on npkg.site.je)");
        return;
    }
    const nefu::json::Value* sv = sys->val.get("version");
    String ver = sv && !sv->s.empty() ? sv->s : String("?");
    String line = "update: found system package nefuos-system v" + ver + " - installing via npkg";
    term_ext_print(t, line.c_str());
    // 3) install/upgrade it through npkg (same code path as any package)
    {
        const char* a[3] = { "npkg", "install", "nefuos-system" };
        npkg_cmd(t, 3, a);
    }
    term_ext_print(t, "update: system update complete (restart to apply)");
}

void update_cmd(TermState* t, int argc, const char** argv) {

    if (argc < 2) { update_run_check(t, 0); return; }   // bare "update" -> check
    const char* sub = argv[1];
    if (nefu::strcmp(sub, "status") == 0) { update_status(t); return; }
    if (nefu::strcmp(sub, "cancel") == 0) {
        g_prog.abort = true;
        term_ext_print(t, "update: cancel requested");
        return;
    }
    const char* url = (argc >= 3) ? argv[2] : 0;
    if (nefu::strcmp(sub, "check") == 0) { update_run_check(t, url); return; }
    if (nefu::strcmp(sub, "apply") == 0) { update_run_apply(t, url, false); return; }
    if (nefu::strcmp(sub, "force") == 0) { update_run_apply(t, url, true);  return; }
    if (nefu::strcmp(sub, "fast") == 0)   { update_run_fast(t, url); return; }
    if (nefu::strcmp(sub, "system") == 0) { update_run_system(t); return; }
    update_usage(t);
}


// global shortcut entry (Ctrl+Shift+U): quick update with the progress GUI,
// no terminal needed. Refuses to stack on a running download.
void update_global_shortcut() {
    if (g_prog.state == UPD_DL_ISO || g_prog.state == UPD_DL_LLM ||
        g_prog.state == UPD_FETCH || g_prog.state == UPD_INSTALL ||
        g_prog.state == UPD_VERIFY) {
        return;   // a download is already running
    }
    g_prog.abort = false;
    g_prog.received = 0; g_prog.total = 0; g_prog.pct = 0;
    prog_set(UPD_FETCH, "starting quick update (Ctrl+Shift+U)...");
    prog_win_launch();
    UpdateJob* job = new UpdateJob();
    job->manifest_url = "";
    job->force = false;
    platform_thread_create_stk(update_worker, job, 256 * 1024);
}

} // namespace nefu