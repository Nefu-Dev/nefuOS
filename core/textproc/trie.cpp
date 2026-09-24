// nefuOS 文本处理库 —— 前缀树 / 压缩前缀树(实现)
#include "trie.h"
#include "../klib/klib.h"

namespace nefu {
namespace textproc {

static inline int slen(const char* s) { return s ? (int)strlen(s) : 0; }

// 字母表映射：a-z -> 0..25，其它字符返回 -1
static inline int chidx(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= 'A' && c <= 'Z') return c - 'A';
    return -1;
}

// =========================================================================
// Trie
// =========================================================================
struct TrieNode {
    TrieNode* child[26];
    bool terminal;
    int pass;       // 经过本节点的词数(子树里有多少完整词路过)
    TrieNode() : terminal(false), pass(0) {
        for (int i = 0; i < 26; i++) child[i] = 0;
    }
};

struct Trie {
    TrieNode* root;
    int words;
};

Trie* trie_create() {
    Trie* t = new Trie;
    t->root = new TrieNode();
    t->words = 0;
    return t;
}

static void trie_free_node(TrieNode* n) {
    if (!n) return;
    for (int i = 0; i < 26; i++) trie_free_node(n->child[i]);
    delete n;
}

void trie_destroy(Trie* t) {
    if (!t) return;
    trie_free_node(t->root);
    delete t;
}

void trie_insert(Trie* t, const char* word) {
    if (!t || !word) return;
    TrieNode* cur = t->root;
    cur->pass++;
    for (const char* p = word; *p; p++) {
        int c = chidx(*p);
        if (c < 0) continue;         // 忽略标点/数字
        if (!cur->child[c]) cur->child[c] = new TrieNode();
        cur = cur->child[c];
        cur->pass++;
    }
    if (!cur->terminal) t->words++;
    cur->terminal = true;
}

bool trie_contains(Trie* t, const char* word) {
    if (!t || !word) return false;
    TrieNode* cur = t->root;
    for (const char* p = word; *p; p++) {
        int c = chidx(*p);
        if (c < 0) continue;
        if (!cur->child[c]) return false;
        cur = cur->child[c];
    }
    return cur->terminal;
}

int trie_count_prefix(Trie* t, const char* prefix) {
    if (!t || !prefix) return 0;
    TrieNode* cur = t->root;
    for (const char* p = prefix; *p; p++) {
        int c = chidx(*p);
        if (c < 0) continue;
        if (!cur->child[c]) return 0;
        cur = cur->child[c];
    }
    return cur->pass;
}

int trie_size(const Trie* t) { return t ? t->words : 0; }

// DFS 收集补全词
static void trie_collect(TrieNode* n, char* cur, int depth,
                         char** bufs, int max_out, int bufsz, int* found) {
    if (!n || *found >= max_out) return;
    if (n->terminal) {
        cur[depth] = 0;
        // 拷出当前路径
        int i = *found;
        for (int k = 0; k < depth && k < bufsz - 1; k++) bufs[i][k] = cur[k];
        bufs[i][depth < bufsz ? depth : bufsz - 1] = 0;
        (*found)++;
    }
    for (int c = 0; c < 26 && *found < max_out; c++) {
        if (n->child[c]) {
            if (depth < bufsz - 1) cur[depth] = (char)('a' + c);
            trie_collect(n->child[c], cur, depth + 1, bufs, max_out, bufsz, found);
        }
    }
}

int trie_autocomplete(Trie* t, const char* prefix,
                      char** bufs, int max_out, int bufsz) {
    if (!t || !prefix) return 0;
    TrieNode* cur = t->root;
    char* path = new char[bufsz];
    int depth = 0;
    for (const char* p = prefix; *p; p++) {
        int c = chidx(*p);
        if (c < 0) continue;
        if (!cur->child[c]) { delete[] path; return 0; }
        cur = cur->child[c];
        if (depth < bufsz - 1) path[depth++] = (char)('a' + c);
    }
    int found = 0;
    trie_collect(cur, path, depth, bufs, max_out, bufsz, &found);
    delete[] path;
    return found;
}

// =========================================================================
// Radix Tree(压缩前缀树)
//   每个节点带一条边标签 label；从根到某节点路径上所有边标签拼成完整词。
//   单孩子中间节点不再单独成层，而是把字符串压进同一条边。
// =========================================================================
struct RadixNode {
    char* label;          // 本条边的标签(非空串，根除外)
    int   len;
    bool  terminal;
    RadixNode* child[26];
    RadixNode() : len(0), terminal(false) {
        label = 0;
        for (int i = 0; i < 26; i++) child[i] = 0;
    }
};

struct Radix { RadixNode* root; };

Radix* radix_create() {
    Radix* r = new Radix;
    r->root = new RadixNode();
    r->root->label = 0;
    return r;
}

static void radix_free(RadixNode* n) {
    if (!n) return;
    for (int i = 0; i < 26; i++) radix_free(n->child[i]);
    delete[] n->label;
    delete n;
}

void radix_destroy(Radix* r) {
    if (!r) return;
    radix_free(r->root);
    delete r;
}

static char* dupn(const char* s, int n) {
    char* o = new char[n + 1];
    for (int i = 0; i < n; i++) o[i] = s[i];
    o[n] = 0;
    return o;
}

void radix_insert(Radix* r, const char* word) {
    if (!r || !word) return;
    // 归一为小写字母串
    char buf[256]; int bl = 0;
    for (const char* p = word; *p && bl < 255; p++) {
        int c = chidx(*p);
        if (c >= 0) buf[bl++] = (char)('a' + c);
    }
    buf[bl] = 0;
    if (bl == 0) return;
    RadixNode* node = r->root;
    int i = 0;
    while (i < bl) {
        int c = buf[i] - 'a';
        if (!node->child[c]) {
            // 新建边，挂剩余整串
            RadixNode* nx = new RadixNode();
            nx->label = dupn(buf + i, bl - i);
            nx->len = bl - i;
            nx->terminal = true;
            node->child[c] = nx;
            return;
        }
        RadixNode* nx = node->child[c];
        // 比较边标签与剩余串的公共前缀长度
        int j = 0;
        while (j < nx->len && i + j < bl && nx->label[j] == buf[i + j]) j++;
        if (j == nx->len) {
            // 整条边匹配完，沿下去
            node = nx;
            i += j;
            if (i == bl) { node->terminal = true; return; }
        } else {
            // 需要在 j 处分裂：新建一个中间节点
            RadixNode* mid = new RadixNode();
            mid->label = dupn(nx->label, j);
            mid->len = j;
            // 原边缩短
            char* rest = dupn(nx->label + j, nx->len - j);
            delete[] nx->label;
            nx->label = rest;
            nx->len -= j;
            mid->child[(uint8_t)nx->label[0] - 'a'] = nx;
            node->child[c] = mid;
            if (i + j == bl) { mid->terminal = true; return; }
            // 把剩余串挂到 mid
            RadixNode* tail = new RadixNode();
            tail->label = dupn(buf + i + j, bl - (i + j));
            tail->len = bl - (i + j);
            tail->terminal = true;
            mid->child[(uint8_t)tail->label[0] - 'a'] = tail;
            return;
        }
    }
    node->terminal = true;
}

static bool radix_walk(Radix* r, const char* word, bool require_terminal) {
    if (!r || !word) return false;
    char buf[256]; int bl = 0;
    for (const char* p = word; *p && bl < 255; p++) {
        int c = chidx(*p);
        if (c >= 0) buf[bl++] = (char)('a' + c);
    }
    buf[bl] = 0;
    RadixNode* node = r->root;
    int i = 0;
    while (i < bl) {
        int c = buf[i] - 'a';
        RadixNode* nx = node->child[c];
        if (!nx) return false;
        for (int j = 0; j < nx->len; j++) {
            if (i + j >= bl) return !require_terminal;   // 前缀到此结束
            if (nx->label[j] != buf[i + j]) return false;
        }
        i += nx->len;
        node = nx;
    }
    return require_terminal ? node->terminal : true;
}

bool radix_contains(Radix* r, const char* word) { return radix_walk(r, word, true); }
bool radix_starts_with(Radix* r, const char* p)  { return radix_walk(r, p, false); }

// =========================================================================
// 自检
// =========================================================================
int trie_self_test() {
    int f = 0;
    Trie* t = trie_create();
    trie_insert(t, "hello");
    trie_insert(t, "hell");
    trie_insert(t, "hat");
    trie_insert(t, "cat");
    trie_insert(t, "hash");
    f += trie_contains(t, "hello") ? 0 : 1;
    f += trie_contains(t, "hell")  ? 0 : 1;
    f += trie_contains(t, "hel")    ? 1 : 0;   // 不是完整词
    f += trie_contains(t, "world") ? 1 : 0;
    // 前缀计数: "he" 下面有 hell/hello/hat? hat 不以 he 开头 -> hell,hello = 2
    f += (trie_count_prefix(t, "he") == 2) ? 0 : 1;
    f += (trie_count_prefix(t, "h") == 4) ? 0 : 1;   // hell/hello/hat/hash = 4
    f += (trie_size(t) == 5) ? 0 : 1;
    // 自动补全
    char* bufs[5]; char storage[5][64];
    for (int i = 0; i < 5; i++) bufs[i] = storage[i];
    int n = trie_autocomplete(t, "he", bufs, 5, 64);
    f += (n == 2) ? 0 : 1;     // hell, hello
    trie_destroy(t);

    // Radix
    Radix* r = radix_create();
    radix_insert(r, "test");
    radix_insert(r, "testing");
    radix_insert(r, "team");
    f += radix_contains(r, "test") ? 0 : 1;
    f += radix_contains(r, "testing") ? 0 : 1;
    f += radix_contains(r, "tea") ? 1 : 0;
    f += radix_starts_with(r, "te") ? 0 : 1;
    f += radix_starts_with(r, "xyz") ? 1 : 0;
    radix_destroy(r);
    return f;
}

} // namespace textproc
} // namespace nefu
