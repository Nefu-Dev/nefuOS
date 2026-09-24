#include <cstdio>
#include <cstring>
#include "complib/arithmetic.h"

int main() {
    const char* txt = "aababbabbaababbaaabbbaaaab";
    int n = (int)strlen(txt);
    unsigned char enc[4096], dec[4096];
    int el = nefu::comp::arithmetic_encode((const unsigned char*)txt, n, enc, sizeof(enc));
    printf("n=%d el=%d\n", n, el);
    int dl = nefu::comp::arithmetic_decode(enc, el, dec, sizeof(dec));
    printf("dl=%d\n", dl);
    if (dl > 0) {
        for (int i = 0; i < dl; i++) printf("%c", dec[i]);
        printf("\n");
        int first = -1;
        for (int i = 0; i < dl && i < n; i++) if (dec[i] != txt[i]) { first = i; break; }
        printf("first-diff=%d\n", first);
    }
    return 0;
}
