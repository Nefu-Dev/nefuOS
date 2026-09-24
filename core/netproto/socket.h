// socket.h - 轻量级 socket 抽象 (面向 UDP/TCP 简化版)
// 不做实际收发，仅维护连接五元组与发送缓冲队列
#ifndef NEFU_NETPROTO_SOCKET_H
#define NEFU_NETPROTO_SOCKET_H

#include <stdint.h>
#include "netproto_common.h"

namespace nefu {
namespace netproto {

// Socket 类型
enum SockType : uint8_t {
    SOCK_INVALID = 0,
    SOCK_DGRAM   = 1,   // UDP
    SOCK_STREAM  = 2,   // TCP
};

// Socket 状态
enum SockState : uint8_t {
    SS_FREE      = 0,
    SS_BOUND     = 1,   // 已 bind
    SS_LISTENING = 2,   // TCP listen
    SS_CONNECTED = 3,   // 已连接
    SS_CLOSING   = 4,
};

// 单个 socket 端点
struct Socket {
    uint8_t  type;      // SockType
    uint8_t  state;     // SockState
    uint16_t local_port;
    uint32_t local_ip;
    uint16_t remote_port;
    uint32_t remote_ip;
    int      rx_bytes;   // 已收字节
    int      tx_bytes;   // 已发字节
    int      backlog;    // listen 队列
};

#define NETPROTO_MAX_SOCKETS 16

// Socket 表
class SocketTable {
public:
    SocketTable();
    ~SocketTable();

    // 创建 socket，返回索引，<0 失败
    int create(int type);
    // bind 到本地地址
    bool bind(int idx, uint32_t lip, uint16_t lport);
    // connect (UDP 为 connect 语义，TCP 为状态迁移)
    bool connect(int idx, uint32_t rip, uint16_t rport);
    // listen (TCP)
    bool listen(int idx, int backlog);
    // close
    void close(int idx);
    // 查找已连接 socket
    int find(uint32_t lip, uint16_t lport,
             uint32_t rip, uint16_t rport) const;
    // 查找监听端口
    int find_listen(uint16_t lport) const;
    // 统计
    int count() const;
    // 格式化状态
    void format_line(int idx, char* buf, int buflen) const;

    Socket* get(int idx);
    const Socket* get(int idx) const;

private:
    Socket socks_[NETPROTO_MAX_SOCKETS];
    int used_;
};

// 自检
int socket_self_test();

} // namespace netproto
} // namespace nefu

#endif // NEFU_NETPROTO_SOCKET_H
