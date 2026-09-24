// =============================================================================
//  termcmds_all.cpp —— 聚合头的实现: 微型格式化器 / glob / 全局状态 / 汇总测试
// =============================================================================
#include "termcmds_all.h"

namespace nefu {
namespace termcmds {

// ---- 模块级全局状态定义(弱符号语义: 只此一份) ----
nefu::String* g_term_stdin = 0;          // 命令的"标准输入"缓冲
static uint32_t default_now_sec() { return 1700000000u; } // 默认固定时刻,便于测试
uint32_t (*g_term_now_sec)() = &default_now_sec;

// -----------------------------------------------------------------------------
//  微型格式化器
// -----------------------------------------------------------------------------
static int write_char(char* out, int outsz, int pos, char c) {
    if (pos + 1 < outsz) out[pos] = c;
    return pos + 1;
}

// 把无符号数按进制转成字符串, 返回写入长度; width/prec 处理宽度与填充。
static int append_uint(char* out, int outsz, int pos, uint32_t v,
                       int base, bool upper, int width, bool zero_pad) {
    char tmp[16];
    int n = 0;
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = dig[v % (uint32_t)base]; v /= (uint32_t)base; }
    // 填充(宽度不足时补空格或 0)
    int pad = width - n;
    char padc = zero_pad ? '0' : ' ';
    while (pad-- > 0) pos = write_char(out, outsz, pos, padc);
    while (n > 0) pos = write_char(out, outsz, pos, tmp[--n]);
    return pos;
}

int term_format(char* out, int outsz, const char* fmt, va_list ap) {
    int pos = 0;
    if (outsz > 0) out[0] = 0;
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            pos = write_char(out, outsz, pos, fmt[i]);
            continue;
        }
        i++;
        // 解析宽度(可选)
        int width = 0;
        bool zero_pad = false;
        if (fmt[i] == '0') { zero_pad = true; i++; }
        while (is_digit(fmt[i])) { width = width * 10 + (fmt[i] - '0'); i++; }
        char spec = fmt[i];
        switch (spec) {
            case '%': pos = write_char(out, outsz, pos, '%'); break;
            case 'c': {
                char c = (char)va_arg(ap, int);
                pos = write_char(out, outsz, pos, c);
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                int slen = (int)nefu::strlen(s);
                int pad = width - slen;
                while (pad-- > 0) pos = write_char(out, outsz, pos, ' ');
                for (int k = 0; s[k]; k++) pos = write_char(out, outsz, pos, s[k]);
                break;
            }
            case 'd': case 'i': {
                int v = va_arg(ap, int);
                if (v < 0) {
                    pos = write_char(out, outsz, pos, '-');
                    if (width > 0) width--;
                    pos = append_uint(out, outsz, pos, (uint32_t)(-(uint32_t)v), 10, false, width, zero_pad);
                } else {
                    pos = append_uint(out, outsz, pos, (uint32_t)v, 10, false, width, zero_pad);
                }
                break;
            }
            case 'u':
                pos = append_uint(out, outsz, pos, va_arg(ap, uint32_t), 10, false, width, zero_pad);
                break;
            case 'x':
                pos = append_uint(out, outsz, pos, va_arg(ap, uint32_t), 16, false, width, zero_pad);
                break;
            case 'X':
                pos = append_uint(out, outsz, pos, va_arg(ap, uint32_t), 16, true, width, zero_pad);
                break;
            default:
                pos = write_char(out, outsz, pos, '%');
                pos = write_char(out, outsz, pos, spec);
                break;
        }
    }
    if (pos < outsz) out[pos] = 0;
    else if (outsz > 0) out[outsz - 1] = 0;
    return pos;
}

int term_snprintf(char* out, int outsz, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = term_format(out, outsz, fmt, ap);
    va_end(ap);
    return n;
}

// TermOutput 的格式化便捷方法
void TermOutput::pf(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    term_format(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    p(buf);
}
void TermOutput::pfln(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    term_format(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    pln(buf);
}

// -----------------------------------------------------------------------------
//  glob: 支持字面量与 '*' 通配(不支持 '?',够用且好测)
// -----------------------------------------------------------------------------
bool glob_match(const char* pattern, const char* text) {
    if (!pattern || !text) return false;
    // 递归式回溯: 遇到 '*' 尝试吃掉 0..n 个字符
    while (*pattern && *text) {
        if (*pattern == '*') {
            // 跳过连续 '*'
            while (*pattern == '*') pattern++;
            if (!*pattern) return true;   // 末尾 '*' 直接匹配
            for (int i = 0; text[i]; i++) {
                if (glob_match(pattern, text + i)) return true;
            }
            return *pattern == 0;
        }
        if (*pattern != *text) return false;
        pattern++; text++;
    }
    while (*pattern == '*') pattern++;
    return *pattern == 0 && *text == 0;
}

// -----------------------------------------------------------------------------
//  汇总 self_test
// -----------------------------------------------------------------------------
int termcmds_self_test() {
    int fails = 0;
    int r;
    r = filecmd_self_test();   if (r) fails += r;
    r = textcmd_self_test();   if (r) fails += r;
    r = syscmd_self_test();    if (r) fails += r;
    r = netcmd_self_test();    if (r) fails += r;
    r = devcmd_self_test();    if (r) fails += r;
    r = funcmd_self_test();    if (r) fails += r;
    r = extracmd_self_test();  if (r) fails += r;
    return fails;
}

} // namespace termcmds
} // namespace nefu
