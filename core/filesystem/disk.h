// nefuOS 文件系统库 —— 虚拟磁盘(块设备)层
//
// 本模块模拟一块可寻址的磁盘:固定 512 字节扇区,内存中一块连续缓冲。
// 上层文件系统(FAT16 / ext2 / MinixFS)只看到 read_sector/write_sector 两个接口,
// 完全不关心数据到底在内存镜像还是真物理盘——这就是块设备抽象的意义。
//
// 同时实现:
//   - MBR 分区表:扇区 0,4 个主分区项,偏移 446,结尾魔数 0xAA55
//   - GPT 分区表:保护 MBR(扇区0)+ GPT 头(扇区1)+ 条目数组(扇区2起)
//   - 坏块管理:坏块位图 +  spare 重定向,写坏块时自动落到备用扇区
//   - 镜像加载/保存:整块缓冲可导出为字节数组(宿主测试里跨进程"落盘")
//
// 注意:本库不依赖 STL / 异常 / RTTI / libc malloc;内存一律走 new[]/delete[]。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace filesystem {

// 扇区固定 512 字节(与真实 x86 BIOS/UEFI 磁盘一致)
const uint32_t SECTOR_SIZE = 512;

// ---- MBR 分区表项(16 字节)----
struct MbrPartition {
    uint8_t  status;     // 0x80 = 可引导
    uint8_t  chs_first[3];
    uint8_t  type;       // 分区类型(0x0E=FAT16 LBA, 0x83=Linux, 0xEF=ESP)
    uint8_t  chs_last[3];
    uint32_t lba_first;  // 起始 LBA 扇区号
    uint32_t lba_count;  // 扇区数
};

// MBR 魔数(最后两字节 = 0x55 0xAA,小端读为 0xAA55)
const uint16_t MBR_SIGNATURE = 0xAA55;

// ---- GPT 头(逻辑扇区 1,128 字节结构)----
// 这里只保留我们关心的字段,其余按字节镜像。
struct GptHeader {
    uint64_t signature;      // "EFI PART" = 0x5452415020494645
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t entry_lba;      // 条目数组所在 LBA(通常 2)
    uint32_t entry_count;
    uint32_t entry_size;     // 通常 128
    uint32_t entry_array_crc32;
};

// GPT 单条目 128 字节
struct GptEntry {
    uint8_t  type_guid[16];  // 0 = 未使用
    uint8_t  unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t flags;
    uint8_t  name[72];       // UTF-16LE 名字(36 字符)
};

const uint64_t GPT_SIGNATURE = 0x5452415020494645ULL; // "EFI PART"

// ---- 分区表类型 ----
enum PartitionStyle {
    PS_RAW = 0,   // 无分区表,整盘一个文件系统
    PS_MBR = 1,   // MBR/DOS 分区表
    PS_GPT = 2,   // GPT
};

// ---- 虚拟磁盘 ----
class Disk {
public:
    Disk();
    ~Disk();

    // 创建一块 nsectors 扇区的空白盘(全部清零)。成功返回 true。
    bool create(uint32_t nsectors);

    // ---- 块设备接口 ----
    // 读一个扇区到 buf(必须 >=512 字节)。坏块会被重定向。返回 true=成功。
    bool read_sector(uint32_t lba, uint8_t* buf);
    // 写一个扇区。若目标是坏块,自动改写到备用扇区。返回 true=成功。
    bool write_sector(uint32_t lba, const uint8_t* buf);

    // 跨扇区连续读/写(按字节),offset 以字节计。
    bool read_bytes(uint64_t offset, uint8_t* buf, uint32_t len);
    bool write_bytes(uint64_t offset, const uint8_t* buf, uint32_t len);

    uint32_t total_sectors() const { return nsec_; }
    uint64_t total_bytes() const { return (uint64_t)nsec_ * SECTOR_SIZE; }

    // ---- MBR ----
    // 写 MBR 分区表项(0..3),并写魔数。type 见 MbrPartition.type。
    bool mbr_set_partition(int idx, uint8_t type, uint32_t lba_first, uint32_t lba_count, bool bootable);
    bool mbr_get_partition(int idx, MbrPartition* out);
    // 在扇区 0 写入保护 MBR(GPT 用)
    void mbr_write_protective(uint32_t total_lba);

    // ---- GPT ----
    // 生成 GPT:写保护 MBR + GPT 头 + entry_count 个空条目。
    bool gpt_write_protective_header(uint32_t entry_count);
    bool gpt_read_header(GptHeader* out);
    // 添加一个 GPT 分区(找第一个空条目)。返回条目索引,-1=满。
    int  gpt_add_partition(uint64_t first_lba, uint64_t last_lba, const char* name_utf16);
    bool gpt_get_entry(int idx, GptEntry* out);

    // ---- 坏块管理 ----
    // 标记一个扇区为坏(模拟磁盘出现介质错误)。
    void mark_bad(uint32_t lba);
    bool is_bad(uint32_t lba) const;
    int  bad_count() const { return bad_count_; }
    // 已重定向(实际数据落在备用区)的扇区数
    int  remap_count() const { return remap_count_; }

    // ---- 镜像导出/导入 ----
    // 把整盘字节导出到新分配的缓冲(调用者 delete[] 释放)。
    uint8_t* export_image(uint32_t* out_size);
    // 从外部字节缓冲导入整盘(会先 create 再逐扇区拷入)。返回 true=成功。
    bool import_image(const uint8_t* data, uint32_t size);

    PartitionStyle style() const { return style_; }
    void set_style(PartitionStyle s) { style_ = s; }

    // 自检需要直接访问底层缓冲
    friend int disk_self_test();

private:
    // 坏块重定向:把逻辑 LBA 映射到物理扇区。
    // 返回实际应访问的物理扇区号(坏块指向备用区里的下一个空闲槽)。
    uint32_t remap(uint32_t lba) const;
    // 坏块位图读写位
    void bad_bit_set(uint32_t lba);
    bool bad_bit_get(uint32_t lba) const;

    uint8_t*   buf_;        // 整盘镜像缓冲(nsectors*512 字节)
    uint32_t   nsec_;       // 总扇区数
    uint8_t*   bad_map_;    // 坏块位图(1 bit / 扇区),字节向上取整
    uint32_t   bad_map_bytes_;
    int        bad_count_;
    int        remap_count_;
    // 备用扇区池:磁盘末尾 reserved_spare_ 个扇区用于坏块重定向
    uint32_t   spare_start_;   // 备用区起始 LBA
    uint32_t   spare_count_;   // 备用区大小
    uint32_t   spare_used_;    // 已用掉的备用扇区数
    PartitionStyle style_;
};

// 模块自检:返回失败断言数(0 = 全部通过)
int disk_self_test();

// ---- 磁盘布局摘要(供 fsview 窗口应用渲染)----
struct DiskReport {
    PartitionStyle style;
    uint32_t total_sectors;
    uint64_t total_bytes;
    int  mbr_partitions;       // 非空主分区数
    uint32_t part_start[4];    // 各分区起始 LBA
    uint32_t part_size[4];     // 各分区扇区数
    uint8_t  part_type[4];     // 分区类型字节
    bool     part_boot[4];
    uint32_t gpt_entries;      // GPT 条目数(若 PS_GPT)
    int  bad_blocks;
    int  remapped;
};

// 扫描磁盘并填充摘要。返回 true=成功。
bool disk_inspect(Disk* d, DiskReport* out);

} // namespace filesystem
} // namespace nefu
