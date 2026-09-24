#include "datlib/lru.h"
#include <cstdio>
int main(){ printf("start\n"); fflush(stdout); int f = nefu::dt::lru_self_test(); printf("lru=%d\n", f); return 0; }
