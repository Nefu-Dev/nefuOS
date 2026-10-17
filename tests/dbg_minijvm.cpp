// throwaway: run a single .class through minijvm v2 with byte-level output.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "core/apps/minijvm.h"
#include "core/klib/klib.h"

static void sink(const char* s, void* ud) {
    (void)ud;
    printf("[out] %s\n", s);
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: dbg <file.class> [arg...]\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { printf("read fail\n"); return 2; }
    fclose(f);
    const char** av = (const char**)(argc > 2 ? argv + 2 : 0);
    int rc = nefu::minijvm_run_class(buf, (uint32_t)n, sink, 0, argc > 2 ? argc - 2 : 0, av);
    printf("rc=%d\n", rc);
    free(buf);
    return 0;
}
