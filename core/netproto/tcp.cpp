// nefuOS 网络协议栈 —— TCP 实现
#include "tcp.h"
#include "netproto_common.h"
#include "ip.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// ===================== 头部封装 / 解析 =====================
int tcp_parse(const uint8_t* seg, int len, TcpHeader& out) {
    if (!seg || len < TCP_HDR_LEN) return -1;
    out.src_port = be16(seg + 0);
    out.dst_port = be16(seg + 2);
    out.seq      = be32(seg + 4);
    out.ack      = be32(seg + 8);
    out.data_offset = (uint8_t)((seg[12] >> 4) * 4);
    out.flags    = seg[13];
    out.window   = be16(seg + 14);
    out.urgent   = be16(seg + 18);
    if (out.data_offset < TCP_HDR_LEN || len < out.data_offset) return -1;
    return out.data_offset;
}

int tcp_pack(uint8_t* out, uint16_t src_port, uint16_t dst_port,
             uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window,
             const uint8_t* payload, int payload_len,
             uint32_t src_ip, uint32_t dst_ip) {
    if (!out) return -1;
    int total = TCP_HDR_LEN + payload_len;
    put_be16(out + 0, src_port);
    put_be16(out + 2, dst_port);
    put_be32(out + 4, seq);
    put_be32(out + 8, ack);
    out[12] = 0x50;              // data offset = 5 (20 字节)
    out[13] = flags;
    put_be16(out + 14, window);
    put_be16(out + 16, 0);       // 校验和先置 0
    put_be16(out + 18, 0);       // urgent
    if (payload && payload_len > 0) np_copy(out + TCP_HDR_LEN, payload, payload_len);
    uint16_t c = transport_csum(out, total, src_ip, dst_ip, IPPROTO_TCP);
    put_be16(out + 16, c);
    return total;
}

bool tcp_verify_checksum(const uint8_t* seg, int seg_len,
                         uint32_t src_ip, uint32_t dst_ip) {
    if (!seg || seg_len < TCP_HDR_LEN) return false;
    return transport_csum(seg, seg_len, src_ip, dst_ip, IPPROTO_TCP) == 0;
}

const char* tcp_state_name(TcpState s) {
    switch (s) {
    case TCP_CLOSED:         return "CLOSED";
    case TCP_LISTEN:         return "LISTEN";
    case TCP_SYN_SENT:       return "SYN_SENT";
    case TCP_SYN_RECEIVED:   return "SYN_RECEIVED";
    case TCP_ESTABLISHED:    return "ESTABLISHED";
    case TCP_FIN_WAIT_1:     return "FIN_WAIT_1";
    case TCP_FIN_WAIT_2:     return "FIN_WAIT_2";
    case TCP_CLOSE_WAIT:     return "CLOSE_WAIT";
    case TCP_LAST_ACK:       return "LAST_ACK";
    case TCP_CLOSING:        return "CLOSING";
    case TCP_TIME_WAIT:      return "TIME_WAIT";
    default:                 return "?";
    }
}

const char* tcp_flags_str(uint8_t flags, char* buf, int bufsz) {
    int o = 0;
    #define PUSH(f, ch) do { if (flags & (f)) { if (o < bufsz - 1) buf[o++] = (ch); } } while (0)
    PUSH(TCP_FIN, 'F');
    PUSH(TCP_SYN, 'S');
    PUSH(TCP_RST, 'R');
    PUSH(TCP_PSH, 'P');
    PUSH(TCP_ACK, 'A');
    PUSH(TCP_URG, 'U');
    #undef PUSH
    buf[o < bufsz ? o : bufsz - 1] = 0;
    return buf;
}

// ===================== 序列号比较（RFC 1982）=====================
// 把序号看成 mod 2^32 的有符号环：差值在 (-2^31, 2^31) 内即 a < b。
bool tcp_seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(b - a) > 0;
}
bool tcp_seq_leq(uint32_t a, uint32_t b) {
    return (int32_t)(b - a) >= 0;
}
bool tcp_seq_between(uint32_t lo, uint32_t seq, uint32_t hi) {
    return tcp_seq_leq(lo, seq) && tcp_seq_lt(seq, hi);
}

