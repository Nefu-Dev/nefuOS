// nefuOS big-integer calculator (arbitrary precision decimals).
// Stores numbers as little-endian uint32 chunks in base 10^9, so up to
// ~9000 digits fit. Modes: A+B, A-B, A*B, A/B (quotient+remainder), A^B,
// A! (factorial), FIB(A) and prime test of A. Pure 32-bit arithmetic, no
// 64-bit division (uses uint64 only for chunk multiply accumulate).
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../sys/settings.h"

namespace nefu {

namespace {

const int WIN_W = 560, WIN_H = 470;
const int MAX_IN = 64;
const int BIG_MAX = 1024;          // chunks -> ~9216 decimal digits

struct Big {
    uint32_t d[BIG_MAX];
    int n;                         // number of chunks used
    bool neg;
};

static void big_zero(Big* b) { b->n = 0; b->neg = false; }
static void big_trim(Big* b) { while (b->n > 0 && b->d[b->n - 1] == 0) b->n--; }
static void big_set_u32(Big* b, uint32_t v) {
    b->n = 1; b->d[0] = v; b->neg = false; big_trim(b);
}
static void big_set_str(Big* b, const char* s) {
    b->n = 0; b->neg = false;
    const char* p = s;
    if (*p == '-') { b->neg = true; p++; }
    if (*p == '+') p++;
    while (*p == '0') p++;
    // length of the remaining digit string
    int len = 0;
    const char* q = p;
    while (q[len]) len++;
    if (len == 0) { b->d[b->n++] = 0; return; }
    int first = len % 9;                 // size of the MOST significant group
    if (first == 0) first = 9;
    uint32_t head = 0;
    for (int i = 0; i < first; i++) head = head * 10 + (uint32_t)(p[i] - '0');
    // 9-digit groups from the RIGHT (least significant first -> d[0])
    int gi = 0;
    for (int i = len; i > first; i -= 9) {
        int start = i - 9;
        uint32_t v = 0;
        for (int j = start; j < i; j++) v = v * 10 + (uint32_t)(p[j] - '0');
        b->d[gi++] = v;
    }
    b->d[gi++] = head;                    // most significant chunk last
    b->n = gi;
    big_trim(b);
    if (b->n == 1 && b->d[0] == 0) b->neg = false;
}

// value compare (ignores sign): returns 1/0/-1
static int big_cmp(const Big* a, const Big* b) {
    if (a->n != b->n) return a->n > b->n ? 1 : -1;
    for (int i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i]) return a->d[i] > b->d[i] ? 1 : -1;
    }
    return 0;
}

// r = a + b (both non-negative)
static void big_add(Big* r, const Big* a, const Big* b) {
    int n = a->n > b->n ? a->n : b->n;
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t v = carry;
        if (i < a->n) v += a->d[i];
        if (i < b->n) v += b->d[i];
        r->d[i] = (uint32_t)(v % 1000000000ULL);
        carry = v / 1000000000ULL;
    }
    r->n = n;
    if (carry) { r->d[r->n++] = (uint32_t)carry; }
    r->neg = false;
    big_trim(r);
}

// r = a - b, requires a >= b (values, non-negative)
static void big_sub(Big* r, const Big* a, const Big* b) {
    int64_t borrow = 0;
    int n = a->n;
    for (int i = 0; i < n; i++) {
        int64_t v = (int64_t)a->d[i] - borrow - (i < b->n ? (int64_t)b->d[i] : 0);
        if (v < 0) { v += 1000000000LL; borrow = 1; } else borrow = 0;
        r->d[i] = (uint32_t)v;
    }
    r->n = n;
    r->neg = false;
    big_trim(r);
}

// r = a * k (k < 1e9)
static void big_mul_small(Big* r, const Big* a, uint32_t k) {
    uint64_t carry = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t v = (uint64_t)a->d[i] * k + carry;
        r->d[i] = (uint32_t)(v % 1000000000ULL);
        carry = v / 1000000000ULL;
    }
    r->n = a->n;
    while (carry) { r->d[r->n++] = (uint32_t)(carry % 1000000000ULL); carry /= 1000000000ULL; }
    r->neg = a->neg;
    big_trim(r);
}

// r = a + k (k < 1e9); safe in-place (r == a)
static void big_add_small(Big* r, const Big* a, uint32_t k) {
    uint64_t carry = k;
    int i = 0;
    while (i < a->n || carry) {
        uint64_t v = carry + (i < a->n ? a->d[i] : 0);
        r->d[i] = (uint32_t)(v % 1000000000ULL);
        carry = v / 1000000000ULL;
        i++;
    }
    r->n = i;
    r->neg = a->neg;
    big_trim(r);
}

