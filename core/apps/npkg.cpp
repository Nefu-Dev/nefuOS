// =============================================================================
//  npkg.cpp — nefuOS npkg command-line package manager
// -----------------------------------------------------------------------------
//  Subcommands:
//    npkg                         usage + installed/registry summary
//    npkg update                 re-download package.json from the fixed
//                                server npkg.site.je (retry limit: 3)
//    npkg install <pkg>           install a package from the npkg server
//    npkg remove <pkg>            remove an installed package
//    npkg start <pkg>[/<script>] [args...]  run a package entry (.ncode)
//    npkg list                    list installed packages
//
//  The registry file format matches the spec:
//    { "app": { "<name>": { "Introduction":"...", "version":"1.0.0",
//                           "server_in":"/htdocs/<rel>", "deps":[...] } } }
//  "server_in" carries the full server path; npkg truncates everything up to
//  and including "htdocs/" to get the package's relative directory on the
//  server. Package content is fetched from <origin>/<rel>/...
//
//  Constraints: no STL / no exceptions; kalloc/kfree + nefu::String/List.
// =============================================================================
#include "npkg.h"
#include "ncode.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include "../vfs/nvfs.h"
#include "../platform.h"
#include "../lib/json.h"
#include "../klib/aes128.h"
#include "term_ext.h"

namespace nefu {

// implemented below (kept at file scope so the anonymous namespace can use it)
void npkg_update_inner(TermState* t);

namespace {

const char* NPKG_DEFAULT_SERVER = "http://npkg.site.je";
const char* NPKG_REGISTRY       = "/package.json";   // real path (probed 2026-10)
const char* NPKG_ROOT           = "/pkg";
const int   NPKG_MAX_RETRY      = 3;        // reconnect limit
const int   NPKG_RETRY_SLEEP_MS = 400;

static void npkg_out(TermState* t, const char* s) { term_ext_print(t, s); }
static void npkg_out(TermState* t, const String& s) { term_ext_print(t, s.c_str()); }

// Persist the current VFS tree into the NVFS image and flush it to disk right
// away, so installed/removed packages survive a crash or a reboot without
// waiting for a graceful shutdown (which is the only other save point).
#ifdef NCODE_HOST_TEST
static void npkg_persist(TermState* t) { (void)t; }   // host test: no NVFS
#else
static void npkg_persist(TermState* t) {
    if (!nvfs::is_mounted() || !g_vfs) return;
    nvfs::vfs_to_nvfs();
    uint8_t* img = 0;
    uint32_t isz = 0;
    if (nvfs::image(&img, &isz) && img) {
        platform_fs_save(img, isz);
        char m[96];
        ksprintf(m, sizeof(m), "npkg: persisted to NVFS (%u bytes)", (unsigned)isz);
        npkg_out(t, m);
    } else {
        npkg_out(t, "npkg: NVFS persist failed");
    }
}
#endif

// mkdir -p
static FSNode* npkg_mkdirs(TermState* t, const char* path) {
    if (!path || !*path) return 0;
    if (g_vfs->resolve(path)) return g_vfs->resolve(path);
    // build path part by part
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
                if (!d) { (void)t; return 0; }
            }
            acc = part;
        }
        if (!slash) break;
        p = slash + 1;
    }
    return g_vfs->resolve(path);
}

// recursive remove
static void npkg_rmtree(FSNode* n) {
    if (!n) return;
    if (n->is_dir) {
        // remove children in reverse order (leaf first)
        for (int i = n->children.size() - 1; i >= 0; i--) {
            npkg_rmtree(n->children[i]);
        }
    }
    g_vfs->remove_node(n);
}

// truncate server_in: keep everything after the LAST "htdocs/"
static String npkg_rel_of(const char* server_in) {
    String s = server_in ? server_in : "";
    const char* hit = 0;
    for (const char* p = s.c_str(); *p; ) {
        const char* f = nefu::strstr(p, "htdocs/");
        if (!f) break;
        hit = f + 7;                     // skip "htdocs/"
        p = hit;
    }
    if (hit) return String(hit);
    // no htdocs/ -> strip leading slashes
    const char* q = s.c_str();
    while (*q == '/') q++;
    return String(q);
}

