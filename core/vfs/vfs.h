// nefuOS 虚拟文件树（VFS）
#pragma once
#include "../klib/klib.h"

namespace nefu {

struct FSNode {
    String name;
    bool is_dir;
    uint32_t size;
    uint32_t mtime;              // 秒（伪时钟）
    FSNode* parent;
    List<FSNode*> children;      // 目录使用
    uint8_t* data;               // 文件使用
    bool expanded;               // 树视图展开状态
    FSNode() : is_dir(false), size(0), mtime(0), parent(0), data(0), expanded(false) {}
};

class VFS {
public:
    VFS();
    ~VFS();
    FSNode* root() { return &root_; }

    FSNode* resolve(const char* path);                    // 相对 cwd 或绝对
    FSNode* resolve_from(FSNode* base, const char* path);
    FSNode* mkdir(const char* path);
    FSNode* create_file(const char* path);
    bool write_file(FSNode* f, const uint8_t* data, uint32_t size);
    bool remove_node(FSNode* n);
    FSNode* move_node(FSNode* n, FSNode* dst_dir, const char* new_name = 0);

    // Trash: move files to /home/user/Trash instead of deleting
    FSNode* trash_dir();
    FSNode* trash_file(FSNode* n);
    FSNode* restore_file(FSNode* n, FSNode* dst_dir);
    int     empty_trash(); // 移动/改名

    FSNode* cwd;
    void set_cwd(FSNode* n);

    void create_default_tree();
    void cleanup_stray_nodes();                            // remove legacy broken nodes
    bool save(uint8_t** out, uint32_t* out_size);         // 调用方负责 kfree
    bool load(const uint8_t* data, uint32_t size);
    uint32_t total_bytes();                               // 全部文件字节数
    int node_count();

private:
    FSNode* alloc_node();
    void free_tree(FSNode* n);
    FSNode* find_child(FSNode* d, const char* name);
    void split_path(const char* path, List<String>& parts);
    void ser_node(uint8_t*& p, FSNode* n);
    bool deser_node(const uint8_t*& p, const uint8_t* end, FSNode* parent);
    uint32_t ser_size(FSNode* n);
    void count_tree(FSNode* n, int& files, int& dirs);
    FSNode root_;
};

extern VFS* g_vfs;

} // namespace nefu
