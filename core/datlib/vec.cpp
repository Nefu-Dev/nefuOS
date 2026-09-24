// nefuOS data-types library — vec self test & extra helpers
// 本文件提供 vec 的宿主自测（在宿主环境实例化 int/指针 两种模板），
// 以及若干便捷工具函数（仅 int 特化版，避免模板膨胀）。
#include "vec.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace dt {

// ---- 便捷工具（非模板，供终端命令等使用）----

// 从逗号分隔字符串解析整数列表到 vec<int>；返回成功解析个数
int vec_parse_ints(const char* s, vec<int>& out) {
    out.clear();
    int cur = 0;
    bool have = false;
    for (int i = 0; ; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); have = true; }
        else if (c == '-' && !have) { cur = 0; have = true; /* 负号标记：简单处理 */ }
        else {
            if (have) { out.push_back(cur); cur = 0; have = false; }
            if (c == 0) break;
        }
    }
    return out.size();
}

// 求最大子段和（Kadane 算法）；返回 {和, 起点, 终点}
// 经典 O(n) 动态规划：要么接续前面，要么重新开始。
struct MaxSubarray { int sum; int start; int end; };

MaxSubarray vec_max_subarray(const vec<int>& v) {
    MaxSubarray best = {0, -1, -1};
    int cur = 0, s = 0;
    for (int i = 0; i < v.size(); i++) {
        if (cur + v[i] > v[i]) { cur += v[i]; }
        else { cur = v[i]; s = i; }
        if (cur > best.sum) { best.sum = cur; best.start = s; best.end = i; }
    }
    if (best.start < 0) { best.sum = 0; best.start = 0; best.end = -1; }
    return best;
}

// 原地去重（要求已升序）；返回去重后长度
int vec_unique_sorted(vec<int>& v) {
    if (v.size() <= 1) return v.size();
    int w = 1;
    for (int i = 1; i < v.size(); i++) {
        if (v[i] != v[w - 1]) v[w++] = v[i];
    }
    return w;
}

// 合并两个已排序数组到 out（归并）
void vec_merge_sorted(const vec<int>& a, const vec<int>& b, vec<int>& out) {
    out.clear();
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] <= b[j]) out.push_back(a[i++]); else out.push_back(b[j++]);
    }
    while (i < a.size()) out.push_back(a[i++]);
    while (j < b.size()) out.push_back(b[j++]);
}

// 向量点积（int 版）；长度不等返回 0
int vec_dot(const vec<int>& a, const vec<int>& b) {
    int n = a.size() < b.size() ? a.size() : b.size();
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int vec_self_test() {
    g_fails = 0;
    {
        vec<int> v;
        expect("v-empty", v.empty() && v.size() == 0);
        v.push_back(10); v.push_back(20); v.push_back(30);
        expect("v-size", v.size() == 3);
        expect("v-cap", v.capacity() >= 3);
        expect("v-idx", v[0] == 10 && v[1] == 20 && v[2] == 30);
        expect("v-front-back", v.front() == 10 && v.back() == 30);
        v.pop_back();
        expect("v-pop", v.size() == 2 && v.back() == 20);
        v.insert(1, 15);
        expect("v-insert", v.size() == 3 && v[1] == 15);
        v.erase(1);
        expect("v-erase", v.size() == 2 && v[1] == 20);
        expect("v-index", v.index_of(20) == 1 && v.index_of(99) == -1);
        // 拷贝构造深拷贝
        vec<int> c = v;
        c.push_back(77);
        expect("v-copy", v.size() == 2 && c.size() == 3);
        // 排序 + 二分
        vec<int> s;
        s.push_back(5); s.push_back(1); s.push_back(9); s.push_back(3); s.push_back(7);
        s.sort_asc();
        expect("v-sort", s[0] == 1 && s[4] == 9);
        expect("v-bsearch", s.binary_search(7) == 3 && s.binary_search(2) == -1);
        // 反转
        s.reverse();
        expect("v-reverse", s[0] == 9 && s[4] == 1);
        // 迭代
        int sum = 0;
        for (vec<int>::iterator it = s.begin(); it != s.end(); ++it) sum += *it;
        expect("v-iter", sum == 25);
        // 计数
        v.push_back(20);
        expect("v-count", v.count(20) == 2);
    }
    {
        // 工具函数
        vec<int> a; vec_parse_ints("1,2,3,10", a);
        expect("v-parse", a.size() == 4 && a[3] == 10);
        vec<int> b;
        b.push_back(-2); b.push_back(1); b.push_back(-3); b.push_back(4); b.push_back(-1); b.push_back(2); b.push_back(1);
        MaxSubarray m = vec_max_subarray(b);
        expect("v-kadane", m.sum == 6 && m.start == 3 && m.end == 6);
        vec<int> u;
        u.push_back(1); u.push_back(1); u.push_back(2); u.push_back(3); u.push_back(3); u.push_back(3);
        int un = vec_unique_sorted(u);
        expect("v-unique", un == 3 && u[0] == 1 && u[1] == 2 && u[2] == 3);
        vec<int> m1, m2, mo;
        m1.push_back(1); m1.push_back(3); m1.push_back(5);
        m2.push_back(2); m2.push_back(4); m2.push_back(6);
        vec_merge_sorted(m1, m2, mo);
        expect("v-merge", mo.size() == 6 && mo[0] == 1 && mo[5] == 6);
        vec<int> d1, d2;
        d1.push_back(1); d1.push_back(2); d1.push_back(3);
        d2.push_back(4); d2.push_back(5);
        expect("v-dot", vec_dot(d1, d2) == 4 + 10);
    }
    {
        // 指针模板实例化（保证模板代码路径完整编译）
        vec<int*> pv;
        int x = 1, y = 2;
        pv.push_back(&x); pv.push_back(&y);
        expect("v-ptr", pv.size() == 2 && *pv[1] == 2);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
