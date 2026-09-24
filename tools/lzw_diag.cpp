// lzw 诊断：打印编码/解码过程
#include <cstdio>
#include <cstring>
#include "complib/lzw.h"

int main() {
    const char* txt = "TOBEORNOTTOBEORTOBEORNOT";
    int n = (int)strlen(txt);
    unsigned char enc[8192], dec[8192];
    int el = nefu::comp::lzw_encode((const unsigned char*)txt, n, enc, sizeof(enc));
    printf("n=%d el=%d\n", n, el);
    for (int i = 0; i < el && i < 24; i++) printf("%02x ", enc[i]);
    printf("\n");
    int dl = nefu::comp::lzw_decode(enc, el, dec, sizeof(dec));
    printf("dl=%d\n", dl);
    if (dl > 0) {
        for (int i = 0; i < dl && i < 30; i++) printf("%c", dec[i]);
        printf("\n");
        printf("match=%d\n", dl == n && memcmp(dec, txt, n) == 0);
    }
    return 0;
}
