// 临时编译检查 main
#include "../core/deeplearn/deeplearn_all.h"
#include <cstdio>
#include <cstdlib>
namespace nefu {
void* kalloc(size_t sz) { return std::malloc(sz ? sz : 1); }
void  kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void  platform_dbg(const char* s) { fputs(s, stderr); }
}
using namespace nefu::deeplearn;
int main() {
    int a = tensor_self_test();       printf("tensor=%d\n", a); fflush(stdout);
    int b = autograd_self_test();     printf("autograd=%d\n", b); fflush(stdout);
    int c = activations_self_test();   printf("activations=%d\n", c); fflush(stdout);
    int d = losses_self_test();        printf("losses=%d\n", d); fflush(stdout);
    int e = layers_self_test();        printf("layers=%d\n", e); fflush(stdout);
    int f = optimizers_self_test();    printf("optimizers=%d\n", f); fflush(stdout);
    int g = rnn_self_test();           printf("rnn=%d\n", g); fflush(stdout);
    int h = attention_self_test();     printf("attention=%d\n", h); fflush(stdout);
    return (a+b+c+d+e+f+g+h) ? 1 : 0;
}