// ---- InfinityFree / npkg.site.je JS challenge bypass ----
// Some servers answer every path with a small JS challenge page instead of the
// real file. To pass it: 1) GET the URL -> a,b,c parameters in
// `slowAES.decrypt(c,2,a,b)`; 2) the cookie is the AES-128-CBC decrypt of c
// (key=a, iv=b), rendered as lowercase hex; 3) re-GET with `Cookie: __test=`.
// The cookie is per-request, so it must be recomputed on every fetch.
static int npkg_hexval(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static const char* npkg_find(const char* d, uint32_t len, const char* sub) {
    uint32_t sl = nefu::strlen(sub);
    for (uint32_t i = 0; i + sl <= len; i++) {
        uint32_t j;
        for (j = 0; j < sl; j++) if (d[i + j] != sub[j]) break;
        if (j == sl) return d + i;
    }
    return 0;
}

// returns true when the body is a challenge page and a/b/c were extracted
static bool npkg_parse_challenge(const uint8_t* body, uint32_t blen,
                                 uint8_t key[16], uint8_t iv[16], uint8_t c[16]) {
    if (!npkg_find((const char*)body, blen, "slowAES")) return false;
    uint32_t pos = 0;
    for (int n = 0; n < 3; n++) {
        const char* m = npkg_find((const char*)body + pos, blen - pos, "toNumbers(\"");
        if (!m) return false;
        uint32_t off = (uint32_t)(m - (const char*)body) + (uint32_t)nefu::strlen("toNumbers(\"");
        if (off + 32 > blen) return false;
        uint8_t tmp[16];
        for (int k = 0; k < 16; k++) {
            int hi = npkg_hexval((char)body[off + k*2]);
            int lo = npkg_hexval((char)body[off + k*2 + 1]);
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

// one fetch that transparently solves the challenge when present.
// on success *out is a kalloc'd buffer (kfree) and the function returns true.
static bool npkg_http_get_core(const char* url, uint8_t** out, uint32_t* out_len) {
    *out = 0; *out_len = 0;
    uint8_t* body = 0;
    uint32_t blen = 0;
    if (!platform_http_get(url, &body, &blen) || !body) {
        if (body) kfree(body);
        return false;
    }
    uint8_t key[16], iv[16], c[16];
    if (npkg_parse_challenge(body, blen, key, iv, c)) {
        kfree(body);                       // discard the challenge page
        uint8_t plain[16];
        nefu::aes128_cbc_decrypt(key, iv, c, 16, plain);
        char hdr[64];
        int w = 0;
        static const char* P = "Cookie: __test=";
        for (int i = 0; P[i]; i++) hdr[w++] = P[i];
        for (int i = 0; i < 16; i++) {
            static const char* HEX = "0123456789abcdef";
            hdr[w++] = HEX[(plain[i] >> 4) & 0xf];
            hdr[w++] = HEX[plain[i] & 0xf];
        }
        hdr[w++] = '\r'; hdr[w++] = '\n'; hdr[w] = 0;
        int status = 0;
        uint8_t* rb = 0; uint32_t rlen = 0;
        // the challenge page redirects to <url>?i=1; the server only serves the
        // real file when the i=1 flag is present together with the __test cookie.
        String u2(url);
        if (!npkg_find(url, (uint32_t)nefu::strlen(url), "?")) u2 += "?i=1";
        bool ok = platform_http_request("GET", u2.c_str(), hdr, 0, 0, &status, &rb, &rlen);
        if (ok && rb && rlen > 0) {
            *out = rb; *out_len = rlen;
            return true;
        }
        if (rb) kfree(rb);
        return false;
    }
    // normal file
    *out = body; *out_len = blen;
    return true;
}

// fetch <origin>/<rel> with reconnect limit; returns kalloc'd buffer (kfree).
static bool npkg_fetch(TermState* t, const String& origin, const String& rel,
                        uint8_t** out, uint32_t* out_len) {
    *out = 0; *out_len = 0;
    String url = origin;
    url += "/";
    url += rel;
    for (int attempt = 1; attempt <= NPKG_MAX_RETRY; attempt++) {
        uint8_t* body = 0;
        uint32_t blen = 0;
        if (npkg_http_get_core(url.c_str(), &body, &blen) && body) {
            *out = body;
            *out_len = blen;
            return true;
        }
        if (body) kfree(body);
        if (attempt < NPKG_MAX_RETRY) {
            char msg[96];
            ksprintf(msg, sizeof(msg), "npkg: fetch failed (%d/%d), retrying...",
                     attempt, NPKG_MAX_RETRY - 1);
            npkg_out(t, msg);
            platform_thread_sleep(NPKG_RETRY_SLEEP_MS);
        }
    }
    return false;
}

// fetch one file into the VFS under dst (creates parents)
static bool npkg_fetch_to_vfs(TermState* t, const String& origin, const String& rel,
                              const char* dst) {
    uint8_t* body = 0;
    uint32_t blen = 0;
    if (!npkg_fetch(t, origin, rel, &body, &blen)) {
        npkg_out(t, "npkg: download failed (reconnect limit reached): ");
        npkg_out(t, rel);
        return false;
    }
    // create parent directory only (never a dir at the file path itself)
    String par = dst;
    {
        int ls = -1;
        for (int i = 0; dst[i]; i++) if (dst[i] == '/') ls = i;
        if (ls >= 0) par = String(dst, ls);
        if (par.len() == 0) par = "/";
    }
    npkg_mkdirs(t, par.c_str());
    FSNode* f = g_vfs->resolve(dst);
    if (!f) f = g_vfs->create_file(dst);
    bool ok = false;
    if (f) {
        ok = g_vfs->write_file(f, body, blen);
    }
    kfree(body);
    return ok;
}

// read a VFS file into a String ("" when missing)
static bool npkg_read_file(const char* path, String& out) {
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir || f->size == 0) return false;
    out = String((const char*)f->data, (int)f->size);
    return true;
}

// fixed registry server (custom-source syntax was removed)
static String npkg_origin() {
    return String(NPKG_DEFAULT_SERVER);
}

// ---- registry helpers ----
// The live server returns the registry as a sequence of concatenated JSON
// documents ({"app":{...}}\n{"app":{...}}...), so parsing also supports the
// multi-document form by merging every document's "app" members.
static bool npkg_registry_parse(const String& text, nefu::json::Value& root) {
    const char* err = 0;
    if (nefu::json::parse(text, root, &err)) {
        return root.is_object() && root.has("app") && root.get("app")->is_object();
    }
    nefu::json::Value merged;
    merged.type = nefu::json::JSON_OBJECT;
    nefu::json::Value appObj;
    appObj.type = nefu::json::JSON_OBJECT;
    merged.set("app", appObj);
    int n = text.len();
    int pos = 0;
    bool any = false;
    while (pos < n) {
        int st = pos;
        while (st < n && (text[st] == ' ' || text[st] == '\n' || text[st] == '\r' || text[st] == '\t')) st++;
        if (st >= n || text[st] != '{') break;
        int depth = 0, en = st;
        bool in_str = false;
        for (; en < n; en++) {
            char ch = text[en];
            if (in_str) { if (ch == '"' && text[en - 1] != '\\') in_str = false; continue; }
            if (ch == '"') { in_str = true; continue; }
            if (ch == '{') depth++;
            else if (ch == '}') { depth--; if (depth == 0) { en++; break; } }
        }
        if (depth != 0) break;
        nefu::json::Value doc;
        const char* e2 = 0;
        if (nefu::json::parse(text.substr(st, en - st), doc, &e2) &&
            doc.is_object() && doc.has("app")) {
            const nefu::json::Value* app = doc.get("app");
            if (app && app->is_object()) {
                nefu::json::Value* ra = merged.get("app");
                for (int i = 0; i < app->obj.size(); i++) {
                    ra->set(app->obj[i].key.c_str(), app->obj[i].val);
                    any = true;
                }
            }
        }
        pos = en;
    }
    if (any) { root = merged; return true; }
    return false;
}

static int npkg_registry_count(const nefu::json::Value& root) {
    const nefu::json::Value* app = root.get("app");
    return app ? app->obj.size() : 0;
}

static const nefu::json::Member* npkg_registry_find(const nefu::json::Value& root,
                                                     const char* name) {
    const nefu::json::Value* app = root.get("app");
    if (!app) return 0;
    for (int i = 0; i < app->obj.size(); i++) {
        if (app->obj[i].key == name) return &app->obj[i];
    }
    return 0;
}

// ensure a registry copy exists locally; refresh when missing
static bool npkg_ensure_registry(TermState* t) {
    FSNode* f = g_vfs->resolve(NPKG_REGISTRY);
    if (f && !f->is_dir && f->size > 0) return true;
    npkg_out(t, "npkg: local registry missing, updating...");
    npkg_update_inner(t);
    return g_vfs->resolve(NPKG_REGISTRY) != 0;
}

// ---- subcommands ----
static void npkg_usage(TermState* t) {
    npkg_out(t, "npkg - nefuOS package manager");
    npkg_out(t, "usage:");
    npkg_out(t, "  npkg update                     refresh package.json from the server");
    npkg_out(t, "  npkg install <pkg>              install a package");
    npkg_out(t, "  npkg remove <pkg>               remove an installed package");
    npkg_out(t, "  npkg start <pkg>[/<script>] [args...]   run a package entry (.ncode)");
    npkg_out(t, "  npkg list                       list installed packages");
    npkg_out(t, "  npkg upgrade                    refresh registry + upgrade ALL installed packages");
    npkg_out(t, "server: " + String(NPKG_DEFAULT_SERVER) + " (fixed)");
}

static void npkg_list(TermState* t) {
    FSNode* d = g_vfs->resolve(NPKG_ROOT);
    npkg_out(t, "installed packages:");
    if (d && d->is_dir) {
        int n = 0;
        for (int i = 0; i < d->children.size(); i++) {
            FSNode* c = d->children[i];
            if (c->is_dir) {
                npkg_out(t, "  " + c->name);
                n++;
            }
        }
        if (n == 0) npkg_out(t, "  (none)");
    } else {
        npkg_out(t, "  (none)");
    }
    // registry summary
    String reg;
    if (npkg_read_file(NPKG_REGISTRY, reg)) {
        nefu::json::Value root;
        if (npkg_registry_parse(reg, root)) {
            char msg[96];
            ksprintf(msg, sizeof(msg), "registry: %d package(s) available",
                     npkg_registry_count(root));
            npkg_out(t, msg);
        }
    }
}

} // namespace

// (kept out of the anonymous namespace so npkg_ensure_registry can call it)
void npkg_update_inner(TermState* t) {
    String origin = String(NPKG_DEFAULT_SERVER);
    // normalize: strip trailing '/'
    while (origin.len() > 0 && origin[origin.len() - 1] == '/') {
        origin = origin.substr(0, origin.len() - 1);
    }
    npkg_out(t, "npkg: updating from " + origin);
    uint8_t* body = 0;
    uint32_t blen = 0;
    if (!npkg_fetch(t, origin, "package.json", &body, &blen)) {
        npkg_out(t, "npkg: update failed (reconnect limit reached)");
        return;
    }
    String text((const char*)body, (int)blen);
    kfree(body);
    nefu::json::Value root;
    if (!npkg_registry_parse(text, root)) {
        npkg_out(t, "npkg: bad package.json (expected { \"app\": { ... } })");
        return;
    }
    npkg_mkdirs(t, "/pkg");
    FSNode* f = g_vfs->create_file(NPKG_REGISTRY);
    if (!f || !g_vfs->write_file(f, (const uint8_t*)text.c_str(), (uint32_t)text.len())) {
        npkg_out(t, "npkg: failed to store registry");
        return;
    }
    char msg[96];
    ksprintf(msg, sizeof(msg), "npkg: registry updated: %d package(s)",
             npkg_registry_count(root));
    npkg_out(t, msg);
    npkg_persist(t);
}

namespace {
// ---- direct-URL install helpers ----
// fetch a full URL with the reconnect limit (returns kalloc'd buffer, kfree)
static bool npkg_fetch_url(TermState* t, const String& url, uint8_t** out, uint32_t* out_len) {
    *out = 0; *out_len = 0;
    for (int attempt = 1; attempt <= NPKG_MAX_RETRY; attempt++) {
        uint8_t* body = 0;
        uint32_t blen = 0;
        if (npkg_http_get_core(url.c_str(), &body, &blen) && body) {
            *out = body; *out_len = blen; return true;
        }
        if (body) kfree(body);
        if (attempt < NPKG_MAX_RETRY) {
            npkg_out(t, "npkg: fetch failed (retrying)");
            platform_thread_sleep(NPKG_RETRY_SLEEP_MS);
        }
    }
    return false;
}

// directory part of a URL (everything before the last '/')
static String npkg_url_dir(const String& u) {
    int ls = -1;
    for (int i = 0; i < u.len(); i++) if (u[i] == '/') ls = i;
    if (ls < 0) return u;
    return u.substr(0, ls);
}

// fetch a full URL into the VFS under dst (creates parents)
static bool npkg_fetch_url_to_vfs(TermState* t, const String& url, const char* dst) {
    uint8_t* body = 0;
    uint32_t blen = 0;
    if (!npkg_fetch_url(t, url, &body, &blen)) {
        npkg_out(t, "npkg: download failed (reconnect limit reached): " + url);
        return false;
    }
    String par = dst;
    {
        int ls = -1;
        for (int i = 0; dst[i]; i++) if (dst[i] == '/') ls = i;
        if (ls >= 0) par = String(dst, ls);
        if (par.len() == 0) par = "/";
    }
    npkg_mkdirs(t, par.c_str());
    FSNode* f = g_vfs->resolve(dst);
    if (!f) f = g_vfs->create_file(dst);
    bool ok = f && g_vfs->write_file(f, body, blen);
    kfree(body);
    return ok;
}

// direct install: "pkg:URL"  (URL like "npkg.site.je/pkg/main.ncode")
static void npkg_install_url(TermState* t, const char* name, const String& raw) {
    String url = raw;
    if (url.find("https://") != 0 && url.find("http://") != 0) {
        url = String("http://") + url;      // add scheme if missing
    }
    String pkgdir = String("/pkg/") + name;
    FSNode* old = g_vfs->resolve(pkgdir.c_str());
    if (old && old->is_dir) npkg_rmtree(old);
    if (!npkg_mkdirs(t, pkgdir.c_str())) {
        npkg_out(t, "npkg: cannot create package dir");
        return;
    }
    if (!npkg_fetch_url_to_vfs(t, url, String(pkgdir + "/main.ncode").c_str())) {
        npkg_out(t, "npkg: install incomplete (package dir kept)");
        return;
    }
    npkg_out(t, "npkg: installed " + String(name) + " (direct URL) -> " + pkgdir);
    // optional manifest.json in the same directory: extra files
    String mdir = npkg_url_dir(url);
    {
        uint8_t* body = 0; uint32_t blen = 0;
        String manurl = mdir; manurl += "/manifest.json";
        if (npkg_fetch_url(t, manurl, &body, &blen) && body) {
            String txt((const char*)body, (int)blen);
            kfree(body);
            nefu::json::Value manroot;
            const char* err = 0;
            if (nefu::json::parse(txt, manroot, &err) && manroot.is_object() && manroot.has("files")) {
                const nefu::json::Value* files = manroot.get("files");
                for (int i = 0; files && i < files->size(); i++) {
                    if (!files->at(i).is_string()) continue;
                    String frel = files->at(i).s;
                    if (frel == "main.ncode") continue;
                    String dst = pkgdir; dst += "/"; dst += frel;
                    if (npkg_fetch_url_to_vfs(t, mdir + "/" + frel, dst.c_str())) {
                        npkg_out(t, "  + " + frel);
                    } else {
                        npkg_out(t, "  ! " + frel + " (failed)");
                    }
                }
            }
        } else if (body) kfree(body);
    }
    npkg_persist(t);
    npkg_out(t, "  run: npkg start " + String(name));
}

static void npkg_install(TermState* t, const char* name) {
    if (!name || !*name) { npkg_out(t, "usage: npkg install <pkg>"); return; }
    // direct-URL form: "pkg:http://host/.../main.ncode"
    int colon = -1;
    for (int i = 0; name[i]; i++) if (name[i] == ':' ) { colon = i; break; }
    if (colon >= 0) {
        String pkg(name, colon);
        String url(name + colon + 1);
        if (pkg.empty() || url.empty()) { npkg_out(t, "usage: npkg install <pkg>[:<URL>]"); return; }
        npkg_install_url(t, pkg.c_str(), url);
        return;
    }

    if (!npkg_ensure_registry(t)) {
        npkg_out(t, "npkg: no registry available (try 'npkg update')");
        return;
    }
    String reg;
    if (!npkg_read_file(NPKG_REGISTRY, reg)) {
        npkg_out(t, "npkg: registry read failed");
        return;
    }
    nefu::json::Value root;
    if (!npkg_registry_parse(reg, root)) {
        npkg_out(t, "npkg: registry corrupt");
        return;
    }
    const nefu::json::Member* m = npkg_registry_find(root, name);
    if (!m) {
        npkg_out(t, "npkg: package not found in registry: " + String(name));
        return;
    }
    const nefu::json::Value& meta = m->val;
    String version = meta.get_str("version", "?");
    String intro   = meta.get_str("Introduction", "");
    String server_in = meta.get_str("server_in", "");
    String rel = npkg_rel_of(server_in.c_str());
    if (rel.empty()) {
        npkg_out(t, "npkg: package has no server path");
        return;
    }
    String origin = npkg_origin();

    npkg_out(t, "npkg: installing " + String(name) + " v" + version);
    if (!intro.empty()) npkg_out(t, "  " + intro);

    String pkgdir = String("/pkg/") + name;
    // remove an existing install first (clean reinstall)
    FSNode* old = g_vfs->resolve(pkgdir.c_str());
    if (old && old->is_dir) npkg_rmtree(old);
    if (!npkg_mkdirs(t, pkgdir.c_str())) {
        npkg_out(t, "npkg: cannot create package dir");
        return;
    }

    // entry file: main.ncode (fall back to <name>.ncode)
    bool got_entry = false;
    {
        uint8_t* body = 0;
        uint32_t blen = 0;
        String ent = rel;
        ent += "/main.ncode";
        if (npkg_fetch(t, origin, ent, &body, &blen) && body) {
            FSNode* f = g_vfs->create_file(String(pkgdir + "/main.ncode").c_str());
            if (f) g_vfs->write_file(f, body, blen);
            kfree(body);
            got_entry = true;
        } else if (body) kfree(body);
    }
    if (!got_entry) {
        String ent = rel;
        ent += "/";
        ent += name;
        ent += ".ncode";
        uint8_t* body = 0;
        uint32_t blen = 0;
        if (npkg_fetch(t, origin, ent, &body, &blen) && body) {
            FSNode* f = g_vfs->create_file(String(pkgdir + "/main.ncode").c_str());
            if (f) g_vfs->write_file(f, body, blen);
            kfree(body);
            got_entry = true;
        } else if (body) kfree(body);
    }
    if (!got_entry) {
        npkg_out(t, "npkg: no main.ncode / <pkg>.ncode entry on the server");
        npkg_out(t, "npkg: install incomplete (package dir kept)");
        return;
    }

    // optional manifest.json with an extra file list
    {
        uint8_t* body = 0;
        uint32_t blen = 0;
        String man = rel;
        man += "/manifest.json";
        if (npkg_fetch(t, origin, man, &body, &blen) && body) {
            String txt((const char*)body, (int)blen);
            kfree(body);
            nefu::json::Value manroot;
            const char* err = 0;
            if (nefu::json::parse(txt, manroot, &err) && manroot.is_object() && manroot.has("files")) {
                const nefu::json::Value* files = manroot.get("files");
                for (int i = 0; files && i < files->size(); i++) {
                    if (files->at(i).is_string()) {
                        String frel = files->at(i).s;
                        // skip the entry (already installed)
                        if (frel == "main.ncode") continue;
                        String dst = pkgdir;
                        dst += "/";
                        dst += frel;
                        if (npkg_fetch_to_vfs(t, origin, rel + "/" + frel, dst.c_str())) {
                            npkg_out(t, "  + " + frel);
                        } else {
                            npkg_out(t, "  ! " + frel + " (failed)");
                        }
                    }
                }
            }
        } else if (body) kfree(body);
    }

    npkg_out(t, "npkg: installed " + String(name) + " v" + version + " -> " + pkgdir);
    const nefu::json::Value* deps = meta.get("deps");
    if (deps && deps->is_array() && deps->size() > 0) {
        String d = "  deps: ";
        for (int i = 0; i < deps->size(); i++) {
            if (i > 0) d += " ";
            d += deps->at(i).s;
        }
        npkg_out(t, d);
    }
    npkg_persist(t);
    npkg_out(t, "  run: npkg start " + String(name));
}

static void npkg_remove(TermState* t, const char* name) {
    if (!name || !*name) { npkg_out(t, "usage: npkg remove <pkg>"); return; }
    String pkgdir = String("/pkg/") + name;
    FSNode* d = g_vfs->resolve(pkgdir.c_str());
    if (!d || !d->is_dir) {
        npkg_out(t, "npkg: package not installed: " + String(name));
        return;
    }
    npkg_rmtree(d);
    npkg_out(t, "npkg: removed " + String(name));
    npkg_persist(t);
}

static void npkg_start(TermState* t, int argc, const char** argv) {
    // argv: [npkg, start, spec, args...]
    if (argc < 3) { npkg_out(t, "usage: npkg start <pkg>[/<script>] [args...]"); return; }
    const char* spec = argv[2];
    // split pkg[/script]
    String pkg;
    String script = "main";
    const char* slash = strchr(spec, '/');
    if (slash) {
        pkg = String(spec, (int)(slash - spec));
        if (slash[1]) script = slash + 1;
    } else {
        pkg = spec;
    }
    if (pkg.empty()) { npkg_out(t, "npkg: bad package name"); return; }
    String entry = String("/pkg/") + pkg + "/" + script + ".ncode";
    FSNode* f = g_vfs->resolve(entry.c_str());
    if (!f || f->is_dir) {
        // allow bare 'npkg start <pkg>' to run main.ncode even if a sub was requested
        if (script == "main") {
            entry = String("/pkg/") + pkg + "/main.ncode";
            f = g_vfs->resolve(entry.c_str());
        }
        if (!f || f->is_dir) {
            npkg_out(t, "npkg: no entry script for " + String(pkg) + " (expected " + entry + ")");
            return;
        }
    }
    npkg_out(t, "npkg: starting " + String(pkg) + " (" + entry + ")");
    // extra args
    const char** extra = 0;
    int extra_argc = argc - 3;
    if (extra_argc > 0) extra = argv + 3;
    struct Sink { static void cb(void* ud, const char* s) { term_ext_print((TermState*)ud, s); } };
    NCOut sink;
    sink.print = &Sink::cb;
    sink.ud = t;
    ncode_run_file(&sink, f, extra, extra_argc);
}

} // namespace

// =============================================================================
//  entry point
// =============================================================================
// forward decl (npkg_upgrade defined below npkg_cmd)
static void npkg_upgrade(TermState* t);
void npkg_cmd(TermState* t, int argc, const char** argv) {
    if (argc < 2) { npkg_usage(t); return; }
    const char* a1 = argv[1];
    if (nefu::strcmp(a1, "update") == 0) {
        npkg_update_inner(t);
    } else if (nefu::strcmp(a1, "install") == 0) {
        npkg_install(t, argc >= 3 ? argv[2] : 0);
    } else if (nefu::strcmp(a1, "remove") == 0) {
        npkg_remove(t, argc >= 3 ? argv[2] : 0);
    } else if (nefu::strcmp(a1, "start") == 0) {
        npkg_start(t, argc, argv);
    } else if (nefu::strcmp(a1, "list") == 0) {
        npkg_list(t);
    } else if (nefu::strcmp(a1, "upgrade") == 0) {
        npkg_upgrade(t);
    } else if (nefu::strcmp(a1, "help") == 0 || nefu::strcmp(a1, "--help") == 0) {
        npkg_usage(t);
    } else {
        npkg_out(t, "npkg: unknown subcommand: " + String(a1));
        npkg_usage(t);
    }
}


// "upgrade": refresh the registry, then re-install every installed package so
// all of them are brought up to the latest version (clean reinstall, idempotent).
static void npkg_upgrade(TermState* t) {
    npkg_out(t, "npkg: upgrading all installed packages...");
    // 1) refresh the registry from the server
    npkg_update_inner(t);
    // 2) collect installed package dirs under /pkg
    FSNode* d = g_vfs->resolve(NPKG_ROOT);
    if (!d || !d->is_dir) {
        npkg_out(t, "npkg: no packages installed (nothing to upgrade)");
        return;
    }
    int upgraded = 0;
    int failed = 0;
    for (int i = 0; i < d->children.size(); i++) {
        FSNode* c = d->children[i];
        if (!c || !c->is_dir) continue;
        if (c->name == "package.json") continue;
        npkg_out(t, "npkg: upgrading " + c->name + " ...");
        npkg_install(t, c->name.c_str());
        upgraded++;
    }
    char msg[96];
    ksprintf(msg, sizeof(msg), "npkg: upgrade finished (%d package(s), %d error(s))",
             upgraded, failed);
    npkg_out(t, msg);
    npkg_persist(t);
}

} // namespace

namespace nefu {

// =============================================================================
//  GUI store integration (no TermState)
// =============================================================================
// nefud .deb chain (nefud.cpp): install data.tar + locate the static ELF
int  nefud_deb_install(FSNode* f, char* out, int outsz);
int  nefud_deb_find_elf(FSNode* f, char* vfs_path, int path_sz);

bool npkg_gui_fetch_registry(uint8_t** body, uint32_t* blen) {
    *body = 0; *blen = 0;
    String url = String(NPKG_DEFAULT_SERVER) + NPKG_REGISTRY;
    if (!npkg_http_get_core(url.c_str(), body, blen) || !*body) return false;
    return true;
}

int npkg_gui_registry_count(const uint8_t* body, uint32_t blen) {
    if (!body || blen == 0) return 0;
    String text((const char*)body, (int)blen);
    nefu::json::Value root;
    if (!npkg_registry_parse(text, root)) return 0;
    return npkg_registry_count(root);
}

static void gui_copy(char* dst, int cap, const String& s) {
    int i = 0;
    while (s[i] && i < cap - 1) { dst[i] = s[i]; i++; }
    dst[i] = 0;
}

bool npkg_gui_registry_entry(const uint8_t* body, uint32_t blen, int idx,
                             char* name, int ncap, char* version, int vcap,
                             char* intro, int icap) {
    if (!body || blen == 0) return false;
    String text((const char*)body, (int)blen);
    nefu::json::Value root;
    if (!npkg_registry_parse(text, root)) return false;
    const nefu::json::Value* app = root.get("app");
    if (!app || idx < 0 || idx >= app->obj.size()) return false;
    const nefu::json::Member& m = app->obj[idx];
    if (name && ncap > 0) gui_copy(name, ncap, m.key);
    if (version && vcap > 0) gui_copy(version, vcap, m.val.get_str("version", ""));
    if (intro && icap > 0) gui_copy(intro, icap, m.val.get_str("Introduction", ""));
    return true;
}

// bounded "ends with" helper for String
static bool gui_ends_with(const String& s, const char* suf) {
    int ls = s.len(), lf = 0;
    while (suf[lf]) lf++;
    if (lf > ls) return false;
    return nefu::strcmp(s.c_str() + ls - lf, suf) == 0;
}

bool npkg_gui_install(const char* name, char* msg, int mcap) {
    String out;
    // 1) local registry (refresh from the network when missing)
    String reg;
    if (!npkg_read_file(NPKG_REGISTRY, reg)) {
        uint8_t* body = 0; uint32_t blen = 0;
        if (npkg_gui_fetch_registry(&body, &blen) && body) {
            npkg_mkdirs(0, "/pkg");
            FSNode* rf = g_vfs->create_file(NPKG_REGISTRY);
            if (rf) g_vfs->write_file(rf, body, blen);
            kfree(body);
        }
    }
    if (!npkg_read_file(NPKG_REGISTRY, reg)) {
        out = "no local registry - run npkg update first";
        gui_copy(msg, mcap, out); return false;
    }
    nefu::json::Value root;
    if (!npkg_registry_parse(reg, root)) {
        out = "registry corrupt"; gui_copy(msg, mcap, out); return false;
    }
    const nefu::json::Member* m = npkg_registry_find(root, name);
    if (!m) {
        out = "not in registry: "; out += name; gui_copy(msg, mcap, out); return false;
    }
    const nefu::json::Value& meta = m->val;
    String version = meta.get_str("version", "?");
    String server_in = meta.get_str("server_in", "");
    String rel = npkg_rel_of(server_in.c_str());
    if (rel.empty()) {
        out = "no server path"; gui_copy(msg, mcap, out); return false;
    }
    String origin = npkg_origin();

    // 2) .deb package? (server path ends with .deb -> nefud chain)
    if (gui_ends_with(rel, ".deb") || server_in.find(".deb") >= 0) {
        String url = origin; url += "/"; url += rel;
        uint8_t* body = 0; uint32_t blen = 0;
        if (!npkg_http_get_core(url.c_str(), &body, &blen) || !body) {
            out = "download failed: "; out += url; gui_copy(msg, mcap, out); return false;
        }
        g_vfs->mkdir("/usr/share/apps");
        String dst = String("/usr/share/apps/") + name + ".deb";
        FSNode* f = g_vfs->create_file(dst.c_str());
        bool ok = f && g_vfs->write_file(f, body, blen);
        kfree(body);
        if (!ok) { out = "vfs write failed"; gui_copy(msg, mcap, out); return false; }
        char ib[512];
        int n = nefud_deb_install(f, ib, (int)sizeof(ib));
        if (n < 0) {
            out = "nefud install failed"; gui_copy(msg, mcap, out); return false;
        }
        out = "installed .deb "; out += name; out += " v"; out += version;
        gui_copy(msg, mcap, out);
        return true;
    }

    // 3) ncode package -> /pkg/<name> (same layout as `npkg install`)
    String pkgdir = String("/pkg/") + name;
    FSNode* old = g_vfs->resolve(pkgdir.c_str());
    if (old && old->is_dir) npkg_rmtree(old);
    if (!npkg_mkdirs(0, pkgdir.c_str())) {
        out = "cannot create package dir"; gui_copy(msg, mcap, out); return false;
    }
    bool got = false;
    {
        String ent = rel; ent += "/main.ncode";
        String url = origin; url += "/"; url += ent;
        uint8_t* body = 0; uint32_t blen = 0;
        if (npkg_http_get_core(url.c_str(), &body, &blen) && body) {
            FSNode* f = g_vfs->create_file(String(pkgdir + "/main.ncode").c_str());
            if (f) g_vfs->write_file(f, body, blen);
            kfree(body);
            got = f != 0;
        } else if (body) kfree(body);
    }
    if (!got) {
        out = "no main.ncode for "; out += name; gui_copy(msg, mcap, out); return false;
    }
    out = "installed "; out += name; out += " v"; out += version;
    gui_copy(msg, mcap, out);
    return true;
}

} // namespace nefu
