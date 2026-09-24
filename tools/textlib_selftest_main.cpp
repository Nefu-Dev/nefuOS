// textlib standalone self-test host
#include "textlib/text_all.h"
#include <cstdio>
int main() {
    printf("levenshtein=%d\n", nefu::text::levenshtein_self_test());
    printf("lcs=%d\n", nefu::text::lcs_self_test());
    printf("search=%d\n", nefu::text::search_self_test());
    printf("aho=%d\n", nefu::text::aho_self_test());
    printf("token=%d\n", nefu::text::token_self_test());
    printf("ngram=%d\n", nefu::text::ngram_self_test());
    printf("regex=%d\n", nefu::text::regex_self_test());
    printf("diff=%d\n", nefu::text::diff_self_test());
    printf("TOTAL=%d\n", nefu::text::text_all_self_test());
    return 0;
}