// ===================== TCP 选项 =====================
const char* tcp_option_name(uint8_t kind) {
    switch (kind) {
    case 0:  return "EOL";
    case 1:  return "NOP";
    case 2:  return "MSS";
    case 3:  return "WScale";
    case 4:  return "SACKPerm";
    case 8:  return "Timestamp";
    default: return "?";
    }
}

void tcp_parse_options(const uint8_t* seg, int hdr_len, TcpOptions& out) {
    out.mss = 0; out.wscale = 0; out.sack_ok = false; out.opt_len = 0;
    if (hdr_len <= 20) return;
    int o = 20;
    int end = hdr_len;
    int steps = 0;
    while (o < end && steps++ < 40) {
        uint8_t kind = seg[o];
        if (kind == TCP_OPT_EOL) break;
        if (kind == TCP_OPT_NOP) { o++; continue; }
        if (o + 1 >= end) break;
        uint8_t len = seg[o + 1];
        if (len < 2 || o + len > end) break;
        if (kind == TCP_OPT_MSS && len == 4) {
            out.mss = be16(seg + o + 2);
        } else if (kind == TCP_OPT_WSCALE && len == 3) {
            out.wscale = seg[o + 2];
        } else if (kind == TCP_OPT_SACKOK && len == 2) {
            out.sack_ok = true;
        }
        o += len;
    }
    out.opt_len = hdr_len - 20;
}

int tcp_write_mss_option(uint8_t* out, uint16_t mss) {
    // 在 20 字节头后写：NOP, NOP, MSS(kind=2,len=4,mss), NOP, NOP = 8 字节
    // （选项必须以 32 位对齐结束，故补 2 个 NOP）
    out[20] = TCP_OPT_NOP;
    out[21] = TCP_OPT_NOP;
    out[22] = TCP_OPT_MSS;
    out[23] = 4;
    put_be16(out + 24, mss);
    out[26] = TCP_OPT_NOP;
    out[27] = TCP_OPT_NOP;
    out[12] = 0x70;   // data offset = 7 (28 字节)
    return 28;
}

// ===================== RTO（RFC 6298，整数）=====================
void TcpRtoEstimator::reset() {
    srtt_scaled = 0; rttvar_scaled = 0; rto_ms = 1000; initialized = false;
}

int TcpRtoEstimator::sample(int rtt_ms) {
    if (rtt_ms < 1) rtt_ms = 1;
    if (!initialized) {
        srtt_scaled = rtt_ms << 3;
        rttvar_scaled = rtt_ms << 1;     // RTTVAR = SRTT/2 (scaled by 2)
        initialized = true;
    } else {
        // SRTT <- 7/8 SRTT + 1/8 R   (srtt_scaled 是 SRTT<<3)
        // 用整数：srtt_scaled = (7*srtt_scaled + rtt_ms) / 8  -- 先乘 8 对齐
        int delta = (rtt_ms << 3) - srtt_scaled;        // (R<<3) - SRTT<<3
        if (delta < 0) delta = -delta;
        // RTTVAR <- 3/4 RTTVAR + 1/4 |delta| （rttvar_scaled 是 RTTVAR<<2）
        rttvar_scaled = (3 * rttvar_scaled + delta) / 4;
        srtt_scaled = (7 * srtt_scaled + (rtt_ms << 3)) / 8;
    }
    // RTO = SRTT + max(G, 4*RTTVAR)，G=0（整数模型）
    int srtt = srtt_scaled >> 3;
    int rttvar = rttvar_scaled >> 2;
    int rto = srtt + 4 * rttvar;
    if (rto < 200) rto = 200;        // 下限 200ms
    if (rto > 60000) rto = 60000;    // 上限 60s
    rto_ms = rto;
    return rto_ms;
}

// ===================== 滑动窗口 =====================
void TcpWindow::init(int mss_) {
    mss = mss_ > 0 ? mss_ : 536;
    cwnd = mss;             // 慢启动初始 1 MSS
    ssthresh = 65535;
    snd_wnd = 65535;
    in_flight = 0;
}