// r = a * b (schoolbook). Copies inputs first so r may alias a or b.
static void big_mul(Big* r, const Big* a, const Big* b) {
    Big A = *a, B = *b;                 // local copies (r may alias a/b)
    int an = A.n, bn = B.n;
    for (int i = 0; i < an + bn; i++) r->d[i] = 0;
    r->n = an + bn;
    for (int i = 0; i < an; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < bn; j++) {
            uint64_t v = (uint64_t)A.d[i] * B.d[j] + r->d[i + j] + carry;
            r->d[i + j] = (uint32_t)(v % 1000000000ULL);
            carry = v / 1000000000ULL;
        }
        int k = bn;
        while (carry) {
            uint64_t v = (uint64_t)r->d[i + k] + carry;
            r->d[i + k] = (uint32_t)(v % 1000000000ULL);
            carry = v / 1000000000ULL;
            k++;
        }
    }
    r->neg = A.neg != B.neg;
    big_trim(r);
}

// q = a / b, rem = a % b (b non-zero; values non-negative)
static void big_divmod(Big* q, Big* rem, const Big* a, const Big* b) {
    big_zero(q);
    big_zero(rem);
    if (b->n == 0 || (b->n == 1 && b->d[0] == 0)) return;
    Big cur;                            // running remainder (LSB first)
    big_zero(&cur);
    Big qm;                             // quotient chunks, MOST significant first
    qm.n = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        // cur = cur * 1e9 + a->d[i]
        Big t;
        big_mul_small(&t, &cur, 1000000000u);
        big_add_small(&cur, &t, a->d[i]);
        if (big_cmp(&cur, b) < 0) {
            if (qm.n > 0) qm.d[qm.n++] = 0;
            continue;
        }
        // binary search quotient digit (cur / b) in [0, 1e9)
        uint32_t lo = 0, hi = 1000000000u;
        while (lo + 1 < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            Big t2;
            big_mul_small(&t2, b, mid);
            if (big_cmp(&t2, &cur) <= 0) lo = mid; else hi = mid;
        }
        qm.d[qm.n++] = lo;
        Big t3;
        big_mul_small(&t3, b, lo);
        big_sub(&cur, &cur, &t3);
    }
    // reverse MSF quotient into LSB-first q
    q->n = qm.n;
    for (int i = 0; i < qm.n; i++) q->d[i] = qm.d[qm.n - 1 - i];
    if (q->n == 0) q->d[q->n++] = 0;
    big_trim(q);
    *rem = cur;
    big_trim(rem);
}

// a % k (k < 1e9)
static uint32_t big_mod_small(const Big* a, uint32_t k) {
    uint64_t r = 0;
    for (int i = a->n - 1; i >= 0; i--) {
        r = (r % k) * 1000000000ULL + a->d[i];
    }
    return (uint32_t)(r % k);
}

static void big_pow(Big* r, const Big* a, int e) {
    big_set_u32(r, 1);
    Big base = *a;
    for (int i = 0; i < e; i++) big_mul(r, r, &base);
    if (e == 0) big_set_u32(r, 1);
}

static void big_fact(Big* r, int n) {
    big_set_u32(r, 1);
    for (int i = 2; i <= n; i++) big_mul_small(r, r, (uint32_t)i);
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
    uint32_t top = a->d[a->n - 1];
    int t = 1;
    while (top >= 10) { top /= 10; t++; }
    return (a->n - 1) * 9 + t;
}

// render up to ~500 chars + "...(N digits)"
static void big_to_str(char* out, size_t n, const Big* a) {
    if (!out || n == 0) return;
    if (n == 1) { out[0] = 0; return; }
    size_t w = 0;
    if (a->neg && w < n - 1) out[w++] = '-';
    if (a->n == 0) { out[w++] = '0'; out[w] = 0; return; }
    // total decimal length
    int digits = big_digits(a);
    char tmp[520];
    int tn = 0;
    // most significant chunk (no padding), then 9-digit padded chunks
    uint32_t top = a->d[a->n - 1];
    char tbuf[12];
    int tt = 0;
    if (top == 0) tbuf[tt++] = '0';
    while (top > 0 && tt < 11) { tbuf[tt++] = (char)('0' + top % 10); top /= 10; }
    for (int i = tt - 1; i >= 0 && tn < 510; i--) tmp[tn++] = tbuf[i];
    for (int i = a->n - 2; i >= 0 && tn < 510; i--) {
        uint32_t v = a->d[i];
        char cb[12];
        int cn = 0;
        for (int j = 0; j < 9; j++) { cb[cn++] = (char)('0' + v % 10); v /= 10; }
        for (int j = 8; j >= 0 && tn < 510; j--) tmp[tn++] = cb[j];
    }
    // copy to out, possibly truncating
    size_t shown = 0;
    for (int i = 0; i < tn && w < n - 1; i++) { out[w++] = tmp[i]; shown++; }
    if (shown < (size_t)digits && w < n - 2) {
        char suf[40];
        ksprintf(suf, sizeof(suf), "...(%d digits)", digits);
        for (int i = 0; suf[i] && w < n - 1; i++) out[w++] = suf[i];
    }
    out[w] = 0;
}

