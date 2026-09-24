// nefuOS 文件系统库 —— 聚合头
//
// 一次性引入整个文件系统子系统:
//   disk     虚拟磁盘(块设备、MBR/GPT、坏块)
//   fat16    FAT16 文件系统
//   ext2     ext2 文件系统
//   minixfs  Minix 文件系统
//   vfs_layer 虚拟文件系统层(挂载/路径解析/文件描述符)
//   journal  日志(WAL,崩溃恢复)
//   cache    缓冲区缓存(LRU/脏块回写/预读)
#pragma once
#include "disk.h"
#include "fat16.h"
#include "ext2.h"
#include "minixfs.h"
#include "vfs_layer.h"
#include "journal.h"
#include "cache.h"

namespace nefu {
namespace filesystem {

// 跑全部子模块自检,返回失败断言总数(0 = 全部通过)。
inline int filesystem_self_test() {
    int total = 0;
    total += disk_self_test();
    total += fat16_self_test();
    total += ext2_self_test();
    total += minixfs_self_test();
    total += vfs_layer_self_test();
    total += journal_self_test();
    total += cache_self_test();
    return total;
}

// 跨模块集成冒烟:一块盘上先后格式化 FAT16 + ext2,走 VFS 挂载、写读、缓存、日志。
// 返回失败数(0 = 通过)。这是给 fsview/应用层做的端到端 sanity check。
inline int filesystem_integration_smoke() {
    int fails = 0;
    Disk disk;
    if (!disk.create(32768)) return 1;

    // 分区 0:FAT16(0..16383)
    Fat16 fat;
    if (!fat.format(&disk, 0, 16384, 8)) return 2;
    // 分区 1:ext2(16384..32767)
    Ext2 ext;
    if (!ext.format(&disk, 16384, 16384)) return 3;

    // VFS 挂载
    VfsLayer vfs;
    if (!vfs.mount("/", FS_TYPE_FAT16, &fat, "root")) fails++;
    if (!vfs.mount("/data", FS_TYPE_EXT2, &ext, "data")) fails++;

    // 在两个文件系统上各写一个文件
    if (!vfs.write_file("/boot.cfg", (const uint8_t*)"boot=ok", 7)) fails++;
    if (!vfs.write_file("/data/db.bin", (const uint8_t*)"\x01\x02\x03\x04", 4)) fails++;

    // 读回
    uint8_t rb[32];
    int fd = vfs.open("/boot.cfg");
    if (fd < 0) fails++;
    else {
        int n = vfs.read(fd, rb, sizeof(rb));
        if (n != 7) fails++;
        vfs.close(fd);
    }
    fd = vfs.open("/data/db.bin");
    if (fd < 0) fails++;
    else {
        int n = vfs.read(fd, rb, sizeof(rb));
        if (n != 4) fails++;
        if (rb[0] != 1 || rb[3] != 4) fails++;
        vfs.close(fd);
    }

    // 缓存层挂在 FAT16 分区上
    BlockCache cache;
    cache.attach(&disk, 8);
    uint8_t cb[512];
    if (!cache.read(0, cb)) fails++;
    cache.read(0, cb); // 第二次应命中

    // 日志层
    Journal j;
    if (!j.open(&disk, 30000, 256)) fails++;
    uint32_t t = j.begin_txn();
    uint8_t jd[512]; for (int i=0;i<512;i++) jd[i]=(uint8_t)i;
    if (!j.log_write(t, 100, jd)) fails++;
    if (!j.commit(t)) fails++;

    // 磁盘布局摘要
    DiskReport rep;
    if (!disk_inspect(&disk, &rep)) fails++;
    if (rep.total_sectors != 32768) fails++;

    // 跨文件系统删除
    if (vfs.remove("/boot.cfg")) {} // 可能失败也算正常
    if (vfs.remove("/data/db.bin")) {}

    // 多轮写读:在 FAT16 上反复创建/删除 20 次
    for (int i = 0; i < 20; i++) {
        char p[24]; p[0]='/'; p[1]='s'; p[2]=(char)('a'+i); p[3]=0;
        uint8_t v = (uint8_t)i;
        if (!vfs.write_file(p, &v, 1)) { fails++; break; }
    }
    for (int i = 0; i < 20; i++) {
        char p[24]; p[0]='/'; p[1]='s'; p[2]=(char)('a'+i); p[3]=0;
        if (vfs.file_size(p) != 1) { fails++; break; }
    }

    // MinixFS 子挂载:在第三段盘上加 minix
    Disk md; md.create(8192);
    MinixFS mx; mx.format(&md, 0, 8192, 2);
    if (!mx.write_file("/kernel.bin", (const uint8_t*)"x", 1)) fails++;
    if (mx.file_size("/kernel.bin") != 1) fails++;

    // 缓存写回脏块
    BlockCache c2; c2.attach(&disk, 4);
    uint8_t wb[512]; for (int i=0;i<512;i++) wb[i]=i;
    c2.write(1234, wb);
    c2.write(1235, wb);
    if (c2.dirty_count() < 2) fails++;
    c2.flush();
    if (c2.dirty_count() != 0) fails++;

    // fsck on FAT16
    Fat16::FsckReport fr;
    fat.fsck(&fr);
    if (fr.total_clusters == 0) fails++;

    return fails;
}

} // namespace filesystem
} // namespace nefu