void TcpWindow::on_ack(int acked_bytes) {
    if (acked_bytes > in_flight) acked_bytes = in_flight;
    in_flight -= acked_bytes;
    if (cwnd < ssthresh) {
        cwnd += mss;                    // 慢启动：每个 ACK 加一个 MSS
    } else {
        // 拥塞避免：每个 RTT 加一个 MSS，近似每个 ACK 加 mss*mss/cwnd
        cwnd += (mss * mss) / (cwnd > 0 ? cwnd : 1);
    }
}

void TcpWindow::on_timeout() {
    ssthresh = cwnd / 2;
    if (ssthresh < mss) ssthresh = mss;    // 下限 1 个 MSS，避免退化为 0
    cwnd = mss;                         // 回到慢启动
}

int TcpWindow::can_send() const {
    int wnd = snd_wnd < cwnd ? snd_wnd : cwnd;
    int avail = wnd - in_flight;
    return avail > 0 ? avail : 0;
}

// ===================== 连接状态机 =====================
TcpConn::TcpConn()
    : local_ip(0), remote_ip(0), local_port(0), remote_port(0),
      state(TCP_CLOSED), snd_nxt(0), snd_una(0), rcv_nxt(0), iss(0), irs(0),
      isn_(0x01000000u) {}

void TcpConn::init(uint32_t lip, uint16_t lport, uint32_t rip, uint16_t rport) {
    local_ip = lip; local_port = lport;
    remote_ip = rip; remote_port = rport;
    state = TCP_CLOSED;
    snd_nxt = snd_una = rcv_nxt = iss = irs = 0;
    isn_ = 0x01000000u ^ ((uint32_t)lport << 16) ^ rport;
}

uint32_t TcpConn::next_isn() {
    // 确定性 LCG：避免 FPU、避免随机源，方便自测复现
    isn_ = isn_ * 1103515245u + 12345u;
    return isn_;
}

uint8_t TcpConn::connect() {
    iss = next_isn();
    snd_nxt = iss + 1;     // SYN 消耗一个序号
    snd_una = iss;
    state = TCP_SYN_SENT;
    return TCP_SYN;
}

void TcpConn::listen() {
    state = TCP_LISTEN;
}

uint8_t TcpConn::on_segment(uint8_t in_flags, uint32_t in_seq, uint32_t in_ack, int in_len) {
    bool syn = (in_flags & TCP_SYN) != 0;
    bool ack = (in_flags & TCP_ACK) != 0;
    bool fin = (in_flags & TCP_FIN) != 0;
    bool rst = (in_flags & TCP_RST) != 0;
    uint8_t out = 0;

    switch (state) {
    case TCP_LISTEN:
        if (syn && !rst) {
            irs = in_seq;
            rcv_nxt = in_seq + 1;          // SYN 占一个序号
            iss = next_isn();
            snd_nxt = iss + 1;
            state = TCP_SYN_RECEIVED;
            out = TCP_SYN | TCP_ACK;
        }
        break;

    case TCP_SYN_SENT:
        if (rst) { state = TCP_CLOSED; break; }
        if (syn && ack) {
            irs = in_seq;
            rcv_nxt = in_seq + 1;
            snd_una = in_ack;             // 对方确认了我们的 SYN
            state = TCP_ESTABLISHED;
            out = TCP_ACK;
        } else if (syn) {
            irs = in_seq;
            rcv_nxt = in_seq + 1;
            state = TCP_SYN_RECEIVED;
            out = TCP_SYN | TCP_ACK;
        }
        break;

    case TCP_SYN_RECEIVED:
        if (rst) { state = TCP_CLOSED; break; }
        if (ack) {
            snd_una = in_ack;
            state = TCP_ESTABLISHED;       // 三次握手完成
        }
        break;

    case TCP_ESTABLISHED:
        if (rst) { state = TCP_CLOSED; break; }
        if (fin) {
            rcv_nxt = in_seq + in_len + 1; // FIN 占一个序号
            state = TCP_CLOSE_WAIT;
            out = TCP_ACK;
        } else if (in_len > 0) {
            rcv_nxt = in_seq + in_len;
            out = TCP_ACK;
        }
        break;

    case TCP_FIN_WAIT_1:
        if (ack) {
            snd_una = in_ack;
            if (fin) {
                rcv_nxt = in_seq + 1;
                state = TCP_TIME_WAIT;
                out = TCP_ACK;
            } else {
                state = TCP_FIN_WAIT_2;
            }
        } else if (fin) {
            rcv_nxt = in_seq + 1;
            state = TCP_CLOSING;           // 同时关闭
            out = TCP_ACK;
        }
        break;

    case TCP_FIN_WAIT_2:
        if (fin) {
            rcv_nxt = in_seq + 1;
            state = TCP_TIME_WAIT;
            out = TCP_ACK;
        }
        break;

    case TCP_CLOSING:
        if (ack) state = TCP_TIME_WAIT;
        break;

    case TCP_CLOSE_WAIT:
        // 对端已关，本端应用尚未调用 close()
        break;

    case TCP_LAST_ACK:
        if (ack) state = TCP_CLOSED;
        break;

    default:
        break;
    }
    return out;
}

