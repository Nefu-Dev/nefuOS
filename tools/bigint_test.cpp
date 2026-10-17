// Standalone unit test for the big-int algorithms used by core/apps/bigint.cpp
// (copied verbatim so the math is tested without the GUI). Compile:
//   g++ -std=c++17 -O2 -o bigint_test.exe tools/bigint_test.cpp && bigint_test.exe
#include <stdio.h>
#include <string.h>
#include <math.h>

const int BIG_MAX = 1024;
struct Big {
    unsigned int d[BIG_MAX];
    int n;
    bool neg;
};
static void big_zero(Big* b) { b->n = 0; b->neg = false; }
static void big_trim(Big* b) { while (b->n > 0 && b->d[b->n - 1] == 0) b->n--; }
static void big_set_u32(Big* b, unsigned int v) {
    b->n = 1; b->d[0] = v; b->neg = false; big_trim(b);
}
static void big_set_str(Big* b, const char* s) {
    b->n = 0; b->neg = false;
    const char* p = s;
    if (*p == '-') { b->neg = true; p++; }
    if (*p == '+') p++;
    while (*p == '0') p++;
    int len = 0;
    const char* q = p;
    while (q[len]) len++;
    if (len == 0) { b->d[b->n++] = 0; return; }
    int first = len % 9;
    if (first == 0) first = 9;
    unsigned int head = 0;
    for (int i = 0; i < first; i++) head = head * 10 + (unsigned int)(p[i] - '0');
    int gi = 0;
    for (int i = len; i > first; i -= 9) {
        int start = i - 9;
        unsigned int v = 0;
        for (int j = start; j < i; j++) v = v * 10 + (unsigned int)(p[j] - '0');
        b->d[gi++] = v;
    }
    b->d[gi++] = head;
    b->n = gi;
    big_trim(b);
    if (b->n == 1 && b->d[0] == 0) b->neg = false;
}
static int big_cmp(const Big* a, const Big* b) {
    if (a->n != b->n) return a->n > b->n ? 1 : -1;
    for (int i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i]) return a->d[i] > b->d[i] ? 1 : -1;
    }
    return 0;
}
static void big_add(Big* r, const Big* a, const Big* b) {
    int n = a->n > b->n ? a->n : b->n;
    unsigned long long carry = 0;
    for (int i = 0; i < n; i++) {
        unsigned long long v = carry;
        if (i < a->n) v += a->d[i];
        if (i < b->n) v += b->d[i];
        r->d[i] = (unsigned int)(v % 1000000000ULL);
        carry = v / 1000000000ULL;
    }
    r->n = n;
    if (carry) { r->d[r->n++] = (unsigned int)carry; }
    r->neg = false;
    big_trim(r);
}
static void big_sub(Big* r, const Big* a, const Big* b) {
    long long borrow = 0;
    int n = a->n;
    for (int i = 0; i < n; i++) {
        long long v = (long long)a->d[i] - borrow - (i < b->n ? (long long)b->d[i] : 0);
        if (v < 0) { v += 1000000000LL; borrow = 1; } else borrow = 0;
        r->d[i] = (unsigned int)v;
    }
    r->n = n;
    r->neg = false;
    big_trim(r);
}
static void big_mul_small(Big* r, const Big* a, unsigned int k) {
    unsigned long long carry = 0;
    for (int i = 0; i < a->n; i++) {
        unsigned long long v = (unsigned long long)a->d[i] * k + carry;
        r->d[i] = (unsigned int)(v % 1000000000ULL);
        carry = v / 1000000000ULL;
    }
    r->n = a->n;
    while (carry) { r->d[r->n++] = (unsigned int)(carry % 1000000000ULL); carry /= 1000000000ULL; }
    r->neg = a->neg;
    big_trim(r);
}
static void big_add_small(Big* r, const Big* a, unsigned int k) {
    unsigned long long carry = k;
    int i = 0;
    while (i < a->n || carry) {
        unsigned long long v = carry + (i < a->n ? a->d[i] : 0);
        r->d[i] = (unsigned int)(v % 1000000000ULL);
        carry = v / 1000000000ULL;
        i++;
    }
    r->n = i;
    r->neg = a->neg;
    big_trim(r);
}
static void big_mul(Big* r, const Big* a, const Big* b) {
    Big A = *a, B = *b;
    int an = A.n, bn = B.n;
    for (int i = 0; i < an + bn; i++) r->d[i] = 0;
    r->n = an + bn;
    for (int i = 0; i < an; i++) {
        unsigned long long carry = 0;
        for (int j = 0; j < bn; j++) {
            unsigned long long v = (unsigned long long)A.d[i] * B.d[j] + r->d[i + j] + carry;
            r->d[i + j] = (unsigned int)(v % 1000000000ULL);
            carry = v / 1000000000ULL;
        }
        int k = bn;
        while (carry) {
            unsigned long long v = (unsigned long long)r->d[i + k] + carry;
            r->d[i + k] = (unsigned int)(v % 1000000000ULL);
            carry = v / 1000000000ULL;
            k++;
        }
    }
    r->neg = A.neg != B.neg;
    big_trim(r);
}
static void big_divmod(Big* q, Big* rem, const Big* a, const Big* b) {
    big_zero(q);
    big_zero(rem);
    if (b->n == 0 || (b->n == 1 && b->d[0] == 0)) return;
    Big cur;
    big_zero(&cur);
    Big qm;
    qm.n = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        Big t;
        big_mul_small(&t, &cur, 1000000000u);
        big_add_small(&cur, &t, a->d[i]);
        if (big_cmp(&cur, b) < 0) {
            if (qm.n > 0) qm.d[qm.n++] = 0;
            continue;
        }
        unsigned int lo = 0, hi = 1000000000u;
        while (lo + 1 < hi) {
            unsigned int mid = lo + (hi - lo) / 2;
            Big t2;
            big_mul_small(&t2, b, mid);
            if (big_cmp(&t2, &cur) <= 0) lo = mid; else hi = mid;
        }
        qm.d[qm.n++] = lo;
        Big t3;
        big_mul_small(&t3, b, lo);
        big_sub(&cur, &cur, &t3);
    }
    q->n = qm.n;
    for (int i = 0; i < qm.n; i++) q->d[i] = qm.d[qm.n - 1 - i];
    if (q->n == 0) q->d[q->n++] = 0;
    big_trim(q);
    *rem = cur;
    big_trim(rem);
}
static unsigned int big_mod_small(const Big* a, unsigned int k) {
    unsigned long long r = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        r = (r % k) * 1000000000ULL + a->d[i];
    }
    return (unsigned int)(r % k);
}
static void big_pow(Big* r, const Big* a, int e) {
    big_set_u32(r, 1);
    Big base = *a;
    for (int i = 0; i < e; i++) big_mul(r, r, &base);
    if (e == 0) big_set_u32(r, 1);
}
static void big_fact(Big* r, int n) {
    big_set_u32(r, 1);
    for (int i = 2; i <= n; i++) big_mul_small(r, r, (unsigned int)i);
}
static void big_fib(Big* r, int n) {
    Big a, b;
    big_set_u32(&a, 0);
    big_set_u32(&b, 1);
    for (int i = 0; i < n; i++) {
        Big c;
        big_add(&c, &a, &b);
        a = b;
        b = c;
    }
    *r = a;
}
static int big_digits(const Big* a) {
    if (a->n == 0) return 1;
    unsigned int top = a->d[a->n - 1];
    int t = 1;
    while (top >= 10) { top /= 10; t++; }
    return (a->n - 1) * 9 + t;
}
static void big_to_str(char* out, size_t n, const Big* a) {
    size_t w = 0;
    if (a->neg && w < n - 1) out[w++] = '-';
    if (a->n == 0) { out[w++] = '0'; out[w] = 0; return; }
    int digits = big_digits(a);
    char tmp[520];
    int tn = 0;
    unsigned int top = a->d[a->n - 1];
    char tbuf[12];
    int tt = 0;
    if (top == 0) tbuf[tt++] = '0';
    while (top > 0 && tt < 11) { tbuf[tt++] = (char)('0' + top % 10); top /= 10; }
    for (int i = tt - 1; i >= 0 && tn < 510; i--) tmp[tn++] = tbuf[i];
    for (int i = a->n - 2; i >= 0 && tn < 510; i--) {
        unsigned int v = a->d[i];
        char cb[12];
        int cn = 0;
        for (int j = 0; j < 9; j++) { cb[cn++] = (char)('0' + v % 10); v /= 10; }
        for (int j = 8; j >= 0 && tn < 510; j--) tmp[tn++] = cb[j];
    }
    size_t shown = 0;
    for (int i = 0; i < tn && w < n - 1; i++) { out[w++] = tmp[i]; shown++; }
    if (shown < (size_t)digits && w < n - 2) {
        char suf[40];
        snprintf(suf, sizeof(suf), "...(%d digits)", digits);
        for (int i = 0; suf[i] && w < n - 1; i++) out[w++] = suf[i];
    }
    out[w] = 0;
}