// ---- UI ----
struct BiState {
    char a[MAX_IN], b[MAX_IN];
    int  cursor_a, cursor_b;
    int  focus;              // 0 = A, 1 = B
    int  mode;               // 0..7
    char result[600];
    int  result_digits;
    int  result_lines;
};

static BiState* bi_of(Window* w) { return (BiState*)w->userdata; }

static const char* s_mode_names[8] = {"A+B", "A-B", "A*B", "A/B", "A^B", "A!", "FIB", "PRIME"};

static void insert_at(char* buf, int* cur, const char* t) {
    int len = (int)strlen(buf);
    int tl = (int)strlen(t);
    if (len + tl >= MAX_IN - 1) return;
    for (int i = len; i >= *cur; i--) buf[i + tl] = buf[i];
    for (int i = 0; i < tl; i++) buf[*cur + i] = t[i];
    *cur += tl;
}

static void run(BiState* st) {
    Big A, B, R, R2;
    big_set_str(&A, st->a[0] ? st->a : "0");
    big_set_str(&B, st->b[0] ? st->b : "0");
    st->result[0] = 0;
    st->result_digits = 0;
    char tmp[600];
    char line1[600], line2[600];
    line1[0] = 0; line2[0] = 0;
    int digits1 = 0;

    switch (st->mode) {
    case 0: big_add(&R, &A, &B); break;
    case 1: {
        if (big_cmp(&A, &B) >= 0) { big_sub(&R, &A, &B); R.neg = false; }
        else { big_sub(&R, &B, &A); R.neg = true; }
        break;
    }
    case 2: big_mul(&R, &A, &B); break;
    case 3: {
        if (B.n == 1 && B.d[0] == 0) { strcpy(st->result, T("除数不能为 0", "division by zero")); return; }
        big_divmod(&R, &R2, &A, &B);
        big_to_str(line1, sizeof(line1), &R);
        big_to_str(line2, sizeof(line2), &R2);
        char t2[80];
        ksprintf(t2, sizeof(t2), T("商 %d 位，余数 %d 位", "quotient %d digits, rem %d digits"),
                 big_digits(&R), big_digits(&R2));
        strcat(line1, "\n");
        strcat(line1, line2);
        strcat(line1, "\n");
        strcat(line1, t2);
        strcpy(st->result, line1);
        return;
    }
    case 4: {
        if (st->b[0] && strlen(st->b) > 3) { strcpy(st->result, T("指数需 0~200", "exponent 0..200")); return; }
        int e = atoi(st->b[0] ? st->b : "0");
        if (e < 0 || e > 200) { strcpy(st->result, T("指数需 0~200", "exponent 0..200")); return; }
        if (e == 0) { big_set_u32(&R, 1); } else { big_pow(&R, &A, e); }
        break;
    }
    case 5: {
        if (st->a[0] && strlen(st->a) > 4) { strcpy(st->result, T("阶乘需 0~3000", "factorial 0..3000")); return; }
        int n = atoi(st->a[0] ? st->a : "0");
        if (n < 0 || n > 3000) { strcpy(st->result, T("阶乘需 0~3000", "factorial 0..3000")); return; }
        big_fact(&R, n);
        break;
    }
    case 6: {
        if (st->a[0] && strlen(st->a) > 5) { strcpy(st->result, T("需 F(0)~F(10000)", "F(0)..F(10000)")); return; }
        int n = atoi(st->a[0] ? st->a : "0");
        if (n < 0 || n > 10000) { strcpy(st->result, T("需 F(0)~F(10000)", "F(0)..F(10000)")); return; }
        big_fib(&R, n);
        break;
    }
    case 7: {
        // prime test: quick small check via long division mod small primes
        if (A.n == 1 && A.d[0] < 2) { strcpy(st->result, T("不是质数", "not prime")); return; }
        if (big_mod_small(&A, 2) == 0) {
            if (A.n == 1 && A.d[0] == 2) { strcpy(st->result, T("2 是质数", "2 is prime")); return; }
            strcpy(st->result, T("不是质数（可被 2 整除）", "not prime (divisible by 2)")); return;
        }
        // trial divide by odd numbers up to 46340
        for (uint32_t k = 3; k <= 46340; k += 2) {
            if (big_mod_small(&A, k) == 0) {
                char t3[80];
                ksprintf(t3, sizeof(t3), T("不是质数（最小因子 %u）", "not prime (factor %u)"), k);
                strcpy(st->result, t3);
                return;
            }
        }
        strcpy(st->result, T("是质数", "prime"));
        return;
    }
    }
    big_to_str(tmp, sizeof(tmp), &R);
    strcpy(st->result, tmp);
    st->result_digits = big_digits(&R);
}

