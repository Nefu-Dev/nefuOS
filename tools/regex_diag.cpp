#include "textlib/regexlite.h"
#include <cstdio>
int main() {
    printf("rx-star=%d\n", nefu::text::regex_match("ab*c", "ac"));
    printf("rx-star2=%d\n", nefu::text::regex_match("ab*c", "abbbc"));
    printf("rx-class-no=%d\n", nefu::text::regex_match("[abc]+", "cabd"));
    printf("rx-digit-no=%d\n", nefu::text::regex_match("\\d+", "12a3"));
    printf("rx-range-no=%d\n", nefu::text::regex_match("[a-f]+", "deadbeefz"));
    printf("rx-email=%d\n", nefu::text::regex_match("\\w+@\\w+\\.\\w+", "a@b.com"));
    return 0;
}
