// nefuOS text library — Aho-Corasick implementation
#include "aho.h"

namespace nefu {
namespace text {

void Aho::init() {
    if (!nodes) nodes = new AhoNode[AHO_MAX_NODES];
    for (int i = 0; i < AHO_MAX_NODES; i++) {
        AhoNode& n = nodes[i];
        for (int c = 0; c < 256; c++) n.next[c] = -1;
        n.fail = 0; n.out = -1; n.parent = -1; n.out_len = 0;
    }
    node_count = 1;   // 根节点 0
    pat_count = 0;
}

int Aho::add_pattern(const char* pat) {
    if (pat_count >= AHO_MAX_PATS) return -1;
    int len = 0;
    while (pat[len]) len++;
    if (len <= 0 || len >= AHO_MAX_PAT_LEN) return -1;
    int cur = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)pat[i];
        if (nodes[cur].next[c] < 0) {
            if (node_count >= AHO_MAX_NODES) return -1;
            nodes[cur].next[c] = node_count;
            nodes[node_count].parent = cur;
            node_count++;
        }
        cur = nodes[cur].next[c];
    }
    nodes[cur].out = pat_count;          // 模式终点标记
    nodes[cur].out_len = len;
    for (int i = 0; i < len; i++) pats[pat_count][i] = pat[i];
    pats[pat_count][len] = 0;
    return pat_count++;
}

void Aho::build() {
    // 宽度优先建立 fail 指针
    int* queue = new int[node_count];
    int head = 0, tail = 0;
    for (int c = 0; c < 256; c++) {
        int v = nodes[0].next[c];
        if (v >= 0) { nodes[v].fail = 0; queue[tail++] = v; }
        else nodes[0].next[c] = 0;       // 根节点补全 goto
    }
    while (head < tail) {
        int u = queue[head++];
        for (int c = 0; c < 256; c++) {
            int v = nodes[u].next[c];
            if (v < 0) {
                nodes[u].next[c] = nodes[nodes[u].fail].next[c];   // 转移压缩
            } else {
                nodes[v].fail = nodes[nodes[u].fail].next[c];
                queue[tail++] = v;
            }
        }
    }
    delete[] queue;
}

int Aho::find_all(const char* text, AhoMatch* out, int out_cap) {
    int cnt = 0;
    int cur = 0;
    for (int i = 0; text[i] && cnt < out_cap; i++) {
        unsigned char c = (unsigned char)text[i];
        cur = nodes[cur].next[c];
        // 沿 fail 链收集全部命中（如 she 终点同时命中 he）
        int p = cur;
        while (p != 0 && cnt < out_cap) {
            if (nodes[p].out >= 0) {
                out[cnt].pat_id = nodes[p].out;
                out[cnt].pos = i - nodes[p].out_len + 1;   // 起点下标
                cnt++;
            }
            p = nodes[p].fail;
        }
    }
    return cnt;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) g_fails++; (void)what; }
} // namespace

int aho_self_test() {
    g_fails = 0;
    Aho a;
    a.init();
    int id0 = a.add_pattern("he");
    int id1 = a.add_pattern("she");
    int id2 = a.add_pattern("hers");
    expect("aho-add", id0 == 0 && id1 == 1 && id2 == 2);
    a.build();
    AhoMatch out[32];
    int n = a.find_all("ushers", out, 32);
    // ushers = u s h e r s：she@1, he@2, hers@2
    expect("aho-count", n == 3);
    bool found[3] = {false, false, false};
    for (int i = 0; i < n; i++) {
        if (out[i].pat_id == 1 && out[i].pos == 1) found[0] = true;
        if (out[i].pat_id == 0 && out[i].pos == 2) found[1] = true;
        if (out[i].pat_id == 2 && out[i].pos == 2) found[2] = true;
    }
    expect("aho-pos", found[0] && found[1] && found[2]);
    // 无命中
    Aho b;
    b.init();
    b.add_pattern("xyz");
    b.build();
    expect("aho-none", b.find_all("abcabc", out, 8) == 0);
    // 敏感词场景
    Aho c;
    c.init();
    c.add_pattern("bad");
    c.add_pattern("word");
    c.build();
    expect("aho-filter", c.find_all("this has bad word here", out, 16) == 2);
    return g_fails;
}

} // namespace text
} // namespace nefu
