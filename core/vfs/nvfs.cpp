// nefuOS NVFS - nefu Virtual File System (implementation)
//
// On-disk layout (single-partition image, 512-byte blocks, 4 MiB default):
//
//   block 0                         : Superblock (512 bytes)
//   blocks 1 .. 222                 : inode table (1024 inodes x 111 bytes)
//   blocks 223 .. 224               : block bitmap (8192 bits = 1024 bytes)
//   blocks 225 .. 352               : journal (redo log, 128 blocks = 64 KiB)
//   blocks 353 .. 8191              : user data blocks
//
// Journaling: every mutating operation is wrapped in a transaction
// (start -> write records -> commit). Each JOURNAL_WRITE record snapshots the
// *new* content of one block; recovery on mount replays every recorded block
// write (idempotent), then clears the journal. sync() applies + clears.
//
// System files are baked into the kernel (create_default_tree / ensure_* in
// vfs.cpp); the NVFS image only carries the user-visible tree that was saved.
//
// Endianness: host-native (x86 little-endian), format version 1.

#include "nvfs.h"
#include "../platform.h"

namespace nefu {
namespace nvfs {

// ---------------- layout constants ----------------
static const uint32_t BLOCK_SIZE        = 512;
static const uint32_t TOTAL_BLOCKS      = 8192;                 // 4 MiB image
static const uint32_t MAX_INODES        = 1024;
static const uint32_t INODE_TABLE_BLOCKS =
    (MAX_INODES * sizeof(Inode) + BLOCK_SIZE - 1) / BLOCK_SIZE; // 222
static const uint32_t BITMAP_START      = 1 + INODE_TABLE_BLOCKS;
static const uint32_t BITMAP_BLOCKS     = (TOTAL_BLOCKS + 7) / 8 / BLOCK_SIZE;
static const uint32_t JRN_START_BLOCK   = BITMAP_START + BITMAP_BLOCKS; // 225
static const uint32_t JOURNAL_BLOCKS    = 128;
static const uint32_t USER_DATA_START   = JRN_START_BLOCK + JOURNAL_BLOCKS; // 353
static const uint32_t USER_DATA_BLOCKS  = TOTAL_BLOCKS - USER_DATA_START;

// Journal record header on disk: packed JournalEntry (17 bytes) + 7 pad = 24.
static const uint32_t JREC_HDR          = 24;
static const uint32_t JREC_MAX          = JREC_HDR + BLOCK_SIZE; // 536
static const uint32_t DIR_ENTRY_SIZE    = 68;                    // sizeof(DirEntry)
static const uint32_t MAX_DIR_ENTRIES   = 512;                   // cap per dir

// ---------------- image state ----------------
NVFS g_nvfs;
static uint8_t*  s_image      = 0;
static uint32_t  s_image_size = 0;
static uint32_t  s_journal_seq = 0;     // transaction sequence number
static uint32_t  s_journal_pos = 0;     // next record offset inside journal region
static uint32_t  s_journal_writes = 0;  // pending (not yet applied) write records
static uint32_t  s_journal_depth = 0;   // nested transaction depth

static uint32_t now() { return platform_tick_ms() / 1000; }

static uint32_t u32_min(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint32_t u32_max(uint32_t a, uint32_t b) { return a > b ? a : b; }

static uint8_t* jptr() { return s_image + (size_t)JRN_START_BLOCK * BLOCK_SIZE; }

// FNV-1a 32-bit checksum over a journal payload.
static uint32_t fnv1a(const uint8_t* d, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h;
}

static Inode* inode_ptr(uint32_t ino) {
    if (ino == 0 || ino >= MAX_INODES) return 0;
    return (Inode*)(s_image + BLOCK_SIZE + (size_t)ino * sizeof(Inode));
}

// ---------------- block bitmap (raw, no journal) ----------------
static bool bitmap_get(uint32_t b) {
    if (b >= TOTAL_BLOCKS) return true;   // out of range = used
    uint32_t byte = b / 8;
    uint32_t bit  = b % 8;
    return (s_image[(size_t)BITMAP_START * BLOCK_SIZE + byte] >> bit) & 1;
}
static void bitmap_set(uint32_t b) {
    if (b >= TOTAL_BLOCKS) return;
    uint32_t byte = b / 8, bit = b % 8;
    s_image[(size_t)BITMAP_START * BLOCK_SIZE + byte] |= (uint8_t)(1u << bit);
}
static void bitmap_clear(uint32_t b) {
    if (b >= TOTAL_BLOCKS) return;
    uint32_t byte = b / 8, bit = b % 8;
    s_image[(size_t)BITMAP_START * BLOCK_SIZE + byte] &= (uint8_t)~(1u << bit);
}
static uint32_t bitmap_block_of(uint32_t b) {
    return BITMAP_START + (b / 8) / BLOCK_SIZE;
}

// ---------------- journal ----------------
// Write one raw record (header + optional payload) at s_journal_pos.
static bool jrec_append(uint8_t type, uint32_t block, const uint8_t* payload, uint32_t size) {
    if (!s_image) return false;
    if (s_journal_pos + JREC_HDR + size > JOURNAL_BLOCKS * BLOCK_SIZE) return false;
    JournalEntry e;
    e.seq = s_journal_seq;
    e.type = (uint8_t)type;
    e.block = block;
    e.size = size;
    e.checksum = (type == JOURNAL_WRITE && payload) ? fnv1a(payload, size) : 0;
    uint8_t* dst = jptr() + s_journal_pos;
    memcpy(dst, &e, sizeof(JournalEntry));            // 17 bytes
    memset(dst + sizeof(JournalEntry), 0, JREC_HDR - sizeof(JournalEntry));
    if (size > 0 && payload) memcpy(dst + JREC_HDR, payload, size);
    s_journal_pos += JREC_HDR + size;
    return true;
}

// Roll the journal forward: replay every pending record into the image, then
// clear the region. Idempotent - safe to call at any point.
static void replay_and_clear() {
    if (!s_image) return;
    uint8_t* j = jptr();
    uint32_t pos = 0;
    const uint32_t jsz = JOURNAL_BLOCKS * BLOCK_SIZE;
    while (pos + sizeof(JournalEntry) <= jsz) {
        JournalEntry e;
        memcpy(&e, j + pos, sizeof(JournalEntry));
        if (e.seq == 0) break;                        // clean region
        if (e.type == JOURNAL_WRITE && e.size > 0 && e.size <= BLOCK_SIZE &&
            e.block < TOTAL_BLOCKS && pos + JREC_HDR + e.size <= jsz) {
            if (e.checksum == fnv1a(j + pos + JREC_HDR, e.size)) {
                memcpy(s_image + (size_t)e.block * BLOCK_SIZE, j + pos + JREC_HDR, e.size);
            } else {
                break;                                // torn record - stop replay
            }
        } else if (e.type != JOURNAL_START && e.type != JOURNAL_COMMIT &&
                   e.type != JOURNAL_WRITE) {
            break;                                    // unknown type - stop
        }
        pos += JREC_HDR + e.size;
    }
    memset(j, 0, jsz);
    s_journal_pos = 0;
    s_journal_writes = 0;
}

void journal_start() {
    if (!is_mounted()) return;
    if (s_journal_depth == 0) {
        if (s_journal_pos + JREC_MAX > JOURNAL_BLOCKS * BLOCK_SIZE) replay_and_clear();
        jrec_append(JOURNAL_START, 0, 0, 0);
    }
    s_journal_depth++;
}

void journal_write_block(uint32_t block, const void* data, uint32_t size) {
    if (!is_mounted() || !data) return;
    if (size == 0 || size > BLOCK_SIZE || block >= TOTAL_BLOCKS) return;
    if (s_journal_pos + JREC_MAX > JOURNAL_BLOCKS * BLOCK_SIZE) replay_and_clear();
    if (jrec_append(JOURNAL_WRITE, block, (const uint8_t*)data, size)) s_journal_writes++;
}

void journal_commit() {
    if (!is_mounted() || s_journal_depth == 0) return;
    s_journal_depth--;
    if (s_journal_depth == 0) {
        if (s_journal_pos + JREC_HDR > JOURNAL_BLOCKS * BLOCK_SIZE) replay_and_clear();
        jrec_append(JOURNAL_COMMIT, 0, 0, 0);
    }
}

void sync() {
    if (!s_image) return;
    replay_and_clear();
}

uint32_t journal_pending() { return s_journal_writes; }

// ---------------- block allocation (journaled; call inside a transaction) ----------------
static int alloc_block() {
    for (uint32_t b = USER_DATA_START; b < TOTAL_BLOCKS; b++) {
        if (!bitmap_get(b)) {
            bitmap_set(b);
            journal_write_block(bitmap_block_of(b), s_image + (size_t)bitmap_block_of(b) * BLOCK_SIZE, BLOCK_SIZE);
            if (g_nvfs.sb.free_blocks > 0) g_nvfs.sb.free_blocks--;
            journal_write_block(0, s_image, BLOCK_SIZE);
            return (int)b;
        }
    }
    return -1;
}
static int alloc_block_raw() {   // format-time only (no journal)
    for (uint32_t b = USER_DATA_START; b < TOTAL_BLOCKS; b++) {
        if (!bitmap_get(b)) { bitmap_set(b); return (int)b; }
    }
    return -1;
}
static void free_block(uint32_t b) {
    if (b < USER_DATA_START || b >= TOTAL_BLOCKS) return;
    bitmap_clear(b);
    journal_write_block(bitmap_block_of(b), s_image + (size_t)bitmap_block_of(b) * BLOCK_SIZE, BLOCK_SIZE);
    g_nvfs.sb.free_blocks++;
    journal_write_block(0, s_image, BLOCK_SIZE);
}

// journal a whole block's current content (redo snapshot after mutation)
static void jrn_block(uint32_t b) {
    if (b < TOTAL_BLOCKS) journal_write_block(b, s_image + (size_t)b * BLOCK_SIZE, BLOCK_SIZE);
}
static void jrn_inode(Inode* in) {
    if (in && in->inode_num < MAX_INODES) {
        jrn_block(1 + (in->inode_num * sizeof(Inode)) / BLOCK_SIZE);
    }
}

// ---------------- inode allocation ----------------
static int alloc_inode(uint8_t type, uint16_t mode) {
    for (uint32_t ino = 2; ino < MAX_INODES; ino++) {
        Inode* in = inode_ptr(ino);
        if (!in) continue;
        if (in->type == 0) {
            memset(in, 0, sizeof(Inode));
            in->inode_num = ino;
            in->type = type;
            in->mode = mode;
            in->ctime = in->mtime = in->atime = now();
            jrn_inode(in);
            return (int)ino;
        }
    }
    return -1;
}
static void free_inode(uint32_t ino) {
    Inode* in = inode_ptr(ino);
    if (!in) return;
    in->type = 0;
    in->size = 0;
    jrn_inode(in);
}

// ---------------- block mapping (direct 12 + single + double indirect) ----------------
static int bmap_lookup(Inode* in, uint32_t i) {
    if (!in) return -1;
    if (i < 12) return in->blocks[i] ? (int)in->blocks[i] : -1;
    uint32_t idx = i - 12;
    if (idx < 128) {
        if (!in->indirect) return -1;
        uint32_t* tbl = (uint32_t*)(s_image + (size_t)in->indirect * BLOCK_SIZE);
        return tbl[idx] ? (int)tbl[idx] : -1;
    }
    uint32_t idx2 = idx - 128;
    uint32_t i1 = idx2 / 128, i2 = idx2 % 128;
    if (!in->dbl_indirect) return -1;
    uint32_t* tbl1 = (uint32_t*)(s_image + (size_t)in->dbl_indirect * BLOCK_SIZE);
    if (!tbl1[i1]) return -1;
    uint32_t* tbl2 = (uint32_t*)(s_image + (size_t)tbl1[i1] * BLOCK_SIZE);
    return tbl2[i2] ? (int)tbl2[i2] : -1;
}

// Ensure the block for logical index i exists (allocate as needed). New data
// blocks are zeroed so sparse files read as zeros. Journals every change.
static int bmap_alloc(Inode* in, uint32_t i) {
    if (!in) return -1;
    if (i < 12) {
        if (!in->blocks[i]) {
            int b = alloc_block();
            if (b < 0) return -1;
            memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
            jrn_block((uint32_t)b);
            in->blocks[i] = (uint32_t)b;
            jrn_inode(in);
        }
        return (int)in->blocks[i];
    }
    uint32_t idx = i - 12;
    if (idx < 128) {
        if (!in->indirect) {
            int b = alloc_block();
            if (b < 0) return -1;
            memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
            jrn_block((uint32_t)b);
            in->indirect = (uint32_t)b;
            jrn_inode(in);
        }
        uint32_t* tbl = (uint32_t*)(s_image + (size_t)in->indirect * BLOCK_SIZE);
        if (!tbl[idx]) {
            int b = alloc_block();
            if (b < 0) return -1;
            memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
            jrn_block((uint32_t)b);
            tbl[idx] = (uint32_t)b;
            jrn_block(in->indirect);
        }
        return (int)tbl[idx];
    }
    uint32_t idx2 = idx - 128;
    uint32_t i1 = idx2 / 128, i2 = idx2 % 128;
    if (!in->dbl_indirect) {
        int b = alloc_block();
        if (b < 0) return -1;
        memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
        jrn_block((uint32_t)b);
        in->dbl_indirect = (uint32_t)b;
        jrn_inode(in);
    }
    uint32_t* tbl1 = (uint32_t*)(s_image + (size_t)in->dbl_indirect * BLOCK_SIZE);
    if (!tbl1[i1]) {
        int b = alloc_block();
        if (b < 0) return -1;
        memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
        jrn_block((uint32_t)b);
        tbl1[i1] = (uint32_t)b;
        jrn_block(in->dbl_indirect);
    }
    uint32_t* tbl2 = (uint32_t*)(s_image + (size_t)tbl1[i1] * BLOCK_SIZE);
    if (!tbl2[i2]) {
        int b = alloc_block();
        if (b < 0) return -1;
        memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
        jrn_block((uint32_t)b);
        tbl2[i2] = (uint32_t)b;
        jrn_block(tbl1[i1]);
    }
    return (int)tbl2[i2];
}

// Free every data block of an inode (direct + indirect + double indirect).
static void bmap_free_all(Inode* in) {
    if (!in) return;
    for (uint32_t i = 0; i < 12; i++) {
        if (in->blocks[i]) { free_block(in->blocks[i]); in->blocks[i] = 0; }
    }
    if (in->indirect) {
        uint32_t* tbl = (uint32_t*)(s_image + (size_t)in->indirect * BLOCK_SIZE);
        for (uint32_t i = 0; i < 128; i++) if (tbl[i]) free_block(tbl[i]);
        free_block(in->indirect);
        in->indirect = 0;
    }
    if (in->dbl_indirect) {
        uint32_t* tbl1 = (uint32_t*)(s_image + (size_t)in->dbl_indirect * BLOCK_SIZE);
        for (uint32_t i = 0; i < 128; i++) {
            if (tbl1[i]) {
                uint32_t* tbl2 = (uint32_t*)(s_image + (size_t)tbl1[i] * BLOCK_SIZE);
                for (uint32_t j = 0; j < 128; j++) if (tbl2[j]) free_block(tbl2[j]);
                free_block(tbl1[i]);
            }
        }
        free_block(in->dbl_indirect);
        in->dbl_indirect = 0;
    }
    jrn_inode(in);
}

// ---------------- byte-range helpers over an inode ----------------
static int read_bytes_at(Inode* in, uint32_t off, uint8_t* dst, uint32_t n) {
    if (!in || off >= in->size) return 0;
    n = u32_min(n, in->size - off);
    uint32_t done = 0;
    while (done < n) {
        uint32_t blk_idx = (off + done) / BLOCK_SIZE;
        uint32_t in_blk  = (off + done) % BLOCK_SIZE;
        int phys = bmap_lookup(in, blk_idx);
        if (phys < 0) break;
        uint32_t chunk = u32_min(n - done, BLOCK_SIZE - in_blk);
        memcpy(dst + done, s_image + (size_t)phys * BLOCK_SIZE + in_blk, chunk);
        done += chunk;
    }
    return (int)done;
}

static int write_bytes_at(Inode* in, uint32_t off, const uint8_t* src, uint32_t n) {
    if (!in || n == 0) return -1;
    while (n > 0) {
        uint32_t blk_idx = off / BLOCK_SIZE;
        uint32_t in_blk  = off % BLOCK_SIZE;
        int phys = bmap_alloc(in, blk_idx);
        if (phys < 0) return -1;
        uint32_t chunk = u32_min(n, BLOCK_SIZE - in_blk);
        memcpy(s_image + (size_t)phys * BLOCK_SIZE + in_blk, src, chunk);
        jrn_block((uint32_t)phys);
        src += chunk; off += chunk; n -= chunk;
    }
    return 0;
}

// ---------------- directories ----------------
struct DirBuf {
    DirEntry entries[MAX_DIR_ENTRIES];
    int count;
};

static int load_dir(uint32_t ino, DirBuf& db) {
    db.count = 0;
    Inode* in = inode_ptr(ino);
    if (!in || in->type != INODE_DIR) return -1;
    uint8_t* tmp = (uint8_t*)kalloc(u32_max(in->size, DIR_ENTRY_SIZE));
    if (!tmp) return -1;
    int got = read_bytes_at(in, 0, tmp, in->size);
    int n = (int)(got / DIR_ENTRY_SIZE);
    int count = 0;
    for (int i = 0; i < n; i++) {
        DirEntry e;
        memcpy(&e, tmp + (size_t)i * DIR_ENTRY_SIZE, DIR_ENTRY_SIZE);
        if (e.inode == 0) break;                 // terminator
        if (count >= (int)MAX_DIR_ENTRIES) break;
        db.entries[count] = e;
        if (e.name_len > 59) db.entries[count].name_len = 59;
        db.entries[count].name[db.entries[count].name_len] = 0;
        count++;
    }
    db.count = count;
    kfree(tmp);
    return count;
}

static int store_dir(uint32_t ino, const DirBuf& db) {
    Inode* in = inode_ptr(ino);
    if (!in || in->type != INODE_DIR) return -1;
    uint32_t need = ((uint32_t)db.count + 1) * DIR_ENTRY_SIZE;
    uint8_t* tmp = (uint8_t*)kalloc(need);
    if (!tmp) return -1;
    memset(tmp, 0, need);
    for (int i = 0; i < db.count; i++) {
        DirEntry e;
        memset(&e, 0, sizeof(e));
        e.inode = db.entries[i].inode;
        e.rec_len = DIR_ENTRY_SIZE;
        e.type = db.entries[i].type;
        uint32_t nl = (uint32_t)strlen(db.entries[i].name);
        if (nl > 59) nl = 59;
        e.name_len = (uint8_t)nl;
        memcpy(e.name, db.entries[i].name, nl);
        memcpy(tmp + (size_t)i * DIR_ENTRY_SIZE, &e, DIR_ENTRY_SIZE);
    }
    // last entry is the terminator (all zeros)
    write_bytes_at(in, 0, tmp, need);
    kfree(tmp);
    in->size = need;
    in->mtime = now();
    jrn_inode(in);
    return db.count;
}

static int dir_find(uint32_t ino, const char* name) {
    DirBuf db;
    if (load_dir(ino, db) < 0) return -1;
    for (int i = 0; i < db.count; i++) {
        if (strcmp(db.entries[i].name, name) == 0) return (int)db.entries[i].inode;
    }
    return -1;
}

static int dir_add(uint32_t ino, const char* name, uint32_t child_ino, uint8_t type) {
    DirBuf db;
    if (load_dir(ino, db) < 0) return -1;
    for (int i = 0; i < db.count; i++)
        if (strcmp(db.entries[i].name, name) == 0) return -1;
    if (db.count >= (int)MAX_DIR_ENTRIES) return -1;
    DirEntry& e = db.entries[db.count];
    memset(&e, 0, sizeof(e));
    e.inode = child_ino;
    e.rec_len = DIR_ENTRY_SIZE;
    e.type = type;
    uint32_t nl = (uint32_t)strlen(name);
    if (nl > 59) nl = 59;
    e.name_len = (uint8_t)nl;
    memcpy(e.name, name, nl);
    db.count++;
    return store_dir(ino, db);
}

static int dir_remove(uint32_t ino, const char* name) {
    DirBuf db;
    if (load_dir(ino, db) < 0) return -1;
    int found = -1;
    for (int i = 0; i < db.count; i++) {
        if (strcmp(db.entries[i].name, name) == 0) { found = i; break; }
    }
    if (found < 0) return -1;
    for (int i = found; i < db.count - 1; i++) db.entries[i] = db.entries[i + 1];
    db.count--;
    return store_dir(ino, db);
}

// ---------------- path helpers ----------------
static void split_parent(const char* path, char* parent, int ps, char* base, int bs) {
    const char* slash = strrchr(path, '/');
    if (!slash) {
        if (ps > 0) parent[0] = 0;
        strncpy(base, path, bs - 1);
        base[bs - 1] = 0;
        return;
    }
    int pl = (int)(slash - path);
    if (pl == 0) {
        if (ps > 1) { strcpy(parent, "/"); }
        else if (ps > 0) parent[0] = 0;
    } else {
        if (pl >= ps) pl = ps - 1;
        memcpy(parent, path, (size_t)pl);
        parent[pl] = 0;
    }
    strncpy(base, slash + 1, bs - 1);
    base[bs - 1] = 0;
}

// ---------------- public API ----------------
bool load_image(uint8_t* data, uint32_t size) {
    if (s_image) { kfree(s_image); s_image = 0; s_image_size = 0; }
    g_nvfs.mounted = false;
    memset(&g_nvfs.sb, 0, sizeof(Superblock));
    s_journal_seq = 0; s_journal_pos = 0; s_journal_writes = 0; s_journal_depth = 0;
    if (!data || size < BLOCK_SIZE) return false;
    s_image = data;
    s_image_size = size;
    return true;
}

bool image(uint8_t** out, uint32_t* out_size) {
    if (!out || !out_size || !s_image) return false;
    *out = s_image;
    *out_size = s_image_size;
    return true;
}

bool is_mounted() { return g_nvfs.mounted && s_image != 0; }

// Format a fresh NVFS image (reuses the current buffer when present).
static void format_image() {
    Superblock& sb = g_nvfs.sb;
    memset(&sb, 0, sizeof(Superblock));
    sb.magic = NVFS_MAGIC;
    sb.version = NVFS_VERSION;
    sb.block_size = BLOCK_SIZE;
    sb.total_blocks = TOTAL_BLOCKS;
    sb.free_blocks = USER_DATA_BLOCKS;
    sb.root_inode = 2;
    sb.journal_start = JOURNAL_START;
    sb.journal_blocks = JOURNAL_BLOCKS;
    sb.user_data_start = USER_DATA_START;
    sb.user_data_blocks = USER_DATA_BLOCKS;
    strncpy(sb.volume_name, "nefuOS", 31);
    sb.volume_name[31] = 0;
    // inode table + bitmap + journal start clean
    memset(s_image + BLOCK_SIZE, 0,
           (size_t)(INODE_TABLE_BLOCKS + BITMAP_BLOCKS + JOURNAL_BLOCKS) * BLOCK_SIZE);
    // metadata region is always used
    for (uint32_t b = 0; b < USER_DATA_START; b++) bitmap_set(b);
    // root directory inode (inode 2), empty dir with one terminator block
    Inode* root = inode_ptr(sb.root_inode);
    memset(root, 0, sizeof(Inode));
    root->inode_num = sb.root_inode;
    root->type = INODE_DIR;
    root->mode = 0755;
    root->ctime = root->mtime = root->atime = now();
    int b = alloc_block_raw();
    if (b >= 0) {
        memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
        root->blocks[0] = (uint32_t)b;
        root->size = DIR_ENTRY_SIZE;   // one zeroed terminator entry
        sb.free_blocks--;              // the root dir block is now used
    }
    memcpy(s_image, &sb, sizeof(Superblock));
    s_journal_seq = 0; s_journal_pos = 0; s_journal_writes = 0; s_journal_depth = 0;
}

bool init() {
    if (s_image) { kfree(s_image); s_image = 0; s_image_size = 0; }
    g_nvfs.mounted = false;
    s_image = (uint8_t*)kalloc((size_t)TOTAL_BLOCKS * BLOCK_SIZE);
    if (!s_image) return false;
    s_image_size = (uint32_t)TOTAL_BLOCKS * BLOCK_SIZE;
    memset(s_image, 0, s_image_size);
    format_image();
    g_nvfs.mounted = true;
    g_nvfs.mount_point_inode = g_nvfs.sb.root_inode;
    return true;
}

bool mount(uint32_t partition_lba) {
    (void)partition_lba;
    if (!s_image) return false;
    if (s_image_size < (uint32_t)USER_DATA_START * BLOCK_SIZE) return false;
    Superblock sb;
    memcpy(&sb, s_image, sizeof(Superblock));
    if (sb.magic != NVFS_MAGIC) return false;
    if (sb.version != NVFS_VERSION) return false;
    g_nvfs.sb = sb;
    // crash recovery: replay the journal left by an unclean shutdown
    replay_and_clear();
    g_nvfs.mounted = true;
    g_nvfs.mount_point_inode = g_nvfs.sb.root_inode;
    return true;
}

void unmount() {
    if (s_image) { kfree(s_image); s_image = 0; s_image_size = 0; }
    g_nvfs.mounted = false;
    memset(&g_nvfs.sb, 0, sizeof(Superblock));
    s_journal_seq = 0; s_journal_pos = 0; s_journal_writes = 0; s_journal_depth = 0;
}

void recover() {
    replay_and_clear();
}

int lookup(const char* path) {
    if (!is_mounted() || !path) return -1;
    uint32_t cur = g_nvfs.sb.root_inode;
    const char* p = path;
    while (*p == '/') p++;
    while (*p) {
        char comp[64];
        int n = 0;
        while (*p && *p != '/') {
            if (n < 63) comp[n++] = *p;
            p++;
        }
        comp[n] = 0;
        if (n == 0) break;
        Inode* in = inode_ptr(cur);
        if (!in || in->type != INODE_DIR) return -1;
        int next = dir_find(cur, comp);
        if (next < 0) return -1;
        cur = (uint32_t)next;
        while (*p == '/') p++;
    }
    return (int)cur;
}

int create_file(const char* path, int mode) {
    if (!is_mounted() || !path || !*path) return -1;
    char parent[128], name[64];
    split_parent(path, parent, sizeof(parent), name, sizeof(name));
    if (!name[0]) return -1;
    int pino = lookup(parent[0] ? parent : "/");
    if (pino < 0) return -1;
    Inode* pin = inode_ptr((uint32_t)pino);
    if (!pin || pin->type != INODE_DIR) return -1;
    if (dir_find((uint32_t)pino, name) >= 0) return -1;
    journal_start();
    int ino = alloc_inode(INODE_FILE, (uint16_t)mode);
    if (ino < 0) { journal_commit(); return -1; }
    if (dir_add((uint32_t)pino, name, (uint32_t)ino, INODE_FILE) < 0) {
        free_inode((uint32_t)ino);
        journal_commit();
        return -1;
    }
    journal_commit();
    return ino;
}

int mkdir(const char* path, int mode) {
    if (!is_mounted() || !path || !*path) return -1;
    char parent[128], name[64];
    split_parent(path, parent, sizeof(parent), name, sizeof(name));
    if (!name[0]) return -1;
    int pino = lookup(parent[0] ? parent : "/");
    if (pino < 0) return -1;
    Inode* pin = inode_ptr((uint32_t)pino);
    if (!pin || pin->type != INODE_DIR) return -1;
    if (dir_find((uint32_t)pino, name) >= 0) return -1;
    journal_start();
    int ino = alloc_inode(INODE_DIR, (uint16_t)mode);
    if (ino < 0) { journal_commit(); return -1; }
    // empty directory: one zeroed terminator block
    Inode* nin = inode_ptr((uint32_t)ino);
    int b = alloc_block();
    if (b >= 0) {
        memset(s_image + (size_t)b * BLOCK_SIZE, 0, BLOCK_SIZE);
        jrn_block((uint32_t)b);
        nin->blocks[0] = (uint32_t)b;
        nin->size = DIR_ENTRY_SIZE;
        jrn_inode(nin);
    }
    if (dir_add((uint32_t)pino, name, (uint32_t)ino, INODE_DIR) < 0) {
        free_inode((uint32_t)ino);
        journal_commit();
        return -1;
    }
    journal_commit();
    return ino;
}

int unlink(const char* path) {
    if (!is_mounted() || !path || !*path) return -1;
    int ino = lookup(path);
    if (ino < 0) return -1;
    Inode* in = inode_ptr((uint32_t)ino);
    if (!in || in->type == INODE_DIR) return -1;
    char parent[128], name[64];
    split_parent(path, parent, sizeof(parent), name, sizeof(name));
    int pino = lookup(parent[0] ? parent : "/");
    if (pino < 0) return -1;
    journal_start();
    bmap_free_all(in);
    free_inode((uint32_t)ino);
    dir_remove((uint32_t)pino, name);
    journal_commit();
    return 0;
}

int rmdir(const char* path) {
    if (!is_mounted() || !path || !*path) return -1;
    int ino = lookup(path);
    if (ino < 0) return -1;
    if ((uint32_t)ino == g_nvfs.sb.root_inode) return -1;
    Inode* in = inode_ptr((uint32_t)ino);
    if (!in || in->type != INODE_DIR) return -1;
    DirBuf db;
    if (load_dir((uint32_t)ino, db) < 0) return -1;
    if (db.count != 0) return -1;              // not empty
    char parent[128], name[64];
    split_parent(path, parent, sizeof(parent), name, sizeof(name));
    int pino = lookup(parent[0] ? parent : "/");
    if (pino < 0) return -1;
    journal_start();
    bmap_free_all(in);
    free_inode((uint32_t)ino);
    dir_remove((uint32_t)pino, name);
    journal_commit();
    return 0;
}

int read_file(int inode_num, uint8_t* buf, uint32_t offset, uint32_t size) {
    if (!is_mounted() || !buf) return -1;
    Inode* in = inode_ptr((uint32_t)inode_num);
    if (!in || in->type == INODE_DIR) return -1;
    return read_bytes_at(in, offset, buf, size);
}

int write_file(int inode_num, const uint8_t* buf, uint32_t offset, uint32_t size) {
    if (!is_mounted() || !buf || size == 0) return -1;
    Inode* in = inode_ptr((uint32_t)inode_num);
    if (!in || in->type == INODE_DIR) return -1;
    journal_start();
    if (write_bytes_at(in, offset, buf, size) < 0) { journal_commit(); return -1; }
    in->size = u32_max(in->size, offset + size);
    in->mtime = now();
    jrn_inode(in);
    journal_commit();
    return (int)size;
}

int read_dir(int inode_num, DirEntry* entries, int max_entries) {
    if (!is_mounted() || !entries || max_entries <= 0) return -1;
    Inode* in = inode_ptr((uint32_t)inode_num);
    if (!in || in->type != INODE_DIR) return -1;
    DirBuf db;
    if (load_dir((uint32_t)inode_num, db) < 0) return -1;
    int n = u32_min((uint32_t)db.count, (uint32_t)max_entries);
    for (int i = 0; i < n; i++) entries[i] = db.entries[i];
    return n;
}

void vfs_to_nvfs() {
    if (!is_mounted() || !g_vfs) return;
    // Rebuild the image from the current VFS tree (fresh format keeps the
    // on-disk state exactly in sync with the in-memory tree).
    format_image();
    journal_start();
    // recursive helper: create dirs first (DFS pre-order), then files
    struct Rebuild {
        static void go(FSNode* n, const char* prefix) {
            for (int i = 0; i < n->children.size(); i++) {
                FSNode* c = n->children[i];
                String full = prefix;
                if (prefix[0]) full += "/";
                full += c->name.c_str();
                if (c->is_dir) {
                    if (mkdir(full.c_str(), 0755) >= 0) go(c, full.c_str());
                } else {
                    int ino = create_file(full.c_str(), 0644);
                    if (ino >= 0) {
                        if (c->size > 0 && c->data) write_file(ino, c->data, 0, c->size);
                        Inode* in = inode_ptr((uint32_t)ino);
                        if (in) {
                            in->mtime = c->mtime;
                            jrn_inode(in);
                        }
                    }
                }
            }
        }
    };
    FSNode* r = g_vfs->root();
    Rebuild::go(r, "");
    journal_commit();
    replay_and_clear();
}

void nvfs_to_vfs() {
    if (!is_mounted() || !g_vfs) return;
    struct Rebuild {
        static void go(int ino, const char* prefix) {
            Inode* in = inode_ptr((uint32_t)ino);
            if (!in) return;
            if (in->type == INODE_DIR) {
                DirBuf db;
                if (load_dir((uint32_t)ino, db) < 0) return;
                for (int i = 0; i < db.count; i++) {
                    DirEntry& e = db.entries[i];
                    String full = prefix;
                    if (prefix[0]) full += "/";
                    full += e.name;
                    Inode* cin = inode_ptr(e.inode);
                    if (!cin) continue;
                    if (e.type == INODE_DIR) {
                        if (g_vfs->mkdir(full.c_str())) go(e.inode, full.c_str());
                    } else {
                        FSNode* f = g_vfs->create_file(full.c_str());
                        if (f) {
                            f->size = cin->size;
                            f->mtime = cin->mtime;
                            if (cin->size > 0) {
                                f->data = (uint8_t*)kalloc(cin->size);
                                if (f->data) read_file((int)e.inode, f->data, 0, cin->size);
                            }
                        }
                    }
                }
            }
        }
    };
    Rebuild::go(g_nvfs.sb.root_inode, "");
}

uint64_t free_space() {
    if (!is_mounted()) return 0;
    return (uint64_t)g_nvfs.sb.free_blocks * g_nvfs.sb.block_size;
}

uint64_t total_space() {
    if (!is_mounted()) return 0;
    return (uint64_t)g_nvfs.sb.total_blocks * g_nvfs.sb.block_size;
}

int used_inode_count() {
    if (!is_mounted()) return 0;
    int c = 0;
    for (uint32_t ino = 2; ino < MAX_INODES; ino++) {
        Inode* in = inode_ptr(ino);
        if (in && in->type != 0) c++;
    }
    return c;
}

} // namespace nvfs
} // namespace nefu
