// nefuOS graph algorithm library — implementation & self test
// See graph.h for the API contract.
#include "graph.h"
#include <string.h>

namespace nefu {
namespace algo {

// ---------------------------------------------------------------------
// IntVec — growable int array
// ---------------------------------------------------------------------
void IntVec::reserve(int n) {
    if (n <= cap) return;
    int nc = cap ? cap : 8;
    while (nc < n) nc *= 2;
    int* nd = new int[(size_t)nc];
    if (data) {
        for (int i = 0; i < len; i++) nd[i] = data[i];
        delete[] data;
    }
    data = nd;
    cap = nc;
}

void IntVec::push(int v) {
    if (len >= cap) reserve(cap ? cap * 2 : 8);
    data[len++] = v;
}

void IntVec::clear() {
    delete[] data;
    data = 0;
    len = cap = 0;
}

// ---------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------
void Graph::init(int vertices, bool dir) {
    clear_all();
    n = vertices < GRAPH_MAXV ? vertices : GRAPH_MAXV;
    directed = dir;
    m = 0;
}

void Graph::add_edge(int u, int v, int w) {
    if (u < 0 || u >= n || v < 0 || v >= n) return;
    if (m >= GRAPH_MAXE) return;
    head[u].push(v);
    whead[u].push(w);
    efrom[m] = u; eto[m] = v; ew[m] = w; m++;
    if (!directed && m < GRAPH_MAXE) {
        // undirected graphs must store the reverse edge too, otherwise
        // edge-array algorithms (Bellman-Ford, Floyd, Kruskal) miss half
        // of the adjacency.
        head[v].push(u);
        whead[v].push(w);
        efrom[m] = v; eto[m] = u; ew[m] = w; m++;
    }
}

void Graph::clear_all() {
    for (int i = 0; i < GRAPH_MAXV; i++) {
        head[i].clear();
        whead[i].clear();
    }
    n = m = 0;
}

// ---------------------------------------------------------------------
// BFS — queue of vertices, dist[] from start
// ---------------------------------------------------------------------
int bfs(const Graph& g, int start, int* order, int* dist) {
    if (start < 0 || start >= g.n) return 0;
    int q[GRAPH_MAXV];
    bool seen[GRAPH_MAXV];
    for (int i = 0; i < g.n; i++) { seen[i] = false; if (dist) dist[i] = -1; }
    int qh = 0, qt = 0, cnt = 0;
    q[qt++] = start;
    seen[start] = true;
    if (dist) dist[start] = 0;
    while (qh < qt) {
        int u = q[qh++];
        if (order) order[cnt] = u;
        cnt++;
        for (int i = 0; i < g.head[u].len; i++) {
            int v = g.head[u].at(i);
            if (seen[v]) continue;
            seen[v] = true;
            if (dist) dist[v] = dist[u] + 1;
            q[qt++] = v;
        }
    }
    return cnt;
}

// ---------------------------------------------------------------------
// DFS (recursive)
// ---------------------------------------------------------------------
static int dfs_rec(const Graph& g, int u, bool* seen, int* order, int& cnt) {
    seen[u] = true;
    if (order) order[cnt] = u;
    cnt++;
    for (int i = 0; i < g.head[u].len; i++) {
        int v = g.head[u].at(i);
        if (!seen[v]) dfs_rec(g, v, seen, order, cnt);
    }
    return cnt;
}

int dfs(const Graph& g, int start, int* order) {
    if (start < 0 || start >= g.n) return 0;
    bool seen[GRAPH_MAXV];
    for (int i = 0; i < g.n; i++) seen[i] = false;
    int cnt = 0;
    dfs_rec(g, start, seen, order, cnt);
    return cnt;
}

// ---------------------------------------------------------------------
// iterative DFS with explicit stack + parent recording
// ---------------------------------------------------------------------
int dfs_iter(const Graph& g, int start, int* order, int* parent) {
    if (start < 0 || start >= g.n) return 0;
    int stack[GRAPH_MAXV], top = 0;
    bool seen[GRAPH_MAXV];
    for (int i = 0; i < g.n; i++) { seen[i] = false; if (parent) parent[i] = -1; }
    stack[top++] = start;
    int cnt = 0;
    while (top > 0) {
        int u = stack[--top];
        if (seen[u]) continue;
        seen[u] = true;
        if (order) order[cnt] = u;
        cnt++;
        for (int i = g.head[u].len - 1; i >= 0; i--) {  // reverse for stable order
            int v = g.head[u].at(i);
            if (!seen[v]) {
                if (parent) parent[v] = u;
                stack[top++] = v;
            }
        }
    }
    return cnt;
}

// ---------------------------------------------------------------------
// topological sort (Kahn)
// ---------------------------------------------------------------------
int topo_sort(const Graph& g, int* order) {
    if (!g.directed) return -1;
    int indeg[GRAPH_MAXV];
    for (int i = 0; i < g.n; i++) indeg[i] = 0;
    for (int u = 0; u < g.n; u++)
        for (int i = 0; i < g.head[u].len; i++) indeg[g.head[u].at(i)]++;
    int q[GRAPH_MAXV], qh = 0, qt = 0;
    for (int i = 0; i < g.n; i++) if (indeg[i] == 0) q[qt++] = i;
    int cnt = 0;
    while (qh < qt) {
        int u = q[qh++];
        if (order) order[cnt] = u;
        cnt++;
        for (int i = 0; i < g.head[u].len; i++) {
            int v = g.head[u].at(i);
            if (--indeg[v] == 0) q[qt++] = v;
        }
    }
    return (cnt == g.n) ? cnt : -1;       // -1 signals a cycle
}

// ---------------------------------------------------------------------
// Dijkstra
// ---------------------------------------------------------------------
int dijkstra(const Graph& g, int src, int* dist, int* prev) {
    if (src < 0 || src >= g.n) return 0;
    bool done[GRAPH_MAXV];
    for (int i = 0; i < g.n; i++) {
        dist[i] = 0x7FFFFFFF;
        if (prev) prev[i] = -1;
        done[i] = false;
    }
    dist[src] = 0;
    int reached = 0;
    for (;;) {
        // pick the unsettled vertex with the smallest tentative distance
        int u = -1, best = 0x7FFFFFFF;
        for (int i = 0; i < g.n; i++) {
            if (!done[i] && dist[i] < best) { best = dist[i]; u = i; }
        }
        if (u < 0) break;                    // nothing reachable left
        done[u] = true;
        reached++;
        // relax all outgoing edges of u
        for (int i = 0; i < g.head[u].len; i++) {
            int v = g.head[u].at(i);
            int w = g.whead[u].at(i);
            if (!done[v] && dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                if (prev) prev[v] = u;
            }
        }
    }
    return reached;
}

// ---------------------------------------------------------------------
// Bellman-Ford
// ---------------------------------------------------------------------
bool bellman_ford(const Graph& g, int src, int* dist, int* prev) {
    if (src < 0 || src >= g.n) return false;
    for (int i = 0; i < g.n; i++) { dist[i] = 0x7FFFFFFF; if (prev) prev[i] = -1; }
    dist[src] = 0;
    // relax every edge V-1 times
    for (int k = 0; k < g.n - 1; k++) {
        bool changed = false;
        for (int e = 0; e < g.m; e++) {
            int u = g.efrom[e], v = g.eto[e], w = g.ew[e];
            if (dist[u] != 0x7FFFFFFF && dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                if (prev) prev[v] = u;
                changed = true;
            }
        }
        if (!changed) break;
    }
    // one more pass to detect negative cycles
    for (int e = 0; e < g.m; e++) {
        int u = g.efrom[e], v = g.eto[e], w = g.ew[e];
        if (dist[u] != 0x7FFFFFFF && dist[u] + w < dist[v]) return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// Floyd-Warshall (all pairs)
// ---------------------------------------------------------------------
bool floyd_warshall(const Graph& g, int* dist) {
    int n = g.n;
    for (int i = 0; i < n * n; i++) dist[i] = 0x7FFFFFFF;
    for (int i = 0; i < n; i++) dist[i * n + i] = 0;
    for (int e = 0; e < g.m; e++) {
        int u = g.efrom[e], v = g.eto[e], w = g.ew[e];
        if (w < dist[u * n + v]) dist[u * n + v] = w;
        if (!g.directed && w < dist[v * n + u]) dist[v * n + u] = w;
    }
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                if (dist[i * n + k] != 0x7FFFFFFF &&
                    dist[k * n + j] != 0x7FFFFFFF &&
                    dist[i * n + k] + dist[k * n + j] < dist[i * n + j])
                    dist[i * n + j] = dist[i * n + k] + dist[k * n + j];
    // negative cycle check: dist[i][i] < 0
    for (int i = 0; i < n; i++) if (dist[i * n + i] < 0) return false;
    return true;
}

// ---------------------------------------------------------------------
// union-find (shared by Kruskal)
// ---------------------------------------------------------------------
struct DSU {
    int p[GRAPH_MAXV];
    void init(int n) { for (int i = 0; i < n; i++) p[i] = i; }
    int find(int x) { return p[x] == x ? x : (p[x] = find(p[x])); }
    bool unite(int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return false;
        p[a] = b;
        return true;
    }
};

// ---------------------------------------------------------------------
// Kruskal MST
// ---------------------------------------------------------------------
static void sort_edges_by_w(GraphEdge* es, int lo, int hi) {
    // simple insertion sort: edge lists are small in practice
    for (int i = lo + 1; i < hi; i++) {
        GraphEdge key = es[i];
        int j = i - 1;
        while (j >= lo && es[j].w > key.w) { es[j + 1] = es[j]; j--; }
        es[j + 1] = key;
    }
}

int kruskal_mst(const Graph& g, GraphEdge* mst_out) {
    if (g.directed) return -1;
    GraphEdge es[GRAPH_MAXE];
    for (int i = 0; i < g.m; i++) {
        es[i].from = g.efrom[i];
        es[i].to = g.eto[i];
        es[i].w = g.ew[i];
    }
    sort_edges_by_w(es, 0, g.m);
    DSU dsu;
    dsu.init(g.n);
    int total = 0, taken = 0;
    for (int i = 0; i < g.m && taken < g.n - 1; i++) {
        if (dsu.unite(es[i].from, es[i].to)) {
            if (mst_out) mst_out[taken] = es[i];
            total += es[i].w;
            taken++;
        }
    }
    if (taken != g.n - 1) return -1;         // disconnected graph
    return total;
}

// ---------------------------------------------------------------------
// Prim MST (O(V^2))
// ---------------------------------------------------------------------
int prim_mst(const Graph& g, GraphEdge* mst_out) {
    if (g.directed || g.n <= 0) return -1;
    bool in_tree[GRAPH_MAXV];
    int key[GRAPH_MAXV], par[GRAPH_MAXV];
    for (int i = 0; i < g.n; i++) { in_tree[i] = false; key[i] = 0x7FFFFFFF; par[i] = -1; }
    key[0] = 0;
    int total = 0;
    for (int it = 0; it < g.n; it++) {
        int u = -1, best = 0x7FFFFFFF;
        for (int i = 0; i < g.n; i++)
            if (!in_tree[i] && key[i] < best) { best = key[i]; u = i; }
        if (u < 0) return -1;                 // disconnected
        in_tree[u] = true;
        total += key[u];
        if (mst_out && par[u] >= 0) {
            // find the exact edge weight for the tree record
            int w = 0x7FFFFFFF;
            for (int i = 0; i < g.m; i++) {
                if ((g.efrom[i] == par[u] && g.eto[i] == u) ||
                    (g.efrom[i] == u && g.eto[i] == par[u])) {
                    if (g.ew[i] < w) w = g.ew[i];
                }
            }
            mst_out[it - 1].from = par[u];
            mst_out[it - 1].to = u;
            mst_out[it - 1].w = w;
        }
        for (int i = 0; i < g.head[u].len; i++) {
            int v = g.head[u].at(i);
            int w = g.whead[u].at(i);
            if (!in_tree[v] && w < key[v]) { key[v] = w; par[v] = u; }
        }
    }
    return total;
}

// ---------------------------------------------------------------------
// Tarjan SCC
// ---------------------------------------------------------------------
namespace {
int g_scc_idx, g_scc_count;
int g_dfn[GRAPH_MAXV], g_low[GRAPH_MAXV], g_stack[GRAPH_MAXV], g_top;
bool g_onstack[GRAPH_MAXV];

void tarjan_visit(const Graph& g, int u, int* comp) {
    g_dfn[u] = g_low[u] = g_scc_idx++;
    g_stack[g_top++] = u;
    g_onstack[u] = true;
    for (int i = 0; i < g.head[u].len; i++) {
        int v = g.head[u].at(i);
        if (g_dfn[v] < 0) {
            tarjan_visit(g, v, comp);
            if (g_low[v] < g_low[u]) g_low[u] = g_low[v];
        } else if (g_onstack[v] && g_dfn[v] < g_low[u]) {
            g_low[u] = g_dfn[v];
        }
    }
    if (g_low[u] == g_dfn[u]) {
        // pop the whole component off the stack
        for (;;) {
            int v = g_stack[--g_top];
            g_onstack[v] = false;
            comp[v] = g_scc_count;
            if (v == u) break;
        }
        g_scc_count++;
    }
}
} // namespace

int tarjan_scc(const Graph& g, int* comp) {
    g_scc_idx = g_scc_count = g_top = 0;
    for (int i = 0; i < g.n; i++) { g_dfn[i] = -1; g_onstack[i] = false; }
    for (int i = 0; i < g.n; i++)
        if (g_dfn[i] < 0) tarjan_visit(g, i, comp);
    return g_scc_count;
}

// ---------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------
bool graph_is_connected(const Graph& g) {
    if (g.n <= 0) return false;
    int order[GRAPH_MAXV];
    int cnt = bfs(g, 0, order, 0);
    return cnt == g.n;
}

void graph_reconstruct_path(int* prev, int src, int dst, int* out, int* out_len) {
    int tmp[GRAPH_MAXV], k = 0;
    int cur = dst;
    while (cur >= 0 && cur != src) { tmp[k++] = cur; cur = prev[cur]; }
    if (cur == src) tmp[k++] = src;
    *out_len = k;
    for (int i = 0; i < k; i++) out[i] = tmp[k - 1 - i];
}

// ---------------------------------------------------------------------
// self test
// ---------------------------------------------------------------------
namespace {
int g_graph_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_graph_fails++;
    (void)what;
}
} // namespace

int graph_self_test() {
    g_graph_fails = 0;

    // --- simple undirected chain 0-1-2-3 ---
    Graph g;
    g.init(4, false);
    g.add_edge(0, 1);
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    int order[8], dist[8];
    expect("bfs-chain", bfs(g, 0, order, dist) == 4);
    expect("bfs-d0", dist[0] == 0);
    expect("bfs-d1", dist[1] == 1);
    expect("bfs-d3", dist[3] == 3);
    expect("dfs-chain", dfs(g, 0, order) == 4);
    expect("connected", graph_is_connected(g));
    GraphEdge mst[8];
    expect("kruskal-chain", kruskal_mst(g, mst) == 3);
    expect("prim-chain", prim_mst(g, mst) == 3);

    // --- weighted graph: 0-1(4) 0-2(1) 1-2(2) 1-3(5) ---
    Graph gw;
    gw.init(4, false);
    gw.add_edge(0, 1, 4);
    gw.add_edge(0, 2, 1);
    gw.add_edge(1, 2, 2);
    gw.add_edge(1, 3, 5);
    int d[4], p[4];
    dijkstra(gw, 0, d, p);
    expect("dijkstra-02", d[2] == 1);
    expect("dijkstra-01", d[1] == 3);        // 0->2->1 = 1+2
    expect("dijkstra-03", d[3] == 8);        // 0->2->1->3 = 1+2+5
    expect("bf-ok", bellman_ford(gw, 0, d, p));
    expect("bf-01", d[1] == 3);
    int all[4 * 4];
    expect("floyd", floyd_warshall(gw, all));
    expect("floyd-13", all[1 * 4 + 3] == 5);
    expect("kruskal-w", kruskal_mst(gw, mst) == 8);   // 1+2+5
    expect("prim-w", prim_mst(gw, mst) == 8);

    // --- DAG topological sort: 0->2, 1->2, 2->3 ---
    Graph dag;
    dag.init(4, true);
    dag.add_edge(0, 2);
    dag.add_edge(1, 2);
    dag.add_edge(2, 3);
    int to[8];
    expect("topo-len", topo_sort(dag, to) == 4);
    // verify: every edge u->v has pos(u) < pos(v)
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        for (int e = 0; e < dag.head[i].len; e++) {
            int v = dag.head[i].at(e);
            int pu = -1, pv = -1;
            for (int k = 0; k < 4; k++) {
                if (to[k] == i) pu = k;
                if (to[k] == v) pv = k;
            }
            if (pu < 0 || pv < 0 || pu > pv) ok = false;
        }
    }
    expect("topo-order", ok);

    // --- cycle detection: 0->1->2->0 ---
    Graph cyc;
    cyc.init(3, true);
    cyc.add_edge(0, 1);
    cyc.add_edge(1, 2);
    cyc.add_edge(2, 0);
    expect("topo-cycle", topo_sort(cyc, to) == -1);

    // --- negative weight graph: 0->1(4) 1->2(-6) 0->2(2) ---
    Graph ng;
    ng.init(3, true);
    ng.add_edge(0, 1, 4);
    ng.add_edge(1, 2, -6);
    ng.add_edge(0, 2, 2);
    expect("bf-neg", bellman_ford(ng, 0, d, p));
    expect("bf-neg-d2", d[2] == -2);         // 0->1->2 = 4-6

    // --- negative cycle: 0->1(1) 1->2(-3) 2->0(1) ---
    Graph nc;
    nc.init(3, true);
    nc.add_edge(0, 1, 1);
    nc.add_edge(1, 2, -3);
    nc.add_edge(2, 0, 1);
    expect("bf-negcycle", !bellman_ford(nc, 0, d, p));
    expect("floyd-negcycle", !floyd_warshall(nc, all));

    // --- SCC: 0<->1, 2 alone, 3->2 ---
    Graph sg;
    sg.init(4, true);
    sg.add_edge(0, 1);
    sg.add_edge(1, 0);
    sg.add_edge(3, 2);
    int comp[4];
    int cc = tarjan_scc(sg, comp);
    expect("scc-count", cc == 3);
    expect("scc-pair", comp[0] == comp[1]);
    expect("scc-2", comp[2] != comp[3]);

    // --- path reconstruction ---
    {
        Graph g2;
        g2.init(4, false);
        g2.add_edge(0, 1, 2);
        g2.add_edge(1, 2, 3);
        g2.add_edge(0, 3, 9);
        int dd[4], pp[4];
        dijkstra(g2, 0, dd, pp);
        int path[8], plen = 0;
        graph_reconstruct_path(pp, 0, 2, path, &plen);
        expect("path-len", plen == 3);
        expect("path-seq", path[0] == 0 && path[1] == 1 && path[2] == 2);
    }
    return g_graph_fails;
}

} // namespace algo
} // namespace nefu
