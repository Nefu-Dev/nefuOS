// nefuOS dblib —— 页管理器 page
// 教学版：固定大小页的分配/释放/读写（模拟简单存储引擎的页层）。
#pragma once
#include <vector>
#include <string>

namespace nefu {
namespace dbx {

// 页：固定 PAGE_SIZE 字节
class Page {
public:
    Page() : used_(0) {}
    void clear() { used_ = 0; data_.assign(PAGE_SIZE, 0); }
    // 写入（从 off 起 len 字节；返回是否成功）
    bool write(int off, const void* src, int len);
    // 读取
    bool read(int off, void* dst, int len) const;
    // 追加
    bool append(const void* src, int len);
    int used() const { return used_; }
    bool full() const { return used_ >= PAGE_SIZE; }
    const std::vector<unsigned char>& bytes() const { return data_; }

    static const int PAGE_SIZE = 512;

private:
    std::vector<unsigned char> data_;
    int used_;
};

// 页管理器：固定数量页的分配位图
class PageManager {
public:
    PageManager(int pages) : pages_(pages) {
        bitmap_.assign(pages, false);
        for (int i = 0; i < pages; i++) store_.push_back(Page());
    }

    // 分配一页（返回页号；无可用返回 -1）
    int alloc();
    // 释放
    void free(int page);
    // 页是否在用
    bool in_use(int page) const { return page >= 0 && page < pages_ && bitmap_[page]; }
    // 空闲页数
    int free_count() const {
        int c = 0;
        for (int i = 0; i < pages_; i++) if (!bitmap_[i]) c++;
        return c;
    }
    int total() const { return pages_; }
    // 访问页
    Page* page(int idx) { return idx >= 0 && idx < pages_ ? &store_[idx] : 0; }
    const Page* page(int idx) const { return idx >= 0 && idx < pages_ ? &store_[idx] : 0; }

    // ---- self test ----
    static int self_test();

private:
    int pages_;
    std::vector<bool> bitmap_;
    std::vector<Page> store_;
};

} // namespace dbx
} // namespace nefu