uint8_t TcpConn::close() {
    uint8_t out = 0;
    switch (state) {
    case TCP_ESTABLISHED:
        snd_nxt += 1;                      // FIN 占一个序号
        state = TCP_FIN_WAIT_1;
        out = TCP_FIN | TCP_ACK;
        break;
    case TCP_CLOSE_WAIT:
        snd_nxt += 1;
        state = TCP_LAST_ACK;
        out = TCP_FIN | TCP_ACK;
        break;
    default:
        break;
    }
    return out;
}

// ===================== TCP 端点表 =====================
bool TcpTable::listen(uint16_t local_port) {
    for (int i = 0; i < slots_.size(); i++)
        if (slots_[i].in_use && slots_[i].local_port == local_port &&
            slots_[i].remote_port == 0) return false;
    TcpSocket s;
    np_zero(&s, sizeof(s));
    s.local_port = local_port;
    s.remote_port = 0;
    s.in_use = true;
    slots_.push(s);
    return true;
}

int TcpTable::find(uint32_t lip, uint16_t lport, uint32_t rip, uint16_t rport) const {
    for (int i = 0; i < slots_.size(); i++) {
        const TcpSocket& s = slots_[i];
        if (!s.in_use) continue;
        if (s.local_port == lport && s.local_ip == lip &&
            s.remote_ip == rip && s.remote_port == rport) return i;
    }
    return -1;
}

int TcpTable::open(uint32_t lip, uint16_t lport, uint32_t rip, uint16_t rport) {
    TcpSocket s;
    np_zero(&s, sizeof(s));
    s.local_ip = lip; s.local_port = lport;
    s.remote_ip = rip; s.remote_port = rport;
    s.in_use = true;
    s.conn.init(lip, lport, rip, rport);
    slots_.push(s);
    return slots_.size() - 1;
}

void TcpTable::close(int idx) {
    if (idx < 0 || idx >= slots_.size()) return;
    slots_[idx].in_use = false;
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [tcp] FAIL: %s\n", what); }
}
} // namespace