static int failures = 0;
static void check_str(const char* what, const char* got, const char* expect) {
    if (strcmp(got, expect) != 0) {
        printf("FAIL %-42s got=%s expect=%s\n", what, got, expect);
        failures++;
    }
}
static void check_i(const char* what, long long got, long long expect) {
    if (got != expect) {
        printf("FAIL %-42s got=%lld expect=%lld\n", what, got, expect);
        failures++;
    }
}
static void eval_to(char* out, size_t n, void (*fn)(Big*, const Big*, const Big*), const char* a, const char* b) {
    Big A, B, R;
    big_set_str(&A, a);
    big_set_str(&B, b);
    fn(&R, &A, &B);
    big_to_str(out, n, &R);
}
static void div_wrap(Big* r, const Big* a, const Big* b) { Big q2, r2; big_divmod(&q2, &r2, a, b); *r = q2; }
static void fact_wrap(Big* r, const Big* /*unused*/, int p) { big_fact(r, p); }
static void fib_wrap(Big* r, const Big* /*unused*/, int p) { big_fib(r, p); }
static void unary_to(char* out, size_t n, void (*fn)(Big*, const Big*, int), const char* a, int p) {
    Big A, R;
    big_set_str(&A, a);
    fn(&R, &A, p);
    big_to_str(out, n, &R);
}

