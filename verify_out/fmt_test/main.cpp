// quick standalone test for ksprintf %f support
#include <klib/klib.h>
#include <cstdio>
int main() {
    char b[128];
    nefu::ksprintf(b, sizeof(b), "%.4f %s", 0.1, "cm");
    printf("[%s]\n", b);
    nefu::ksprintf(b, sizeof(b), "%.4f %s", 1.0, "m");
    printf("[%s]\n", b);
    nefu::ksprintf(b, sizeof(b), "%.2f", 123.456);
    printf("[%s]\n", b);
    nefu::ksprintf(b, sizeof(b), "%.0f Hz", 440.6);
    printf("[%s]\n", b);
    nefu::ksprintf(b, sizeof(b), "%.3f seconds", 1.23456);
    printf("[%s]\n", b);
    nefu::ksprintf(b, sizeof(b), "%.4f", 999999.99995);
    printf("[%s]\n", b);
    nefu::ksprintf(b, sizeof(b), "%d and %.1f", 42, 3.7);
    printf("[%s]\n", b);
    return 0;
}
