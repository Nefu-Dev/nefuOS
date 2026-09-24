#include "datlib/ipq.h"
#include <cstdio>
namespace nefu { namespace dt {
void ipq_dijkstra(int n, const int edges[][3], int ecount, int src, int* dist);
}}
int main() {
    int edges[][3] = {
        {0, 1, 4}, {0, 2, 2}, {1, 2, 1}, {1, 3, 5},
        {2, 3, 8}, {2, 4, 10}, {3, 4, 2}, {3, 5, 6}, {4, 5, 3}
    };
    int dist[8];
    nefu::dt::ipq_dijkstra(6, edges, 9, 0, dist);
    for (int i = 0; i < 6; i++) printf("dist[%d]=%d\n", i, dist[i]);
    return 0;
}
