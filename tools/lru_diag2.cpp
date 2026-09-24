#include "datlib/lru.h"
#include <cstdio>
int main(){
    nefu::dt::lru c(1);
    int v, ev;
    c.put(7, 0, ev);
    c.debug_dump_hash(); fflush(stdout);
    printf("--- before get(0)/put(0) ---\n");
    c.get(0, v);
    printf("get0 miss=%d\n", !c.contains(0)); fflush(stdout);
    // 手动模拟 put(0)：先观察 hash_remove
    // 直接调 put
    c.put(0, 1, ev);
    printf("after put0 ev=%d\n", ev); fflush(stdout);
    c.debug_dump_hash(); fflush(stdout);
    printf("DONE\n");
    return 0;
}
