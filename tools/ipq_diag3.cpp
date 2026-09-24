#include "datlib/ipq.h"
#include <cstdio>
int main() {
    const int INF = 0x3f3f3f3f;
    struct Edge { int to, w; };
    Edge adj[64][8];
    int deg[64] = {0};
    int edges[][3] = {
        {0, 1, 4}, {0, 2, 2}, {1, 2, 1}, {1, 3, 5},
        {2, 3, 8}, {2, 4, 10}, {3, 4, 2}, {3, 5, 6}, {4, 5, 3}
    };
    int n = 6, ecount = 9;
    for (int i = 0; i < ecount; i++) {
        int u = edges[i][0], v = edges[i][1], w = edges[i][2];
        adj[u][deg[u]].to = v; adj[u][deg[u]].w = w; deg[u]++;
        printf("edge %d->%d w=%d\n", u, v, w);
    }
    int dist[8];
    for (int i = 0; i < n; i++) dist[i] = INF;
    dist[0] = 0;
    nefu::dt::ipq q;
    q.push(0, 0);
    int step = 0;
    while (!q.empty()) {
        int u, d;
        q.pop_min(u, d);
        printf("step%d pop u=%d d=%d\n", step++, u, d);
        for (int k = 0; k < deg[u]; k++) {
            int v = adj[u][k].to, w = adj[u][k].w;
            if (d + w < dist[v]) {
                printf("  relax v=%d w=%d old=%d new=%d\n", v, w, dist[v], d + w);
                dist[v] = d + w;
                if (q.contains(v)) { printf("  decrease_key(%d,%d)\n", v, dist[v]); q.decrease_key(v, dist[v]); }
                else { printf("  push(%d,%d)\n", v, dist[v]); q.push(v, dist[v]); }
            }
        }
    }
    for (int i = 0; i < n; i++) printf("dist[%d]=%d\n", i, dist[i]);
    return 0;
}
