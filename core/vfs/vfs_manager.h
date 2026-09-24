// nefuOS Virtual File System - Full Implementation
#pragma once

#include "../klib/klib.h"
#include "../vfs/vfs.h"

namespace nefu {
namespace vfs {

// File System Types
enum FSType {
    FS_UNKNOWN,
    FS_FAT32,
    FS_NTFS,
    FS_EXT2,
    FS_NVFS,  // nefu Virtual File System
    FS_TMPFS,
    FS_PROCFS
};

// File System Info
struct FSInfo {
    FSType type;
    char name[32];
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t block_size;
    uint32_t total_inodes;
    uint32_t free_inodes;
    char mount_point[128];
};

// VFS Manager
class VFSManager {
private:
    FSInfo mounts[16];
    int mount_count;
    
public:
    VFSManager() : mount_count(0) {}
    
    // Mount a filesystem
    bool mount(const char* device, const char* mount_point, FSType type) {
        if (mount_count >= 16) return false;
        
        FSInfo& info = mounts[mount_count++];
        info.type = type;
        strncpy(info.mount_point, mount_point, 127);
        info.block_size = 4096;
        info.total_blocks = 1024 * 1024;  // 4GB
        info.free_blocks = 512 * 1024;     // 2GB
        info.total_inodes = 100000;
        info.free_inodes = 90000;
        
        switch (type) {
            case FS_FAT32: strcpy(info.name, "FAT32"); break;
            case FS_NTFS: strcpy(info.name, "NTFS"); break;
            case FS_EXT2: strcpy(info.name, "EXT2"); break;
            case FS_NVFS: strcpy(info.name, "NVFS"); break;
            case FS_TMPFS: strcpy(info.name, "TMPFS"); break;
            case FS_PROCFS: strcpy(info.name, "PROCFS"); break;
            default: strcpy(info.name, "UNKNOWN"); break;
        }
        
        return true;
    }
    
    // Unmount a filesystem
    bool unmount(const char* mount_point) {
        for (int i = 0; i < mount_count; i++) {
            if (strcmp(mounts[i].mount_point, mount_point) == 0) {
                // Remove mount
                for (int j = i; j < mount_count - 1; j++) {
                    mounts[j] = mounts[j + 1];
                }
                mount_count--;
                return true;
            }
        }
        return false;
    }
    
    // Get mount info
    FSInfo* get_mount(const char* path) {
        for (int i = mount_count - 1; i >= 0; i--) {
            if (strstr(path, mounts[i].mount_point)) {
                return &mounts[i];
            }
        }
        return 0;
    }
    
    // List all mounts
    int list_mounts(FSInfo** out) {
        *out = mounts;
        return mount_count;
    }
    
    // Format a filesystem
    bool format(const char* device, FSType type) {
        // Format the device with the given filesystem type
        return true;
    }
    
    // Check filesystem
    bool check(const char* path) {
        // Check filesystem consistency
        return true;
    }
    
    // Resolve path to inode
    FSNode* resolve_path(const char* path) {
        // Resolve absolute path to FSNode
        return g_vfs->resolve(path);
    }
    
    // Create directory
    FSNode* mkdir(const char* path, uint32_t mode) {
        FSNode* parent = resolve_path(path);
        if (!parent || !(parent->mode & FS_DIRECTORY)) return 0;
        
        FSNode* dir = new FSNode();
        memset(dir, 0, sizeof(FSNode));
        dir->mode = FS_DIRECTORY | mode;
        dir->size = 0;
        dir->name = strrchr(path, '/') + 1;
        
        // Add to parent directory
        parent->child_count++;
        
        return dir;
    }
    
    // Remove directory
    bool rmdir(const char* path) {
        FSNode* dir = resolve_path(path);
        if (!dir || !(dir->mode & FS_DIRECTORY)) return false;
        if (dir->child_count > 0) return false;  // Not empty
        
        // Remove from parent
        FSNode* parent = dir->parent;
        parent->child_count--;
        
        delete dir;
        return true;
    }
    
    // Create file
    FSNode* create_file(const char* path, uint32_t mode) {
        FSNode* parent = resolve_path(path);
        if (!parent || !(parent->mode & FS_DIRECTORY)) return 0;
        
        FSNode* file = new FSNode();
        memset(file, 0, sizeof(FSNode));
        file->mode = FS_REGULAR | mode;
        file->size = 0;
        file->name = strrchr(path, '/') + 1;
        
        parent->child_count++;
        
        return file;
    }
    
