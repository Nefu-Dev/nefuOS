// nefuOS 压缩算法库 —— 聚合自测实现
#include "compress_all.h"

namespace nefu {
namespace compress {

int compress_self_test() {
    int fails = 0;
    fails += rle_self_test();
    fails += huffman_self_test();
    fails += lz_self_test();
    fails += bwt_self_test();
    fails += arith_self_test();
    fails += dict_self_test();
    fails += other_self_test();
    return fails;
}

} // namespace compress
} // namespace nefu
