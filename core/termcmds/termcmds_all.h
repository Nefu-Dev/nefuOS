// =============================================================================
//  termcmds_all.h — nefuOS 终端命令扩展库聚合头
// -----------------------------------------------------------------------------
//  本文件定义:
//    * TermOutput      —— 命令输出回调结构(print/println)
//    * BufferTermOutput—— 把输出捕获到内存 String,供 self_test 断言
//    * term_format     —— 不依赖 libc stdio 的微型格式化器(支持 %d %u %x %s %c %%)
//    * 一组跨命令复用的字符串/行处理工具(均为 inline,无 STL)
//  各子模块的 xxx_self_test() 在此汇总声明,termcmds_self_test() 统一调用。
//
//  约束: 无 STL 容器 / 无异常 / 无 RTTI; 内存用 new[]/delete[] 与 nefu::List/String。
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include "klib/klib.h"   // nefu::String, nefu::List, memcpy/memset/strlen...

namespace nefu {
namespace termcmds {

// =============================================================================
//  TermOutput —— 命令的输出Sink。真实终端里 print/println 指向终端写入函数;
//  单元测试里指向 BufferTermOutput 的捕获回调。
// =============================================================================
struct TermOutput {
    void (*print)(const char* s, void* user);   // 写一段(不换行)
    void (*println)(const char* s, void* user); // 写一行(内部补 '\n')
    void* user;

    // ---- 便捷包装 ----
    void p(const char* s)            { if (print)   print(s ? s : "", user); }
    void pln(const char* s)          { if (println) println(s ? s : "", user); }
    void pln()                       { if (println) println("", user); }
    void pch(char c)                 { char b[2]; b[0] = c; b[1] = 0; p(b); }

    // 用微型格式化器输出(见 term_format)
    void pf(const char* fmt, ...);
    void pfln(const char* fmt, ...);
};

// =============================================================================
//  BufferTermOutput —— 把所有输出累积进一个 nefu::String,便于测试断言。
//  用法:
//      BufferTermOutput buf;
//      TermOutput* out = buf.out();
//      cmd_xxx(2, argv, out);
//      assert(buf.buf.find("expected") >= 0);
// =============================================================================
struct BufferTermOutput {
    nefu::String buf;   // 累积的全部输出(含换行)

    static void cb_print(const char* s, void* u) {
        BufferTermOutput* self = (BufferTermOutput*)u;
        self->buf += s;
    }
    static void cb_println(const char* s, void* u) {
        BufferTermOutput* self = (BufferTermOutput*)u;
        self->buf += s;
        self->buf += "\n";
    }

    // 填充一个 TermOutput 指向本对象
    TermOutput out() {
        TermOutput t;
        t.print   = &BufferTermOutput::cb_print;
        t.println = &BufferTermOutput::cb_println;
        t.user    = this;
        return t;
    }

    // 断言辅助: 是否包含子串
    bool contains(const char* s) const { return buf.find(s) >= 0; }
    // 行数(按 '\n' 计)
    int line_count() const {
        int n = 0;
        for (int i = 0; i < buf.len(); i++) if (buf[i] == '\n') n++;
        return n;
    }
    void clear() { buf.clear(); }
};

// =============================================================================
//  微型格式化器 —— 不引入 <cstdio>,在裸机与宿主下都可用。
//  支持: %%  %c  %s  %d/%i  %u  %x(小写)  %X(大写)
//        %Nd / %Ns(最小宽度,右侧补空格)  %0Nd(零填充)
//  返回写入 out 的字符数(不含结尾 NUL)。outsz 为缓冲容量。
// =============================================================================
int  term_format(char* out, int outsz, const char* fmt, va_list ap);
int  term_snprintf(char* out, int outsz, const char* fmt, ...);

// =============================================================================
//  共享字符串工具(均为 inline,中文注释)
// =============================================================================

// 字符串长度(直接转发 nefu::strlen)
inline int tlen(const char* s) { return s ? (int)nefu::strlen(s) : 0; }

// 转小写字母
inline char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
inline char to_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

// 大小写不敏感比较
inline int str_cmpi(const char* a, const char* b) {
    while (*a && *b) {
        char ca = to_lower(*a), cb = to_lower(*b);
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    return (int)to_lower(*a) - (int)to_lower(*b);
}

// 判断是否空白字符(空格/Tab)
inline bool is_sp(char c) { return c == ' ' || c == '\t'; }
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

// 去掉首尾空白,返回新 String
inline nefu::String str_trim(const char* s) {
    nefu::String r;
    if (!s) return r;
    while (*s && is_sp(*s)) s++;
    int len = (int)nefu::strlen(s);
    const char* e = s + len - 1;
    while (e >= s && is_sp(*e)) e--;
    int n = (int)(e - s) + 1;
    if (n <= 0) return r;
    r.reserve(n + 1);
    for (int i = 0; i < n; i++) r += s[i];
    return r;
}

// 前缀判断
inline bool str_starts(const char* s, const char* pre) {
    while (*pre) {
        if (*s != *pre) return false;
        s++; pre++;
    }
    return true;
}

// 把一段文本按 '\n' 切成若干行(去掉每行末尾 '\r'),追加到 out
inline void split_lines(const char* text, nefu::List<nefu::String>& out) {
    if (!text) return;
    int start = 0;
    int n = (int)nefu::strlen(text);
    for (int i = 0; i <= n; i++) {
        if (text[i] == '\n' || text[i] == 0) {
            int len = i - start;
            // 去掉行尾 '\r'
            while (len > 0 && (text[start + len - 1] == '\r')) len--;
            // 文本以 '\n' 结尾时, 末尾不再补一个空行
            if (len > 0 || text[i] != 0)
                out.push(nefu::String(text + start, len));
            start = i + 1;
            if (text[i] == 0) break;
        }
    }
}

// 把若干行重新拼成以 '\n' 连接的文本
inline nefu::String join_lines(const nefu::List<nefu::String>& lines) {
    nefu::String r;
    for (int i = 0; i < lines.size(); i++) {
        r += lines[i];
        if (i + 1 < lines.size()) r += "\n";
    }
    return r;
}

// 简单模式匹配: 支持字面量 + '*' 通配(任意多字符)。用于 which/find/which。
bool glob_match(const char* pattern, const char* text);

// 确定性伪随机数(LCG, glibc 常数),带可设种子。funcmd / netcmd ping 用。
struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 1u) {}
    uint32_t next() {
        state = state * 1103515245u + 12345u;
        return (state >> 16) & 0x7fff;
    }
    // [lo, hi] 闭区间
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + (int)(next() % (uint32_t)(hi - lo + 1));
    }
};

// =============================================================================
//  各子模块 self_test 声明 —— 返回失败用例数(0 = 全部通过)
// =============================================================================
int filecmd_self_test();
int textcmd_self_test();
int syscmd_self_test();
int netcmd_self_test();
int devcmd_self_test();
int funcmd_self_test();
int extracmd_self_test();

// 汇总: 依次调用上面六个 self_test,打印结果,返回总失败数。
int  termcmds_self_test();

// 模块级全局状态: 当命令没有给定文件参数时,从这里读取"标准输入"。
// 真实终端把它接到 shell 的管道缓冲; self_test 直接赋值。
extern nefu::String* g_term_stdin;

// 模块级时钟(秒)。真实系统可指向 platform; 测试里可覆盖以验证 date/cal。
extern uint32_t (*g_term_now_sec)();

} // namespace termcmds
} // namespace nefu
