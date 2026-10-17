#include <cstdio>
#include <cstdint>
#include "../core/apps/deb_selfcheck_bin.h"
int main() {
    printf("LEN=%u sizeof(ARRAY)=%zu\n", (unsigned)NEFU_DEB_SELFCHECK_LEN, sizeof(NEFU_DEB_SELFCHECK_BIN));
    printf("head=%02x%02x%02x%02x%02x%02x%02x%02x\n", NEFU_DEB_SELFCHECK_BIN[0], NEFU_DEB_SELFCHECK_BIN[1], NEFU_DEB_SELFCHECK_BIN[2], NEFU_DEB_SELFCHECK_BIN[3], NEFU_DEB_SELFCHECK_BIN[4], NEFU_DEB_SELFCHECK_BIN[5], NEFU_DEB_SELFCHECK_BIN[6], NEFU_DEB_SELFCHECK_BIN[7]);
    return 0;
}
