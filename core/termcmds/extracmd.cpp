// =============================================================================
//  extracmd.cpp —— 扩展命令与真实算法实现
// =============================================================================
#include "extracmd.h"

namespace nefu {
namespace termcmds {

// -----------------------------------------------------------------------------
//  base64 编解码(RFC 4648)
// -----------------------------------------------------------------------------
static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

nefu::String base64_encode(const char* data, int len) {
    nefu::String out;
    if (!data || len < 0) return out;
    int i = 0;
    while (i + 3 <= len) {
        uint32_t n = ((uint8_t)data[i] << 16) | ((uint8_t)data[i+1] << 8) | (uint8_t)data[i+2];
        out += B64_CHARS[(n >> 18) & 63];
        out += B64_CHARS[(n >> 12) & 63];
        out += B64_CHARS[(n >> 6) & 63];
        out += B64_CHARS[n & 63];
        i += 3;
    }
    int rem = len - i;
    if (rem == 1) {
        uint32_t n = (uint8_t)data[i] << 16;
        out += B64_CHARS[(n >> 18) & 63];
        out += B64_CHARS[(n >> 12) & 63];
        out += '='; out += '=';
    } else if (rem == 2) {
        uint32_t n = ((uint8_t)data[i] << 16) | ((uint8_t)data[i+1] << 8);
        out += B64_CHARS[(n >> 18) & 63];
        out += B64_CHARS[(n >> 12) & 63];
        out += B64_CHARS[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool base64_decode(const char* b64, nefu::String& out) {
    if (!b64) return false;
    uint32_t acc = 0;
    int bits = 0;
    for (const char* p = b64; *p; p++) {
        if (*p == '=' || *p == '\n' || *p == '\r') break;
        int v = b64_val(*p);
        if (v < 0) continue; // 跳过空白
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((acc >> bits) & 0xFF);
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
//  CRC32(表驱动, 多项式 0xEDB88320)
// -----------------------------------------------------------------------------
uint32_t crc32_calc(const char* data, int len) {
    static uint32_t table[256];
    static bool table_ready = false;
    if (!table_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        table_ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++)
        crc = table[(crc ^ (uint8_t)data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// -----------------------------------------------------------------------------
//  SHA-1(真实, FIPS 180-1, 输出 40 位十六进制)
// -----------------------------------------------------------------------------
static uint32_t sha_rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

nefu::String sha1_calc(const char* data, int len) {
    uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u, h4 = 0xC3D2E1F0u;

    // 计算填充后总长度(字节): 原数据 + 0x80 + 补零 + 8 字节大端长度(位)
    int bit_len = len * 8;
    int padded = len + 1;
    while (padded % 64 != 56) padded++;
    padded += 8;

    // 分配缓冲, 手动清零(避免 -O2 memset 误优化)
    uint8_t* msg = new uint8_t[padded];
    for (int i = 0; i < padded; i++) msg[i] = 0;
    for (int i = 0; i < len; i++) msg[i] = (uint8_t)data[i];
    msg[len] = 0x80;
    // 长度(位)写最后 8 字节, 大端
    for (int i = 0; i < 8; i++)
        msg[padded - 1 - i] = (uint8_t)((uint64_t)bit_len >> (8 * i));

    for (int off = 0; off < padded; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[off + i*4] << 24) |
                   ((uint32_t)msg[off + i*4 + 1] << 16) |
                   ((uint32_t)msg[off + i*4 + 2] << 8) |
                   ((uint32_t)msg[off + i*4 + 3]);
        for (int i = 16; i < 80; i++)
            w[i] = sha_rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a=h0, b=h1, c=h2, d=h3, e=h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
            uint32_t t = sha_rotl(a,5) + f + e + k + w[i];
            e = d; d = c; c = sha_rotl(b,30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    delete[] msg;

    nefu::String out;
    uint32_t hs[5] = {h0,h1,h2,h3,h4};
    const char* hex = "0123456789abcdef";
    for (int i = 0; i < 5; i++) {
        uint8_t* bytes = (uint8_t*)&hs[i];
        // 大端输出
        for (int j = 3; j >= 0; j--) {
            out += hex[(bytes[j] >> 4) & 0xF];
            out += hex[bytes[j] & 0xF];
        }
    }
    return out;
}

// -----------------------------------------------------------------------------
//  bc: 极简算术表达式求值(Shunting-yard + 双精度)
// -----------------------------------------------------------------------------
bool bc_eval(const char* expr, double* result) {
    if (!expr || !result) return false;
    nefu::List<double> vals;
    nefu::List<char>  ops;
    const char* p = expr;
    bool expect_operand = true;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '(') { ops.push('('); p++; expect_operand = true; continue; }
        if (*p == ')') {
            while (!ops.empty() && ops[ops.size()-1] != '(') {
                if (vals.size() < 2) return false;
                double b = vals.pop();
                double a = vals.pop();
                char op = ops.pop();
                double r = 0;
                if (op == '+') r = a + b; else if (op == '-') r = a - b;
                else if (op == '*') r = a * b; else { if (b == 0) return false; r = a / b; }
                vals.push(r);
            }
            if (ops.empty()) return false;
            ops.pop(); // 弹出 '('
            p++; expect_operand = false; continue;
        }
        if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
            char op = *p;
            if (expect_operand && (op == '-' || op == '+')) {
                if (op == '-') vals.push(0);
                ops.push('-');
                p++; expect_operand = true; continue;
            }
            int prec = (op == '*' || op == '/') ? 2 : 1;
            while (!ops.empty() && ops[ops.size()-1] != '(') {
                char top = ops[ops.size()-1];
                int tprec = (top == '*' || top == '/') ? 2 : 1;
                if (tprec < prec) break;
                if (vals.size() < 2) return false;
                double b = vals.pop();
                double a = vals.pop();
                char o = ops.pop();
                double r = 0;
                if (o == '+') r = a + b; else if (o == '-') r = a - b;
                else if (o == '*') r = a * b; else { if (b == 0) return false; r = a / b; }
                vals.push(r);
            }
            ops.push(op);
            p++; expect_operand = true; continue;
        }
        // 数字(手写 strtod)
        double ip = 0;
        while (*p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; }
        double frac = 0, scale = 0.1;
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9') { frac += (*p - '0') * scale; scale *= 0.1; p++; }
        }
        vals.push(ip + frac);
        expect_operand = false;
    }
    while (!ops.empty()) {
        if (vals.size() < 2) return false;
        double b = vals.pop();
        double a = vals.pop();
        char op = ops.pop();
        double r = 0;
        if (op == '+') r = a + b; else if (op == '-') r = a - b;
        else if (op == '*') r = a * b; else { if (b == 0) return false; r = a / b; }
        vals.push(r);
    }
    if (vals.size() != 1) return false;
    *result = vals[0];
    return true;
}

// -----------------------------------------------------------------------------
//  tr: 字符翻译/删除
// -----------------------------------------------------------------------------
nefu::String tr_translate(const char* text, const char* set1, const char* set2, bool delete_mode) {
    nefu::String out;
    if (!text) return out;
    int s2len = (set2 && !delete_mode) ? (int)nefu::strlen(set2) : 0;
    for (const char* p = text; *p; p++) {
        int idx = -1;
        for (int k = 0; set1 && set1[k]; k++) {
            if (*p == set1[k]) { idx = k; break; }
        }
        if (idx < 0) { out += *p; continue; }
        if (delete_mode) continue;
        char rep = (idx < s2len) ? set2[idx] : (s2len > 0 ? set2[s2len-1] : *p);
        out += rep;
    }
    return out;
}

// -----------------------------------------------------------------------------
//  cut: 按分隔符取列
// -----------------------------------------------------------------------------
nefu::String cut_field(const char* line, char sep, int field) {
    nefu::String out;
    if (!line || field < 1) return out;
    int cur = 1;
    const char* start = line;
    for (const char* p = line; ; p++) {
        if (*p == sep || *p == 0) {
            if (cur == field) { out = nefu::String(start, (int)(p - start)); break; }
            cur++;
            start = p + 1;
        }
        if (*p == 0) break;
    }
    return out;
}

// -----------------------------------------------------------------------------
//  comm: 两已排序列表的集合比较
// -----------------------------------------------------------------------------
void comm_compare(const nefu::List<nefu::String>& a,
                  const nefu::List<nefu::String>& b,
                  nefu::List<nefu::String>& only1,
                  nefu::List<nefu::String>& only2,
                  nefu::List<nefu::String>& both) {
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        int c = nefu::strcmp(a[i].c_str(), b[j].c_str());
        if (c == 0) { both.push(a[i]); i++; j++; }
        else if (c < 0) { only1.push(a[i]); i++; }
        else { only2.push(b[j]); j++; }
    }
    while (i < a.size()) only1.push(a[i++]);
    while (j < b.size()) only2.push(b[j++]);
}

// -----------------------------------------------------------------------------
//  diff: 基于 LCS 的行级 diff, 返回差异行数(简化: 输出增删行)
// -----------------------------------------------------------------------------
int diff_lines(const nefu::List<nefu::String>& a,
               const nefu::List<nefu::String>& b,
               nefu::List<nefu::String>& report) {
    int n = a.size(), m = b.size();
    // DP 表(动态分配, 避免栈溢出)
    int* dp = new int[(n+1) * (m+1)];
    for (int i = 0; i <= n; i++) dp[i*(m+1) + m] = 0;
    for (int j = 0; j <= m; j++) dp[n*(m+1) + j] = 0;
    for (int i = n - 1; i >= 0; i--) {
        for (int j = m - 1; j >= 0; j--) {
            if (nefu::strcmp(a[i].c_str(), b[j].c_str()) == 0)
                dp[i*(m+1) + j] = dp[(i+1)*(m+1) + (j+1)] + 1;
            else {
                int d1 = dp[(i+1)*(m+1) + j];
                int d2 = dp[i*(m+1) + (j+1)];
                dp[i*(m+1) + j] = d1 > d2 ? d1 : d2;
            }
        }
    }
    // 回溯
    int i = 0, j = 0;
    int changes = 0;
    while (i < n && j < m) {
        if (nefu::strcmp(a[i].c_str(), b[j].c_str()) == 0) { i++; j++; }
        else if (dp[(i+1)*(m+1) + j] >= dp[i*(m+1) + (j+1)]) {
            nefu::String line("- "); line += a[i]; report.push(line); changes++; i++;
        } else {
            nefu::String line("+ "); line += b[j]; report.push(line); changes++; j++;
        }
    }
    while (i < n) { nefu::String l("- "); l += a[i]; report.push(l); changes++; i++; }
    while (j < m) { nefu::String l("+ "); l += b[j]; report.push(l); changes++; j++; }
    delete[] dp;
    return changes;
}

// -----------------------------------------------------------------------------
//  seq: 生成 start..end (step) 的整数序列, 每行一个
// -----------------------------------------------------------------------------
void seq_generate(int start, int end, int step, nefu::List<nefu::String>& out) {
    if (step == 0) step = 1;
    if (step > 0) {
        for (int v = start; v <= end; v += step) {
            char buf[16];
            term_snprintf(buf, sizeof(buf), "%d", v);
            out.push(nefu::String(buf));
        }
    } else {
        for (int v = start; v >= end; v += step) {
            char buf[16];
            term_snprintf(buf, sizeof(buf), "%d", v);
            out.push(nefu::String(buf));
        }
    }
}

// -----------------------------------------------------------------------------
//  paste: 把等长行列表按列用 tab 拼接
// -----------------------------------------------------------------------------
nefu::String paste_join(const nefu::List<nefu::String>& a,
                        const nefu::List<nefu::String>& b) {
    nefu::String out;
    int n = a.size() > b.size() ? a.size() : b.size();
    for (int i = 0; i < n; i++) {
        if (i < a.size()) out += a[i].c_str();
        out += '\t';
        if (i < b.size()) out += b[i].c_str();
        out += '\n';
    }
    return out;
}

// -----------------------------------------------------------------------------
//  URL encode / decode
// -----------------------------------------------------------------------------
static bool unreserved(char c) {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
           c=='-'||c=='_'||c=='.'||c=='~';
}
nefu::String url_encode(const char* s) {
    nefu::String out;
    if (!s) return out;
    const char* hex = "0123456789ABCDEF";
    for (const char* p = s; *p; p++) {
        if (unreserved(*p)) out += *p;
        else {
            out += '%';
            out += hex[((uint8_t)*p >> 4) & 0xF];
            out += hex[(uint8_t)*p & 0xF];
        }
    }
    return out;
}
nefu::String url_decode(const char* s) {
    nefu::String out;
    if (!s) return out;
    for (const char* p = s; *p; p++) {
        if (*p == '%' && p[1] && p[2]) {
            int hi = (*(++p) >= 'A') ? ((*(p)|0x20)-'a'+10) : (*p-'0');
            int lo = (*(++p) >= 'A') ? ((*(p)|0x20)-'a'+10) : (*p-'0');
            out += (char)((hi << 4) | lo);
        } else if (*p == '+') {
            out += ' ';
        } else {
            out += *p;
        }
    }
    return out;
}

// -----------------------------------------------------------------------------
//  环境变量表(固定容量, 纯内存)
// -----------------------------------------------------------------------------
static const int ENV_CAP = 32;
static nefu::String env_names[ENV_CAP];
static nefu::String env_vals[ENV_CAP];
static int env_count = 0;

void env_clear() {
    for (int i = 0; i < env_count; i++) { env_names[i].clear(); env_vals[i].clear(); }
    env_count = 0;
}

void env_set(const char* name, const char* value) {
    if (!name || !value) return;
    for (int i = 0; i < env_count; i++) {
        if (nefu::strcmp(env_names[i].c_str(), name) == 0) {
            env_vals[i] = value;
            return;
        }
    }
    if (env_count >= ENV_CAP) return;
    env_names[env_count] = name;
    env_vals[env_count] = value;
    env_count++;
}

const char* env_get(const char* name) {
    if (!name) return 0;
    for (int i = 0; i < env_count; i++)
        if (nefu::strcmp(env_names[i].c_str(), name) == 0)
            return env_vals[i].c_str();
    return 0;
}

// -----------------------------------------------------------------------------
//  factorize: 试除法质因数分解
// -----------------------------------------------------------------------------
void factorize(uint32_t n, nefu::List<uint32_t>& factors) {
    factors.clear();
    if (n < 2) return;
    uint32_t d = 2;
    while (d * d <= n) {
        while (n % d == 0) { factors.push(d); n /= d; }
        d++;
    }
    if (n > 1) factors.push(n);
}

// -----------------------------------------------------------------------------
//  base32(RFC 4648)
// -----------------------------------------------------------------------------
static const char B32_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
nefu::String base32_encode(const char* data, int len) {
    nefu::String out;
    if (!data || len < 0) return out;
    int i = 0;
    while (i + 5 <= len) {
        uint64_t n = ((uint64_t)(uint8_t)data[i] << 32) |
                     ((uint64_t)(uint8_t)data[i+1] << 24) |
                     ((uint64_t)(uint8_t)data[i+2] << 16) |
                     ((uint64_t)(uint8_t)data[i+3] << 8) |
                     ((uint64_t)(uint8_t)data[i+4]);
        out += B32_CHARS[(n >> 35) & 31];
        out += B32_CHARS[(n >> 30) & 31];
        out += B32_CHARS[(n >> 25) & 31];
        out += B32_CHARS[(n >> 20) & 31];
        out += B32_CHARS[(n >> 15) & 31];
        out += B32_CHARS[(n >> 10) & 31];
        out += B32_CHARS[(n >> 5) & 31];
        out += B32_CHARS[n & 31];
        i += 5;
    }
    int rem = len - i;
    if (rem == 1) {
        uint64_t n = (uint64_t)(uint8_t)data[i] << 32;
        out += B32_CHARS[(n >> 35) & 31];
        out += B32_CHARS[(n >> 30) & 31];
        out += "======";
    } else if (rem == 2) {
        uint64_t n = ((uint64_t)(uint8_t)data[i] << 32) | ((uint64_t)(uint8_t)data[i+1] << 24);
        out += B32_CHARS[(n >> 35) & 31];
        out += B32_CHARS[(n >> 30) & 31];
        out += B32_CHARS[(n >> 25) & 31];
        out += B32_CHARS[(n >> 20) & 31];
        out += "====";
    } else if (rem == 3) {
        uint64_t n = ((uint64_t)(uint8_t)data[i] << 32) | ((uint64_t)(uint8_t)data[i+1] << 24) |
                     ((uint64_t)(uint8_t)data[i+2] << 16);
        out += B32_CHARS[(n >> 35) & 31];
        out += B32_CHARS[(n >> 30) & 31];
        out += B32_CHARS[(n >> 25) & 31];
        out += B32_CHARS[(n >> 20) & 31];
        out += B32_CHARS[(n >> 15) & 31];
        out += "===";
    } else if (rem == 4) {
        uint64_t n = ((uint64_t)(uint8_t)data[i] << 32) | ((uint64_t)(uint8_t)data[i+1] << 24) |
                     ((uint64_t)(uint8_t)data[i+2] << 16) | ((uint64_t)(uint8_t)data[i+3] << 8);
        out += B32_CHARS[(n >> 35) & 31];
        out += B32_CHARS[(n >> 30) & 31];
        out += B32_CHARS[(n >> 25) & 31];
        out += B32_CHARS[(n >> 20) & 31];
        out += B32_CHARS[(n >> 15) & 31];
        out += B32_CHARS[(n >> 10) & 31];
        out += B32_CHARS[(n >> 5) & 31];
        out += "=";
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Luhn 校验(银行卡号)
// -----------------------------------------------------------------------------
bool luhn_valid(const char* digits) {
    if (!digits) return false;
    int sum = 0, dbl = 0;
    int len = (int)nefu::strlen(digits);
    for (int i = len - 1; i >= 0; i--) {
        char c = digits[i];
        if (c < '0' || c > '9') continue;
        int d = c - '0';
        if (dbl) { d *= 2; if (d > 9) d -= 9; }
        sum += d;
        dbl = !dbl;
    }
    return sum % 10 == 0;
}

// -----------------------------------------------------------------------------
//  整数转任意进制(2..36)
// -----------------------------------------------------------------------------
nefu::String int_to_base(uint32_t v, int base) {
    nefu::String out;
    if (base < 2 || base > 36) return out;
    const char* dig = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[40]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = dig[v % (uint32_t)base]; v /= (uint32_t)base; }
    while (n > 0) out += tmp[--n];
    return out;
}

// -----------------------------------------------------------------------------
//  罗马数字 <-> 整数
// -----------------------------------------------------------------------------
static int roman_val(char c) {
    switch (c) {
        case 'I': return 1; case 'V': return 5; case 'X': return 10;
        case 'L': return 50; case 'C': return 100; case 'D': return 500;
        case 'M': return 1000;
    }
    return 0;
}
int roman_to_int(const char* s) {
    if (!s) return 0;
    int total = 0, prev = 0;
    for (int i = (int)nefu::strlen(s) - 1; i >= 0; i--) {
        int v = roman_val(s[i]);
        if (v < prev) total -= v; else total += v;
        prev = v;
    }
    return total;
}

// -----------------------------------------------------------------------------
//  tac: 逆序输出行
// -----------------------------------------------------------------------------
void tac_lines(const nefu::List<nefu::String>& in, nefu::List<nefu::String>& out) {
    for (int i = in.size() - 1; i >= 0; i--) out.push(in[i]);
}
// -----------------------------------------------------------------------------
//  Levenshtein 编辑距离(DP)
// -----------------------------------------------------------------------------
int levenshtein(const char* a, const char* b) {
    if (!a || !b) return -1;
    int n = (int)nefu::strlen(a), m = (int)nefu::strlen(b);
    int* dp = new int[(n+1)*(m+1)];
    for (int i = 0; i <= n; i++) dp[i*(m+1)] = i;
    for (int j = 0; j <= m; j++) dp[j] = j;
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int d1 = dp[(i-1)*(m+1) + j] + 1;
            int d2 = dp[i*(m+1) + (j-1)] + 1;
            int d3 = dp[(i-1)*(m+1) + (j-1)] + cost;
            int v = d1 < d2 ? d1 : d2;
            v = v < d3 ? v : d3;
            dp[i*(m+1) + j] = v;
        }
    int r = dp[n*(m+1) + m];
    delete[] dp;
    return r;
}

// -----------------------------------------------------------------------------
//  GCD / LCM(欧几里得)
// -----------------------------------------------------------------------------
uint32_t gcd_u32(uint32_t a, uint32_t b) {
    while (b) { uint32_t t = a % b; a = b; b = t; }
    return a;
}
uint32_t lcm_u32(uint32_t a, uint32_t b) {
    uint32_t g = gcd_u32(a, b);
    return g ? (a / g * b) : 0;
}

// -----------------------------------------------------------------------------
//  汉明距离(等长串按位比较)
// -----------------------------------------------------------------------------
int hamming(const char* a, const char* b) {
    if (!a || !b) return -1;
    int d = 0;
    while (*a && *b) { if (*a != *b) d++; a++; b++; }
    return d;
}

// -----------------------------------------------------------------------------
//  素数埃氏筛(标记 up_to 以内的素数, 命中素数追加到 out)
// -----------------------------------------------------------------------------
void sieve(int up_to, nefu::List<int>& out) {
    out.clear();
    if (up_to < 2) return;
    bool* sieve = new bool[up_to + 1];
    for (int i = 0; i <= up_to; i++) sieve[i] = true;
    sieve[0] = sieve[1] = false;
    for (int i = 2; i * i <= up_to; i++) {
        if (sieve[i]) {
            for (int j = i * i; j <= up_to; j += i) sieve[j] = false;
        }
    }
    for (int i = 2; i <= up_to; i++) if (sieve[i]) out.push(i);
    delete[] sieve;
}
// -----------------------------------------------------------------------------
//  base32 decode
// -----------------------------------------------------------------------------
static int b32_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}
nefu::String base32_decode(const char* b32) {
    nefu::String out;
    if (!b32) return out;
    uint32_t acc = 0; int bits = 0;
    for (const char* p = b32; *p; p++) {
        if (*p == '=') break;
        int v = b32_val(*p);
        if (v < 0) continue;
        acc = (acc << 5) | (uint32_t)v;
        bits += 5;
        if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 0xFF); }
    }
    return out;
}

// -----------------------------------------------------------------------------
//  IPv4 parse and subnet match
// -----------------------------------------------------------------------------
bool ip_prefix_parse(const char* s, uint32_t* out) {
    if (!s || !out) return false;
    uint32_t v = 0; int parts = 0, cur = 0, have = 0;
    for (const char* p = s; ; p++) {
        if (*p >= '0' && *p <= '9') { cur = cur*10 + (*p-'0'); have = 1; }
        else if (*p == '.' || *p == 0) {
            if (!have) return false;
            if (cur > 255) return false;
            v = (v << 8) | (uint32_t)cur;
            parts++; cur = 0; have = 0;
            if (*p == 0) break;
        } else return false;
    }
    if (parts != 4) return false;
    *out = v;
    return true;
}
bool ip_prefix_match(uint32_t ip, uint32_t net, int prefix) {
    if (prefix <= 0) return true;
    if (prefix >= 32) return ip == net;
    uint32_t mask = 0xFFFFFFFFu << (32 - prefix);
    return (ip & mask) == (net & mask);
}

// -----------------------------------------------------------------------------
//  CSV field escape
// -----------------------------------------------------------------------------
nefu::String csv_escape(const char* field) {
    nefu::String out; out += '"';
    for (const char* q = field ? field : ""; *q; q++) {
        if (*q == '"') { out += '"'; out += '"'; }
        else out += *q;
    }
    out += '"';
    return out;
}

// -----------------------------------------------------------------------------
//  string hashes: djb2 and FNV-1a
// -----------------------------------------------------------------------------
uint32_t hash_djb2(const char* s) {
    uint32_t h = 5381;
    if (!s) return h;
    for (const char* p = s; *p; p++) h = ((h << 5) + h) ^ (uint8_t)*p;
    return h;
}
uint32_t hash_fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    if (!s) return h;
    for (const char* p = s; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    return h;
}

// -----------------------------------------------------------------------------
//  xor stream cipher (symmetric, printable)
// -----------------------------------------------------------------------------
nefu::String xor_crypt(const char* data, int len, const char* key) {
    nefu::String out;
    if (!data || len < 0 || !key || !*key) return out;
    int klen = (int)nefu::strlen(key);
    for (int i = 0; i < len; i++)
        out += (char)((uint8_t)data[i] ^ (uint8_t)key[i % klen]);
    return out;
}


// -----------------------------------------------------------------------------
//  morse code encode (letters and digits only)
// -----------------------------------------------------------------------------
nefu::String morse_encode(const char* s) {
    nefu::String out;
    if (!s) return out;
    static const char* TAB[36] = {
        ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
        "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
        "..-","...-",".--","-..-","-.--","--..",
        "-----",".----","..---","...--","....-",".....","-....","--...","---..","----."
    };
    bool first = true;
    for (const char* p = s; *p; p++) {
        char c = (char)((*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p);
        int idx = -1;
        if (c >= 'a' && c <= 'z') idx = c - 'a';
        else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
        if (idx < 0) continue;
        if (!first) out += ' ';
        out += TAB[idx];
        first = false;
    }
    return out;
}

// -----------------------------------------------------------------------------
//  self_test
// -----------------------------------------------------------------------------
int extracmd_self_test() {
    int fails = 0;

    // base64: 已知向量 "foobar"
    {
        nefu::String e = base64_encode("foobar", 6);
        if (e != "Zm9vYmFy") fails++;
        nefu::String d;
        base64_decode("Zm9vYmFy", d);
        if (d != "foobar") fails++;
        nefu::String e2 = base64_encode("hello", 5);
        nefu::String d2;
        base64_decode(e2.c_str(), d2);
        if (d2 != "hello") fails++;
        nefu::String e3 = base64_encode("f", 1);
        if (e3.len() != 4 || e3[2] != '=') fails++;
    }
    // CRC32: 已知 "123456789" -> 0xCBF43926
    {
        uint32_t c = crc32_calc("123456789", 9);
        if (c != 0xCBF43926u) fails++;
    }
    // SHA-1: 已知 "" -> da39a3ee5e6b4b0d3255bfef95601890afd80709
    {
        nefu::String h = sha1_calc("", 0);
        if (h != "da39a3ee5e6b4b0d3255bfef95601890afd80709") fails++;
        // "abc" -> a9993e364706816aba3e25717850c26c9cd0d89d
        nefu::String h2 = sha1_calc("abc", 3);
        if (h2 != "a9993e364706816aba3e25717850c26c9cd0d89d") fails++;
    }
    // bc 求值
    {
        double r = 0;
        if (!bc_eval("1+2*3", &r) || !(r == 7.0)) fails++;
        if (!bc_eval("(1+2)*3", &r) || !(r == 9.0)) fails++;
        if (!bc_eval("10/4", &r) || !(r == 2.5)) fails++;
        if (!bc_eval("-5+3", &r) || !(r == -2.0)) fails++;
        if (bc_eval("1/0", &r)) fails++;
    }
    // tr
    {
        nefu::String t = tr_translate("abcabc", "abc", "XYZ", false);
        if (t != "XYZXYZ") fails++;
        nefu::String d = tr_translate("a1b2c3", "0123456789", 0, true);
        if (d != "abc") fails++;
    }
    // cut
    {
        nefu::String c = cut_field("a,b,c,d", ',', 3);
        if (c != "c") fails++;
    }
    // comm
    {
        nefu::List<nefu::String> a, b, o1, o2, both;
        a.push(nefu::String("a")); a.push(nefu::String("b")); a.push(nefu::String("c"));
        b.push(nefu::String("b")); b.push(nefu::String("c")); b.push(nefu::String("d"));
        comm_compare(a, b, o1, o2, both);
        if (o1.size() != 1 || o1[0] != "a") fails++;
        if (o2.size() != 1 || o2[0] != "d") fails++;
        if (both.size() != 2) fails++;
    }
    // diff / LCS
    {
        nefu::List<nefu::String> a, b, rep;
        a.push(nefu::String("a")); a.push(nefu::String("b")); a.push(nefu::String("c"));
        b.push(nefu::String("a")); b.push(nefu::String("x")); b.push(nefu::String("c"));
        int ch = diff_lines(a, b, rep);
        if (ch != 2) fails++; // -b +x
    }
    // seq
    {
        nefu::List<nefu::String> s;
        seq_generate(1, 5, 2, s);
        if (!(s.size()==3 && s[0]=="1" && s[1]=="3" && s[2]=="5")) fails++;
    }
    // URL 编解码
    {
        nefu::String e = url_encode("a b");
        if (e != "a%20b") fails++;
        nefu::String d = url_decode("a%20b");
        if (d != "a b") fails++;
    }
    // env
    {
        env_clear();
        env_set("HOME", "/root");
        if (!env_get("HOME") || nefu::strcmp(env_get("HOME"), "/root") != 0) fails++;
        if (env_get("NOPE")) fails++;
        env_set("HOME", "/home/nefu");
        if (!env_get("HOME") || nefu::strcmp(env_get("HOME"), "/home/nefu") != 0) fails++;
    }
    // factor
    {
        nefu::List<uint32_t> f;
        factorize(12, f);
        if (!(f.size() == 3 && f[0]==2 && f[1]==2 && f[2]==3)) fails++;
        factorize(97, f);
        if (!(f.size() == 1 && f[0]==97)) fails++;
    }
    // base32: "f" -> MY======
    {
        nefu::String e = base32_encode("f", 1);
        if (e != "MY======") fails++;
    }
    // luhn: 合法卡号 49927398716
    {
        if (!luhn_valid("49927398716")) fails++;
        if (luhn_valid("49927398717")) fails++;
    }
    // 进制转换
    {
        if (int_to_base(255, 16) != "ff") fails++;
        if (int_to_base(10, 2) != "1010") fails++;
    }
    // 罗马数字
    {
        if (roman_to_int("XIV") != 14) fails++;
        if (roman_to_int("MCMXCIV") != 1994) fails++;
    }
    // tac
    {
        nefu::List<nefu::String> in, out;
        in.push(nefu::String("a")); in.push(nefu::String("b")); in.push(nefu::String("c"));
        tac_lines(in, out);
        if (!(out.size()==3 && out[0]=="c" && out[1]=="b" && out[2]=="a")) fails++;
    }
    // levenshtein
    {
        if (levenshtein("kitten", "sitting") != 3) fails++;
        if (levenshtein("abc", "abc") != 0) fails++;
    }
    // gcd/lcm
    {
        if (gcd_u32(12, 18) != 6) fails++;
        if (lcm_u32(4, 6) != 12) fails++;
    }
    // hamming
    {
        if (hamming("karolin", "kathrin") != 3) fails++;
    }
    // sieve
    {
        nefu::List<int> p;
        sieve(30, p);
        // 30 以内素数: 2,3,5,7,11,13,17,19,23,29 = 10 个
        if (p.size() != 10) fails++;
        if (!(p[0]==2 && p[p.size()-1]==29)) fails++;
    }
    // base32 round trip
    {
        nefu::String e = base32_encode("foobar", 6);
        nefu::String d = base32_decode(e.c_str());
        if (d != "foobar") fails++;
    }
    // ipv4 parse / subnet
    {
        uint32_t ip = 0;
        if (!ip_prefix_parse("192.168.1.50", &ip)) fails++;
        uint32_t net = 0;
        if (!ip_prefix_parse("192.168.1.0", &net)) fails++;
        if (!ip_prefix_match(ip, net, 24)) fails++;
        uint32_t out2 = 0;
        if (!ip_prefix_parse("192.168.2.50", &out2)) fails++;
        if (ip_prefix_match(out2, net, 24)) fails++;
        if (ip_prefix_parse("999.1.1.1", &ip)) fails++;
    }
    // csv escape
    {
        nefu::String c = csv_escape("a\"b");
        if (c != "\"a\"\"b\"") fails++;
    }
    // hash deterministic + xor round trip + trim
    {
        uint32_t h1 = hash_djb2("hello");
        uint32_t h2 = hash_djb2("hello");
        if (h1 != h2) fails++;
        if (hash_djb2("hello") == hash_djb2("world")) fails++;
        nefu::String e = xor_crypt("secret", 6, "key");
        nefu::String d = xor_crypt(e.c_str(), e.len(), "key");
        if (d != "secret") fails++;
        nefu::String tr = str_trim("  hi  ");
        if (tr != "hi") fails++;
    }
    // morse: "SOS" -> "... --- ..."
    {
        nefu::String m = morse_encode("SOS");
        if (m != "... --- ...") fails++;
    }
    return fails;
}




} // namespace termcmds
} // namespace nefu
