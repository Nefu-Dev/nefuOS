// nefuOS NVFS - nefu Virtual File System
// - Journaling filesystem
// - System files baked into kernel
// - User files on disk
// - No hidden files

#pragma once

#include "../klib/klib.h"
#include "vfs.h"

namespace nefu {
namespace nvfs {

// NVFS magic
const uint32_t NVFS_MAGIC = 0x4E564653;  // "NVFS"
const uint32_t NVFS_VERSION = 1;

// Superblock (first sector of NVFS partition)
struct Superblock {
    uint32_t magic;             // NVFS_MAGIC
    uint32_t version;           // NVFS_VERSION
    uint32_t block_size;        // Block size (bytes)
    uint32_t total_blocks;      // Total blocks
    uint32_t free_blocks;       // Free blocks
    uint32_t root_inode;        // Root inode number
    uint32_t journal_start;     // Journal start block
    uint32_t journal_blocks;    // Journal size
    uint32_t user_data_start;   // User data start block
    uint32_t user_data_blocks;  // User data size
    char volume_name[32];       // Volume name
    uint8_t  reserved[468];     // Reserved (pad to 512 bytes)
} __attribute__((packed));

// Inode types
enum InodeType {
    INODE_FILE = 1,
    INODE_DIR = 2,
    INODE_SYMLINK = 3,
};

// Inode (on disk)
struct Inode {
    uint32_t inode_num;         // Inode number
    uint8_t  type;              // InodeType
    uint16_t mode;              // Permissions
    uint32_t size;              // File size
    uint32_t blocks[12];        // Direct blocks
    uint32_t indirect;          // Indirect block
    uint32_t dbl_indirect;      // Double indirect
    uint32_t ctime;             // Creation time
    uint32_t mtime;             // Modification time
    uint32_t atime;             // Access time
    uint32_t uid;               // User ID
    uint32_t gid;               // Group ID
    uint8_t  reserved[24];      // Reserved
} __attribute__((packed));

// Directory entry
struct DirEntry {
    uint32_t inode;             // Inode number
    uint16_t rec_len;           // Record length
    uint8_t  name_len;          // Name length
    uint8_t  type;              // File type
    char name[60];              // Name (flexible)
} __attribute__((packed));

// Journal entry types
enum JournalType {
    JOURNAL_START = 1,
    JOURNAL_COMMIT = 2,
    JOURNAL_WRITE = 3,
    JOURNAL_DELETE = 4,
    JOURNAL_MKDIR = 5,
    JOURNAL_RMDIR = 6,
};

// Journal entry
struct JournalEntry {
    uint32_t seq;               // Sequence number
    uint8_t  type;              // JournalType
    uint32_t block;             // Block number
    uint32_t size;              // Data size
    uint32_t checksum;          // Checksum
} __attribute__((packed));

// NVFS instance
struct NVFS {
    Superblock sb;
    bool mounted;
    uint32_t mount_point_inode;
};

// Global NVFS
extern NVFS g_nvfs;

// Initialize NVFS
bool init();

// Mount NVFS partition
bool mount(uint32_t partition_lba);

// Unmount
void unmount();

// Create file
int create_file(const char* path, int mode);

// Create directory
int mkdir(const char* path, int mode);

// Read file
int read_file(int inode_num, uint8_t* buf, uint32_t offset, uint32_t size);

// Write file
int write_file(int inode_num, const uint8_t* buf, uint32_t offset, uint32_t size);

// Delete file
int unlink(const char* path);

// Remove directory
int rmdir(const char* path);

// Read directory
int read_dir(int inode_num, DirEntry* entries, int max_entries);

// Lookup path
int lookup(const char* path);

// Journal operations
void journal_start();
void journal_commit();
void journal_write_block(uint32_t block, const void* data, uint32_t size);

// Recovery (replay journal)
void recover();

// Convert VFS node to NVFS inode
void vfs_to_nvfs();
void nvfs_to_vfs();

// Get free space
uint64_t free_space();

// Get total space
uint64_t total_space();

// ---- integration helpers (host boot / shutdown wiring) ----
// Adopt a raw NVFS image buffer (ownership transferred to NVFS; the buffer
// is freed by unmount() or by a subsequent load_image()/init()).
bool load_image(uint8_t* data, uint32_t size);

// Hand back the current image buffer for platform_fs_save().
bool image(uint8_t** out, uint32_t* out_size);

// Whether an NVFS image is currently mounted.
bool is_mounted();

// Flush the journal: apply every recorded block write to the image and
// clear the journal region. Safe to call at any time.
void sync();

// Number of un-applied (pending) journal records.
uint32_t journal_pending();

// Number of in-use inodes in the mounted image.
int used_inode_count();

} // namespace nvfs
} // namespace nefu
