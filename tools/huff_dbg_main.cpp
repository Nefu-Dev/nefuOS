// 直接用 debug 版本重测：在 decode 循环中定位
#include <cstdio>
#include <cstring>
#include "complib/huffman.h"

namespace nefu { namespace comp {
extern int huffman_decode_debug(const unsigned char* in, int n, unsigned char* out, int outcap);
} }

int main() {
    const char* txt = "this is a test of huffman coding this is a test this is a test of huffman ";
    int n = (int)strlen(txt);
    unsigned char enc[4096], dec[4096];
    int el = nefu::comp::huffman_encode((const unsigned char*)txt, n, enc, sizeof(enc));
    int dl = nefu::comp::huffman_decode_debug(enc, el, dec, sizeof(dec));
    printf("dl=%d match=%d\n", dl, dl == n && memcmp(dec, txt, n) == 0);
    return 0;
}