static void paint(Window* w) {
    Surface& s = w->back;
    BiState* st = bi_of(w);
    int cw = w->content_w, ch = w->content_h;
    s.fill(0x00E9E7E1);

    gfx::text(s, 10, 6, T("大数计算器（任意精度整数）", "Big Integer Calculator"), 0x00222222, 0x00E9E7E1);

    // mode buttons
    for (int i = 0; i < 8; i++) {
        int x = 10 + i * 68;
        int y = 26;
        uint32_t bg = st->mode == i ? 0x002F6FB6 : 0x005A6A7A;
        gfx::fillrect(s, x, y, 64, 26, bg);
        gfx::rect(s, x, y, 64, 26, 0x001B2733);
        int tw = gfx::text_width(s_mode_names[i]);
        gfx::text(s, x + (64 - tw) / 2, y + 5, s_mode_names[i], 0x00FFFFFF, bg);
    }

    // inputs
    for (int i = 0; i < 2; i++) {
        int y = 60 + i * 30;
        const char* label = i == 0 ? "A =" : "B =";
        gfx::text(s, 10, y + 6, label, 0x00B03A2E, 0x00E9E7E1);
        gfx::fillrect(s, 44, y, cw - 54, 26, 0x00FFFFFF);
        gfx::rect(s, 44, y, cw - 54, 26, st->focus == i ? 0x002F6FB6 : 0x00C9C8C2);
        char* buf = i == 0 ? st->a : st->b;
        int cur = i == 0 ? st->cursor_a : st->cursor_b;
        char pre[MAX_IN], post[MAX_IN];
        pre[0] = 0; post[0] = 0;
        int len = (int)strlen(buf);
        int maxch = (cw - 60) / 8;
        int show = len - maxch;
        if (show < 0) show = 0;
        int cl = cur - show;
        if (cl < 0) cl = 0;
        if (cl > len - show) cl = len - show;
        if (cl > 0) { strncpy(pre, buf + show, cl); pre[cl] = 0; }
        if (show + cl < len) strcpy(post, buf + show + cl);
        gfx::text(s, 48, y + 6, pre, 0x001A1B1C, 0x00FFFFFF);
        gfx::fillrect(s, 48 + gfx::text_width(pre), y + 6, 6, 16, 0x002F6FB6);
        gfx::text(s, 48 + gfx::text_width(pre) + 8, y + 6, post, 0x001A1B1C, 0x00FFFFFF);
    }

    // action buttons
    {
        const char* acts[5] = {T("计算 GO", "GO"), T("清空", "CLR"), T("退格", "DEL"),
                               T("左移", "<-"), T("右移", "->")};
        for (int i = 0; i < 5; i++) {
            int x = 10 + i * 108;
            int y = 124;
            uint32_t bg = i == 0 ? 0x0027AE60 : 0x003B4654;
            gfx::fillrect(s, x, y, 104, 26, bg);
            gfx::rect(s, x, y, 104, 26, 0x001B2733);
            int tw = gfx::text_width(acts[i]);
            gfx::text(s, x + (104 - tw) / 2, y + 5, acts[i], 0x00FFFFFF, bg);
        }
    }

    // result header + box
    char hdr[96];
    ksprintf(hdr, sizeof(hdr), "%s  (%d %s)", T("结果", "Result"),
             st->result_digits > 0 ? st->result_digits : (int)strlen(st->result),
             T("位", "digits"));
    gfx::text(s, 10, 158, hdr, 0x00505050, 0x00E9E7E1);
    int ry = 176, rh = ch - ry - 8;
    gfx::fillrect(s, 8, ry, cw - 16, rh, 0x00FDFCF8);
    gfx::rect(s, 8, ry, cw - 16, rh, 0x00B8B4AC);
    // render result wrapped to box width
    int maxw = (cw - 24) / 8;
    int line = 0;
    int idx = 0;
    int len = (int)strlen(st->result);
    while (idx < len && line < rh / 16) {
        char lb[80];
        int n2 = 0;
        while (idx < len && n2 < maxw) lb[n2++] = st->result[idx++];
        lb[n2] = 0;
        gfx::text(s, 12, ry + 4 + line * 16, lb, 0x001A1B1C, 0x00FDFCF8);
        line++;
    }
}

