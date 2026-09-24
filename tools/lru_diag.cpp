#include "datlib/lru.h"
#include <cstdio>
namespace nefu { namespace dt { int lru_page_faults(const int* pages, int n, int frames); }}
int main(){
    int pages[] = {7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1};
    for (int f = 1; f <= 4; f++) {
        printf("frames=%d faults=%d\n", f, nefu::dt::lru_page_faults(pages, 20, f));
        fflush(stdout);
    }
    printf("DONE\n");
    return 0;
}