int tcp_self_test() {
    g_fails = 0;

    // ---- 头部封装 / 解析 ----
    uint8_t seg[128];
    uint32_t lip = 0xC0A8010Au, rip = 0x08080808u;
    int n = tcp_pack(seg, 12345, 80, 1000, 2000, TCP_SYN | TCP_ACK, 64240,
                     0, 0, lip, rip);
    expect("tcp pack len", n == TCP_HDR_LEN);
    TcpHeader h;
    int r = tcp_parse(seg, n, h);
    expect("tcp parse", r == TCP_HDR_LEN);
    expect("tcp parse ports", h.src_port == 12345 && h.dst_port == 80);
    expect("tcp parse seq", h.seq == 1000 && h.ack == 2000);
    expect("tcp parse flags", h.flags == (TCP_SYN | TCP_ACK));
    expect("tcp parse window", h.window == 64240);
    // 带数据
    const char* body = "GET / HTTP/1.1\r\n";
    int blen = (int)strlen(body);
    n = tcp_pack(seg, 12345, 80, 5000, 6000, TCP_ACK | TCP_PSH, 8192,
                 (const uint8_t*)body, blen, lip, rip);
    expect("tcp pack data len", n == TCP_HDR_LEN + blen);
    // 校验和：重算应得 0
    // （transport_csum 对含校验和字段的整段求和应为 0）
    {
        uint16_t c = transport_csum(seg, n, lip, rip, IPPROTO_TCP);
        expect("tcp csum", c == 0);
    }

    // ---- 三次握手 + 四次挥手状态机 ----
    TcpConn client, server;
    client.init(lip, 12345, rip, 80);
    server.init(rip, 80, lip, 12345);

    // 1. client active open
    uint8_t c = client.connect();
    expect("hs client send SYN", c == TCP_SYN);
    expect("hs client state", client.state == TCP_SYN_SENT);
    expect("hs client seq advanced", client.snd_nxt == client.iss + 1);

    // server listen
    server.listen();
    expect("hs server listen", server.state == TCP_LISTEN);

    // 2. server 收到 client 的 SYN
    c = server.on_segment(TCP_SYN, client.iss, 0, 0);
    expect("hs server reply SYN|ACK", c == (TCP_SYN | TCP_ACK));
    expect("hs server state", server.state == TCP_SYN_RECEIVED);
    expect("hs server irs", server.irs == client.iss);

    // 3. client 收到 server 的 SYN|ACK
    c = client.on_segment(TCP_SYN | TCP_ACK, server.iss, client.iss + 1, 0);
    expect("hs client reply ACK", c == TCP_ACK);
    expect("hs client established", client.state == TCP_ESTABLISHED);

    // 4. server 收到 client 的 ACK
    c = server.on_segment(TCP_ACK, client.iss + 1, server.iss + 1, 0);
    expect("hs server established", server.state == TCP_ESTABLISHED);
    expect("hs server no reply", c == 0);

    // 5. client 发数据（100 字节），server 回 ACK
    c = server.on_segment(TCP_ACK, client.snd_nxt, server.iss + 1, 100);
    expect("hs server acks data", c == TCP_ACK);
    expect("hs server rcv_nxt", server.rcv_nxt == client.iss + 1 + 100);

    // 6. client 主动关闭
    c = client.close();
    expect("hs client FIN", c == (TCP_FIN | TCP_ACK));
    expect("hs client fin_wait_1", client.state == TCP_FIN_WAIT_1);

    // 7. server 收到 FIN
    c = server.on_segment(TCP_FIN | TCP_ACK, client.snd_nxt, server.iss + 1, 0);
    expect("hs server acks FIN", c == TCP_ACK);
    expect("hs server close_wait", server.state == TCP_CLOSE_WAIT);

    // 8. client 收到对 FIN 的 ACK
    c = client.on_segment(TCP_ACK, client.snd_nxt, server.iss + 1, 0);
    expect("hs client fin_wait_2", client.state == TCP_FIN_WAIT_2);

    // 9. server 应用层关闭
    c = server.close();
    expect("hs server last_ack fin", c == (TCP_FIN | TCP_ACK));
    expect("hs server last_ack", server.state == TCP_LAST_ACK);

    // 10. client 收到 server 的 FIN
    c = client.on_segment(TCP_FIN | TCP_ACK, server.snd_nxt, client.snd_nxt, 0);
    expect("hs client time_wait", client.state == TCP_TIME_WAIT);
    expect("hs client acks fin", c == TCP_ACK);

    // 11. server 收到最后的 ACK -> CLOSED
    c = server.on_segment(TCP_ACK, client.snd_nxt, server.snd_nxt + 1, 0);
    expect("hs server closed", server.state == TCP_CLOSED);

    // 标志串
    char fb[16];
    tcp_flags_str(TCP_SYN | TCP_ACK, fb, sizeof(fb));
    expect("flags str", strcmp(fb, "SA") == 0);

    // ---- 序列号回绕比较 ----
    expect("seq lt basic", tcp_seq_lt(100, 200));
    expect("seq lt wrap", tcp_seq_lt(0xFFFFFFF0u, 10));        // 回绕后 10 更大
    expect("seq lt anti", !tcp_seq_lt(200, 100));
    expect("seq leq equal", tcp_seq_leq(5, 5));
    expect("seq between", tcp_seq_between(100, 150, 200));
    expect("seq between edge", !tcp_seq_between(100, 200, 200));

    // ---- TCP 选项解析：构造一个带 MSS 的 SYN ----
    {
        uint8_t opt_seg[64];
        int hl = tcp_pack(opt_seg, 1111, 80, 1000, 0, TCP_SYN, 64240, 0, 0, lip, rip);
        (void)hl;
        int nhl = tcp_write_mss_option(opt_seg, 1460);
        TcpOptions opts;
        tcp_parse_options(opt_seg, nhl, opts);
        expect("opt mss", opts.mss == 1460);
        expect("opt len", opts.opt_len == 8);
    }
    // 无选项
    {
        uint8_t plain[64];
        tcp_pack(plain, 1, 2, 0, 0, TCP_ACK, 100, 0, 0, lip, rip);
        TcpOptions opts;
        tcp_parse_options(plain, 20, opts);
        expect("opt none", opts.mss == 0 && !opts.sack_ok);
    }

    // ---- RTO 估计：多次采样应收敛 ----
    {
        TcpRtoEstimator rto; rto.reset();
        int r1 = rto.sample(100);
        expect("rto first", r1 >= 200 && r1 <= 1000);
        rto.sample(105); rto.sample(98); rto.sample(102);
        int r = rto.rto_ms;
        expect("rto stable", r >= 200 && r <= 1500);
    }

    // ---- 滑动窗口：慢启动 -> 拥塞避免 -> 超时 ----
    {
        TcpWindow w; w.init(536);
        expect("wnd initial", w.cwnd == 536);
        expect("wnd can send", w.can_send() == 536);
        // 在途 536
        w.in_flight = 536;
        expect("wnd full", w.can_send() == 0);
        // ACK 掉一个 MSS
        w.on_ack(536);
        expect("wnd after ack", w.cwnd > 536);
        // 超时
        int old = w.cwnd;
        w.on_timeout();
        expect("wnd timeout cwnd", w.cwnd == 536);
        expect("wnd timeout ssthresh", w.ssthresh < old);
    }

    // 11) TCP 端点表
    {
        TcpTable tbl;
        expect("tcp listen", tbl.listen(80));
        expect("tcp listen dup", !tbl.listen(80));
        uint32_t lip = (192u<<24)|(168u<<16)|10u;
        uint32_t rip = (93u<<24)|(184u<<16)|216u<<8|34u;
        int idx = tbl.open(lip, 80, rip, 54321);
        expect("tcp open", idx >= 0);
        expect("tcp find", tbl.find(lip, 80, rip, 54321) == idx);
        expect("tcp find miss", tbl.find(lip, 80, rip, 9999) == -1);
        tbl.close(idx);
        expect("tcp closed", tbl.find(lip, 80, rip, 54321) == -1);
    }


    // 校验和验证：刚 pack 出来的段应通过；改一个字节应失败
    {
        uint8_t seg[64];
        uint32_t lip=(192u<<24)|(168u<<16)|10u, rip=(93u<<24)|(184u<<16)|216u;
        tcp_pack(seg, 1234, 80, 1000, 0, TCP_SYN, 65535, 0, 0, lip, rip);
        expect("tcp csum good", tcp_verify_checksum(seg, 20, lip, rip));
        seg[18] ^= 0xFF;
        expect("tcp csum bad", !tcp_verify_checksum(seg, 20, lip, rip));
    }

    expect("tcp opt name", strcmp(tcp_option_name(2), "MSS") == 0 &&
           strcmp(tcp_option_name(3), "WScale") == 0);
    return g_fails;
}

} // namespace netproto
} // namespace nefu