int main() {
    char s[600];
    printf("stage rt\n"); fflush(stdout);
    // parse / print roundtrips
    struct R { const char* in; const char* expect; };
    const R rt[] = {
        {"0", "0"}, {"123", "123"}, {"123456789", "123456789"},
        {"12345678901234567890", "12345678901234567890"},
        {"999999999999999999", "999999999999999999"},
        {"1000000000000000000", "1000000000000000000"},
        {"00042", "42"}, {"-98765432109876543210", "-98765432109876543210"},
    };
    for (size_t i = 0; i < sizeof(rt)/sizeof(rt[0]); i++) {
        fprintf(stderr, "rt[%d]=%s\n", (int)i, rt[i].in);
        Big B;
        big_set_str(&B, rt[i].in);
        fprintf(stderr, "  set ok n=%d\n", B.n);
        big_to_str(s, sizeof(s), &B);
        fprintf(stderr, "  str=%s\n", s);
        check_str(rt[i].in, s, rt[i].expect);
    }
    printf("stage add\n"); fflush(stdout);
    // addition
    eval_to(s, sizeof(s), big_add, "99999", "88888");        check_str("add 99999+88888", s, "188887");
    eval_to(s, sizeof(s), big_add, "99999999999999999999", "1"); check_str("add 20x9 + 1", s, "100000000000000000000");
    eval_to(s, sizeof(s), big_add, "12345678901234567890", "98765432109876543210"); check_str("add big+big", s, "111111111011111111100");
    // subtraction (a >= b)
    eval_to(s, sizeof(s), big_sub, "100000000000000000000", "1"); check_str("sub 1e20 - 1", s, "99999999999999999999");
    eval_to(s, sizeof(s), big_sub, "111111111011111111100", "12345678901234567890"); check_str("sub big-big", s, "98765432109876543210");
    printf("stage mul\n"); fflush(stdout);
    // multiplication
    eval_to(s, sizeof(s), big_mul, "12345", "6789");          check_str("mul 12345*6789", s, "83810205");
    eval_to(s, sizeof(s), big_mul, "123456789", "987654321"); check_str("mul 123456789*987654321", s, "121932631112635269");
    eval_to(s, sizeof(s), big_mul, "99999999999999999999", "99999999999999999999"); check_str("mul 20x9^2", s, "9999999999999999999800000000000000000001");
    printf("stage div\n"); fflush(stdout);
    // division
    eval_to(s, sizeof(s), div_wrap, "100", "7");              check_str("div 100/7", s, "14");
    {
        Big A, B, Q, R2;
        big_set_str(&A, "12345678901234567890");
        big_set_str(&B, "1000000000");
        big_divmod(&Q, &R2, &A, &B);
        big_to_str(s, sizeof(s), &Q);  check_str("div 12345678901234567890/1e9 (q)", s, "12345678901");
        big_to_str(s, sizeof(s), &R2); check_str("div 12345678901234567890/1e9 (r)", s, "234567890");
    }
    eval_to(s, sizeof(s), div_wrap, "100000000000000000000", "99999999999999999999"); check_str("div 1e20/20x9 (q=1)", s, "1");
    {
        Big A, B, Q, R2;
        big_set_str(&A, "100000000000000000000");
        big_set_str(&B, "99999999999999999999");
        big_divmod(&Q, &R2, &A, &B);
        big_to_str(s, sizeof(s), &R2); check_str("div 1e20 % 20x9 (r=1)", s, "1");
    }
    printf("stage pow/fact/fib\n"); fflush(stdout);
    // power
    unary_to(s, sizeof(s), big_pow, "2", 10);  check_str("pow 2^10", s, "1024");
    unary_to(s, sizeof(s), big_pow, "2", 100); check_str("pow 2^100", s, "1267650600228229401496703205376");
    unary_to(s, sizeof(s), big_pow, "3", 0);   check_str("pow 3^0", s, "1");
    // factorial
    unary_to(s, sizeof(s), fact_wrap, "x", 10); check_str("10!", s, "3628800");
    unary_to(s, sizeof(s), fact_wrap, "x", 25); check_str("25!", s, "15511210043330985984000000");
    unary_to(s, sizeof(s), fact_wrap, "x", 100);
    {
        Big F; big_fact(&F, 100);
        check_i("100! digits", big_digits(&F), 158);
        check_str("100! head", (big_to_str(s, sizeof(s), &F), s), "93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000");
    }
    // fibonacci
    unary_to(s, sizeof(s), fib_wrap, "x", 10);  check_str("F(10)", s, "55");
    unary_to(s, sizeof(s), fib_wrap, "x", 20);  check_str("F(20)", s, "6765");
    unary_to(s, sizeof(s), fib_wrap, "x", 100); check_str("F(100)", s, "354224848179261915075");
    // mod small + prime checks (as in PRIME mode)
    {
        Big A;
        big_set_str(&A, "12345678901234567890");
        check_i("mod 7 of 20-digit", (long long)big_mod_small(&A, 7), 1);
    }
    {
        Big A;
        big_set_str(&A, "97");
        check_i("97 mod 2", (long long)big_mod_small(&A, 2), 1);
        big_set_str(&A, "1000000007");
        check_i("1e9+7 mod 3", (long long)big_mod_small(&A, 3), (long long)(1000000007LL % 3));
    }
    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
