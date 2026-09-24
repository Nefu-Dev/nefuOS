// nefuOS 网络协议栈 —— TCP（RFC 793）
//
// 头部（最小 20 字节）：
//   源端口(2) 目的端口(2) 序号(4) 确认号(4)
//   偏移/保留/标志(2) 窗口(2) 校验和(2) 紧急指针(2)
//
// 标志位：FIN=0x01 SYN=0x02 RST=0x04 PSH=0x08 ACK=0x10 URG=0x20
//
// 本模块：TCP 头部解析/封装（含伪首部校验和）、连接状态机
// （CLOSED/LISTEN/SYN_SENT/SYN_RECEIVED/ESTABLISHED/FIN_WAIT_1/2/
//   CLOSE_WAIT/LAST_ACK/CLOSING/TIME_WAIT）、序列号管理。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

const int TCP_HDR_LEN = 20;

// 标志位
const uint8_t TCP_FIN = 0x01;
const uint8_t TCP_SYN = 0x02;
const uint8_t TCP_RST = 0x04;
const uint8_t TCP_PSH = 0x08;
const uint8_t TCP_ACK = 0x10;
const uint8_t TCP_URG = 0x20;

// 连接状态
enum TcpState {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_CLOSING,
    TCP_TIME_WAIT,
    TCP_STATE_COUNT
};

struct TcpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;   // 头部长度（字节），通常 20
    uint8_t  flags;
    uint16_t window;
    uint16_t urgent;
};

// 解析 TCP 段。成功返回头部长度，失败 -1。
int tcp_parse(const uint8_t* seg, int len, TcpHeader& out);

// 封装 TCP 段头部（自动计算伪首部校验和）。
// payload/payload_len 是段内数据（可为空）。返回段总长度。
int tcp_pack(uint8_t* out, uint16_t src_port, uint16_t dst_port,
             uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window,
             const uint8_t* payload, int payload_len,
             uint32_t src_ip, uint32_t dst_ip);

// 校验一个入站 TCP 段的校验和（含伪首部）。合法返回 true。
bool tcp_verify_checksum(const uint8_t* seg, int seg_len,
                         uint32_t src_ip, uint32_t dst_ip);

const char* tcp_state_name(TcpState s);
const char* tcp_flags_str(uint8_t flags, char* buf, int bufsz);

// ---- 序列号比较（RFC 1982，mod 2^32 有符号序）----
// 判断 a < b / a <= b，考虑序列号回绕。
bool tcp_seq_lt(uint32_t a, uint32_t b);
bool tcp_seq_leq(uint32_t a, uint32_t b);
// 区间 [lo, hi) 是否覆盖 seq（用于确认到达判断）
bool tcp_seq_between(uint32_t lo, uint32_t seq, uint32_t hi);

// ---- TCP 选项 ----
// 选项类型
const uint8_t TCP_OPT_EOL   = 0;
const uint8_t TCP_OPT_NOP   = 1;
const uint8_t TCP_OPT_MSS   = 2;
const uint8_t TCP_OPT_WSCALE = 3;
const uint8_t TCP_OPT_SACKOK = 4;

struct TcpOptions {
    uint16_t mss;         // 0 = 未出现
    uint8_t  wscale;       // 0 = 未出现
    bool     sack_ok;
    int      opt_len;      // 头部里选项占用的字节数
};

// 从 TCP 头部（data_offset > 20 时其后是选项）解析选项。
// hdr_len 是 tcp_parse 返回的头部总长度。
void tcp_parse_options(const uint8_t* seg, int hdr_len, TcpOptions& out);
// TCP 选项 kind -> 名字
const char* tcp_option_name(uint8_t kind);

// 在已有的 20 字节头后写入 MSS 选项（用于 SYN）。返回新的头部总长。
int tcp_write_mss_option(uint8_t* out, uint16_t mss);

// ---- RTO 估计（RFC 6298，整数定点，避免 FPU）----
// srtt / rttvar 以 8 倍精度存储（左移 3），rto 以毫秒为单位。
struct TcpRtoEstimator {
    int srtt_scaled;    // SRTT << 3
    int rttvar_scaled;  // RTTVAR << 2
    int rto_ms;
    bool initialized;

    void reset();
    // 采样一个 RTT（毫秒），更新估计，返回新的 RTO（毫秒）。
    int sample(int rtt_ms);
};

// ---- 滑动窗口 / 拥塞控制（极简整数模型）----
struct TcpWindow {
    int cwnd;        // 拥塞窗口（ MSS 倍数，最小 1）
    int ssthresh;    // 慢启动阈值
    int snd_wnd;     // 对端通告窗口（字节）
    int in_flight;   // 已发送未确认字节
    int mss;

    void init(int mss_);
    // 收到 ACK：增加在途量的释放；cwnd 在慢启动 +1，拥塞避免 +1/cwnd
    void on_ack(int acked_bytes);
    // 超时：ssthresh = cwnd/2，cwnd 回 1（乘性减小）
    void on_timeout();
    // 还能发多少字节
    int can_send() const;
};

// ===================== 连接状态机 =====================
// 一个极简 TCP 端点：用确定性 ISN 模拟三次握手与四次挥手。
// 它不真正发包，只根据"收到的段"推进状态、并算出"应发什么标志"。
class TcpConn {
public:
    uint32_t local_ip, remote_ip;
    uint16_t local_port, remote_port;
    TcpState state;
    uint32_t snd_nxt;     // 下一个要发送的序号
    uint32_t snd_una;     // 已被对方确认的最小序号
    uint32_t rcv_nxt;     // 期待对方下一个序号
    uint32_t iss, irs;    // 本端/对端初始序号

    TcpConn();
    void init(uint32_t lip, uint16_t lport, uint32_t rip, uint16_t rport);

    // 主动打开：返回要发送的标志（SYN），状态进入 SYN_SENT。
    uint8_t connect();
    // 被动打开：状态进入 LISTEN。
    void listen();

    // 喂入一个入站段。返回本端应当发出的标志位（0 表示无需回复）。
    // in_len = 段中数据字节数（SYN/FIN 各占 1 序号，由本函数内部计入）。
    uint8_t on_segment(uint8_t in_flags, uint32_t in_seq, uint32_t in_ack, int in_len);

    // 本端主动关闭：返回要发送的标志（FIN/ACK），状态相应推进。
    uint8_t close();

    const char* state_name() const { return tcp_state_name(state); }

private:
    uint32_t isn_;       // 确定性 ISN 生成器状态
    uint32_t next_isn();
};

// ===================== TCP 端点表（极简） =====================
// 一个五元组标识的连接槽。
struct TcpSocket {
    uint32_t local_ip, remote_ip;
    uint16_t local_port, remote_port;
    TcpConn  conn;
    bool     in_use;
};

class TcpTable {
public:
    TcpTable() : slots_() {}
    // 在 local_port 上监听。已监听返回 false。
    bool listen(uint16_t local_port);
    // 查找一个已建立的连接（按四元组）。找到返回槽位索引，否则 -1。
    int  find(uint32_t lip, uint16_t lport, uint32_t rip, uint16_t rport) const;
    // 新建一个连接槽并返回索引；满返回 -1。
    int  open(uint32_t lip, uint16_t lport, uint32_t rip, uint16_t rport);
    void close(int idx);
    int  size() const { return slots_.size(); }
    TcpSocket& at(int i) { return slots_[i]; }
    void clear() { slots_.clear(); }
private:
    List<TcpSocket> slots_;
};

int tcp_self_test();

} // namespace netproto
} // namespace nefu
