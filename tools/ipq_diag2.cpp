#include "datlib/ipq.h"
#include <cstdio>
int main() {
    nefu::dt::ipq q;
    q.push(0, 0);
    q.push(1, 4);
    q.push(2, 2);
    int id, p;
    q.pop_min(id, p);
    printf("pop %d prio %d\n", id, p);
    printf("contains1=%d pos?\n", q.contains(1));
    q.decrease_key(1, 3);
    printf("after decrease_key(1,3)\n");
    int ks[8]; int n = 0;
    // 依次弹出全部看顺序
    while (q.pop_min(id, p)) printf("pop %d prio %d\n", id, p);
    (void)ks; (void)n;
    return 0;
}
