// nefuOS 文件系统库 —— ext2 文件系统实现
#include "ext2.h"
#include "../klib/klib.h"

namespace nefu {
namespace filesystem {

static inline void w16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void w32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline uint16_t r16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
}
static inline uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void zbc(void* dst, int n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    while (n-- > 0) *d++ = 0;
}

Ext2::Ext2()
    : disk_(0), part_start_(0), total_blocks_(0),
      inodes_per_group_(64), blocks_per_group_(0), mounted_(false) {
}
Ext2::~Ext2() { disk_ = 0; }

bool Ext2::rd_lba(uint32_t lba, uint8_t* buf) { return disk_->read_sector(part_start_+lba, buf); }
bool Ext2::wr_lba(uint32_t lba, const uint8_t* buf) { return disk_->write_sector(part_start_+lba, buf); }
bool Ext2::rd_block(uint32_t blk, uint8_t* buf) {
    return rd_lba(blk*EXT2_SECTORS_PER_BLOCK, buf)
        && rd_lba(blk*EXT2_SECTORS_PER_BLOCK+1, buf+512);
}
bool Ext2::wr_block(uint32_t blk, const uint8_t* buf) {
    return wr_lba(blk*EXT2_SECTORS_PER_BLOCK, buf)
        && wr_lba(blk*EXT2_SECTORS_PER_BLOCK+1, buf+512);
}

// 超级块在块 1,1024 字节
bool Ext2::read_super(Ext2Super* s) {
    uint8_t blk[1024];
    if (!rd_block(1, blk)) return false;
    s->inodes_count = r32(blk+0);
    s->blocks_count = r32(blk+4);
    s->free_blocks_count = r32(blk+12);
    s->free_inodes_count = r32(blk+16);
    s->first_data_block = r32(blk+20);
    s->log_block_size = r32(blk+24);
    s->blocks_per_group = r32(blk+32);
    s->inodes_per_group = r32(blk+40);
    s->magic = r16(blk+56);
    return true;
}
bool Ext2::write_super(const Ext2Super* s) {
    uint8_t blk[1024];
    zbc(blk, sizeof(blk));
    w32(blk+0, s->inodes_count);
    w32(blk+4, s->blocks_count);
    w32(blk+12, s->free_blocks_count);
    w32(blk+16, s->free_inodes_count);
    w32(blk+20, s->first_data_block);
    w32(blk+24, s->log_block_size);
    w32(blk+32, s->blocks_per_group);
    w32(blk+40, s->inodes_per_group);
    w16(blk+56, s->magic);
    return wr_block(1, blk);
}

// 组描述符在块 2,只取前 32 字节
bool Ext2::read_gd(Ext2GroupDesc* g) {
    uint8_t blk[1024];
    if (!rd_block(2, blk)) return false;
    g->block_bitmap = r32(blk+0);
    g->inode_bitmap = r32(blk+4);
    g->inode_table = r32(blk+8);
    g->free_blocks_count = r16(blk+12);
    g->free_inodes_count = r16(blk+14);
    g->used_dirs_count = r16(blk+16);
    return true;
}
bool Ext2::write_gd(const Ext2GroupDesc* g) {
    uint8_t blk[1024];
    zbc(blk, sizeof(blk));
    w32(blk+0, g->block_bitmap);
    w32(blk+4, g->inode_bitmap);
    w32(blk+8, g->inode_table);
    w16(blk+12, g->free_blocks_count);
    w16(blk+14, g->free_inodes_count);
    w16(blk+16, g->used_dirs_count);
    return wr_block(2, blk);
}

// ===================== 格式化 =====================
bool Ext2::format(Disk* disk, uint32_t start_lba, uint32_t total_lba) {
    disk_ = disk;
    part_start_ = start_lba;
    total_blocks_ = total_lba / EXT2_SECTORS_PER_BLOCK;
    inodes_per_group_ = 64;
    blocks_per_group_ = total_blocks_;

    // 布局:块 0 boot,1 super,2 gd,3 bbitmap,4 ibitmap,5..12 inode 表(8 块 = 64 inode)
    uint32_t inode_table_blk = 5;
    uint32_t itbl_blocks = (inodes_per_group_ * 128) / 1024; // 8
    uint32_t first_data = inode_table_blk + itbl_blocks;

    // 清所有块(块 0..first_data-1 已分配保留)
    uint8_t zblk[1024];
    zbc(zblk, sizeof(zblk));
    for (uint32_t b = 0; b < total_blocks_; b++) wr_block(b, zblk);

    // 写超级块
    Ext2Super s;
    zbc(&s, sizeof(s));
    s.inodes_count = inodes_per_group_;
    s.blocks_count = total_blocks_;
    s.free_blocks_count = total_blocks_ - first_data - 1; // 元数据块 + 根目录数据块
    s.free_inodes_count = inodes_per_group_ - 2; // 保留 inode 1(坏块)+ inode 2(根)
    s.first_data_block = 0;
    s.log_block_size = 0; // 1024 字节
    s.blocks_per_group = blocks_per_group_;
    s.inodes_per_group = inodes_per_group_;
    s.magic = EXT2_MAGIC;
    write_super(&s);

    // 写组描述符
    Ext2GroupDesc g;
    zbc(&g, sizeof(g));
    g.block_bitmap = 3;
    g.inode_bitmap = 4;
    g.inode_table = inode_table_blk;
    g.free_blocks_count = (uint16_t)(total_blocks_ - first_data - 1);
    g.free_inodes_count = (uint16_t)(inodes_per_group_ - 2);
    g.used_dirs_count = 1; // 根目录
    write_gd(&g);

    // 初始化块位图:块 0..first_data-1 是元数据保留块,first_data 是根目录数据块,
    // 都要标记为已用,否则 alloc_block 会把根目录数据块当成空闲块分配出去。
    uint8_t bb[1024];
    rd_block(3, bb);
    for (uint32_t b = 0; b <= first_data; b++) bb[b/8] |= (uint8_t)(1 << (b%8));
    wr_block(3, bb);

    // 初始化 inode 位图:alloc_inode 用 1<<(i%8),即 inode N 占 bit N。
    // inode 1(坏块保留)、inode 2(根目录)已用 -> bit1,bit2 = 0x06。
    uint8_t ib[1024];
    rd_block(4, ib);
    ib[0] = 0x06;
    wr_block(4, ib);

    // 创建根目录 inode 2
    Ext2Inode root;
    zbc(&root, sizeof(root));
    root.mode = EXT2_IFDIR | 0755;
    root.size = 1024;
    root.links_count = 2;
    root.blocks = 1;
    root.i_block[0] = first_data; // 根目录数据块
    write_inode(2, &root);

    // 根目录数据块:写 "." 和 ".."
    uint8_t db[1024];
    zbc(db, sizeof(db));
    Ext2DirHeader* h = (Ext2DirHeader*)db;
    h->inode = 2; h->rec_len = 12; h->name_len = 1; h->file_type = 2;
    db[8] = '.';
    h = (Ext2DirHeader*)(db + 12);
    h->inode = 2; h->rec_len = 1012; h->name_len = 2; h->file_type = 2;
    db[12+8] = '.'; db[12+9] = '.';
    wr_block(first_data, db);

    mounted_ = true;
    return true;
}

bool Ext2::mount(Disk* disk, uint32_t start_lba) {
    disk_ = disk;
    part_start_ = start_lba;
    Ext2Super s;
    if (!read_super(&s)) return false;
    if (s.magic != EXT2_MAGIC) return false;
    total_blocks_ = s.blocks_count;
    inodes_per_group_ = s.inodes_per_group;
    blocks_per_group_ = s.blocks_per_group;
    mounted_ = true;
    return true;
}

// ===================== 位图分配 =====================
uint32_t Ext2::alloc_block() {
    Ext2GroupDesc g; read_gd(&g);
    Ext2Super s; read_super(&s);
    uint8_t bb[1024];
    rd_block(g.block_bitmap, bb);
    // 找第一个空闲块(从数据区开始)
    uint32_t itbl_blocks = (inodes_per_group_*128)/1024;
    uint32_t first_data = g.inode_table + itbl_blocks;
    for (uint32_t b = first_data; b < total_blocks_; b++) {
        if (!(bb[b/8] & (1 << (b%8)))) {
            bb[b/8] |= (1 << (b%8));
            wr_block(g.block_bitmap, bb);
            g.free_blocks_count--; s.free_blocks_count--;
            write_gd(&g); write_super(&s);
            return b;
        }
    }
    return 0;
}
void Ext2::free_block(uint32_t blk) {
    if (blk == 0 || blk >= total_blocks_) return;
    Ext2GroupDesc g; read_gd(&g);
    uint8_t bb[1024];
    rd_block(g.block_bitmap, bb);
    if (bb[blk/8] & (1 << (blk%8))) {
        bb[blk/8] &= ~(1 << (blk%8));
        wr_block(g.block_bitmap, bb);
        g.free_blocks_count++;
        Ext2Super s; read_super(&s); s.free_blocks_count++;
        write_gd(&g); write_super(&s);
    }
}
uint32_t Ext2::alloc_inode() {
    Ext2GroupDesc g; read_gd(&g);
    Ext2Super s; read_super(&s);
    uint8_t ib[1024];
    rd_block(g.inode_bitmap, ib);
    for (uint32_t i = 1; i < inodes_per_group_; i++) {
        if (!(ib[i/8] & (1 << (i%8)))) {
            ib[i/8] |= (1 << (i%8));
            wr_block(g.inode_bitmap, ib);
            g.free_inodes_count--; s.free_inodes_count--;
            write_gd(&g); write_super(&s);
            return i;
        }
    }
    return 0;
}
void Ext2::free_inode(uint32_t ino) {
    if (ino == 0 || ino >= inodes_per_group_) return;
    Ext2GroupDesc g; read_gd(&g);
    uint8_t ib[1024];
    rd_block(g.inode_bitmap, ib);
    if (ib[ino/8] & (1 << (ino%8))) {
        ib[ino/8] &= ~(1 << (ino%8));
        wr_block(g.inode_bitmap, ib);
        g.free_inodes_count++;
        Ext2Super s; read_super(&s); s.free_inodes_count++;
        write_gd(&g); write_super(&s);
    }
}

// ===================== inode 读写 =====================
bool Ext2::read_inode(uint32_t ino, Ext2Inode* in) {
    if (ino == 0 || ino > inodes_per_group_) return false;
    Ext2GroupDesc g; read_gd(&g);
    uint32_t idx = ino - 1;                 // inode 从 1 开始
    uint32_t off = idx * 128;
    uint32_t blk = g.inode_table + off / 1024;
    uint32_t boff = off % 1024;
    uint8_t b[1024];
    if (!rd_block(blk, b)) return false;
    const uint8_t* p = b + boff;
    in->mode = r16(p+0);
    in->uid = r16(p+2);
    in->size = r32(p+4);
    in->links_count = r16(p+26);
    in->blocks = r32(p+28);
    for (int i = 0; i < 15; i++) in->i_block[i] = r32(p+40+i*4);
    return true;
}
bool Ext2::write_inode(uint32_t ino, const Ext2Inode* in) {
    if (ino == 0 || ino > inodes_per_group_) return false;
    Ext2GroupDesc g; read_gd(&g);
    uint32_t idx = ino - 1;
    uint32_t off = idx * 128;
    uint32_t blk = g.inode_table + off / 1024;
    uint32_t boff = off % 1024;
    uint8_t b[1024];
    rd_block(blk, b);
    uint8_t* p = b + boff;
    w16(p+0, in->mode);
    w16(p+2, in->uid);
    w32(p+4, in->size);
    w16(p+26, in->links_count);
    w32(p+28, in->blocks);
    for (int i = 0; i < 15; i++) w32(p+40+i*4, in->i_block[i]);
    return wr_block(blk, b);
}

// ===================== 文件块映射 =====================
uint32_t Ext2::file_block_to_phys(Ext2Inode* in, uint32_t lb) {
    if (lb < 12) return in->i_block[lb];
    // 单间接
    if (in->i_block[12] == 0) return 0;
    uint8_t ib[1024];
    rd_block(in->i_block[12], ib);
    uint32_t idx = lb - 12;
    if (idx >= 256) return 0;
    return r32(ib + idx*4);
}
uint32_t Ext2::ensure_block(Ext2Inode* in, uint32_t lb) {
    uint32_t phys = file_block_to_phys(in, lb);
    if (phys != 0) return phys;
    if (lb < 12) {
        phys = alloc_block();
        if (phys == 0) return 0;
        in->i_block[lb] = phys;
        in->blocks += 2; // 每块 1K = 2 扇区(512)
        return phys;
    }
    // 间接块
    if (in->i_block[12] == 0) {
        in->i_block[12] = alloc_block();
        if (in->i_block[12] == 0) return 0;
        in->blocks += 2;
        uint8_t zib[1024]; zbc(zib, sizeof(zib));
        wr_block(in->i_block[12], zib);
    }
    uint32_t idx = lb - 12;
    if (idx >= 256) return 0;
    phys = alloc_block();
    if (phys == 0) return 0;
    uint8_t ib[1024];
    rd_block(in->i_block[12], ib);
    w32(ib + idx*4, phys);
    wr_block(in->i_block[12], ib);
    in->blocks += 2;
    return phys;
}

// ===================== 路径解析 =====================
uint32_t Ext2::dir_lookup(uint32_t parent_ino, const char* name) {
    Ext2Inode din;
    if (!read_inode(parent_ino, &din)) return 0;
    uint8_t blk[1024];
    // 简化:目录最多一个直接块(1024 字节)
    if (din.i_block[0] == 0) return 0;
    rd_block(din.i_block[0], blk);
    uint16_t off = 0;
    while (off < 1024) {
        Ext2DirHeader* h = (Ext2DirHeader*)(blk + off);
        if (h->inode == 0) break;
        uint16_t rec = h->rec_len;
        if (rec == 0) break;
        // 比较名
        char nm[64];
        int nl = h->name_len;
        if (nl >= 64) nl = 63;
        for (int i = 0; i < nl; i++) nm[i] = (char)blk[off+8+i];
        nm[nl] = 0;
        if (nefu::strcmp(nm, name) == 0) return h->inode;
        off += rec;
    }
    return 0;
}

uint32_t Ext2::namei(const char* path) {
    if (!path || path[0] != '/') return 0;
    if (nefu::strcmp(path, "/") == 0) return 2;
    uint32_t cur = 2;
    char seg[64];
    const char* p = path + 1;
    while (*p) {
        int k = 0;
        while (*p && *p != '/' && k < 63) seg[k++] = *p++;
        seg[k] = 0;
        while (*p == '/') p++;
        uint32_t nxt = dir_lookup(cur, seg);
        if (nxt == 0) return 0;
        cur = nxt;
    }
    return cur;
}

bool Ext2::resolve_parent(const char* path, uint32_t* parent_inode, char* leaf, int leaf_sz) {
    // 找最后一个 '/'
    int n = 0; while (path[n]) n++;
    int ls = -1;
    for (int i = 0; i < n; i++) if (path[i] == '/') ls = i;
    if (ls < 0) return false;
    char parent[128];
    int pn = 0;
    for (int i = 0; i < ls && pn < 127; i++) parent[pn++] = path[i];
    parent[pn] = 0;
    const char* lf = path + ls + 1;
    if (!lf[0]) return false;
    uint32_t pi = (ls == 0) ? 2 : namei(parent);
    if (pi == 0) return false;
    *parent_inode = pi;
    int k = 0;
    while (lf[k] && k < leaf_sz-1) { leaf[k] = lf[k]; k++; }
    leaf[k] = 0;
    return true;
}

// ===================== 目录项增删 =====================
bool Ext2::dir_add_entry(uint32_t parent_ino, const char* name, uint32_t target_ino, uint8_t file_type) {
    Ext2Inode din;
    if (!read_inode(parent_ino, &din)) return false;
    uint8_t blk[1024];
    rd_block(din.i_block[0], blk);
    // 找最后一个 entry,把它的 rec_len 收缩到自身真实占用,新条目接在后面
    uint16_t off = 0;
    uint16_t last_off = 0; uint8_t last_namelen = 0;
    bool have_last = false;
    while (off < 1024) {
        Ext2DirHeader* h = (Ext2DirHeader*)(blk+off);
        if (h->inode == 0) break;
        last_off = off; last_namelen = h->name_len;
        have_last = true;
        off += h->rec_len;
    }
    int namelen = (int)nefu::strlen(name);
    int entlen = 8 + namelen;
    while (entlen % 4 != 0) entlen++;
    // 末项真实占用长度(对齐)
    int last_real = 8 + last_namelen;
    while (last_real % 4 != 0) last_real++;
    uint16_t new_off;
    if (!have_last) {
        new_off = 0;
    } else {
        // 收缩末项
        Ext2DirHeader* lh = (Ext2DirHeader*)(blk+last_off);
        lh->rec_len = (uint16_t)last_real;
        new_off = (uint16_t)(last_off + last_real);
    }
    if (new_off + entlen > 1024) return false; // 目录块满(简化)
    Ext2DirHeader* h = (Ext2DirHeader*)(blk+new_off);
    h->inode = target_ino;
    h->rec_len = (uint16_t)(1024 - new_off);
    h->name_len = (uint8_t)namelen;
    h->file_type = file_type;
    for (int i = 0; i < namelen; i++) blk[new_off+8+i] = (uint8_t)name[i];
    wr_block(din.i_block[0], blk);
    return true;
}

bool Ext2::dir_remove_entry(uint32_t parent_ino, const char* name) {
    Ext2Inode din;
    if (!read_inode(parent_ino, &din)) return false;
    uint8_t blk[1024];
    rd_block(din.i_block[0], blk);
    uint16_t off = 0; uint16_t prev_off = 0;
    while (off < 1024) {
        Ext2DirHeader* h = (Ext2DirHeader*)(blk+off);
        if (h->inode == 0) break;
        char nm[64]; int nl = h->name_len; if (nl >= 64) nl = 63;
        for (int i = 0; i < nl; i++) nm[i] = (char)blk[off+8+i];
        nm[nl] = 0;
        if (nefu::strcmp(nm, name) == 0) {
            // 把这一项合并到前一项
            Ext2DirHeader* ph = (Ext2DirHeader*)(blk+prev_off);
            ph->rec_len += h->rec_len;
            h->inode = 0;
            wr_block(din.i_block[0], blk);
            return true;
        }
        prev_off = off;
        off += h->rec_len;
    }
    return false;
}

// ===================== 创建/删除 =====================
bool Ext2::create_file(const char* path) {
    uint32_t parent; char leaf[64];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    if (dir_lookup(parent, leaf) != 0) return false;
    uint32_t ino = alloc_inode();
    if (ino == 0) return false;
    Ext2Inode in; zbc(&in, sizeof(in));
    in.mode = EXT2_IFREG | 0644;
    in.size = 0; in.links_count = 1; in.blocks = 0;
    write_inode(ino, &in);
    return dir_add_entry(parent, leaf, ino, 1);
}

bool Ext2::mkdir(const char* path) {
    uint32_t parent; char leaf[64];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    if (dir_lookup(parent, leaf) != 0) return false;
    uint32_t ino = alloc_inode();
    if (ino == 0) return false;
    uint32_t blk = alloc_block();
    if (blk == 0) { free_inode(ino); return false; }
    Ext2Inode in; zbc(&in, sizeof(in));
    in.mode = EXT2_IFDIR | 0755;
    in.size = 1024; in.links_count = 2; in.blocks = 2;
    in.i_block[0] = blk;
    write_inode(ino, &in);
    // 目录块写 "." 和 ".."
    uint8_t db[1024]; zbc(db, sizeof(db));
    Ext2DirHeader* h = (Ext2DirHeader*)db;
    h->inode = ino; h->rec_len = 12; h->name_len = 1; h->file_type = 2;
    db[8] = '.';
    h = (Ext2DirHeader*)(db+12);
    h->inode = parent; h->rec_len = 1012; h->name_len = 2; h->file_type = 2;
    db[20]='.'; db[21]='.';
    wr_block(blk, db);
    return dir_add_entry(parent, leaf, ino, 2);
}

bool Ext2::remove(const char* path) {
    uint32_t ino = namei(path);
    if (ino == 0) return false;
    Ext2Inode in; read_inode(ino, &in);
    // 释放所有数据块
    for (int lb = 0; lb < 12; lb++) if (in.i_block[lb]) free_block(in.i_block[lb]);
    if (in.i_block[12]) {
        uint8_t ib[1024];
        rd_block(in.i_block[12], ib);
        for (int i = 0; i < 256; i++) {
            uint32_t b = r32(ib+i*4);
            if (b) free_block(b);
        }
        free_block(in.i_block[12]);
    }
    // 父目录去掉项
    uint32_t parent; char leaf[64];
    resolve_parent(path, &parent, leaf, sizeof(leaf));
    dir_remove_entry(parent, leaf);
    free_inode(ino);
    return true;
}

bool Ext2::symlink(const char* target, const char* linkpath) {
    uint32_t parent; char leaf[64];
    if (!resolve_parent(linkpath, &parent, leaf, sizeof(leaf))) return false;
    if (dir_lookup(parent, leaf) != 0) return false;
    uint32_t ino = alloc_inode();
    if (ino == 0) return false;
    Ext2Inode in; zbc(&in, sizeof(in));
    in.mode = EXT2_IFLNK | 0x777;
    // 短符号链接:目标路径直接存在 i_block 里(<=60 字节)
    uint32_t tlen = (uint32_t)nefu::strlen(target);
    in.size = tlen;
    in.links_count = 1;
    uint8_t* p = (uint8_t*)in.i_block;
    for (uint32_t i = 0; i < tlen && i < 60; i++) p[i] = (uint8_t)target[i];
    write_inode(ino, &in);
    return dir_add_entry(parent, leaf, ino, 7); // file_type=7 符号链接
}
bool Ext2::readlink(const char* path, char* buf, int bufsz) {
    uint32_t ino = namei(path);
    if (ino == 0) return false;
    Ext2Inode in; read_inode(ino, &in);
    if (!(in.mode & EXT2_IFLNK)) return false;
    uint8_t* p = (uint8_t*)in.i_block;
    uint32_t n = in.size;
    if (n > 60) n = 60;
    if (n > (uint32_t)(bufsz-1)) n = bufsz-1;
    for (uint32_t i = 0; i < n; i++) buf[i] = (char)p[i];
    buf[n] = 0;
    return true;
}

// ===================== 读/写文件 =====================
long Ext2::file_size(const char* path) {
    uint32_t ino = namei(path);
    if (ino == 0) return -1;
    Ext2Inode in; read_inode(ino, &in);
    return (long)in.size;
}
int Ext2::read_file(const char* path, uint8_t* buf, uint32_t bufsz) {
    uint32_t ino = namei(path);
    if (ino == 0) return -1;
    Ext2Inode in; read_inode(ino, &in);
    uint32_t toread = in.size;
    if (toread > bufsz) toread = bufsz;
    uint8_t blk[1024];
    uint32_t done = 0;
    while (done < toread) {
        uint32_t lb = done / 1024;
        uint32_t phys = file_block_to_phys(&in, lb);
        if (phys == 0) break;
        rd_block(phys, blk);
        uint32_t off = done % 1024;
        uint32_t chunk = 1024 - off;
        if (done + chunk > toread) chunk = toread - done;
        nefu::memcpy(buf + done, blk + off, chunk);
        done += chunk;
    }
    return (int)done;
}
bool Ext2::write_file(const char* path, const uint8_t* data, uint32_t len) {
    uint32_t ino = namei(path);
    if (ino == 0) {
        // 文件不存在则自动创建(类似 FAT16 行为)
        if (!create_file(path)) return false;
        ino = namei(path);
        if (ino == 0) return false;
    }
    Ext2Inode in; read_inode(ino, &in);
    if (in.mode & EXT2_IFDIR) return false;
    // 释放旧数据块
    for (int lb = 0; lb < 12; lb++) if (in.i_block[lb]) { free_block(in.i_block[lb]); in.i_block[lb]=0; }
    if (in.i_block[12]) {
        uint8_t ib[1024]; rd_block(in.i_block[12], ib);
        for (int i = 0; i < 256; i++) { uint32_t b=r32(ib+i*4); if (b) free_block(b); }
        free_block(in.i_block[12]); in.i_block[12]=0;
    }
    in.blocks = 0;
    // 写新数据
    uint32_t done = 0;
    uint8_t blk[1024];
    while (done < len) {
        uint32_t lb = done / 1024;
        uint32_t phys = ensure_block(&in, lb);
        if (phys == 0) { write_inode(ino, &in); return false; }
        zbc(blk, sizeof(blk));
        uint32_t chunk = 1024;
        if (done + chunk > len) chunk = len - done;
        nefu::memcpy(blk, data + done, chunk);
        wr_block(phys, blk);
        done += chunk;
    }
    in.size = len;
    return write_inode(ino, &in);
}

// ===================== 列目录 =====================
int Ext2::list_dir(const char* path, Ext2DirInfo* out, int max_out) {
    uint32_t ino = namei(path);
    if (ino == 0) return 0;
    Ext2Inode din; read_inode(ino, &din);
    if (!(din.mode & EXT2_IFDIR)) return 0;
    uint8_t blk[1024];
    if (din.i_block[0] == 0) return 0;
    rd_block(din.i_block[0], blk);
    int n = 0;
    uint16_t off = 0;
    while (off < 1024 && n < max_out) {
        Ext2DirHeader* h = (Ext2DirHeader*)(blk+off);
        if (h->inode == 0) break;
        uint16_t rec = h->rec_len; if (rec==0) break;
        int nl = h->name_len; if (nl >= 63) nl = 63;
        for (int i = 0; i < nl; i++) out[n].name[i] = (char)blk[off+8+i];
        out[n].name[nl] = 0;
        // 跳过 "." 和 ".."
        if (out[n].name[0] == '.' ) { off += rec; continue; }
        out[n].is_dir = (h->file_type == 2);
        out[n].is_symlink = (h->file_type == 7);
        out[n].inode = h->inode;
        // 取大小
        Ext2Inode ein; read_inode(h->inode, &ein);
        out[n].size = ein.size;
        n++;
        off += rec;
    }
    return n;
}

// ===================== 权限 =====================
uint16_t Ext2::get_mode(const char* path) {
    uint32_t ino = namei(path);
    if (ino == 0) return 0;
    Ext2Inode in; read_inode(ino, &in);
    return in.mode;
}
bool Ext2::set_mode(const char* path, uint16_t mode) {
    uint32_t ino = namei(path);
    if (ino == 0) return false;
    Ext2Inode in; read_inode(ino, &in);
    in.mode = (in.mode & 0xF000) | (mode & 0x0FFF);
    return write_inode(ino, &in);
}

// ===================== 统计 =====================
uint32_t Ext2::free_blocks() { Ext2Super s; read_super(&s); return s.free_blocks_count; }
uint32_t Ext2::total_blocks() { return total_blocks_; }
uint32_t Ext2::free_inodes() { Ext2Super s; read_super(&s); return s.free_inodes_count; }

// 重命名:在父目录块里找到旧项,改 name_len 和名字字节。
bool Ext2::rename(const char* oldpath, const char* newname) {
    uint32_t parent; char leaf[64];
    if (!resolve_parent(oldpath, &parent, leaf, sizeof(leaf))) return false;
    if (dir_lookup(parent, newname) != 0) return false;
    uint32_t old_ino = dir_lookup(parent, leaf);
    if (old_ino == 0) return false;
    Ext2Inode din; read_inode(parent, &din);
    uint8_t b[1024];
    rd_block(din.i_block[0], b);
    uint16_t off = 0;
    while (off < 1024) {
        Ext2DirHeader* h = (Ext2DirHeader*)(b+off);
        if (h->inode == 0) break;
        if (h->inode == old_ino) {
            int nl = (int)nefu::strlen(newname);
            int entlen = 8 + nl; while (entlen % 4) entlen++;
            if (off + entlen > 1024) return false;
            h->name_len = (uint8_t)nl;
            for (int i = 0; i < nl; i++) b[off+8+i] = (uint8_t)newname[i];
            wr_block(din.i_block[0], b);
            return true;
        }
        off += h->rec_len;
    }
    return false;
}

int Ext2::count_entries(const char* path) {
    Ext2DirInfo en[32];
    return list_dir(path, en, 32);
}

// ===================== 自检 =====================
int ext2_self_test() {
    int fails = 0;
    Disk disk;
    // 4096 扇区 = 2048 块
    if (!disk.create(4096)) return 1;
    Ext2 fs;
    if (!fs.format(&disk, 0, 4096)) return 2;
    if (fs.total_blocks() != 2048) fails++;
    if (fs.free_blocks() < 100) fails++;

    if (!fs.create_file("/a.txt")) fails++;
    const char* msg = "ext2 hello";
    uint32_t mlen = (uint32_t)nefu::strlen(msg);
    if (!fs.write_file("/a.txt", (const uint8_t*)msg, mlen)) fails++;
    if (fs.file_size("/a.txt") != (long)mlen) fails++;
    uint8_t rb[64]; int r = fs.read_file("/a.txt", rb, sizeof(rb));
    if (r != (int)mlen) fails++;
    for (uint32_t i = 0; i < mlen; i++) if (rb[i] != msg[i]) { fails++; break; }

    if (!fs.mkdir("/home")) fails++;
    if (!fs.mkdir("/home/user")) fails++;
    if (!fs.write_file("/home/user/note.txt", (const uint8_t*)"hi", 2)) fails++;
    if (fs.file_size("/home/user/note.txt") != 2) fails++;

    // 列根目录
    Ext2DirInfo en[16];
    int n = fs.list_dir("/", en, 16);
    if (n != 2) fails++; // a.txt + home

    // 大文件,跨多块
    uint8_t big[5000];
    for (int i = 0; i < 5000; i++) big[i] = (uint8_t)(i & 0xFF);
    if (!fs.write_file("/big.bin", big, 5000)) fails++;
    uint8_t br[5000]; int brr = fs.read_file("/big.bin", br, sizeof(br));
    if (brr != 5000) fails++;
    for (int i = 0; i < 5000; i++) if (br[i] != big[i]) { fails++; break; }

    // 符号链接
    if (!fs.symlink("/a.txt", "/alink")) fails++;
    char lbuf[64];
    if (!fs.readlink("/alink", lbuf, sizeof(lbuf))) fails++;
    if (nefu::strcmp(lbuf, "/a.txt") != 0) fails++;

    // 权限
    uint16_t m = fs.get_mode("/a.txt");
    if ((m & 0777) != 0644) fails++;
    if (!fs.set_mode("/a.txt", 0600)) fails++;
    if ((fs.get_mode("/a.txt") & 0777) != 0600) fails++;

    // 删除
    uint32_t fb = fs.free_blocks();
    if (!fs.remove("/big.bin")) fails++;
    if (fs.free_blocks() <= fb) fails++; // 应释放块

    // 重挂载持久化
    Ext2 fs2;
    if (!fs2.mount(&disk, 0)) fails++;
    if (fs2.file_size("/home/user/note.txt") != 2) fails++;

    // 重命名
    if (!fs.write_file("/oldname.txt", (const uint8_t*)"rn", 2)) fails++;
    if (!fs.rename("/oldname.txt", "newname.txt")) fails++;
    if (fs.file_size("/oldname.txt") != -1) fails++;
    if (fs.file_size("/newname.txt") != 2) fails++;

    // count_entries:根目录应有 a.txt, home, big.bin(已删), alink, newname.txt
    int ce = fs.count_entries("/");
    if (ce < 3) fails++;

    // 多文件压力:连续创建 10 个小文件并逐一读回
    for (int i = 0; i < 10; i++) {
        char p[32]; p[0]='/';
        p[1]='f'; p[2]=(char)('0'+i); p[3]='.'; p[4]='t'; p[5]='x'; p[6]='t'; p[7]=0;
        char body[16]; int k=0;
        body[k++]=(char)('A'+i); body[k++]=0;
        if (!fs.write_file(p,(const uint8_t*)body,1)) { fails++; break; }
    }
    for (int i = 0; i < 10; i++) {
        char p[32]; p[0]='/';
        p[1]='f'; p[2]=(char)('0'+i); p[3]='.'; p[4]='t'; p[5]='x'; p[6]='t'; p[7]=0;
        if (fs.file_size(p) != 1) { fails++; break; }
    }

    // 嵌套目录:mkdir /a/b/c,写文件
    if (!fs.mkdir("/alpha")) fails++;
    if (!fs.mkdir("/alpha/beta")) fails++;
    if (!fs.write_file("/alpha/beta/gamma.txt",(const uint8_t*)"deep",4)) fails++;
    if (fs.file_size("/alpha/beta/gamma.txt") != 4) fails++;

    return fails;
}

} // namespace filesystem
} // namespace nefu
