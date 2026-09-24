// nefuOS 文件系统库 —— VFS 层实现
#include "vfs_layer.h"
#include "../klib/klib.h"

namespace nefu {
namespace filesystem {

static inline void zbc(void* dst, int n) {
    volatile uint8_t* d = (volatile uint8_t*)dst; while(n-->0) *d++ = 0;
}

VfsLayer::VfsLayer() : mount_count_(0) {
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        fds_[i].used = false;
        fds_[i].mount_idx = -1;
        fds_[i].offset = 0; fds_[i].size = 0; fds_[i].is_dir = false;
    }
}
VfsLayer::~VfsLayer() {}

bool VfsLayer::mount(const char* prefix, FsType type, void* fs, const char* dev_name) {
    if (mount_count_ >= VFS_MAX_MOUNTS) return false;
    if (!prefix || prefix[0] != '/') return false;
    MountPoint* m = &mounts_[mount_count_];
    int i = 0;
    for (; prefix[i] && i < 63; i++) m->prefix[i] = prefix[i];
    m->prefix[i] = 0;
    m->type = type; m->fs = fs;
    int j = 0; for (; dev_name[j] && j < 31; j++) m->dev_name[j] = dev_name[j]; m->dev_name[j]=0;
    mount_count_++;
    return true;
}
bool VfsLayer::umount(const char* prefix) {
    for (int i = 0; i < mount_count_; i++) {
        if (nefu::strcmp(mounts_[i].prefix, prefix) == 0) {
            for (int k = i; k < mount_count_-1; k++) mounts_[k] = mounts_[k+1];
            mount_count_--;
            return true;
        }
    }
    return false;
}

int VfsLayer::resolve(const char* abspath, char* inner_path, int inner_sz) {
    if (!abspath || abspath[0] != '/') return -1;
    // 找最长匹配前缀
    int best = -1; int best_len = 0;
    for (int i = 0; i < mount_count_; i++) {
        int pl = (int)nefu::strlen(mounts_[i].prefix);
        // 前缀必须整段匹配
        if (nefu::strncmp(abspath, mounts_[i].prefix, pl) == 0) {
            // 边界:abspath == prefix 或 abspath[pl]=='/'
            if (abspath[pl] == 0 || abspath[pl] == '/' || nefu::strcmp(mounts_[i].prefix,"/")==0) {
                if (pl >= best_len) { best = i; best_len = pl; }
            }
        }
    }
    if (best < 0) return -1;
    // inner_path = abspath[best_len..],挂载在 "/" 时 best_len=1
    const char* rest = abspath + best_len;
    if (mounts_[best].prefix[1] == 0) {
        // 挂在 /:inner = abspath(已含前导 /)
        rest = abspath;
    } else {
        // rest 应以 '/' 开头,确保 inner 以 '/' 开头
    }
    int k = 0;
    while (*rest && k < inner_sz-1) { inner_path[k] = *rest; k++; rest++; }
    inner_path[k] = 0;
    // 若 inner 为空,变成 "/"
    if (inner_path[0] == 0) { inner_path[0]='/'; inner_path[1]=0; }
    return best;
}

int VfsLayer::open(const char* path) {
    char inner[128];
    int mi = resolve(path, inner, sizeof(inner));
    if (mi < 0) return -1;
    int fd = -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) if (!fds_[i].used) { fd = i; break; }
    if (fd < 0) return -1;
    FsFile* f = &fds_[fd];
    f->used = true; f->mount_idx = mi; f->offset = 0; f->is_dir = false;
    int k = 0; for (; inner[k] && k < 127; k++) f->path[k] = inner[k]; f->path[k]=0;
    // 查大小
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) {
        Fat16* fs = (Fat16*)m->fs;
        long sz = fs->file_size(f->path);
        f->size = (sz < 0) ? 0 : (uint32_t)sz;
    } else if (m->type == FS_TYPE_EXT2) {
        Ext2* fs = (Ext2*)m->fs;
        long sz = fs->file_size(f->path);
        f->size = (sz < 0) ? 0 : (uint32_t)sz;
    } else if (m->type == FS_TYPE_MINIX) {
        MinixFS* fs = (MinixFS*)m->fs;
        long sz = fs->file_size(f->path);
        f->size = (sz < 0) ? 0 : (uint32_t)sz;
    }
    return fd;
}

