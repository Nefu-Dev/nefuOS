// nefuOS FAT32 Driver - Minimal read-only support
// Based on FAT32 specification (Microsoft)
// Supports: BPB parsing, root directory, file reading, cluster chain traversal

#pragma once

#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include <cstring>

namespace nefu {
namespace fat32 {

// FAT32 BIOS Parameter Block (first sector)
struct BPB {
    uint8_t  jump[3];          // 0x00: Jump instruction
    uint8_t  oem_name[8];      // 0x03: OEM name
    uint16_t bytes_per_sec;    // 0x0B: Bytes per sector (512/1024/2048/4096)
    uint8_t  sec_per_clus;     // 0x0D: Sectors per cluster
    uint16_t rsvd_sec_cnt;     // 0x0E: Reserved sector count
    uint8_t  num_fats;         // 0x10: Number of FATs
    uint16_t root_ent_cnt;     // 0x11: Root entries (0 for FAT32)
    uint16_t tot_sec16;        // 0x13: Total sectors 16-bit
    uint8_t  media;            // 0x15: Media descriptor
    uint16_t fsz_fat16;        // 0x16: FAT size 16-bit
    uint16_t sec_per_trk;      // 0x18: Sectors per track
    uint16_t num_heads;        // 0x1A: Number of heads
    uint32_t hidd_sec;         // 0x1C: Hidden sectors
    uint32_t tot_sec32;        // 0x20: Total sectors 32-bit

    // FAT32 Extended BPB
    uint32_t fsz_fat32;        // 0x24: FAT size 32-bit (sectors)
    uint16_t ext_flags;        // 0x28: Extended flags
    uint16_t fs_ver;           // 0x2A: Filesystem version
    uint32_t root_clus;        // 0x2C: Root directory cluster
    uint16_t fs_info;           // 0x30: FSInfo sector
    uint16_t bk_boot_sec;      // 0x32: Backup boot sector
    uint8_t  reserved[12];     // 0x34: Reserved
    uint8_t  drv_num;           // 0x40: Drive number
    uint8_t  reserved1;        // 0x41: Reserved
    uint8_t  boot_sig;         // 0x42: Boot signature (0x29)
    uint32_t vol_id;           // 0x43: Volume ID
    uint8_t  vol_label[11];    // 0x47: Volume label
    uint8_t  fs_type[8];       // 0x52: Filesystem type ("FAT32   ")
} __attribute__((packed));

// Directory entry (32 bytes)
struct DirEntry {
    uint8_t  name[11];         // 0x00: Name (8.3 format)
    uint8_t  attr;             // 0x0B: Attributes
    uint8_t  ntres;            // 0x0C: Reserved
    uint8_t  crt_time_tenth;   // 0x0D: Creation time tenths
    uint16_t crt_time;         // 0x0E: Creation time
    uint16_t crt_date;         // 0x10: Creation date
    uint16_t lst_acc_date;     // 0x12: Last access date
    uint16_t clust_hi;         // 0x14: High cluster number
    uint16_t wrt_time;         // 0x16: Write time
    uint16_t wrt_date;         // 0x18: Write date
    uint16_t clust_lo;         // 0x1A: Low cluster number
    uint32_t file_size;        // 0x1C: File size (bytes)
} __attribute__((packed));

// Attributes
const uint8_t ATTR_READ_ONLY = 0x01;
const uint8_t ATTR_HIDDEN     = 0x02;
const uint8_t ATTR_SYSTEM     = 0x04;
const uint8_t ATTR_VOLUME_ID  = 0x08;
const uint8_t ATTR_DIRECTORY  = 0x10;
const uint8_t ATTR_ARCHIVE    = 0x20;
const uint8_t ATTR_LONG_NAME  = 0x0F;

// FAT entry values
const uint32_t FAT_FREE    = 0x00000000;
const uint32_t FAT_END_MIN = 0x0FFFFFF8;
const uint32_t FAT_BAD     = 0x0FFFFFF7;

// FAT32 filesystem instance
struct FAT32FS {
    BPB bpb;
    uint32_t fat_start;        // FAT start sector
    uint32_t root_start;      // Root directory start sector
    uint32_t data_start;      // Data region start sector
    uint32_t total_clusters;  // Total clusters
    bool mounted;

    FAT32FS() : mounted(false) {}

    uint32_t cluster_to_sector(uint32_t cluster) {
        return data_start + (cluster - 2) * bpb.sec_per_clus;
    }

