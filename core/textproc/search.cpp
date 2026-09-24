// nefuOS 文本处理库 —— 多模式搜索 / 回文 / 后缀结构(实现)
// 见 search.h。所有结果数组用 new[] 分配，调用方 delete[]。
#include "search.h"
#include "../klib/klib.h"

namespace nefu {
namespace textproc {

static inline int slen(const char* s) { return s ? (int)strlen(s) : 0; }

// 小工具：动态整型数组(手动扩容，替代 std::vector)
struct IntVec {
    int* d;
    int n, cap;
    IntVec() : d(0), n(0), cap(0) {}
    ~IntVec() { delete[] d; }
    void push(int v) {
        if (n >= cap) {
            int nc = cap > 0 ? cap * 2 : 8;
            int* nd = new int[nc];
            for (int i = 0; i < n; i++) nd[i] = d[i];
            delete[] d;
            d = nd; cap = nc;
        }
        d[n++] = v;
    }
    int* take() {           // 交出所有权，内部指针置空
        int* r = d; d = 0; n = 0; cap = 0; return r;
    }
};

// =========================================================================
// KMP
// =========================================================================
void kmp_build_failure(const char* pat, int* f) {
    int m = slen(pat);
    if (m == 0) return;
    f[0] = 0;
    for (int i = 1; i < m; i++) {
        int j = f[i - 1];
        while (j > 0 && pat[i] != pat[j]) j = f[j - 1];
        if (pat[i] == pat[j]) j++;
        f[i] = j;
    }
}

int* kmp_search(const char* pat, const char* text, int* out_count) {
    int m = slen(pat), n = slen(text);
    IntVec hits;
    if (m == 0 || n < m) { *out_count = 0; return hits.take(); }
    int* f = new int[m];
    kmp_build_failure(pat, f);
    int j = 0;
    for (int i = 0; i < n; i++) {
        while (j > 0 && text[i] != pat[j]) j = f[j - 1];
        if (text[i] == pat[j]) j++;
        if (j == m) { hits.push(i - m + 1); j = f[j - 1]; }
    }
    delete[] f;
    *out_count = hits.n;
    return hits.take();
}

int kmp_first(const char* pat, const char* text) {
    int c;
    int* r = kmp_search(pat, text, &c);
    int v = c > 0 ? r[0] : -1;
    delete[] r;
    return v;
}

// =========================================================================
// Boyer-Moore(坏字符规则，保守移位保证不漏匹配)
// =========================================================================
int* bm_search(const char* pat, const char* text, int* out_count) {
    int m = slen(pat), n = slen(text);
    IntVec hits;
    if (m == 0 || n < m) { *out_count = 0; return hits.take(); }
    // last[c] = c 在模式中最右出现的下标(-1 表示未出现)
    int last[256];
    for (int c = 0; c < 256; c++) last[c] = -1;
    for (int r = 0; r < m; r++) last[(uint8_t)pat[r]] = r;
    int i = 0;
    while (i <= n - m) {
        int j = m - 1;
        while (j >= 0 && pat[j] == text[i + j]) j--;
        if (j < 0) {                     // 完整命中
            hits.push(i);
            i++;                         // 允许重叠命中
            continue;
        }
        int bad = (uint8_t)text[i + j];
        int r = last[bad];
        int shift = (r >= 0 && r < j) ? (j - r) : 1;
        if (shift < 1) shift = 1;
        i += shift;
    }
    *out_count = hits.n;
    return hits.take();
}

// =========================================================================
// Boyer-Moore-Horspool(只看窗口最后一个字符)
// =========================================================================
int* horspool_search(const char* pat, const char* text, int* out_count) {
    int m = slen(pat), n = slen(text);
    IntVec hits;
    if (m == 0 || n < m) { *out_count = 0; return hits.take(); }
    int bc[256];
    for (int c = 0; c < 256; c++) bc[c] = m;       // 不在模式里 -> 整段跳过
    for (int r = 0; r < m - 1; r++) bc[(uint8_t)pat[r]] = m - 1 - r;
    int i = 0;
    while (i <= n - m) {
        int j = m - 1;
        while (j >= 0 && pat[j] == text[i + j]) j--;
        if (j < 0) { hits.push(i); i++; }
        else i += bc[(uint8_t)text[i + m - 1]];    // 按窗口末字符移位
    }
    *out_count = hits.n;
    return hits.take();
}

// =========================================================================
// Rabin-Karp(滚动哈希 + 精确校验防碰撞)
// =========================================================================
static const unsigned long long RK_MOD = 1000000007ULL;
static const unsigned long long RK_BASE = 257ULL;

int* rabin_karp_search(const char* pat, const char* text, int* out_count) {
    int m = slen(pat), n = slen(text);
    IntVec hits;
    if (m == 0 || n < m) { *out_count = 0; return hits.take(); }
    // 计算模式哈希与最高位幂次
    unsigned long long ph = 0, th = 0, h = 1;
    for (int i = 0; i < m - 1; i++) h = (h * RK_BASE) % RK_MOD;
    for (int i = 0; i < m; i++) {
        ph = (ph * RK_BASE + (uint8_t)pat[i]) % RK_MOD;
        th = (th * RK_BASE + (uint8_t)text[i]) % RK_MOD;
    }
    for (int i = 0; i <= n - m; i++) {
        if (ph == th) {
            // 哈希相等仍逐字符校验，杜绝碰撞误判
            bool ok = true;
            for (int k = 0; k < m; k++) if (text[i + k] != pat[k]) { ok = false; break; }
            if (ok) hits.push(i);
        }
        if (i < n - m) {
            th = (th - ((unsigned long long)(uint8_t)text[i]) * h % RK_MOD + RK_MOD) % RK_MOD;
            th = (th * RK_BASE + (uint8_t)text[i + m]) % RK_MOD;
        }
    }
    *out_count = hits.n;
    return hits.take();
}

// =========================================================================
// Z 算法
// =========================================================================
void z_build(const char* s, int* z) {
    int n = slen(s);
    z[0] = n;
    int l = 0, r = 0;
    for (int i = 1; i < n; i++) {
        if (i <= r) z[i] = (r - i + 1) < (z[i - l]) ? (r - i + 1) : z[i - l];
        else z[i] = 0;
        while (i + z[i] < n && s[z[i]] == s[i + z[i]]) z[i]++;
        if (i + z[i] - 1 > r) { l = i; r = i + z[i] - 1; }
    }
}

int* z_search(const char* pat, const char* text, int* out_count) {
    int m = slen(pat), n = slen(text);
    IntVec hits;
    if (m == 0 || n < m) { *out_count = 0; return hits.take(); }
    // 拼接: pat + '\1' + text
    int tot = m + 1 + n;
    char* s = new char[tot + 1];
    for (int i = 0; i < m; i++) s[i] = pat[i];
    s[m] = 1;
    for (int i = 0; i < n; i++) s[m + 1 + i] = text[i];
    s[tot] = 0;
    int* z = new int[tot];
    z_build(s, z);
    for (int i = m + 1; i < tot; i++)
        if (z[i] == m) hits.push(i - (m + 1));
    delete[] s;
    delete[] z;
    *out_count = hits.n;
    return hits.take();
}

// =========================================================================
// Manacher：最长回文子串
// =========================================================================
int manacher_longest_len(const char* s) {
    int n = slen(s);
    if (n == 0) return 0;
    // 构造带分隔符的串: ^#a#b#...$ (这里用 sentinel 0..255 不易冲突的字符)
    int Tlen = 2 * n + 3;
    char* t = new char[Tlen];
    int* P = new int[Tlen];
    t[0] = '^';
    for (int i = 0; i < n; i++) { t[1 + 2 * i] = '#'; t[2 + 2 * i] = s[i]; }
    t[2 * n + 1] = '#';
    t[2 * n + 2] = '$';
    int C = 0, R = 0, best = 0;
    for (int i = 1; i < Tlen - 1; i++) {
        int mirr = 2 * C - i;
        if (i < R) P[i] = (R - i) < P[mirr] ? (R - i) : P[mirr];
        else P[i] = 0;
        while (t[i + P[i] + 1] == t[i - P[i] - 1]) P[i]++;
        if (i + P[i] > R) { C = i; R = i + P[i]; }
        if (P[i] > best) best = P[i];
    }
    delete[] t;
    delete[] P;
    return best;
}

char* manacher_longest(const char* s) {
    int n = slen(s);
    if (n == 0) { char* e = new char[1]; e[0] = 0; return e; }
    int Tlen = 2 * n + 3;
    char* t = new char[Tlen];
    int* P = new int[Tlen];
    t[0] = '^';
    for (int i = 0; i < n; i++) { t[1 + 2 * i] = '#'; t[2 + 2 * i] = s[i]; }
    t[2 * n + 1] = '#';
    t[2 * n + 2] = '$';
    int C = 0, R = 0, best = 0, center = 0;
    for (int i = 1; i < Tlen - 1; i++) {
        int mirr = 2 * C - i;
        if (i < R) P[i] = (R - i) < P[mirr] ? (R - i) : P[mirr];
        else P[i] = 0;
        while (t[i + P[i] + 1] == t[i - P[i] - 1]) P[i]++;
        if (i + P[i] > R) { C = i; R = i + P[i]; }
        if (P[i] > best) { best = P[i]; center = i; }
    }
    // 还原回文在原串中的起点与长度
    int start = (center - best) / 2;
    char* out = new char[best + 1];
    for (int k = 0; k < best; k++) out[k] = s[start + k];
    out[best] = 0;
    delete[] t;
    delete[] P;
    return out;
}

// =========================================================================
// Aho-Corasick 多模式自动机
// =========================================================================
struct AcNode {
    int child[128];
    int fail;
    int outPat;     // 若本节点是某模式终点，记录其下标；否则 -1
    int outLink;    // fail 链上第一个终点
    AcNode() : fail(0), outPat(-1), outLink(-1) {
        for (int c = 0; c < 128; c++) child[c] = -1;
    }
};

int* ac_search(const char* const* pats, int npat, const char* text,
               int* out_pair_count) {
    IntVec pairs;
    int n = slen(text);
    if (npat <= 0 || n == 0) { *out_pair_count = 0; return pairs.take(); }
    // 估算节点上限：所有模式长度之和 + 1
    int capNodes = 1;
    for (int p = 0; p < npat; p++) capNodes += slen(pats[p]);
    AcNode* nodes = new AcNode[capNodes];
    int nodeCount = 1;             // 0 = root
    int* patLen = new int[npat];
    // 1) 插入所有模式
    for (int p = 0; p < npat; p++) {
        const char* pat = pats[p];
        int m = slen(pat);
        patLen[p] = m;
        int cur = 0;
        for (int k = 0; k < m; k++) {
            int c = (uint8_t)pat[k];
            if (nodes[cur].child[c] < 0) {
                nodes[cur].child[c] = nodeCount++;
            }
            cur = nodes[cur].child[c];
        }
        nodes[cur].outPat = p;
    }
    // 2) BFS 构造 fail 指针
    int* queue = new int[capNodes];
    int qh = 0, qt = 0;
    // root 的孩子 fail 指向 root
    for (int c = 0; c < 128; c++) {
        int nx = nodes[0].child[c];
        if (nx >= 0) { nodes[nx].fail = 0; queue[qt++] = nx; }
    }
    while (qh < qt) {
        int u = queue[qh++];
        // outLink：沿 fail 找第一个终点
        int f = nodes[u].fail;
        nodes[u].outLink = (nodes[f].outPat >= 0) ? f : nodes[f].outLink;
        for (int c = 0; c < 128; c++) {
            int v = nodes[u].child[c];
            if (v < 0) continue;
            // fail[v] = goto(fail[u], c)
            int ft = nodes[u].fail;
            while (ft != 0 && nodes[ft].child[c] < 0) ft = nodes[ft].fail;
            nodes[v].fail = (nodes[ft].child[c] >= 0) ? nodes[ft].child[c] : 0;
            queue[qt++] = v;
        }
    }
    // 3) 扫描文本
    int cur = 0;
    for (int i = 0; i < n; i++) {
        int c = (uint8_t)text[i];
        while (cur != 0 && nodes[cur].child[c] < 0) cur = nodes[cur].fail;
        cur = (nodes[cur].child[c] >= 0) ? nodes[cur].child[c] : 0;
        // 沿输出链收集所有命中
        for (int v = cur; v != -1; v = nodes[v].outLink) {
            int pi = nodes[v].outPat;
            if (pi >= 0) pairs.push(pi), pairs.push(i - patLen[pi] + 1);
        }
    }
    delete[] nodes;
    delete[] queue;
    delete[] patLen;
    *out_pair_count = pairs.n;
    return pairs.take();
}

// =========================================================================
// 后缀数组(倍增法 + 比较排序) + Kasai LCP
// =========================================================================
int* suffix_array(const char* s) {
    int n = slen(s);
    int* sa = new int[n];
    int* rank = new int[n];
    for (int i = 0; i < n; i++) { sa[i] = i; rank[i] = (uint8_t)s[i]; }
    if (n <= 1) { delete[] rank; return sa; }
    // 倍增：用 (rank[i], rank[i+k]) 作为比较键
    for (int k = 1; k < n; k <<= 1) {
        // 对 sa 按 pair 做插入排序(教学实现；n 较大时应换计数排序)
        for (int i = 1; i < n; i++) {
            int key = sa[i];
            int kr0 = rank[key];
            int kr1 = (key + k < n) ? rank[key + k] : -1;
            int j = i - 1;
            while (j >= 0) {
                int o = sa[j];
                int or0 = rank[o];
                int or1 = (o + k < n) ? rank[o + k] : -1;
                bool greater = (or0 > kr0) || (or0 == kr0 && or1 > kr1);
                if (!greater) break;
                sa[j + 1] = sa[j];
                j--;
            }
            sa[j + 1] = key;
        }
        // 重算 rank
        int* nr = new int[n];
        nr[sa[0]] = 0;
        for (int i = 1; i < n; i++) {
            int a = sa[i - 1], b = sa[i];
            int a1 = (a + k < n) ? rank[a + k] : -1;
            int b1 = (b + k < n) ? rank[b + k] : -1;
            nr[b] = nr[a] + ((rank[a] != rank[b] || a1 != b1) ? 1 : 0);
        }
        for (int i = 0; i < n; i++) rank[i] = nr[i];
        delete[] nr;
        if (rank[sa[n - 1]] == n - 1) break;   // 已经全序
    }
    delete[] rank;
    return sa;
}

int* lcp_array(const char* s, const int* sa) {
    int n = slen(s);
    int* lcp = new int[n];         // lcp[0] 未用(0)
    int* rank = new int[n];
    for (int i = 0; i < n; i++) rank[sa[i]] = i;
    int h = 0;
    for (int i = 0; i < n; i++) {
        if (rank[i] > 0) {
            int j = sa[rank[i] - 1];
            while (i + h < n && j + h < n && s[i + h] == s[j + h]) h++;
            lcp[rank[i]] = h;
            if (h > 0) h--;
        } else lcp[i] = 0;
    }
    lcp[0] = 0;
    delete[] rank;
    return lcp;
}

// =========================================================================
// 简化后缀自动机 SAM
//   最多 2n 个状态，转移用线性数组(ASCII 256)。
// =========================================================================
struct SamState {
    int len;      // 该状态代表的最长等价类长度
    int link;     // 后缀链接
    int next[256];
    SamState() : len(0), link(-1) { for (int c = 0; c < 256; c++) next[c] = -1; }
};

struct Sam {
    SamState* st;
    int size;
    int last;
    SamState& operator[](int i) { return st[i]; }
};

static void sam_init(Sam& sm, int maxStates) {
    sm.st = new SamState[maxStates];
    sm.size = 1;
    sm.last = 0;
}

static void sam_extend(Sam& sm, uint8_t c) {
    int cur = sm.size++;
    sm[cur].len = sm[sm.last].len + 1;
    int p = sm.last;
    while (p != -1 && sm[p].next[c] < 0) { sm[p].next[c] = cur; p = sm[p].link; }
    if (p == -1) sm[cur].link = 0;
    else {
        int q = sm[p].next[c];
        if (sm[p].len + 1 == sm[q].len) sm[cur].link = q;
        else {
            int clone = sm.size++;
            sm[clone].len = sm[p].len + 1;
            for (int ch = 0; ch < 256; ch++) sm[clone].next[ch] = sm[q].next[ch];
            sm[clone].link = sm[q].link;
            while (p != -1 && sm[p].next[c] == q) { sm[p].next[c] = clone; p = sm[p].link; }
            sm[q].link = clone;
            sm[cur].link = clone;
        }
    }
    sm.last = cur;
}

bool sam_contains(const char* text, const char* query) {
    int n = slen(text), m = slen(query);
    if (m == 0) return true;
    if (n == 0) return false;
    Sam sm;
    sam_init(sm, 2 * n + 1);
    for (int i = 0; i < n; i++) sam_extend(sm, (uint8_t)text[i]);
    int cur = 0;
    for (int i = 0; i < m; i++) {
        int nx = sm[cur].next[(uint8_t)query[i]];
        if (nx < 0) { delete[] sm.st; return false; }
        cur = nx;
    }
    delete[] sm.st;
    return true;
}

long sam_distinct_substrings(const char* text) {
    int n = slen(text);
    if (n == 0) return 0;
    Sam sm;
    sam_init(sm, 2 * n + 1);
    for (int i = 0; i < n; i++) sam_extend(sm, (uint8_t)text[i]);
    long total = 0;
    for (int i = 1; i < sm.size; i++) total += (long)(sm[i].len - sm[sm[i].link].len);
    delete[] sm.st;
    return total;
}

int sam_count_occurrences(const char* text, const char* query) {
    int n = slen(text), m = slen(query);
    if (m == 0) return n;
    if (n == 0) return 0;
    Sam sm;
    sam_init(sm, 2 * n + 1);
    int* cnt = new int[2 * n + 1];
    for (int i = 0; i < 2 * n + 1; i++) cnt[i] = 0;
    for (int i = 0; i < n; i++) {
        sam_extend(sm, (uint8_t)text[i]);
        cnt[sm.last] = 1;          // 每次新增的非克隆状态出现一次
    }
    // 按 len 桶排序状态(计数)，再沿 link 从长到短传播 cnt
    int sz = sm.size;
    int* bucket = new int[n + 1];
    for (int i = 0; i <= n; i++) bucket[i] = 0;
    for (int i = 0; i < sz; i++) bucket[sm[i].len]++;
    for (int i = 1; i <= n; i++) bucket[i] += bucket[i - 1];
    int* order = new int[sz];
    for (int i = sz - 1; i >= 0; i--) order[--bucket[sm[i].len]] = i;
    for (int i = sz - 1; i >= 1; i--) {
        int v = order[i];
        cnt[sm[v].link] += cnt[v];
    }
    // 沿 query 走到对应状态
    int cur = 0;
    for (int i = 0; i < m; i++) {
        int nx = sm[cur].next[(uint8_t)query[i]];
        if (nx < 0) { delete[] sm.st; delete[] cnt; delete[] bucket; delete[] order; return 0; }
        cur = nx;
    }
    int r = cnt[cur];
    delete[] sm.st;
    delete[] cnt;
    delete[] bucket;
    delete[] order;
    return r;
}

// =========================================================================
// Shift-Or(bitap)：模式 <= 63，位并行
//   mask[c] 的第 j 位为 0 表示 pat[j]==c。
// =========================================================================
int* shift_or_search(const char* pat, const char* text, int* out_count) {
    int m = slen(pat), n = slen(text);
    IntVec hits;
    if (m == 0 || n < m || m > 63) { *out_count = 0; return hits.take(); }
    // Shift-AND(位并行)：Mask[c] 的第 j 位为 1 表示 pat[j]==c。
    unsigned long long Mask[256];
    for (int c = 0; c < 256; c++) Mask[c] = 0;
    for (int j = 0; j < m; j++) Mask[(uint8_t)pat[j]] |= (1ULL << j);
    unsigned long long R = 0;
    unsigned long long full = 1ULL << (m - 1);
    for (int i = 0; i < n; i++) {
        R = ((R << 1) | 1ULL) & Mask[(uint8_t)text[i]];
        if (R & full) hits.push(i - m + 1);
    }
    *out_count = hits.n;
    return hits.take();
}

// =========================================================================
// 自检
// =========================================================================
static bool eq_int_arr(const int* a, int na, const int* b, int nb) {
    if (na != nb) return false;
    for (int i = 0; i < na; i++) if (a[i] != b[i]) return false;
    return true;
}

int search_self_test() {
    int f = 0, c;
    // KMP 已知值: "ABABC" in "ABABABC" -> 起始下标 2
    {
        int* r = kmp_search("ABABC", "ABABABC", &c);
        f += (c == 1 && r[0] == 2) ? 0 : 1;
        delete[] r;
        f += (kmp_first("ABABC", "ABABABC") == 2) ? 0 : 1;
        f += (kmp_first("xyz", "abcabc") == -1) ? 0 : 1;
    }
    // KMP 多次命中
    {
        int* r = kmp_search("ab", "ababab", &c);
        int want[3] = {0, 2, 4};
        f += (c == 3 && eq_int_arr(r, c, want, 3)) ? 0 : 1;
        delete[] r;
    }
    // BM / Horspool / RabinKarp / Z 都应找到同一处
    {
        int* r1 = bm_search("ABABC", "ABABABC", &c);
        f += (c == 1 && r1[0] == 2) ? 0 : 1; delete[] r1;
        int* r2 = horspool_search("ABABC", "ABABABC", &c);
        f += (c == 1 && r2[0] == 2) ? 0 : 1; delete[] r2;
        int* r3 = rabin_karp_search("ABABC", "ABABABC", &c);
        f += (c == 1 && r3[0] == 2) ? 0 : 1; delete[] r3;
        int* r4 = z_search("ABABC", "ABABABC", &c);
        f += (c == 1 && r4[0] == 2) ? 0 : 1; delete[] r4;
    }
    // Z 数组自身校验
    {
        int z[8];
        z_build("aabaab", z);
        // z[0]=6, "aabaab" 自身; 位置3 也是 "aabaab" -> z[3]=3
        f += (z[0] == 6 && z[3] == 3) ? 0 : 1;
    }
    // Manacher
    {
        f += (manacher_longest_len("abba") == 4) ? 0 : 1;
        f += (manacher_longest_len("abcba") == 5) ? 0 : 1;
        char* p = manacher_longest("abba");
        f += (strcmp(p, "abba") == 0) ? 0 : 1;
        delete[] p;
    }
    // Aho-Corasick
    {
        const char* pats[3] = {"he", "she", "his"};
        int* pr = ac_search(pats, 3, "ushers", &c);
        // "ushers": she 在 1..3 (下标1), he 在 2..3(下标2)
        bool ok = false;
        for (int i = 0; i + 1 < c; i += 2) {
            if (pr[i] == 1 && pr[i + 1] == 1) ok = true;       // "she" @1
            if (pr[i] == 0 && pr[i + 1] == 2) ok = true;       // "he"  @2
        }
        f += ok ? 0 : 1;
        delete[] pr;
    }
    // 后缀数组: "banana" 的最小后缀是 "a"(5), "ana"(3), "anana"(1) ...
    {
        int* sa = suffix_array("banana");
        // sa[0] 必为最后那个 'a'(下标5)，因为它字典序最小
        f += (sa[0] == 5) ? 0 : 1;
        delete[] sa;
    }
    // LCP 不崩溃且长度正确
    {
        int* sa = suffix_array("ababa");
        int* lcp = lcp_array("ababa", sa);
        f += (lcp[0] == 0) ? 0 : 1;
        delete[] sa;
        delete[] lcp;
    }
    // SAM
    {
        f += sam_contains("abcde", "bcd") ? 0 : 1;
        f += sam_contains("abcde", "xyz") ? 1 : 0;
        // "aaa" 的不同子串: a,aa,aaa = 3
        f += (sam_distinct_substrings("aaa") == 3) ? 0 : 1;
        // 出现次数: "ab" 在 "ababab" 中重叠出现 3 次
        f += (sam_count_occurrences("ababab", "ab") == 3) ? 0 : 1;
        f += (sam_count_occurrences("aaaa", "aa") == 3) ? 0 : 1;
    }
    // Shift-Or
    {
        int* r = shift_or_search("ABABC", "ABABABC", &c);
        f += (c == 1 && r[0] == 2) ? 0 : 1;
        delete[] r;
    }
    return f;
}

} // namespace textproc
} // namespace nefu
