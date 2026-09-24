// nefuOS 文件系统库 —— Minix 文件系统实现
#include "minixfs.h"
#include "../klib/klib.h"

namespace nefu {
namespace filesystem {

static inline void w16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void w32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline uint16_t r16(const uint8_t* p) { return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static inline uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline void zbc(void* dst, int n) {
    volatile uint8_t* d=(volatile uint8_t*)dst; while(n-->0) *d++=0;
}

MinixFS::MinixFS() : disk_(0), part_start_(0), version_(1),
    ninodes_(0), nzones_(0), imap_blocks_(1), zmap_blocks_(1),
    firstdatazone_(0), total_blocks_(0) {}
MinixFS::~MinixFS() { disk_ = 0; }

bool MinixFS::rd_lba(uint32_t lba, uint8_t* b) { return disk_->read_sector(part_start_+lba, b); }
bool MinixFS::wr_lba(uint32_t lba, const uint8_t* b) { return disk_->write_sector(part_start_+lba, b); }
bool MinixFS::rd_block(uint32_t blk, uint8_t* b) {
    return rd_lba(blk*2, b) && rd_lba(blk*2+1, b+512);
}
bool MinixFS::wr_block(uint32_t blk, const uint8_t* b) {
    return wr_lba(blk*2, b) && wr_lba(blk*2+1, b+512);
}

bool MinixFS::read_super(MinixSuper* s) {
    uint8_t b[1024];
    if (!rd_block(1, b)) return false;
    if (version_ == 1) {
        s->ninodes = r16(b+0);
        s->nzones = r16(b+2);
        s->imap_blocks = r16(b+4);
        s->zmap_blocks = r16(b+6);
        s->firstdatazone = r16(b+8);
        s->log_zone_size = r16(b+10);
        s->max_size = r32(b+12);
        s->magic = r16(b+24);
    } else {
        s->ninodes = r32(b+0);
        s->nzones = r32(b+4);
        s->imap_blocks = r16(b+8);
        s->zmap_blocks = r16(b+10);
        s->firstdatazone = r32(b+12);
        s->log_zone_size = r32(b+16);
        s->max_size = r32(b+20);
        s->magic = r16(b+24);
    }
    return true;
}
bool MinixFS::write_super(const MinixSuper* s) {
    uint8_t b[1024]; zbc(b, sizeof(b));
    if (version_ == 1) {
        w16(b+0, s->ninodes);
        w16(b+2, s->nzones);
        w16(b+4, s->imap_blocks);
        w16(b+6, s->zmap_blocks);
        w16(b+8, s->firstdatazone);
        w16(b+10, s->log_zone_size);
        w32(b+12, s->max_size);
        w16(b+24, s->magic);
    } else {
        w32(b+0, s->ninodes);
        w32(b+4, s->nzones);
        w16(b+8, s->imap_blocks);
        w16(b+10, s->zmap_blocks);
        w32(b+12, s->firstdatazone);
        w32(b+16, s->log_zone_size);
        w32(b+20, s->max_size);
        w16(b+24, s->magic);
    }
    return wr_block(1, b);
}

uint32_t MinixFS::read_zone_field(const uint8_t* p) {
    return version_ == 1 ? r16(p) : r32(p);
}
void MinixFS::write_zone_field(uint8_t* p, uint32_t z) {
    if (version_ == 1) w16(p, (uint16_t)z); else w32(p, z);
}

bool MinixFS::format(Disk* disk, uint32_t start_lba, uint32_t total_lba, int version) {
    disk_ = disk; part_start_ = start_lba; version_ = version;
    total_blocks_ = total_lba / 2;
    ninodes_ = 64;
    imap_blocks_ = 1;
    zmap_blocks_ = 1;
    // 布局:0 boot,1 super,2 ibitmap,3 zbitmap,4..11 inode 表(8 块=64*64/1024),12+ 数据
    uint32_t itbl = inode_table_blk();
    uint32_t first_data = first_data_blk();
    nzones_ = (uint16_t)(total_blocks_ - first_data);
    firstdatazone_ = (uint16_t)first_data;

    uint8_t zb[1024]; zbc(zb, sizeof(zb));
    for (uint32_t i = 0; i < total_blocks_; i++) wr_block(i, zb);

    MinixSuper s; zbc(&s, sizeof(s));
    s.ninodes = ninodes_; s.nzones = nzones_;
    s.imap_blocks = imap_blocks_; s.zmap_blocks = zmap_blocks_;
    s.firstdatazone = firstdatazone_; s.log_zone_size = 0;
    s.max_size = 0x1000000;
    s.magic = (version == 1) ? MINIX_V1_MAGIC : MINIX_V2_MAGIC;
    write_super(&s);

    // inode 位图:block 2。inode 1 根目录已用
    uint8_t ib[1024]; rd_block(2, ib);
    ib[0] = 0x02; // bit1 = inode 1
    wr_block(2, ib);

    // zone 位图:block 3。块 0..first_data 已用
    uint8_t zb2[1024]; rd_block(3, zb2);
    for (uint32_t z = 0; z <= first_data; z++) zb2[z/8] |= (uint8_t)(1<<(z%8));
    wr_block(3, zb2);

    // 根目录 inode 1
    MinixInode root; zbc(&root, sizeof(root));
    root.mode = MINIX_DIR | 0755;
    root.size = 1024; root.nlinks = 2;
    write_zone_field((uint8_t*)root.zone, first_data); // zone[0]
    write_inode(1, &root);

    // 根目录块:"." 和 ".."(V1 16 字节项 / V2 32 字节项)
    uint8_t db[1024]; zbc(db, sizeof(db));
    if (version_ == 1) {
        w16(db+0, 1); db[2]='.';
        w16(db+16, 1); db[18]='.'; db[19]='.';
    } else {
        w32(db+0, 1); db[4]='.';
        w32(db+32, 1); db[36]='.'; db[37]='.';
    }
    wr_block(first_data, db);
    return true;
}

bool MinixFS::mount(Disk* disk, uint32_t start_lba) {
    disk_ = disk; part_start_ = start_lba;
    // 先按 V1 试探 magic
    version_ = 1;
    MinixSuper s;
    if (!read_super(&s)) return false;
    if (s.magic == MINIX_V2_MAGIC) { version_ = 2; read_super(&s); }
    if (s.magic != MINIX_V1_MAGIC && s.magic != MINIX_V2_MAGIC) return false;
    ninodes_ = (uint16_t)s.ninodes; nzones_ = (uint16_t)s.nzones;
    imap_blocks_ = s.imap_blocks; zmap_blocks_ = s.zmap_blocks;
    firstdatazone_ = s.firstdatazone;
    return true;
}

uint32_t MinixFS::alloc_zone() {
    uint8_t zb[1024];
    rd_block(2+imap_blocks_, zb);
    for (uint32_t z = firstdatazone_; z < total_blocks_; z++) {
        if (!(zb[z/8] & (1<<(z%8)))) {
            zb[z/8] |= (1<<(z%8));
            wr_block(2+imap_blocks_, zb);
            return z;
        }
    }
    return 0;
}
void MinixFS::free_zone(uint32_t z) {
    if (z == 0 || z >= total_blocks_) return;
    uint8_t zb[1024];
    rd_block(2+imap_blocks_, zb);
    if (zb[z/8] & (1<<(z%8))) {
        zb[z/8] &= ~(1<<(z%8));
        wr_block(2+imap_blocks_, zb);
    }
}
uint32_t MinixFS::alloc_inode() {
    uint8_t ib[1024];
    rd_block(2, ib);
    for (uint32_t i = 1; i < ninodes_; i++) {
        if (!(ib[i/8] & (1<<(i%8)))) {
            ib[i/8] |= (1<<(i%8));
            wr_block(2, ib);
            return i;
        }
    }
    return 0;
}
void MinixFS::free_inode(uint32_t ino) {
    if (ino == 0 || ino >= ninodes_) return;
    uint8_t ib[1024];
    rd_block(2, ib);
    if (ib[ino/8] & (1<<(ino%8))) {
        ib[ino/8] &= ~(1<<(ino%8));
        wr_block(2, ib);
    }
}

bool MinixFS::read_inode(uint32_t ino, MinixInode* in) {
    if (ino == 0 || ino > ninodes_) return false;
    uint32_t off = (ino-1)*64;
    uint32_t blk = inode_table_blk() + off/1024;
    uint32_t boff = off%1024;
    uint8_t b[1024];
    if (!rd_block(blk, b)) return false;
    const uint8_t* p = b+boff;
    in->mode = r16(p+0);
    in->uid = r16(p+2);
    in->size = r32(p+4);
    in->time = r32(p+8);
    in->gid = p[12];
    in->nlinks = p[13];
    // zone[9]:V1 占 18 字节(off 14),V2 占 36 字节
    if (version_ == 1) {
        for (int i = 0; i < 9; i++) in->zone[i] = r16(p+14+i*2);
    } else {
        // V2: zone 字段在 offset 12,4 字节 *9 = 36 字节;但我们的 MinixInode.zone 是 uint16[9]
        // 简化:V2 也读前 9 个 32 位,截到低 16 位(小磁盘足够)
        for (int i = 0; i < 9; i++) in->zone[i] = (uint16_t)r32(p+12+i*4);
    }
    return true;
}
bool MinixFS::write_inode(uint32_t ino, const MinixInode* in) {
    if (ino == 0 || ino > ninodes_) return false;
    uint32_t off = (ino-1)*64;
    uint32_t blk = inode_table_blk() + off/1024;
    uint32_t boff = off%1024;
    uint8_t b[1024];
    rd_block(blk, b);
    uint8_t* p = b+boff;
    w16(p+0, in->mode);
    w16(p+2, in->uid);
    w32(p+4, in->size);
    w32(p+8, in->time);
    p[12] = in->gid;
    p[13] = in->nlinks;
    if (version_ == 1) {
        for (int i = 0; i < 9; i++) w16(p+14+i*2, in->zone[i]);
    } else {
        for (int i = 0; i < 9; i++) w32(p+12+i*4, in->zone[i]);
    }
    return wr_block(blk, b);
}

uint32_t MinixFS::file_zone_to_phys(MinixInode* in, uint32_t lb) {
    if (lb < 7) return in->zone[lb];
    // 单间接 zone[7]
    if (in->zone[7] == 0) return 0;
    uint8_t ib[1024];
    rd_block(in->zone[7], ib);
    if (version_ == 1) {
        if (lb-7 >= 512) return 0;
        return r16(ib + (lb-7)*2);
    } else {
        if (lb-7 >= 256) return 0;
        return r32(ib + (lb-7)*4);
    }
}
uint32_t MinixFS::ensure_zone(MinixInode* in, uint32_t lb) {
    uint32_t phys = file_zone_to_phys(in, lb);
    if (phys != 0) return phys;
    if (lb < 7) {
        phys = alloc_zone();
        if (phys == 0) return 0;
        in->zone[lb] = (uint16_t)phys;
        return phys;
    }
    if (in->zone[7] == 0) {
        in->zone[7] = (uint16_t)alloc_zone();
        if (in->zone[7] == 0) return 0;
        uint8_t zib[1024]; zbc(zib, sizeof(zib));
        wr_block(in->zone[7], zib);
    }
    uint32_t idx = lb-7;
    phys = alloc_zone();
    if (phys == 0) return 0;
    uint8_t ib[1024];
    rd_block(in->zone[7], ib);
    if (version_ == 1) w16(ib+idx*2, (uint16_t)phys);
    else w32(ib+idx*4, phys);
    wr_block(in->zone[7], ib);
    return phys;
}

uint32_t MinixFS::dir_lookup(uint32_t parent, const char* name) {
    MinixInode din;
    if (!read_inode(parent, &din)) return 0;
    uint8_t b[1024];
    if (din.zone[0] == 0) return 0;
    rd_block(din.zone[0], b);
    int entsz = (version_ == 1) ? 16 : 32;
    int off = 0;
    while (off < 1024) {
        uint32_t ino = (version_ == 1) ? r16(b+off) : r32(b+off);
        if (ino == 0) break;
        char nm[30];
        int nl = 0;
        if (version_ == 1) {
            for (int i = 0; i < 14 && b[off+2+i]; i++) nm[nl++] = b[off+2+i];
        } else {
            for (int i = 0; i < 28 && b[off+4+i]; i++) nm[nl++] = b[off+4+i];
        }
        nm[nl] = 0;
        if (nefu::strcmp(nm, name) == 0) return ino;
        off += entsz;
    }
    return 0;
}

bool MinixFS::resolve_parent(const char* path, uint32_t* parent, char* leaf, int leaf_sz) {
    int n = 0; while (path[n]) n++;
    int ls = -1; for (int i = 0; i < n; i++) if (path[i]=='/') ls = i;
    if (ls < 0) return false;
    char par[128]; int pn = 0;
    for (int i = 0; i < ls && pn < 127; i++) par[pn++] = path[i];
    par[pn] = 0;
    const char* lf = path+ls+1;
    if (!lf[0]) return false;
    uint32_t cur = 1; // 根目录 inode = 1
    if (ls != 0) {
        const char* p = par;
        while (*p == '/') p++;
        char seg[32];
        while (*p) {
            int k = 0; while (*p && *p != '/' && k < 31) seg[k++] = *p++;
            seg[k] = 0; while (*p == '/') p++;
            uint32_t nx = dir_lookup(cur, seg);
            if (nx == 0) return false;
            cur = nx;
        }
    }
    *parent = cur;
    int k = 0; while (lf[k] && k < leaf_sz-1) { leaf[k]=lf[k]; k++; } leaf[k]=0;
    return true;
}

uint32_t MinixFS::namei(const char* path) {
    if (!path || path[0] != '/') return 0;
    if (nefu::strcmp(path, "/") == 0) return 1;
    uint32_t cur = 1;
    char seg[32];
    const char* p = path+1;
    while (*p) {
        int k = 0; while (*p && *p != '/' && k < 31) seg[k++] = *p++;
        seg[k] = 0; while (*p == '/') p++;
        uint32_t nx = dir_lookup(cur, seg);
        if (nx == 0) return 0;
        cur = nx;
    }
    return cur;
}

bool MinixFS::dir_add_entry(uint32_t parent, const char* name, uint32_t ino) {
    MinixInode din;
    read_inode(parent, &din);
    uint8_t b[1024];
    rd_block(din.zone[0], b);
    int entsz = (version_ == 1) ? 16 : 32;
    int off = 0;
    while (off < 1024) {
        uint32_t cur = (version_==1)?r16(b+off):r32(b+off);
        if (cur == 0) break;
        off += entsz;
    }
    if (off + entsz > 1024) return false;
    if (version_ == 1) {
        w16(b+off, (uint16_t)ino);
        for (int i = 0; name[i] && i < 14; i++) b[off+2+i] = name[i];
    } else {
        w32(b+off, ino);
        for (int i = 0; name[i] && i < 28; i++) b[off+4+i] = name[i];
    }
    wr_block(din.zone[0], b);
    return true;
}
bool MinixFS::dir_remove_entry(uint32_t parent, const char* name) {
    MinixInode din; read_inode(parent, &din);
    uint8_t b[1024]; rd_block(din.zone[0], b);
    int entsz = (version_==1)?16:32;
    int off = 0;
    while (off < 1024) {
        uint32_t cur = (version_==1)?r16(b+off):r32(b+off);
        if (cur == 0) break;
        char nm[30]; int nl=0;
        if (version_==1) { for(int i=0;i<14&&b[off+2+i];i++) nm[nl++]=b[off+2+i]; }
        else { for(int i=0;i<28&&b[off+4+i];i++) nm[nl++]=b[off+4+i]; }
        nm[nl]=0;
        if (nefu::strcmp(nm, name)==0) {
            if (version_==1) w16(b+off,0); else w32(b+off,0);
            wr_block(din.zone[0], b);
            return true;
        }
        off += entsz;
    }
    return false;
}

bool MinixFS::create_file(const char* path) {
    uint32_t parent; char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    if (dir_lookup(parent, leaf) != 0) return false;
    uint32_t ino = alloc_inode();
    if (ino == 0) return false;
    MinixInode in; zbc(&in, sizeof(in));
    in.mode = MINIX_REG | 0644;
    in.size = 0; in.nlinks = 1;
    write_inode(ino, &in);
    return dir_add_entry(parent, leaf, ino);
}
bool MinixFS::mkdir(const char* path) {
    uint32_t parent; char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    if (dir_lookup(parent, leaf) != 0) return false;
    uint32_t ino = alloc_inode();
    if (ino == 0) return false;
    uint32_t blk = alloc_zone();
    if (blk == 0) { free_inode(ino); return false; }
    MinixInode in; zbc(&in, sizeof(in));
    in.mode = MINIX_DIR | 0755;
    in.size = 1024; in.nlinks = 2;
    in.zone[0] = (uint16_t)blk;
    write_inode(ino, &in);
    uint8_t db[1024]; zbc(db, sizeof(db));
    if (version_==1) {
        w16(db+0,(uint16_t)ino); db[2]='.';
        w16(db+16,(uint16_t)parent); db[18]='.'; db[19]='.';
    } else {
        w32(db+0,ino); db[4]='.';
        w32(db+32,parent); db[36]='.'; db[37]='.';
    }
    wr_block(blk, db);
    return dir_add_entry(parent, leaf, ino);
}
bool MinixFS::remove(const char* path) {
    uint32_t ino = namei(path);
    if (ino == 0) return false;
    MinixInode in; read_inode(ino, &in);
    for (int i = 0; i < 7; i++) if (in.zone[i]) free_zone(in.zone[i]);
    if (in.zone[7]) {
        uint8_t ib[1024]; rd_block(in.zone[7], ib);
        int cnt = (version_==1)?512:256;
        for (int i = 0; i < cnt; i++) {
            uint32_t z = (version_==1)?r16(ib+i*2):r32(ib+i*4);
            if (z) free_zone(z);
        }
        free_zone(in.zone[7]);
    }
    uint32_t parent; char leaf[32];
    resolve_parent(path, &parent, leaf, sizeof(leaf));
    dir_remove_entry(parent, leaf);
    free_inode(ino);
    return true;
}

// 重命名:在父目录块里找到旧项,覆盖名字字节(项大小固定)。
bool MinixFS::rename(const char* oldpath, const char* newname) {
    uint32_t parent; char leaf[32];
    if (!resolve_parent(oldpath, &parent, leaf, sizeof(leaf))) return false;
    if (dir_lookup(parent, newname) != 0) return false;
    uint32_t old_ino = dir_lookup(parent, leaf);
    if (old_ino == 0) return false;
    MinixInode din; read_inode(parent, &din);
    uint8_t b[1024]; rd_block(din.zone[0], b);
    int entsz = (version_==1)?16:32;
    int off = 0;
    while (off < 1024) {
        uint32_t cur = (version_==1)?r16(b+off):r32(b+off);
        if (cur == 0) break;
        if (cur == old_ino) {
            int nmlen = (version_==1)?14:28;
            int base = (version_==1)?(off+2):(off+4);
            for (int i = 0; i < nmlen; i++) b[base+i] = 0;
            for (int i = 0; newname[i] && i < nmlen; i++) b[base+i] = (uint8_t)newname[i];
            wr_block(din.zone[0], b);
            return true;
        }
        off += entsz;
    }
    return false;
}

long MinixFS::file_size(const char* path) {
    uint32_t ino = namei(path);
    if (ino == 0) return -1;
    MinixInode in; read_inode(ino, &in);
    return (long)in.size;
}
int MinixFS::read_file(const char* path, uint8_t* buf, uint32_t bufsz) {
    uint32_t ino = namei(path);
    if (ino == 0) return -1;
    MinixInode in; read_inode(ino, &in);
    uint32_t toread = in.size; if (toread > bufsz) toread = bufsz;
    uint8_t b[1024]; uint32_t done = 0;
    while (done < toread) {
        uint32_t phys = file_zone_to_phys(&in, done/1024);
        if (phys == 0) break;
        rd_block(phys, b);
        uint32_t off = done%1024;
        uint32_t chunk = 1024-off;
        if (done+chunk > toread) chunk = toread-done;
        nefu::memcpy(buf+done, b+off, chunk);
        done += chunk;
    }
    return (int)done;
}
bool MinixFS::write_file(const char* path, const uint8_t* data, uint32_t len) {
    uint32_t ino = namei(path);
    if (ino == 0) { if (!create_file(path)) return false; ino = namei(path); if (ino==0) return false; }
    MinixInode in; read_inode(ino, &in);
    if (in.mode & MINIX_DIR) return false;
    for (int i = 0; i < 7; i++) { if (in.zone[i]) free_zone(in.zone[i]); in.zone[i]=0; }
    if (in.zone[7]) {
        uint8_t ib[1024]; rd_block(in.zone[7], ib);
        int cnt = (version_==1)?512:256;
        for (int i=0;i<cnt;i++){uint32_t z=(version_==1)?r16(ib+i*2):r32(ib+i*4); if(z) free_zone(z);}
        free_zone(in.zone[7]); in.zone[7]=0;
    }
    uint32_t done = 0; uint8_t b[1024];
    while (done < len) {
        uint32_t phys = ensure_zone(&in, done/1024);
        if (phys == 0) { write_inode(ino, &in); return false; }
        zbc(b, sizeof(b));
        uint32_t chunk = 1024; if (done+chunk > len) chunk = len-done;
        nefu::memcpy(b, data+done, chunk);
        wr_block(phys, b);
        done += chunk;
    }
    in.size = len;
    return write_inode(ino, &in);
}

int MinixFS::list_dir(const char* path, MinixDirInfo* out, int max_out) {
    uint32_t ino = namei(path);
    if (ino == 0) return 0;
    MinixInode din; read_inode(ino, &din);
    if (!(din.mode & MINIX_DIR)) return 0;
    uint8_t b[1024];
    if (din.zone[0] == 0) return 0;
    rd_block(din.zone[0], b);
    int entsz = (version_==1)?16:32;
    int off = 0; int n = 0;
    while (off < 1024 && n < max_out) {
        uint32_t cur = (version_==1)?r16(b+off):r32(b+off);
        if (cur == 0) break;
        int nl=0;
        if (version_==1) { for(int i=0;i<14&&b[off+2+i];i++) out[n].name[nl++]=b[off+2+i]; }
        else { for(int i=0;i<28&&b[off+4+i];i++) out[n].name[nl++]=b[off+4+i]; }
        out[n].name[nl]=0;
        off += entsz;
        if (out[n].name[0]=='.') continue;
        out[n].is_dir = false; out[n].inode = cur; out[n].size = 0;
        MinixInode ein; if (read_inode(cur, &ein)) { out[n].is_dir = (ein.mode & MINIX_DIR)!=0; out[n].size = ein.size; }
        n++;
    }
    return n;
}

uint32_t MinixFS::free_zones() {
    uint8_t zb[1024]; rd_block(2+imap_blocks_, zb);
    uint32_t f = 0;
    for (uint32_t z = firstdatazone_; z < total_blocks_; z++)
        if (!(zb[z/8] & (1<<(z%8)))) f++;
    return f;
}
uint32_t MinixFS::total_zones() { return total_blocks_ - firstdatazone_; }

int minixfs_self_test() {
    int fails = 0;
    // V1
    {
        Disk disk; disk.create(4096);
        MinixFS fs;
        if (!fs.format(&disk, 0, 4096, 1)) return 1;
        if (fs.version() != 1) fails++;
        fs.write_file("/a.txt", (const uint8_t*)"minix v1", 8);
        if (fs.file_size("/a.txt") != 8) fails++;
        uint8_t rb[32]; int r = fs.read_file("/a.txt", rb, sizeof(rb));
        if (r != 8) fails++;
        fs.mkdir("/etc");
        fs.write_file("/etc/hosts", (const uint8_t*)"127.0.0.1", 9);
        if (fs.file_size("/etc/hosts") != 9) fails++;
        MinixDirInfo en[16]; int n = fs.list_dir("/", en, 16);
        if (n != 2) fails++; // a.txt + etc
        // 大文件
        uint8_t big[3000]; for (int i=0;i<3000;i++) big[i]=(uint8_t)(i&0xFF);
        fs.write_file("/big", big, 3000);
        uint8_t br[3000]; int brr = fs.read_file("/big", br, sizeof(br));
        if (brr != 3000) fails++;
        for (int i=0;i<3000;i++) if (br[i]!=big[i]) { fails++; break; }
        // 重挂载
        MinixFS fs2; fs2.mount(&disk, 0);
        if (fs2.file_size("/etc/hosts") != 9) fails++;
        // 重命名
        if (!fs.rename("/a.txt", "b.txt")) fails++;
        if (fs.file_size("/a.txt") != -1) fails++;
        if (fs.file_size("/b.txt") != 8) fails++;

        // 多文件
        for (int i = 0; i < 5; i++) {
            char p[24]; p[0]='/'; p[1]='d'; p[2]='a'; p[3]='t'; p[4]=(char)('0'+i); p[5]=0;
            if (!fs.write_file(p,(const uint8_t*)"ab",2)) { fails++; break; }
        }
        for (int i = 0; i < 5; i++) {
            char p[24]; p[0]='/'; p[1]='d'; p[2]='a'; p[3]='t'; p[4]=(char)('0'+i); p[5]=0;
            if (fs.file_size(p) != 2) { fails++; break; }
        }
    }
    // V2
    {
        Disk disk; disk.create(4096);
        MinixFS fs;
        if (!fs.format(&disk, 0, 4096, 2)) return 2;
        if (fs.version() != 2) fails++;
        fs.write_file("/v2.txt", (const uint8_t*)"hello v2", 8);
        if (fs.file_size("/v2.txt") != 8) fails++;
        uint8_t rb[32]; int r = fs.read_file("/v2.txt", rb, sizeof(rb));
        if (r != 8) fails++;
        MinixDirInfo en[16]; int n = fs.list_dir("/", en, 16);
        if (n != 1) fails++;
    }
    return fails;
}

} // namespace filesystem
} // namespace nefu
