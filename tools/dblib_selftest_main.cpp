#include <cstdio>
#include "dblib/db_all.h"

int main(int argc, char** argv) {
    using namespace nefu::dbx;
    int which = argc > 1 ? atoi(argv[1]) : 0;
    switch (which) {
        case 1: printf("csv=%d\n", CsvTable::self_test()); break;
        case 2: printf("ini=%d\n", Ini::self_test()); break;
        case 3: printf("json=%d\n", Json::self_test()); break;
        case 4: printf("bptree=%d\n", BpTree::self_test()); break;
        case 5: printf("page=%d\n", PageManager::self_test()); break;
        case 6: printf("table=%d\n", Table::self_test()); break;
        default: printf("usage: dm1 <1..6>\n");
    }
    return 0;
}
