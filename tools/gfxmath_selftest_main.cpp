// nefuOS gfxmath 汇总自测宿主
#include <cstdio>
#include "gfxmath/gfx_all.h"

int main() {
    int t = nefu::gfx::gfx_all_self_test();
    printf("gfxmath self tests total failures = %d\n", t);
    return t == 0 ? 0 : 1;
}