    uint32_t bytes_per_cluster() {
        return bpb.bytes_per_sec * bpb.sec_per_clus;
    }
};

// Read a sector from disk (platform-specific)
// Returns true on success
bool read_sector(uint32_t lba, uint8_t* buf, int count = 1);

// Get next cluster from FAT
uint32_t next_cluster(FAT32FS& fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs.fat_start + (fat_offset / fs.bpb.bytes_per_sec);
    uint32_t ent_offset = fat_offset % fs.bpb.bytes_per_sec;

    uint8_t sector[512];
    read_sector(fat_sector, sector);

    uint32_t entry;
    memcpy(&entry, sector + ent_offset, 4);
    return entry & 0x0FFFFFFF;  // Mask to 28 bits
}

// Read a cluster chain into buffer
uint32_t read_cluster_chain(FAT32FS& fs, uint32_t start_cluster,
                            uint8_t* buf, uint32_t max_bytes) {
    uint32_t bytes_read = 0;
    uint32_t cluster = start_cluster;
    uint32_t cluster_size = fs.bytes_per_cluster();

    while (cluster < FAT_END_MIN && bytes_read < max_bytes) {
        uint32_t sector = fs.cluster_to_sector(cluster);
        uint32_t to_read = cluster_size;
        if (bytes_read + to_read > max_bytes) to_read = max_bytes - bytes_read;

        // Read each sector in cluster
        uint32_t sec_per_clus = fs.bpb.sec_per_clus;
        uint32_t sec_bytes = fs.bpb.bytes_per_sec;
        uint8_t tmp[512];

        for (uint32_t s = 0; s < sec_per_clus && bytes_read < max_bytes; s++) {
            uint32_t this_sec_bytes = (bytes_read + sec_bytes > max_bytes) ?
                (max_bytes - bytes_read) : sec_bytes;
            read_sector(sector + s, tmp);
            memcpy(buf + bytes_read, tmp, this_sec_bytes);
            bytes_read += this_sec_bytes;
        }

        cluster = next_cluster(fs, cluster);
        if (cluster == FAT_BAD) break;
    }

    return bytes_read;
}

// Parse 8.3 filename to normal string
String parse_filename(const uint8_t* name) {
    String result;
    // Name part
    for (int i = 0; i < 8; i++) {
        if (name[i] == ' ') break;
        result += (char)name[i];
    }
    // Extension
    bool has_ext = false;
    for (int i = 8; i < 11; i++) {
        if (name[i] != ' ') {
            has_ext = true;
            break;
        }
    }
    if (has_ext) {
        result += '.';
        for (int i = 8; i < 11; i++) {
            if (name[i] == ' ') break;
            result += (char)name[i];
        }
    }
    return result;
}

// Mount FAT32 filesystem
bool mount(uint32_t partition_lba, FAT32FS& fs) {
    // Read BPB
    uint8_t sector[512];
    if (!read_sector(partition_lba, sector)) return false;

    memcpy(&fs.bpb, sector, sizeof(BPB));

    // Verify FAT32 signature
    if (fs.bpb.bytes_per_sec == 0 || fs.bpb.sec_per_clus == 0) return false;
    if (fs.bpb.root_ent_cnt != 0) return false;  // Not FAT32
    if (fs.bpb.fsz_fat32 == 0) return false;

    // Calculate offsets
    fs.fat_start = partition_lba + fs.bpb.rsvd_sec_cnt;
    uint32_t root_dir_sectors = 0;  // FAT32 has no fixed root dir
    uint32_t fat_size = fs.bpb.fsz_fat32;
    fs.data_start = fs.fat_start + fs.bpb.num_fats * fat_size;

    // Calculate total clusters
    uint32_t total_sectors = (fs.bpb.tot_sec32 != 0) ?
        fs.bpb.tot_sec32 : fs.bpb.tot_sec16;
    uint32_t data_sectors = total_sectors - fs.data_start;
    fs.total_clusters = data_sectors / fs.bpb.sec_per_clus;

    fs.mounted = true;
    return true;
}

// Read directory entries
int read_dir(FAT32FS& fs, uint32_t start_cluster, DirEntry* entries, int max_entries) {
    int count = 0;
    uint32_t cluster = start_cluster;
    uint32_t cluster_size = fs.bytes_per_cluster();

    // Allocate buffer for one cluster
    uint8_t* buf = (uint8_t*)kalloc(cluster_size);

    while (cluster < FAT_END_MIN && count < max_entries) {
        read_cluster_chain(fs, cluster, buf, cluster_size);

        // Parse entries
        int entries_in_cluster = cluster_size / 32;
        for (int i = 0; i < entries_in_cluster && count < max_entries; i++) {
            DirEntry* e = (DirEntry*)(buf + i * 32);

            if (e->name[0] == 0x00) break;      // Empty entry
            if (e->name[0] == 0xE5) continue;   // Deleted entry
            if (e->attr == ATTR_LONG_NAME) continue;  // Long filename

            // Skip volume label
            if (e->attr == ATTR_VOLUME_ID) continue;

            entries[count++] = *e;
        }

        cluster = next_cluster(fs, cluster);
        if (cluster == FAT_BAD) break;
    }

    kfree(buf);
    return count;
}

// Read file content
uint32_t read_file(FAT32FS& fs, DirEntry* entry, uint8_t* buf, uint32_t max_bytes) {
    uint32_t start_cluster = entry->clust_hi << 16 | entry->clust_lo;
    uint32_t file_size = entry->file_size;
    if (file_size > max_bytes) file_size = max_bytes;

    return read_cluster_chain(fs, start_cluster, buf, file_size);
}

// Global FAT32 mount point
extern FAT32FS g_fat32;

} // namespace fat32
} // namespace nefu
