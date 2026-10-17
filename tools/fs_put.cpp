// nefuOS: put a host file into the VFS snapshot using the real VFS API
// (guaranteed byte-identical to what the kernel writes/reads).
// Usage: fs_put <fs_path> <dst_path_in_fs> <host_file>
#include "klib/klib.h"
#include "platform.h"
#include "vfs/vfs.h"
#include <cstdio>
#include <cstdlib>

using namespace nefu;

// host-side tool: use the CRT allocator for klib/kalloc
namespace nefu {
void* kalloc(size_t sz) { return std::malloc(sz ? sz : 1); }
void kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void platform_dbg(const char* s) { std::fputs(s, stdout); }
uint32_t platform_tick_ms() { return 0; }
}

int main(int argc, char** argv) {
    if (argc != 4) { printf("usage: fs_put <fs_path> <dst_path> <host_file>\n"); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)kalloc((size_t)n);
    if (!buf) return 1;
    size_t r = fread(buf, 1, (size_t)n, f); fclose(f);
    if (r != (size_t)n) { printf("short read\n"); return 1; }

    VFS vfs;
    if (!vfs.load(buf, (uint32_t)n)) { printf("vfs load failed (magic?) size=%ld\n", n); kfree(buf); return 1; }
    kfree(buf);

    // mkdir chain + put file
    FSNode* parent = vfs.resolve("/");
    if (!parent) return 1;
    String path = argv[2];
    int last = path.rfind('/');
    String dirs = last >= 0 ? path.substr(0, last) : String(".");
    String name = last >= 0 ? path.substr(last + 1, path.len() - last - 1) : path;
    // create each dir component (absolute path from the real root)
    if (dirs.len() > 1) {
        String acc("/");
        const char* p = dirs.c_str();
        while (*p) {
            while (*p == '/') p++;
            const char* e = p;
            while (*e && *e != '/') e++;
            if (e == p) break;
            if (acc.len() > 1) acc += "/";
            acc += String(p, (int)(e - p));
            vfs.mkdir(acc.c_str());
            p = e;
        }
    }
    FSNode* d = vfs.resolve(dirs.len() > 1 ? dirs.c_str() : "/");
    if (!d || !d->is_dir) { printf("no such dir: %s\n", dirs.c_str()); return 1; }
    FSNode* fn = vfs.create_file(argv[2]);
    if (!fn) { printf("create_file failed: %s\n", argv[2]); return 1; }

    FILE* hf = fopen(argv[3], "rb");
    if (!hf) { printf("cannot open host file %s\n", argv[3]); return 1; }
    fseek(hf, 0, SEEK_END); long hn = ftell(hf); fseek(hf, 0, SEEK_SET);
    uint8_t* hb = (uint8_t*)kalloc((size_t)hn);
    if (!hb) return 1;
    size_t hr = fread(hb, 1, (size_t)hn, hf); fclose(hf);
    if (hr != (size_t)hn) { printf("short host read\n"); return 1; }
    vfs.write_file(fn, hb, (uint32_t)hn);
    kfree(hb);

    uint8_t* out = 0; uint32_t osz = 0;
    if (!vfs.save(&out, &osz)) { printf("save failed\n"); return 1; }
    FILE* wf = fopen(argv[1], "wb");
    if (!wf) { printf("cannot write %s\n", argv[1]); return 1; }
    fwrite(out, 1, osz, wf); fclose(wf);
    kfree(out);
    printf("fs_put: %s (%d bytes) -> %s (%u bytes total)\n", argv[2], (int)hn, argv[1], osz);
    return 0;
}
