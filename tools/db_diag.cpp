#include <cstdio>
#include "dblib/db_all.h"
using namespace nefu::dbx;

static void dump_node(BpTree::Node* n, int depth) {
    if (!n) return;
    for (int i = 0; i < depth; i++) printf("  ");
    printf("L=%d keys=", n->leaf ? 1 : 0);
    for (size_t i = 0; i < n->keys.size(); i++) printf("%d ", n->keys[i]);
    printf(" kids=%d\n", (int)n->kids.size());
    if (!n->leaf)
        for (size_t i = 0; i < n->kids.size(); i++) dump_node(n->kids[i], depth + 1);
}

int main() {
    BpTree t;
    int keys[10] = { 5, 3, 8, 1, 9, 2, 7, 4, 6, 0 };
    for (int i = 0; i < 10; i++) {
        t.insert(keys[i], keys[i] + 100);
        printf("-- after insert %d --\n", keys[i]);
        // 无法直接访问 root_（私有），用 all() 间接看
        std::vector<std::pair<int, int> > a = t.all();
        printf("all(%d): ", (int)a.size());
        for (size_t j = 0; j < a.size(); j++) printf("%d ", a[j].first);
        printf("\n");
    }
    return 0;
}