static void bi_paint(Window* w) { paint(w); }

static void bi_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    BiState* st = bi_of(w);
    if (!st) return;
    char* buf = st->focus == 0 ? st->a : st->b;
    int* cur = st->focus == 0 ? &st->cursor_a : &st->cursor_b;
    if (e->keycode == KEY_BACKSPACE) {
        if (*cur > 0) {
            int l = (int)strlen(buf);
            for (int i = *cur - 1; i < l; i++) buf[i] = buf[i + 1];
            (*cur)--;
        }
        return;
    }
    if (e->keycode == KEY_LEFT) { if (*cur > 0) (*cur)--; return; }
    if (e->keycode == KEY_RIGHT) { if (*cur < (int)strlen(buf)) (*cur)++; return; }
    if (e->keycode == KEY_HOME) { *cur = 0; return; }
    if (e->keycode == KEY_END) { *cur = (int)strlen(buf); return; }
    if (e->keycode == KEY_ENTER) { run(st); return; }
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    // Tab / F2 switch focus
    if (e->keycode == KEY_TAB) { st->focus = 1 - st->focus; return; }
    char c = e->ascii;
    if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
        char ins[2] = {c, 0};
        insert_at(buf, cur, ins);
    }
}

static void bi_mouse(Window* w, int mx, int my, uint8_t buttons) {
    BiState* st = bi_of(w);
    if (!st) return;
    if (!(buttons & 1)) return;
    // focus inputs
    for (int i = 0; i < 2; i++) {
        int y = 60 + i * 30;
        if (my >= y && my < y + 26 && mx >= 44 && mx < w->content_w - 10) {
            st->focus = i;
            return;
        }
    }
    // mode buttons
    for (int i = 0; i < 8; i++) {
        int x = 10 + i * 68;
        if (my >= 26 && my < 52 && mx >= x && mx < x + 64) { st->mode = i; return; }
    }
    // action buttons
    for (int i = 0; i < 5; i++) {
        int x = 10 + i * 108;
        if (my >= 124 && my < 150 && mx >= x && mx < x + 104) {
            switch (i) {
            case 0: run(st); break;
            case 1: st->a[0] = 0; st->b[0] = 0; st->cursor_a = 0; st->cursor_b = 0; st->result[0] = 0; st->result_digits = 0; break;
            case 2: {
                char* buf = st->focus == 0 ? st->a : st->b;
                int* cur = st->focus == 0 ? &st->cursor_a : &st->cursor_b;
                if (*cur > 0) {
                    int l = (int)strlen(buf);
                    for (int k = *cur - 1; k < l; k++) buf[k] = buf[k + 1];
                    (*cur)--;
                }
                break;
            }
            case 3: if (st->focus == 0 ? st->cursor_a : st->cursor_b > 0) (st->focus == 0 ? st->cursor_a : st->cursor_b)--; break;
            case 4: {
                int* cur = st->focus == 0 ? &st->cursor_a : &st->cursor_b;
                char* buf = st->focus == 0 ? st->a : st->b;
                if (*cur < (int)strlen(buf)) (*cur)++;
                break;
            }
            }
            return;
        }
    }
}

static void bi_close(Window* w) {
    if (w->userdata) delete (BiState*)w->userdata;
    w->userdata = 0;
}

} // namespace

void bigint_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("大数计算器", "Big Int Calculator"), x, y, WIN_W, WIN_H);
    if (!w) return;
    BiState* st = new BiState();
    st->a[0] = 0; st->b[0] = 0;
    st->cursor_a = 0; st->cursor_b = 0;
    st->focus = 0;
    st->mode = 0;
    st->result[0] = 0;
    st->result_digits = 0;
    w->userdata = st;
    w->on_paint = bi_paint;
    w->on_key = bi_key;
    w->on_mouse = bi_mouse;
    w->on_close = bi_close;
    g_wm->raise(w);
}

} // namespace nefu