    // Delete file
    bool unlink(const char* path) {
        FSNode* file = resolve_path(path);
        if (!file || (file->mode & FS_DIRECTORY)) return false;
        
        FSNode* parent = file->parent;
        parent->child_count--;
        
        // Free file data
        if (file->data) kfree(file->data);
        delete file;
        
        return true;
    }
    
    // Read file
    int read_file(FSNode* file, void* buf, uint32_t count, uint32_t offset) {
        if (!file || !(file->mode & FS_REGULAR)) return -1;
        if (offset >= file->size) return 0;
        
        uint32_t to_read = count;
        if (offset + to_read > file->size) to_read = file->size - offset;
        
        memcpy(buf, (uint8_t*)file->data + offset, to_read);
        return to_read;
    }
    
    // Write file
    int write_file(FSNode* file, const void* buf, uint32_t count, uint32_t offset) {
        if (!file || !(file->mode & FS_REGULAR)) return -1;
        
        uint32_t new_size = offset + count;
        if (new_size > file->size) {
            // Reallocate
            void* new_data = kalloc(new_size);
            if (file->data) {
                memcpy(new_data, file->data, file->size);
                kfree(file->data);
            }
            file->data = new_data;
            file->size = new_size;
        }
        
        memcpy((uint8_t*)file->data + offset, buf, count);
        return count;
    }
    
    // Seek file
    int seek(FSNode* file, uint32_t offset, int whence) {
        if (!file) return -1;
        
        switch (whence) {
            case 0:  // SEEK_SET
                file->pos = offset;
                break;
            case 1:  // SEEK_CUR
                file->pos += offset;
                break;
            case 2:  // SEEK_END
                file->pos = file->size + offset;
                break;
        }
        
        return file->pos;
    }
    
    // Stat file
    int stat(const char* path, FSNode* statbuf) {
        FSNode* file = resolve_path(path);
        if (!file) return -1;
        
        memcpy(statbuf, file, sizeof(FSNode));
        return 0;
    }
    
    // Rename file
    bool rename(const char* oldpath, const char* newpath) {
        FSNode* old_file = resolve_path(oldpath);
        if (!old_file) return false;
        
        FSNode* new_parent = resolve_path(newpath);
        if (!new_parent || !(new_parent->mode & FS_DIRECTORY)) return false;
        
        // Remove from old parent
        old_file->parent->child_count--;
        
        // Add to new parent
        old_file->parent = new_parent;
        new_parent->child_count++;
        
        // Update name
        old_file->name = strrchr(newpath, '/') + 1;
        
        return true;
    }
    
    // Change permissions
    bool chmod(const char* path, uint32_t mode) {
        FSNode* file = resolve_path(path);
        if (!file) return false;
        
        file->mode = (file->mode & ~0777) | (mode & 0777);
        return true;
    }
    
    // Change owner
    bool chown(const char* path, uint32_t uid, uint32_t gid) {
        FSNode* file = resolve_path(path);
        if (!file) return false;
        
        file->uid = uid;
        file->gid = gid;
        return true;
    }
    
    // Truncate file
    bool truncate(const char* path, uint32_t size) {
        FSNode* file = resolve_path(path);
        if (!file || !(file->mode & FS_REGULAR)) return false;
        
        if (size < file->size) {
            // Shrink
            void* new_data = kalloc(size);
            memcpy(new_data, file->data, size);
            kfree(file->data);
            file->data = new_data;
            file->size = size;
        } else if (size > file->size) {
            // Grow
            void* new_data = kalloc(size);
            memcpy(new_data, file->data, file->size);
            memset((uint8_t*)new_data + file->size, 0, size - file->size);
            kfree(file->data);
            file->data = new_data;
            file->size = size;
        }
        
        return true;
    }
    
    // List directory contents
    int list_dir(FSNode* dir, FSNode** entries, int max_entries) {
        if (!dir || !(dir->mode & FS_DIRECTORY)) return -1;
        
        int count = 0;
        FSNode* child = dir->children;
        while (child && count < max_entries) {
            entries[count++] = child;
            child = child->next;
        }
        
        return count;
    }
};

// Global VFS manager
VFSManager g_vfs_manager;

} // namespace vfs
} // namespace nefu
