/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS (VFS)
#pragma once
#include "../klib/klib.h"

namespace nefu {

struct FSNode {
    String name;
    bool is_dir;
    uint32_t size;
    uint32_t mtime;              // sec()
    FSNode* parent;
    List<FSNode*> children;
    uint8_t* data;
    bool expanded;
    FSNode() : is_dir(false), size(0), mtime(0), parent(0), data(0), expanded(false) {}
};

class VFS {
public:
    VFS();
    ~VFS();
    FSNode* root() { return &root_; }

    FSNode* resolve(const char* path);                    // cwd
    FSNode* resolve_from(FSNode* base, const char* path);
    FSNode* mkdir(const char* path);
    FSNode* create_file(const char* path);
    bool write_file(FSNode* f, const uint8_t* data, uint32_t size);
    void log_event(const char* what, const char* path);   // NVFS journal -> /var/log/syslog
    bool remove_node(FSNode* n);
    FSNode* move_node(FSNode* n, FSNode* dst_dir, const char* new_name = 0);

    // Trash: move files to /home/user/Trash instead of deleting
    FSNode* trash_dir();
    FSNode* trash_file(FSNode* n);
    FSNode* restore_file(FSNode* n, FSNode* dst_dir);
    int     empty_trash(); // move/

    FSNode* cwd;
    void set_cwd(FSNode* n);

    // ---- write-back tracking (NVFS autosave) ----
    // Every mutating call records the affected path in a small fixed table; the
    // autosave mirrors exactly those paths into the on-disk image instead of
    // rebuilding 4 MiB four times a second. A change that cannot be mirrored
    // one path at a time (delete / move / a very long path) raises
    // dirty_all(), which asks the autosave for a full rebuild instead.
    bool        dirty_all() const { return dirty_all_; }
    int         dirty_count() const { return dirty_n_; }
    const char* dirty_path(int i) const {
        return (i >= 0 && i < dirty_n_) ? dirty_paths_[i] : "";
    }
    void        dirty_drop(int n);      // forget the first n (already written) paths
    void        dirty_reset();          // forget everything, no rebuild pending

    void create_default_tree();
    void ensure_standard_dirs();                          // idempotent dir fix-up
    void ensure_default_files();                          // idempotent file fix-up (never clobbers)
    void seed_empty_dirs();                               // every empty dir gets a README
    void cleanup_stray_nodes();                           // remove legacy broken nodes
    bool save(uint8_t** out, uint32_t* out_size);         // kfree
    bool load(const uint8_t* data, uint32_t size);
    uint32_t total_bytes();
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
    void touch(const char* path);          // record one write-back path
    void touch_node(FSNode* n);            // record a node by its full path
    static const int DIRTY_MAX = 64;      // pending paths before a full rebuild
    char dirty_paths_[DIRTY_MAX][96];     // fixed table: no heap churn on bare metal
    int  dirty_n_;
    bool dirty_all_;
    FSNode root_;
};

extern VFS* g_vfs;

} // namespace nefu

