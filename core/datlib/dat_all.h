// nefuOS data-types library — aggregate header (STL-style)
// 聚合头：一次引入全部数据结构模块，并提供整体自检入口。
// 用法： #include "datlib/dat_all.h"
#pragma once

#include "datlib/vec.h"
#include "datlib/ringbuf.h"
#include "datlib/bitset.h"
#include "datlib/hashtab.h"
#include "datlib/rbtree.h"
#include "datlib/treap.h"
#include "datlib/btree.h"
#include "datlib/segtree.h"
#include "datlib/fenwick.h"
#include "datlib/bloom.h"
#include "datlib/lru.h"
#include "datlib/sortedlist.h"
#include "datlib/ipq.h"
#include "datlib/objpool.h"
#include "datlib/radix.h"
#include "datlib/timerwheel.h"

namespace nefu {
namespace dt {

// 运行全部数据结构模块自检，返回总失败数（0 = 全部通过）
inline int dat_all_self_test() {
    return vec_self_test() +
           ringbuf_self_test() +
           bitset_self_test() +
           hashtab_self_test() +
           rbtree_self_test() +
           treap_self_test() +
           btree_self_test() +
           segtree_self_test() +
           fenwick_self_test() +
           bloom_self_test() +
           lru_self_test() +
           sortedlist_self_test() +
           ipq_self_test() +
           objpool_self_test() +
           radix_self_test() +
           timerwheel_self_test();
}

} // namespace dt
} // namespace nefu
