// nefuOS text library — Aho-Corasick multi-pattern matching
// 多模式匹配：一次扫描同时查找所有模式串（自动机 = Trie + fail 指针）。
// 用于敏感词过滤、关键词高亮等。本实现是确定性自动机（完整 goto 表），
// 模式总数与总字符数不宜过大（内存 = 节点数 * 256）。
#pragma once
#include <stdint.h>

namespace nefu {
namespace text {

struct AhoMatch {
    int pat_id;   // 命中的模式编号（0 起）
    int pos;      // 命中位置（模式起点在文本中的下标）
};

struct AhoNode {
    int next[256];   // goto 表；-1 表示不存在
    int fail;        // fail 指针
    int out;         // 该节点命中的模式 id；-1 表示无
    int parent;      // 父节点（便于遍历）
    int out_len;     // out 对应模式的长度（pos 推算用）
};

// 简单定长容量版自动机（最多 MAX_NODES 节点、MAX_PATS 模式）。
// 节点表在 init() 时堆分配，避免 8MB 级栈占用。
enum { AHO_MAX_NODES = 8192, AHO_MAX_PATS = 256, AHO_MAX_PAT_LEN = 256 };

struct Aho {
    AhoNode* nodes;              // 堆分配（容量 AHO_MAX_NODES）
    int node_count;
    char pats[AHO_MAX_PATS][AHO_MAX_PAT_LEN];   // 模式原文（供输出）
    int pat_count;

    Aho() : nodes(0), node_count(0), pat_count(0) {}
    ~Aho() { if (nodes) delete[] nodes; }
    void init();
    // 添加模式，返回模式 id；超容量返回 -1
    int add_pattern(const char* pat);
    // 构建 fail 指针（必须先 add 完所有模式）
    void build();
    // 在 text 中查找所有命中；结果写入 out（最多 out_cap 个），返回命中数
    int find_all(const char* text, AhoMatch* out, int out_cap);
};

int aho_self_test();

} // namespace text
} // namespace nefu
