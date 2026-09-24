#include <cstdio>
#include "audlib/aud_all.h"
using namespace nefu::audx;

int main() {
    AudioBuf a(1000, 1);
    a.silence(200);
    a.data[0] = 8000;
    AudioBuf o = Reverb::apply(a, 0.5, 0.3);
    int hits = 0;
    for (int i = 0; i < o.frames(); i++) {
        if (o.data[i] != 0) {
            printf("i=%d v=%d\n", i, o.data[i]);
            hits++;
        }
    }
    printf("hits=%d\n", hits);
    return 0;
}