int VfsLayer::read(int fd, uint8_t* buf, uint32_t sz) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds_[fd].used) return -1;
    FsFile* f = &fds_[fd];
    MountPoint* m = &mounts_[f->mount_idx];
    // 简化:每次从偏移读(整体读文件再切)
    uint8_t tmp[4096];
    int got = 0;
    if (m->type == FS_TYPE_FAT16) {
        Fat16* fs = (Fat16*)m->fs;
        got = fs->read_file(f->path, tmp, sizeof(tmp));
    } else if (m->type == FS_TYPE_EXT2) {
        Ext2* fs = (Ext2*)m->fs;
        got = fs->read_file(f->path, tmp, sizeof(tmp));
    } else if (m->type == FS_TYPE_MINIX) {
        MinixFS* fs = (MinixFS*)m->fs;
        got = fs->read_file(f->path, tmp, sizeof(tmp));
    }
    if (got < 0) return -1;
    uint32_t avail = (uint32_t)got;
    if (f->offset >= avail) return 0;
    uint32_t n = avail - f->offset;
    if (n > sz) n = sz;
    nefu::memcpy(buf, tmp + f->offset, n);
    f->offset += n;
    return (int)n;
}

int VfsLayer::write(int fd, const uint8_t* buf, uint32_t sz) {
    // 简化:VFS write 不做流式追加,直接走 write_file 整体写
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds_[fd].used) return -1;
    FsFile* f = &fds_[fd];
    return write_file(f->path, buf, sz) ? (int)sz : -1;
}

int VfsLayer::close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS) return -1;
    fds_[fd].used = false;
    return 0;
}
long VfsLayer::size(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds_[fd].used) return -1;
    return (long)fds_[fd].size;
}

bool VfsLayer::mkdir(const char* path) {
    char inner[128];
    int mi = resolve(path, inner, sizeof(inner));
    if (mi < 0) return false;
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) return ((Fat16*)m->fs)->mkdir(inner);
    if (m->type == FS_TYPE_EXT2) return ((Ext2*)m->fs)->mkdir(inner);
    if (m->type == FS_TYPE_MINIX) return ((MinixFS*)m->fs)->mkdir(inner);
    return false;
}
bool VfsLayer::create(const char* path) {
    char inner[128];
    int mi = resolve(path, inner, sizeof(inner));
    if (mi < 0) return false;
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) return ((Fat16*)m->fs)->create_file(inner);
    if (m->type == FS_TYPE_EXT2) return ((Ext2*)m->fs)->create_file(inner);
    if (m->type == FS_TYPE_MINIX) return ((MinixFS*)m->fs)->create_file(inner);
    return false;
}
bool VfsLayer::remove(const char* path) {
    char inner[128];
    int mi = resolve(path, inner, sizeof(inner));
    if (mi < 0) return false;
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) return ((Fat16*)m->fs)->remove(inner);
    if (m->type == FS_TYPE_EXT2) return ((Ext2*)m->fs)->remove(inner);
    if (m->type == FS_TYPE_MINIX) return ((MinixFS*)m->fs)->remove(inner);
    return false;
}
bool VfsLayer::rename(const char* oldpath, const char* newname) {
    char inner[128];
    int mi = resolve(oldpath, inner, sizeof(inner));
    if (mi < 0) return false;
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) return ((Fat16*)m->fs)->rename(inner, newname);
    if (m->type == FS_TYPE_EXT2) return ((Ext2*)m->fs)->rename(inner, newname);
    // MinixFS 暂不支持 rename
    return false;
}

long VfsLayer::file_size(const char* path) {
    char inner[128];
    int mi = resolve(path, inner, sizeof(inner));
    if (mi < 0) return -1;
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) return ((Fat16*)m->fs)->file_size(inner);
    if (m->type == FS_TYPE_EXT2) return ((Ext2*)m->fs)->file_size(inner);
    if (m->type == FS_TYPE_MINIX) return ((MinixFS*)m->fs)->file_size(inner);
    return -1;
}

bool VfsLayer::write_file(const char* path, const uint8_t* data, uint32_t len) {
    char inner[128];
    int mi = resolve(path, inner, sizeof(inner));
    if (mi < 0) return false;
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) return ((Fat16*)m->fs)->write_file(inner, data, len);
    if (m->type == FS_TYPE_EXT2) return ((Ext2*)m->fs)->write_file(inner, data, len);
    if (m->type == FS_TYPE_MINIX) return ((MinixFS*)m->fs)->write_file(inner, data, len);
    return false;
}
int VfsLayer::list_dir(const char* path, Ext2DirInfo* out, int max_out) {
    char inner[128];
    int mi = resolve(path, inner, sizeof(inner));
    if (mi < 0) return 0;
    MountPoint* m = &mounts_[mi];
    if (m->type == FS_TYPE_FAT16) {
        FatDirInfo fi[32]; int n = ((Fat16*)m->fs)->list_dir(inner, fi, max_out < 32?max_out:32);
        for (int i = 0; i < n; i++) {
            int k=0; for(;fi[i].name[k]&&k<62;k++) out[i].name[k]=fi[i].name[k]; out[i].name[k]=0;
            out[i].is_dir = fi[i].is_dir; out[i].is_symlink = false;
            out[i].inode = 0; out[i].size = fi[i].size;
        }
        return n;
    }
    if (m->type == FS_TYPE_EXT2) return ((Ext2*)m->fs)->list_dir(inner, out, max_out);
    if (m->type == FS_TYPE_MINIX) {
        MinixDirInfo mi2[32];
        int n = ((MinixFS*)m->fs)->list_dir(inner, mi2, max_out < 32?max_out:32);
        for (int i = 0; i < n; i++) {
            int k=0; for(;mi2[i].name[k]&&k<62;k++) out[i].name[k]=mi2[i].name[k]; out[i].name[k]=0;
            out[i].is_dir = mi2[i].is_dir; out[i].is_symlink = false;
            out[i].inode = mi2[i].inode; out[i].size = mi2[i].size;
        }
        return n;
    }
    return 0;
}

