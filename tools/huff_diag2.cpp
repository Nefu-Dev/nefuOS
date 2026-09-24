#include <cstdio>
#include <cstring>
#include "complib/huffman.h"

int main() {
    // huff-all 场景
    unsigned char in[256];
    for (int i = 0; i < 256; i++) in[i] = (unsigned char)i;
    unsigned char enc[4096], dec[4096];
    int el = nefu::comp::huffman_encode(in, 256, enc, sizeof(enc));
    printf("all: el=%d\n", el);
    if (el > 0) {
        int dl = nefu::comp::huffman_decode(enc, el, dec, sizeof(dec));
        printf("all: dl=%d match=%d\n", dl, dl == 256 && memcmp(dec, in, 256) == 0);
    }
    // compresses 场景
    const char* txt = "this is a test of huffman coding this is a test this is a test of huffman "
                       "huffman coding is a prefix code based on symbol frequencies "
                       "the more frequent the symbol the shorter the code "
                       "this longer text should compress well below its original size";
    int n = (int)strlen(txt);
    el = nefu::comp::huffman_encode((const unsigned char*)txt, n, enc, sizeof(enc));
    printf("txt: n=%d el=%d\n", n, el);
    if (el > 0) {
        int dl = nefu::comp::huffman_decode(enc, el, dec, sizeof(dec));
        printf("txt: dl=%d match=%d\n", dl, dl == n && memcmp(dec, txt, n) == 0);
    }
    return 0;
}
