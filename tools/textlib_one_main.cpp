#include "textlib/text_all.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    int m = atoi(argv[1]);
    int f = 0;
    switch (m) {
    case 1: f = nefu::text::levenshtein_self_test(); break;
    case 2: f = nefu::text::lcs_self_test(); break;
    case 3: f = nefu::text::search_self_test(); break;
    case 4: f = nefu::text::aho_self_test(); break;
    case 5: f = nefu::text::token_self_test(); break;
    case 6: f = nefu::text::ngram_self_test(); break;
    case 7: f = nefu::text::regex_self_test(); break;
    case 8: f = nefu::text::diff_self_test(); break;
    }
    printf("mod%d=%d\n", m, f);
    return 0;
}