uint32_t VfsLayer::total_free_blocks() {
    uint32_t total = 0;
    for (int i = 0; i < mount_count_; i++) {
        MountPoint* m = &mounts_[i];
        if (m->type == FS_TYPE_FAT16) total += ((Fat16*)m->fs)->free_clusters();
        else if (m->type == FS_TYPE_EXT2) total += ((Ext2*)m->fs)->free_blocks();
        else if (m->type == FS_TYPE_MINIX) total += ((MinixFS*)m->fs)->free_zones();
    }
    return total;
}

int vfs_layer_self_test() {
    int fails = 0;
    Disk disk; disk.create(16384);
    // 一块 FAT16 挂 /
    Fat16 fat;
    if (!fat.format(&disk, 0, 8192, 8)) return 1;
    // 一块 ext2 挂 /home(分区从扇区 8192 起)
    Ext2 ext;
    if (!ext.format(&disk, 8192, 8192)) return 2;

    VfsLayer vfs;
    if (!vfs.mount("/", FS_TYPE_FAT16, &fat, "hd0a")) fails++;
    if (!vfs.mount("/home", FS_TYPE_EXT2, &ext, "hd0b")) fails++;
    if (vfs.mount_count() != 2) fails++;

    // 在 FAT16 根写文件
    if (!vfs.write_file("/hello.txt", (const uint8_t*)"vfs hello", 9)) fails++;
    int fd = vfs.open("/hello.txt");
    if (fd < 0) fails++;
    uint8_t rb[32];
    int r = vfs.read(fd, rb, sizeof(rb));
    if (r != 9) fails++;
    rb[r] = 0;
    if (nefu::strcmp((char*)rb, "vfs hello") != 0) fails++;
    vfs.close(fd);

    // 在 ext2 挂载点写文件
    if (!vfs.write_file("/home/user.txt", (const uint8_t*)"ext2 via vfs", 12)) fails++;
    fd = vfs.open("/home/user.txt");
    if (fd < 0) fails++;
    r = vfs.read(fd, rb, sizeof(rb));
    if (r != 12) fails++;
    vfs.close(fd);

    // 列根目录:应看到 hello.txt(+可能 home 挂载点本身不显示,因为是挂载)
    Ext2DirInfo en[16];
    int n = vfs.list_dir("/", en, 16);
    if (n < 1) fails++;

    // file_size / rename(转发到 FAT16)
    if (vfs.file_size("/hello.txt") != 9) fails++;
    if (!vfs.rename("/hello.txt", "bye.txt")) fails++;
    if (vfs.file_size("/hello.txt") != -1) fails++;
    if (vfs.file_size("/bye.txt") != 9) fails++;

    // 卸载
    if (!vfs.umount("/home")) fails++;
    if (vfs.mount_count() != 1) fails++;

    // 多挂载点下,跨挂载点写文件
    VfsLayer v2;
    Disk d2; d2.create(16384);
    Fat16 f2; f2.format(&d2, 0, 16384, 8);
    Ext2 e2; e2.format(&d2, 8192, 8192);
    if (!v2.mount("/", FS_TYPE_FAT16, &f2, "root")) fails++;
    if (!v2.mount("/var", FS_TYPE_EXT2, &e2, "var")) fails++;
    if (!v2.write_file("/a.txt", (const uint8_t*)"root", 4)) fails++;
    if (!v2.write_file("/var/b.txt", (const uint8_t*)"var", 3)) fails++;
    if (v2.file_size("/a.txt") != 4) fails++;
    if (v2.file_size("/var/b.txt") != 3) fails++;
    if (v2.mount_count() != 2) fails++;
    // 列根:应只看到 FAT16 的 a.txt(挂载点 /var 是目录但不显示内容)
    Ext2DirInfo en2[16];
    int nn = v2.list_dir("/", en2, 16);
    if (nn < 1) fails++;

    return fails;
}

} // namespace filesystem
} // namespace nefu
