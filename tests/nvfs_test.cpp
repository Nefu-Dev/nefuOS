// nvfs_test.cpp - headless functional tests for the nefuOS NVFS layer
// (core/vfs/nvfs.cpp). Exercises format/mount, directory + file ops,
// journaling with crash recovery, journal sync, unlink/rmdir, and the
// vfs_to_nvfs / nvfs_to_vfs tree round-trip against a real VFS.
//
// Build: tests/build_nvfs_test.ps1   (host-only, no GUI needed)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/vfs/nvfs.h"
#include "../core/vfs/vfs.h"

using namespace nefu;
using namespace nefu::nvfs;

static int s_fails = 0;
static int s_checks = 0;

#define CHECK(cond, msg) \
    do { s_checks++; if (!(cond)) { s_fails++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main() {
    printf("== NVFS headless test ==\n");

    // [1] fresh format + mount
    CHECK(init(), "init formats and mounts");
    CHECK(is_mounted(), "mounted after init");
    CHECK(total_space() == 8192u * 512u, "total space = 4 MiB");
    CHECK(free_space() > 0, "has free space");

    // [2] directories + file write/read
    CHECK(nvfs::mkdir("/home", 0755) >= 0, "mkdir /home");
    CHECK(nvfs::mkdir("/home/user", 0755) >= 0, "mkdir /home/user");
    CHECK(lookup("/home/user") >= 0, "lookup /home/user");
    CHECK(lookup("/home/nope") < 0, "lookup missing path");
    int f = create_file("/home/user/hello.txt", 0644);
    CHECK(f >= 0, "create file");
    CHECK(create_file("/home/user/hello.txt", 0644) < 0, "duplicate create rejected");
    const char* hello = "hello nefuOS NVFS";
    int hl = (int)strlen(hello);
    CHECK(write_file(f, (const uint8_t*)hello, 0, (uint32_t)hl) == hl, "write file");
    CHECK(write_file(f, (const uint8_t*)"tail", 0, 0) < 0, "zero-size write rejected");
    char buf[128];
    memset(buf, 0, sizeof(buf));
    CHECK(read_file(f, (uint8_t*)buf, 0, sizeof(buf)) == hl, "read returns exact size");
    CHECK(strcmp(buf, hello) == 0, "read content matches");

    // [3] journal carries pending records after a mutation
    CHECK(journal_pending() > 0, "journal has pending records");

    // [4] crash recovery: corrupt a data block in the image (simulating a torn
    //     write), leave the journal intact, then recover() must replay the
    //     block content from the journal.
    {
        uint8_t* img = 0;
        uint32_t isz = 0;
        CHECK(image(&img, &isz) && img && isz == 8192u * 512u, "image accessor");
        Inode* in = (Inode*)(img + 512 + (size_t)f * sizeof(Inode));
        CHECK(in->inode_num == (uint32_t)f, "inode number round-trip");
        uint32_t db = in->blocks[0];
        CHECK(db >= 353 && db < 8192, "file data block in user region");
        memset(img + (size_t)db * 512, 'X', 512);           // torn block
        memset(buf, 0, sizeof(buf));
        read_file(f, (uint8_t*)buf, 0, sizeof(buf));
        CHECK(strcmp(buf, hello) != 0, "corruption is visible before recovery");
        CHECK(journal_pending() > 0, "journal still pending before recovery");
        recover();                                           // replay journal
        CHECK(journal_pending() == 0, "recovery cleared journal");
        memset(buf, 0, sizeof(buf));
        read_file(f, (uint8_t*)buf, 0, sizeof(buf));
        CHECK(strcmp(buf, hello) == 0, "recovery restored block from journal");
    }

    // [5] sync flushes and clears the journal
    sync();
    CHECK(journal_pending() == 0, "sync cleared journal");

    // [6] unlink + rmdir
    CHECK(nvfs::unlink("/home/user/hello.txt") == 0, "unlink file");
    CHECK(lookup("/home/user/hello.txt") < 0, "file gone after unlink");
    CHECK(nvfs::rmdir("/home/user") == 0, "rmdir /home/user");
    CHECK(nvfs::rmdir("/home") == 0, "rmdir /home");
    CHECK(nvfs::rmdir("/") < 0, "cannot rmdir root");
    CHECK(nvfs::rmdir("/home") < 0, "cannot rmdir missing dir");

    // [7] vfs_to_nvfs / nvfs_to_vfs round-trip through the real VFS tree
    {
        VFS vfs;
        g_vfs = &vfs;
        vfs.mkdir("/home");
        vfs.mkdir("/home/user");
        FSNode* nf = vfs.create_file("/home/user/data.bin");
        CHECK(nf != 0, "vfs create data.bin");
        static uint8_t payload[1000];
        for (int i = 0; i < (int)sizeof(payload); i++) payload[i] = (uint8_t)(i * 7 + 3);
        CHECK(vfs.write_file(nf, payload, sizeof(payload)), "vfs write data.bin");
        vfs_to_nvfs();

        VFS vfs2;
        g_vfs = &vfs2;
        nvfs_to_vfs();
        FSNode* f2 = vfs2.resolve("/home/user/data.bin");
        CHECK(f2 != 0 && !f2->is_dir, "round-trip: file present");
        CHECK(f2 && f2->size == sizeof(payload), "round-trip: size matches");
        CHECK(f2 && f2->data && memcmp(f2->data, payload, sizeof(payload)) == 0,
              "round-trip: content matches");
        FSNode* d = vfs2.resolve("/home/user");
        CHECK(d != 0 && d->is_dir, "round-trip: dir present");
        g_vfs = 0;
    }

    // [8] reboot simulation: persist image bytes, unmount, reload, mount
    {
        uint8_t* img = 0;
        uint32_t isz = 0;
        CHECK(image(&img, &isz) && img && isz > 0, "image for persist");
        uint8_t* copy = (uint8_t*)malloc(isz);
        CHECK(copy != 0, "allocate image copy");
        memcpy(copy, img, isz);
        unmount();
        CHECK(!is_mounted(), "unmounted");
        CHECK(load_image(copy, isz), "reload persisted image");
        CHECK(mount(0), "remount after reboot");
        VFS vfs3;
        g_vfs = &vfs3;
        nvfs_to_vfs();
        CHECK(vfs3.resolve("/home/user/data.bin") != 0, "tree survives reboot");
        g_vfs = 0;
        unmount();
    }

    // [9] journal overflow: a big write rolls the journal forward automatically
    {
        CHECK(init(), "re-init for overflow test");
        int big = create_file("/big.bin", 0644);
        CHECK(big >= 0, "create big file");
        const uint32_t BIG = 96u * 1024u;   // 192 blocks > journal capacity
        uint8_t* bd = (uint8_t*)malloc(BIG);
        CHECK(bd != 0, "alloc big payload");
        for (uint32_t i = 0; i < BIG; i++) bd[i] = (uint8_t)(i & 0xFF);
        CHECK(write_file(big, bd, 0, BIG) == (int)BIG, "write big file");
        uint8_t* rd = (uint8_t*)malloc(BIG);
        CHECK(rd != 0, "alloc read buffer");
        CHECK(read_file(big, rd, 0, BIG) == (int)BIG, "read big file size");
        CHECK(memcmp(rd, bd, BIG) == 0, "big file content intact after auto-sync");
        // read via the 12+128 direct/indirect boundary (block 12 => indirect)
        uint8_t* tail = (uint8_t*)malloc(64);
        CHECK(read_file(big, tail, 12u * 512u, 64) == 64, "read past direct blocks");
        CHECK(memcmp(tail, bd + 12u * 512u, 64) == 0, "indirect block content correct");
        free(tail);
        free(rd);
        free(bd);
        sync();
        CHECK(journal_pending() == 0, "journal clean after big write + sync");
        CHECK(nvfs::rmdir("/") < 0, "root still protected");
    }

    // [10] free space accounting
    {
        uint64_t fs0 = free_space();
        int x = create_file("/tmp.txt", 0644);
        CHECK(x >= 0, "create tmp file");
        uint8_t d[100];
        memset(d, 'a', sizeof(d));
        write_file(x, d, 0, sizeof(d));
        CHECK(free_space() < fs0, "free space decreased after write");
        nvfs::unlink("/tmp.txt");
        CHECK(free_space() >= fs0, "free space restored after unlink");
    }

    unmount();
    printf("== NVFS test: %d checks, %d failures ==\n", s_checks, s_fails);
    return s_fails ? 1 : 0;
}
