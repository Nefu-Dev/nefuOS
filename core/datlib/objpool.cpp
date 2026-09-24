// nefuOS data-types library — objpool implementation & helpers
#include "objpool.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 用对象池模拟"事件缓冲"：生产 n 个事件（写入值），消费读回并释放。
// 返回成功消费数（超过池容量会失败部分）。
struct PoolEvent {
    int kind;
    int data;
};

int objpool_event_demo(int capacity, int n, PoolEvent* in, PoolEvent* out) {
    objpool pool(capacity);
    PoolEvent* slots[256];
    int got = 0;
    for (int i = 0; i < n && i < 256; i++) {
        PoolEvent* e = (PoolEvent*)pool.alloc_ptr();
        if (!e) break;                    // 池空
        e->kind = in[i].kind;
        e->data = in[i].data;
        slots[got++] = e;
    }
    int consumed = 0;
    for (int i = 0; i < got; i++) {
        out[i].kind = slots[i]->kind;
        out[i].data = slots[i]->data;
        pool.free_ptr(slots[i]);
        consumed++;
    }
    return consumed;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int objpool_self_test() {
    g_fails = 0;
    {
        objpool pool(4);
        expect("op-cap", pool.capacity() == 4);
        expect("op-free", pool.free_count() == 4 && pool.used_count() == 0);
        // 全部取出
        void* a = pool.alloc_ptr();
        void* b = pool.alloc_ptr();
        void* c = pool.alloc_ptr();
        void* d = pool.alloc_ptr();
        expect("op-all", a && b && c && d);
        expect("op-free2", pool.free_count() == 0 && pool.used_count() == 4);
        // 池空
        expect("op-empty", pool.alloc_ptr() == 0);
        // 写入读回
        *(int*)a = 42;
        *(int*)b = 43;
        expect("op-io", *(int*)a == 42 && *(int*)b == 43);
        // 归还后复用
        pool.free_ptr(b);
        expect("op-free3", pool.free_count() == 1);
        void* e = pool.alloc_ptr();
        expect("op-reuse", e != 0 && pool.used_count() == 4);
        pool.free_ptr(a);
        pool.free_ptr(c);
        pool.free_ptr(d);
        pool.free_ptr(e);
        expect("op-drain", pool.free_count() == 4);
    }
    {
        // 事件缓冲演示
        PoolEvent in[8], out[8];
        for (int i = 0; i < 8; i++) { in[i].kind = i; in[i].data = i * 100; }
        int n = objpool_event_demo(3, 8, in, out);
        expect("op-events", n == 3);       // 池容量 3，只能缓冲 3 个
        bool ok = true;
        for (int i = 0; i < n; i++) if (out[i].kind != i || out[i].data != i * 100) ok = false;
        expect("op-events-v", ok);
        // 容量足够时全部成功
        int n2 = objpool_event_demo(16, 8, in, out);
        expect("op-events2", n2 == 8);
        ok = true;
        for (int i = 0; i < n2; i++) if (out[i].kind != i) ok = false;
        expect("op-events2-v", ok);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
