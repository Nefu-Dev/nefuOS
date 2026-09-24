#include <cstdio>
#include <cstring>
#include "complib/lzw.h"

int main() {
    // bin 案例
    unsigned char in[500], enc[8192], dec[8192];
    for (int i = 0; i < 500; i++) in[i] = (unsigned char)((i * 7 + 1) % 200);
    int el = nefu::comp::lzw_encode(in, 500, enc, sizeof(enc));
    printf("bin: el=%d\n", el);
    int dl = nefu::comp::lzw_decode(enc, el, dec, sizeof(dec));
    printf("bin: dl=%d match=%d\n", dl, dl == 500 && memcmp(dec, in, 500) == 0);
    // big 案例
    unsigned char in2[900];
    for (int i = 0; i < 900; i++) in2[i] = (unsigned char)((i * 7 + 1) % 200);
    el = nefu::comp::lzw_encode(in2, 900, enc, sizeof(enc));
    printf("big: el=%d\n", el);
    dl = nefu::comp::lzw_decode(enc, el, dec, sizeof(dec));
    printf("big: dl=%d match=%d\n", dl, dl == 900 && memcmp(dec, in2, 900) == 0);
    return 0;
}
