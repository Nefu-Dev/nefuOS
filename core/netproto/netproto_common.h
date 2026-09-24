// nefuOS 网络协议栈 —— 公共基础原语
// 本头文件被 ethernet/arp/ip/icmp/udp/tcp 等模块共同包含。
// 提供：
//   1) 网络字节序（大端）读写原语 be16/be32/put_be16/put_be32
//      —— 全部用移位实现，不依赖主机字节序，bare/host 行为一致。
//   2) 互联网校验和（RFC 1071，16 位反码求和）：
//      csum_add 累加，csum_fold 折叠取反。TCP/UDP 的伪首部也在这里。
//   3) 几个小内联工具 np_min/np_max/np_clamp。
// 设计约束：不使用 STL、不抛异常、不做 RTTI、不用 FPU（纯整数）。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

// ===================== 网络字节序原语 =====================
// 网络协议一律大端（高位在前）。下面四个函数按字节手动拼装，
// 这样无论主机是小端（x86）还是大端，编码结果都相同。

// 读取大端 16 位
inline uint16_t be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

// 写入大端 16 位
inline void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

// 读取大端 32 位
inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

// 写入大端 32 位
inline void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8)  & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

// ===================== 互联网校验和（RFC 1071） =====================
// 算法：把数据看成一串 16 位大端字，全部累加（保留进位），最后
// 把高 16 位折回低 16 位再取反。校验和字段在校验时先置 0。
// 验证时：对整个报文（含校验和字段）求和折叠后结果应为 0x0000。

// 把 len 字节累加到 32 位运行和 sum 上（len 奇数时最后一字节按高 8 位算）。
uint32_t csum_add(uint32_t sum, const uint8_t* data, int len);

// 折叠：把 sum 的高 16 位反复加回低 16 位，最后取反，返回 16 位校验和。
uint16_t csum_fold(uint32_t sum);

// 一步到位：对一段数据计算最终校验和（起始 sum=0）。
// 常用于 ICMP（无伪首部）：先把校验和字段置 0，调用本函数得到校验和，
// 再写回校验和字段；验证时对整包调用本函数结果应为 0。
uint16_t csum_of(const uint8_t* data, int len);

// TCP/UDP 伪首部校验和：
//   伪首部 = src_ip(4) + dst_ip(4) + 0(1) + protocol(1) + tcp_udp_len(2)
// 再叠加真正的传输层报文。proto 取值见 ip.h（IPPROTO_ICMP/TCP/UDP）。
// layer_len = 传输层报文总长度（含 TCP/UDP 头部与数据）。
uint16_t transport_csum(const uint8_t* segment, int layer_len,
                        uint32_t src_ip, uint32_t dst_ip, uint8_t proto);

// ===================== 小工具 =====================
inline int np_min(int a, int b) { return a < b ? a : b; }
inline int np_max(int a, int b) { return a > b ? a : b; }
inline int np_clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 把内存按字节清零的安全封装：用 klib 的 memset（noinline 函数），
// 避免在 -O2 下手写清零循环被 MinGW 误优化成栈崩溃。
inline void np_zero(void* dst, int n) {
    nefu::memset(dst, 0, (size_t)n);
}

// 拷贝 n 字节（同样走 klib 的 memcpy，避免内建展开问题）。
inline void np_copy(void* dst, const void* src, int n) {
    nefu::memcpy(dst, src, (size_t)n);
}

// ===================== Q16.16 定点数 =====================
// bare 环境无 FPU，凡是要做小数运算（如 TCP RTO 指数退避、带宽估计）
// 都用整数 Q16.16：高 16 位整数，低 16 位小数。
struct Fx {
    int32_t v;    // 原始定点值

    Fx() : v(0) {}
    static Fx from_int(int i)    { Fx f; f.v = (int32_t)i << 16; return f; }
    static Fx from_raw(int32_t r){ Fx f; f.v = r; return f; }
    int   to_int() const        { return v >> 16; }
    float to_float() const;      // 仅 host 调试用，bare 不调
    Fx operator+(const Fx& o) const { Fx r; r.v = v + o.v; return r; }
    Fx operator-(const Fx& o) const { Fx r; r.v = v - o.v; return r; }
    Fx operator*(const Fx& o) const {
        // 64 位中间结果再右移 16，避免溢出
        Fx r; r.v = (int32_t)(((int64_t)v * o.v) >> 16); return r;
    }
    Fx operator/(const Fx& o) const {
        Fx r; r.v = (int32_t)(((int64_t)v << 16) / o.v); return r;
    }
    bool operator==(const Fx& o) const { return v == o.v; }
    bool operator<(const Fx& o) const  { return v < o.v; }
};

// ===================== hex dump =====================
// 把 n 字节二进制格式化成 "00 11 22 ..." 文本（调试/netlab 用）。
// out 至少 n*3+1 字节。返回 out。
char* np_hex_dump(const uint8_t* data, int n, char* out, int outsz);

// 协议栈版本与模块信息
const char* netproto_version();
int         netproto_module_count();

// 把 "00:11:22:33:44:55" 解析成字节数组。sep 为分隔符。返回字节数，<0 失败。
int np_parse_hex(const char* s, uint8_t* out, int max_bytes, char sep);

// 把字节数组格式化成 "XX:XX:..." 文本。返回 out。
char* np_format_hex(const uint8_t* data, int n, char sep, char* out, int outsz);

int netproto_common_self_test();

} // namespace netproto
} // namespace nefu
