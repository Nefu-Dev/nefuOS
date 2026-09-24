// datlib standalone self-test host
// 运行 datlib 全部 16 个模块的自检，打印每个模块失败数。
#include "datlib/dat_all.h"
#include <cstdio>

int main() {
    int vec_f = nefu::dt::vec_self_test();
    int ring_f = nefu::dt::ringbuf_self_test();
    int bits_f = nefu::dt::bitset_self_test();
    int hash_f = nefu::dt::hashtab_self_test();
    int rb_f = nefu::dt::rbtree_self_test();
    int tp_f = nefu::dt::treap_self_test();
    int bt_f = nefu::dt::btree_self_test();
    int seg_f = nefu::dt::segtree_self_test();
    int fw_f = nefu::dt::fenwick_self_test();
    int bl_f = nefu::dt::bloom_self_test();
    int lru_f = nefu::dt::lru_self_test();
    int sl_f = nefu::dt::sortedlist_self_test();
    int ipq_f = nefu::dt::ipq_self_test();
    int op_f = nefu::dt::objpool_self_test();
    int rx_f = nefu::dt::radix_self_test();
    int tw_f = nefu::dt::timerwheel_self_test();
    int total = vec_f + ring_f + bits_f + hash_f + rb_f + tp_f + bt_f + seg_f +
                fw_f + bl_f + lru_f + sl_f + ipq_f + op_f + rx_f + tw_f;
    printf("vec=%d ringbuf=%d bitset=%d hashtab=%d rbtree=%d treap=%d btree=%d segtree=%d\n",
           vec_f, ring_f, bits_f, hash_f, rb_f, tp_f, bt_f, seg_f);
    printf("fenwick=%d bloom=%d lru=%d sortedlist=%d ipq=%d objpool=%d radix=%d timerwheel=%d\n",
           fw_f, bl_f, lru_f, sl_f, ipq_f, op_f, rx_f, tw_f);
    printf("TOTAL=%d\n", total);
    return total;
}
