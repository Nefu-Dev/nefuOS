#include "datlib/btree.h"
#include "datlib/treap.h"
#include "datlib/segtree.h"
#include "datlib/fenwick.h"
#include <cstdio>
int main(){
    printf("btree=%d\n", nefu::dt::btree_self_test()); fflush(stdout);
    printf("treap=%d\n", nefu::dt::treap_self_test()); fflush(stdout);
    printf("segtree=%d\n", nefu::dt::segtree_self_test()); fflush(stdout);
    printf("fenwick=%d\n", nefu::dt::fenwick_self_test()); fflush(stdout);
    printf("DONE\n");
    return 0;
}
