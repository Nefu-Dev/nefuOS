#include <cstdio>
#include <cstring>
#include "complib/huffman.h"

int main() {
    const char* txt = "this is a test of huffman coding this is a test this is a test of huffman ";
    int n = (int)strlen(txt);
    unsigned char enc[4096], dec[4096];
    int el = nefu::comp::huffman_encode((const unsigned char*)txt, n, enc, sizeof(enc));
    printf("n=%d el=%d\n", n, el);
    if (el > 0) {
        printf("syms=%d hdr_orig_len=%d\n", enc[0], (enc[1]<<24)|(enc[2]<<16)|(enc[3]<<8)|enc[4]);
        for (int i = 0; i < el && i < 40; i++) printf("%02x ", enc[i]);
        printf("\n");
        int dl = nefu::comp::huffman_decode(enc, el, dec, sizeof(dec));
        printf("dl=%d match=%d\n", dl, dl == n && memcmp(dec, txt, n) == 0);
    }
    return 0;
}
