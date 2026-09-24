// nefuOS dblib —— 页管理器实现 + 自测
#include "dblib/page.h"
#include <cstdio>
#include <cstring>

namespace nefu {
namespace dbx {

bool Page::write(int off, const void* src, int len) {
    if (off < 0 || len < 0 || off + len > PAGE_SIZE) return false;
    const unsigned char* p = (const unsigned char*)src;
    if (off + len > (int)data_.size()) data_.resize(off + len, 0);
    for (int i = 0; i < len; i++) data_[off + i] = p[i];
    if (off + len > used_) used_ = off + len;
    return true;
}

bool Page::read(int off, void* dst, int len) const {
    if (off < 0 || len < 0 || off + len > PAGE_SIZE) return false;
    if (off + len > (int)data_.size()) return false;
    unsigned char* p = (unsigned char*)dst;
    for (int i = 0; i < len; i++) p[i] = data_[off + i];
    return true;
}

bool Page::append(const void* src, int len) {
    if (len < 0 || used_ + len > PAGE_SIZE) return false;
    return write(used_, src, len);
}

int PageManager::alloc() {
    for (int i = 0; i < pages_; i++)
        if (!bitmap_[i]) { bitmap_[i] = true; store_[i].clear(); return i; }
    return -1;
}

void PageManager::free(int page) {
    if (page >= 0 && page < pages_) bitmap_[page] = false;
}

// ---- self test ----
int PageManager::self_test() {
    int fails = 0;
    // 1. 页写入读取
    {
        Page p;
        p.clear();
        int v = 12345;
        if (!p.write(0, &v, sizeof(v))) fails++;
        int r = 0;
        if (!p.read(0, &r, sizeof(r))) fails++;
        if (r != 12345) fails++;
        if (p.used() != sizeof(v)) fails++;
    }
    // 2. 越界保护
    {
        Page p;
        p.clear();
        int v = 1;
        if (p.write(Page::PAGE_SIZE - 1, &v, 4)) fails++;   // 越界应失败
        if (p.read(500, &v, 20)) fails++;
        if (p.write(-1, &v, 4)) fails++;
    }
    // 3. 追加
    {
        Page p;
        p.clear();
        const char* s = "hello";
        if (!p.append(s, 5)) fails++;
        char buf[8] = { 0 };
        if (!p.read(0, buf, 5)) fails++;
        if (std::string(buf) != "hello") fails++;
        // 写满
        char big[600];
        if (p.append(big, 600)) fails++;   // 超容量失败
        if (p.full()) fails++;
    }
    // 4. 分配/释放
    {
        PageManager pm(10);
        if (pm.free_count() != 10) fails++;
        int a = pm.alloc();
        int b = pm.alloc();
        if (a < 0 || b < 0 || a == b) fails++;
        if (!pm.in_use(a)) fails++;
        if (pm.free_count() != 8) fails++;
        pm.free(a);
        if (pm.in_use(a)) fails++;
        if (pm.free_count() != 9) fails++;
        // 释放后再分配可复用
        int c = pm.alloc();
        if (c != a) fails++;   // 顺序扫描取最小空闲页，应复用 a
        pm.free(b); pm.free(c);
        if (pm.free_count() != 10) fails++;
    }
    // 5. 满时分配失败
    {
        PageManager pm(3);
        int a = pm.alloc(), b = pm.alloc(), c = pm.alloc();
        if (a < 0 || b < 0 || c < 0) fails++;
        if (pm.alloc() != -1) fails++;   // 全满
    }
    // 6. 页数据隔离
    {
        PageManager pm(4);
        int a = pm.alloc(), b = pm.alloc();
        int v = 99;
        pm.page(a)->write(0, &v, sizeof(v));
        int r = 0;
        pm.page(b)->read(0, &r, sizeof(r));
        if (r != 0) fails++;   // b 页不应受 a 影响
    }
    return fails;
}

} // namespace dbx
} // namespace nefu
