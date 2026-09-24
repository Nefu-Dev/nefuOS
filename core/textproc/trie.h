// nefuOS 文本处理库 —— 前缀树 / 压缩前缀树
//   - Trie(完整前缀树)：插入、精确查询、前缀计数、自动补全
//   - Radix Tree(压缩前缀树 / 基数树)：把单链节点合并成带边标签的边
// 字母表按小写 a-z 处理(插入时自动转小写)；全部 new[]/delete[] 管理。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace textproc {

// 不透明句柄(实现见 trie.cpp)
struct Trie;
struct Radix;

// ---------------- Trie ----------------
Trie* trie_create();
void  trie_destroy(Trie* t);
void  trie_insert(Trie* t, const char* word);   // 自动转小写、忽略非字母
bool  trie_contains(Trie* t, const char* word); // 是否为完整词
int   trie_count_prefix(Trie* t, const char* prefix); // 以 prefix 开头的词数
int   trie_size(const Trie* t);                   // 树中词总数
// 自动补全：把以 prefix 开头的完整词写入 bufs[0..max_out-1]
// (bufs[i] 为调用方提供的长度 bufsz 的缓冲)，返回实际补全个数。
int   trie_autocomplete(Trie* t, const char* prefix,
                       char** bufs, int max_out, int bufsz);

// ---------------- Radix Tree(压缩前缀树) ----------------
Radix* radix_create();
void   radix_destroy(Radix* r);
void   radix_insert(Radix* r, const char* word);
bool   radix_contains(Radix* r, const char* word);     // 完整词?
bool   radix_starts_with(Radix* r, const char* pref);  // 存在前缀?

// 自检：返回失败数(0 = 全绿)。
int trie_self_test();

} // namespace textproc
} // namespace nefu
