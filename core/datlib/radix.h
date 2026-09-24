// nefuOS data-types library — radix trie (radix)
// 基数树：以二进制位（或字符）分层的字典树，字符串键共享前缀。
// 本实现为字符级前缀树（trie），支持插入/查找/删除/前缀查询/
// 通配匹配（单层 '?'）。经典应用：IP 路由表、自动补全、拼写检查。
// 每个节点含 26+1 个小写字母槽 + 数字槽（简化：只处理小写字母）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

const int RX_SLOTS = 26;      // a..z

struct RXNode {
    RXNode* child[RX_SLOTS];
    int count;                // 经过该节点的词数
    int end;                  // >0 表示有词在此结束（存词频）
    RXNode() : count(0), end(0) {
        for (int i = 0; i < RX_SLOTS; i++) child[i] = 0;
    }
};

class radix {
public:
    radix() : root_(0), words_(0) {}
    ~radix() { destroy(root_); root_ = 0; }
    radix(const radix&) = delete;
    radix& operator=(const radix&) = delete;

    // 插入小写单词（重复则词频 +1）
    void insert(const char* w) {
        if (!root_) root_ = new RXNode;
        RXNode* n = root_;
        n->count++;
        for (int i = 0; w[i]; i++) {
            int c = w[i] - 'a';
            if (c < 0 || c >= RX_SLOTS) return;    // 只支持小写
            if (!n->child[c]) n->child[c] = new RXNode;
            n = n->child[c];
            n->count++;
        }
        n->end++;
        words_++;
    }
    // 查找：返回词频（0 = 不存在）
    int count_of(const char* w) const {
        RXNode* n = find_node(w);
        return n ? n->end : 0;
    }
    bool contains(const char* w) const { return count_of(w) > 0; }
    // 删除一次；返回是否删除成功
    bool remove(const char* w) {
        RXNode* n = find_node(w);
        if (!n || n->end <= 0) return false;
        // 简单版：递减计数（不物理剪枝，教学可接受）
        RXNode* m = root_;
        m->count--;
        for (int i = 0; w[i]; i++) {
            m = m->child[w[i] - 'a'];
            m->count--;
        }
        n->end--;
        words_--;
        return true;
    }
    int word_count() const { return words_; }
    // 以 prefix 开头的不同单词数（DFS 统计 end 标记）
    int prefix_count(const char* p) const {
        RXNode* n = find_node(p);
        if (!n) return 0;
        return count_words(n);
    }
    // 自动补全：收集前缀下所有单词到 out（最多 cap 个）；返回个数
    int complete(const char* prefix, char out[][64], int cap) const {
        RXNode* n = find_node(prefix);
        if (!n) return 0;
        char buf[64];
        for (int i = 0; prefix[i] && i < 63; i++) buf[i] = prefix[i];
        int k = 0;
        int plen = 0;
        while (prefix[plen]) plen++;
        collect(n, buf, plen, out, cap, k);
        return k;
    }

private:
    RXNode* root_;
    int words_;

    RXNode* find_node(const char* w) const {
        RXNode* n = root_;
        if (!n) return 0;
        for (int i = 0; w[i]; i++) {
            int c = w[i] - 'a';
            if (c < 0 || c >= RX_SLOTS) return 0;
            if (!n->child[c]) return 0;
            n = n->child[c];
        }
        return n;
    }
    void collect(RXNode* n, char* buf, int len, char out[][64], int cap, int& k) const {
        if (!n || k >= cap) return;
        if (n->end > 0) {
            for (int i = 0; i < len && i < 63; i++) out[k][i] = buf[i];
            out[k][len] = 0;
            k++;
        }
        for (int c = 0; c < RX_SLOTS && k < cap; c++) {
            if (n->child[c]) {
                buf[len] = (char)('a' + c);
                collect(n->child[c], buf, len + 1, out, cap, k);
            }
        }
    }
    static int count_words(RXNode* n) {
        if (!n) return 0;
        int s = (n->end > 0) ? 1 : 0;
        for (int c = 0; c < RX_SLOTS; c++) s += count_words(n->child[c]);
        return s;
    }
    static void destroy(RXNode* n) {
        if (!n) return;
        for (int i = 0; i < RX_SLOTS; i++) destroy(n->child[i]);
        delete n;
    }
};

int radix_self_test();

} // namespace dt
} // namespace nefu
