// nefuOS Memory Management (Linux-style, pure C++)
// References: Linux mm/, vm_area_struct, page allocator

#ifndef NEFU_MM_H
#define NEFU_MM_H

#include "../klib/klib.h"

namespace nefu {

enum VMFlag : uint32_t {
    VM_READ     = 0x0001,
    VM_WRITE    = 0x0002,
    VM_EXEC     = 0x0004,
    VM_SHARED   = 0x0008,
    VM_GROWSDOWN = 0x0100,
};

struct VMArea {
    uintptr_t vmStart;
    uintptr_t vmEnd;
    uint32_t vmFlags;
    VMArea* next;
};

struct MemoryDescriptor {
    VMArea* mmap;
    uintptr_t startCode, endCode;
    uintptr_t startData, endData;
    uintptr_t startBrk, brk;
    uintptr_t startStack;
};

typedef uint32_t PFN;

struct Page {
    uint32_t flags;
    uint32_t refcount;
    PFN pfn;
};

class MemoryManager {
private:
    uint8_t* bitmap;
    uint32_t totalPages;
    uint32_t freePages;
    uintptr_t memStart;
    uintptr_t memEnd;

public:
    MemoryManager() : bitmap(nullptr), totalPages(0), freePages(0),
                      memStart(0), memEnd(0) {}

    void init(uintptr_t start, uintptr_t end, uint32_t pageSize = 4096) {
        memStart = start;
        memEnd = end;
        totalPages = (uint32_t)((end - start) / pageSize);
        freePages = totalPages;
        uint32_t bitmapSize = (totalPages + 7) / 8;
        bitmap = new uint8_t[bitmapSize];
        for (uint32_t i = 0; i < bitmapSize; i++) bitmap[i] = 0;
    }

    void* allocPage() {
        for (uint32_t i = 0; i < totalPages; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                freePages--;
                return (void*)(memStart + i * 4096);
            }
        }
        return nullptr;
    }

    void freePage(void* addr) {
        uint32_t pfn = (uint32_t)((uintptr_t)addr - memStart) / 4096;
        if (pfn < totalPages) {
            bitmap[pfn / 8] &= ~(1 << (pfn % 8));
            freePages++;
        }
    }

    VMArea* mmap(uintptr_t addr, uint32_t length, uint32_t prot, int flags) {
        VMArea* vma = new VMArea;
        vma->vmStart = addr;
        vma->vmEnd = addr + length;
        vma->vmFlags = prot;
        vma->next = nullptr;
        return vma;
    }

    void munmap(VMArea* vma) { delete vma; }

    void getMemInfo(uint64_t& total, uint64_t& free, uint64_t& used) {
        total = (uint64_t)totalPages * 4096;
        free = (uint64_t)freePages * 4096;
        used = total - free;
    }

    uint32_t getFreePages() const { return freePages; }
    uint32_t getTotalPages() const { return totalPages; }
};

extern MemoryManager* g_mm;

} // namespace nefu

#endif // NEFU_MM_H