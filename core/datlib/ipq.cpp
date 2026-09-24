// nefuOS data-types library — ipq implementation & helpers
#include "ipq.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// Dijkstra 最短路演示：邻接表（静态数组），返回从 0 到各点最短路。
// 图：n 点，edges 为 {u,v,w} 三元组数组。dist 输出（-1 = 不可达）。
// 经典 O((V+E) log V)，索引优先队列是核心（decrease-key）。
void ipq_dijkstra(int n, const int edges[][3], int ecount, int src, int* dist) {
    const int INF = 0x3f3f3f3f;
    // 邻接表：固定上限简化（每个点最多 8 条边，教学规模足够）
    struct Edge { int to, w; };
    Edge adj[64][8];
    int deg[64] = {0};
    for (int i = 0; i < ecount && i < 64; i++) {
        int u = edges[i][0], v = edges[i][1], w = edges[i][2];
        if (u < 0 || u >= n || v < 0 || v >= n) continue;
        // 边表 {u,v,w} 视为无向边：双向存邻接
        if (deg[u] < 8) { adj[u][deg[u]].to = v; adj[u][deg[u]].w = w; deg[u]++; }
        if (deg[v] < 8) { adj[v][deg[v]].to = u; adj[v][deg[v]].w = w; deg[v]++; }
    }
    for (int i = 0; i < n; i++) dist[i] = INF;
    dist[src] = 0;
    ipq q;
    q.push(src, 0);
    while (!q.empty()) {
        int u, d;
        q.pop_min(u, d);
        for (int k = 0; k < deg[u]; k++) {
            int v = adj[u][k].to, w = adj[u][k].w;
            if (d + w < dist[v]) {
                dist[v] = d + w;
                if (q.contains(v)) q.decrease_key(v, dist[v]);
                else q.push(v, dist[v]);
            }
        }
    }
    for (int i = 0; i < n; i++) if (dist[i] == INF) dist[i] = -1;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int ipq_self_test() {
    g_fails = 0;
    {
        ipq q;
        expect("ipq-empty", q.empty());
        q.push(3, 30);
        q.push(1, 10);
        q.push(2, 20);
        expect("ipq-size", q.size() == 3);
        int id = 0, prio = 0;
        expect("ipq-min", q.pop_min(id, prio) && id == 1 && prio == 10);
        expect("ipq-min2", q.pop_min(id, prio) && id == 2 && prio == 20);
        expect("ipq-min3", q.pop_min(id, prio) && id == 3 && prio == 30);
        expect("ipq-empty2", q.empty() && !q.pop_min(id, prio));
        // decrease-key 场景
        q.push(5, 50);
        q.push(6, 60);
        q.push(7, 70);
        q.decrease_key(7, 1);       // 7 变最小
        expect("ipq-dk", q.pop_min(id, prio) && id == 7 && prio == 1);
        q.decrease_key(6, 70);      // 70 > 60 增大 → 忽略
        expect("ipq-dk-ignore", q.pop_min(id, prio) && id == 5 && prio == 50);
        expect("ipq-dk2", q.pop_min(id, prio) && id == 6 && prio == 60);
        // contains / priority
        q.push(9, 99);
        expect("ipq-contains", q.contains(9) && !q.contains(8));
        expect("ipq-prio", q.priority(9) == 99 && q.priority(8) == 0);
        // 重复 push 忽略
        q.push(9, 5);
        expect("ipq-dup", q.size() == 1 && q.priority(9) == 99);
    }
    {
        // Dijkstra：教科书图
        // 0-1(4) 0-2(2) 1-2(1) 1-3(5) 2-3(8) 2-4(10) 3-4(2) 3-5(6) 4-5(3)
        int edges[][3] = {
            {0, 1, 4}, {0, 2, 2}, {1, 2, 1}, {1, 3, 5},
            {2, 3, 8}, {2, 4, 10}, {3, 4, 2}, {3, 5, 6}, {4, 5, 3}
        };
        int dist[8];
        ipq_dijkstra(6, edges, 9, 0, dist);
        expect("ipq-d0", dist[0] == 0);
        expect("ipq-d1", dist[1] == 3);       // 0->2->1 = 2+1
        expect("ipq-d2", dist[2] == 2);
        expect("ipq-d3", dist[3] == 8);       // 0->2->1->3 = 2+1+5
        expect("ipq-d4", dist[4] == 10);      // 0->2->1->3->4 = 2+1+5+2
        expect("ipq-d5", dist[5] == 13);      // ...->4->5 = +3
        // 不可达
        int dist2[8];
        ipq_dijkstra(3, edges, 1, 0, dist2);
        expect("ipq-unreach", dist2[2] == -1);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
