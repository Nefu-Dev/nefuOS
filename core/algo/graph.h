// nefuOS graph algorithm library
// A compact, teaching-oriented graph toolkit built on adjacency lists with
// plain arrays (no STL): BFS/DFS, topological sort, shortest paths
// (Dijkstra / Bellman-Ford / Floyd-Warshall), minimum spanning trees
// (Kruskal / Prim) and strongly connected components (Tarjan).
// The Graph type is edge-list based (nefu::List-like own vector below)
// and supports directed / undirected / weighted modes.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace algo {

// max vertices / edges (compile-time bounds keep everything malloc-light)
const int GRAPH_MAXV = 256;
const int GRAPH_MAXE = 4096;

struct GraphEdge {
    int from, to, w;
};

// Simple adjacency container — growable int array (no STL).
struct IntVec {
    int* data;
    int  len;
    int  cap;
    IntVec() : data(0), len(0), cap(0) {}
    void reserve(int n);
    void push(int v);
    void clear();            // frees memory
    int  at(int i) const { return data[i]; }
};

struct Graph {
    bool directed;
    int  n;                                  // vertex count
    int  m;                                  // edge count (added)
    IntVec head[GRAPH_MAXV];                 // adjacency: neighbor ids
    IntVec whead[GRAPH_MAXV];                // parallel weights
    int  efrom[GRAPH_MAXE];                  // edge list (for Kruskal etc.)
    int  eto[GRAPH_MAXE];
    int  ew[GRAPH_MAXE];
    Graph() : directed(false), n(0), m(0) {}
    void init(int vertices, bool dir);
    void add_edge(int u, int v, int w = 1);  // 0-based vertex ids
    void clear_all();
};

// ---- traversal ----
// visits every reachable vertex once; `order` receives the visit sequence.
// Returns the number of vertices visited.
int bfs(const Graph& g, int start, int* order, int* dist);
int dfs(const Graph& g, int start, int* order);
// iterative DFS with explicit stack; `parent` gets the DFS tree parent.
int dfs_iter(const Graph& g, int start, int* order, int* parent);

// ---- topological sort (directed acyclic graph) ----
// Kahn's algorithm (indegree queue).  Returns number of vertices placed,
// or -1 when the graph has a cycle.
int topo_sort(const Graph& g, int* order);

// ---- shortest paths ----
// Dijkstra (non-negative weights).  dist[] = shortest distances, prev[]
// = predecessor for path reconstruction.  Returns count of reached nodes.
int dijkstra(const Graph& g, int src, int* dist, int* prev);
// Bellman-Ford (negative weights allowed; detects negative cycles).
// Returns true on success, false when a negative cycle exists.
bool bellman_ford(const Graph& g, int src, int* dist, int* prev);
// Floyd-Warshall (all pairs).  dist is n*n row-major.  Returns false if a
// negative cycle exists.
bool floyd_warshall(const Graph& g, int* dist);

// ---- minimum spanning tree ----
// Kruskal (edge sort + union-find).  Writes MST edges into mst_out
// (pairs as from/to) and returns total weight, or -1 if disconnected.
int kruskal_mst(const Graph& g, GraphEdge* mst_out);
// Prim (priority-queue-free O(V^2) version).  Same contract.
int prim_mst(const Graph& g, GraphEdge* mst_out);

// ---- strongly connected components (Tarjan) ----
// Fills comp[] with a component id per vertex; returns the component count.
int tarjan_scc(const Graph& g, int* comp);

// ---- helpers ----
bool graph_is_connected(const Graph& g);    // undirected check
void graph_reconstruct_path(int* prev, int src, int dst, int* out, int* out_len);

// ---- self test ----
int graph_self_test();

} // namespace algo
} // namespace nefu
