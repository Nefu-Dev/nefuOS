// 单模块诊断：complib 分模块跑
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "complib/bitio.h"
#include "complib/rle.h"
#include "complib/huffman.h"
#include "complib/lz77.h"
#include "complib/lzw.h"
#include "complib/arithmetic.h"
#include "complib/bwt.h"

int main(int argc, char** argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;
    int f = 0;
    if (which == 0 || which == 1) { printf("bitio: %d\n", nefu::comp::bitio_self_test()); }
    if (which == 0 || which == 2) { printf("rle: %d\n", nefu::comp::rle_self_test()); }
    if (which == 0 || which == 3) { printf("huff: %d\n", nefu::comp::huffman_self_test()); }
    if (which == 0 || which == 4) { printf("lz77: %d\n", nefu::comp::lz77_self_test()); }
    if (which == 0 || which == 5) { printf("lzw: %d\n", nefu::comp::lzw_self_test()); }
    if (which == 0 || which == 6) { printf("arith: %d\n", nefu::comp::arithmetic_self_test()); }
    if (which == 0 || which == 7) { printf("bwt: %d\n", nefu::comp::bwt_self_test()); }
    printf("DONE\n");
    return f;
}
