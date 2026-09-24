#include "lib/softmath.h"
#include <cstdio>
int main() {
    using namespace nefu::fx;
    printf("PI_2=%d fx_sin=%d fx_cos=%d\n", FX_PI_2, fx_sin(FX_PI_2), fx_cos(FX_PI_2));
    printf("sin(1.5)=%d cos(1.5)=%d\n", fx_sin(fxf(15,10)), fx_cos(fxf(15,10)));
    return 0;
}
