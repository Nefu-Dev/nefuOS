#include "datlib/dat_all.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv){
    const char* m = argc>1?argv[1]:"all";
    int f = 0;
    if (!strcmp(m,"1")||!strcmp(m,"all")) { f=nefu::dt::vec_self_test(); printf("vec=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"2")||!strcmp(m,"all")) { f=nefu::dt::ringbuf_self_test(); printf("ringbuf=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"3")||!strcmp(m,"all")) { f=nefu::dt::bitset_self_test(); printf("bitset=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"4")||!strcmp(m,"all")) { f=nefu::dt::hashtab_self_test(); printf("hashtab=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"5")||!strcmp(m,"all")) { f=nefu::dt::rbtree_self_test(); printf("rbtree=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"6")||!strcmp(m,"all")) { f=nefu::dt::treap_self_test(); printf("treap=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"7")||!strcmp(m,"all")) { f=nefu::dt::btree_self_test(); printf("btree=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"8")||!strcmp(m,"all")) { f=nefu::dt::segtree_self_test(); printf("segtree=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"9")||!strcmp(m,"all")) { f=nefu::dt::fenwick_self_test(); printf("fenwick=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"10")||!strcmp(m,"all")) { f=nefu::dt::bloom_self_test(); printf("bloom=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"11")||!strcmp(m,"all")) { f=nefu::dt::lru_self_test(); printf("lru=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"12")||!strcmp(m,"all")) { f=nefu::dt::sortedlist_self_test(); printf("sortedlist=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"13")||!strcmp(m,"all")) { f=nefu::dt::ipq_self_test(); printf("ipq=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"14")||!strcmp(m,"all")) { f=nefu::dt::objpool_self_test(); printf("objpool=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"15")||!strcmp(m,"all")) { f=nefu::dt::radix_self_test(); printf("radix=%d\n",f); fflush(stdout); }
    if (!strcmp(m,"16")||!strcmp(m,"all")) { f=nefu::dt::timerwheel_self_test(); printf("timerwheel=%d\n",f); fflush(stdout); }
    return 0;
}
