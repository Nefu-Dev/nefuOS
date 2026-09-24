// 打印 huffman 码表检查重复
#include <cstdio>
#include <cstring>
#include "complib/huffman.h"

namespace nefu { namespace comp {
int huffman_encode_debug(const unsigned char* in, int n, unsigned char* out, int outcap);
} }

int main() {
    const char* txt = "this is a test of huffman coding this is a test this is a test of huffman ";
    int n = (int)strlen(txt);
    unsigned char enc[4096];
    int el = nefu::comp::huffman_encode_debug((const unsigned char*)txt, n, enc, sizeof(enc));
    printf("el=%d\n", el);
    return 0;
}
