// dhcp.cpp - DHCP/BOOTP 客户端协议实现
#include "dhcp.h"
#include "netproto_common.h"
#include <string.h>

namespace nefu {
namespace netproto {

// DHCP 魔数
static const uint32_t DHCP_MAGIC = 0x63825363;

// 写一个 DHCP 选项 (TLV)
static int put_dhcp_opt(uint8_t* p, int o, uint8_t code,
                        const uint8_t* data, int len) {
    p[o++] = code;
    p[o++] = (uint8_t)len;
    for (int i = 0; i < len; i++) p[o++] = data[i];
    return o;
}

// 写一个 DHCP 选项 (无数据，仅类型)
static int put_dhcp_opt_u8(uint8_t* p, int o, uint8_t code, uint8_t v) {
    p[o++] = code;
    p[o++] = 1;
    p[o++] = v;
    return o;
}

// 写一个 DHCP 选项 (4 字节大端)
static int put_dhcp_opt_u32(uint8_t* p, int o, uint8_t code, uint32_t v) {
    p[o++] = code;
    p[o++] = 4;
    put_be32(p + o, v);
    return o + 4;
}

// 初始化 BOOTP 固定头
static void init_bootp(DhcpPacket* bp, uint8_t op,
                       const uint8_t* mac, uint32_t xid) {
    np_zero((uint8_t*)bp, sizeof(DhcpPacket));
    bp->op = op;
    bp->htype = 1;       // 以太网
    bp->hlen = 6;
    bp->xid = xid;
    // 客户端硬件地址
    for (int i = 0; i < 6; i++) bp->chaddr[i] = mac[i];
    bp->magic = DHCP_MAGIC;
}

int dhcp_build_discover(uint8_t* out, int out_len,
                        const uint8_t* client_mac, uint32_t xid) {
    if (!out || out_len < 300) return -1;
    DhcpPacket bp;
    init_bootp(&bp, DHCP_OP_BOOTREQUEST, client_mac, xid);
    // 广播标志
    bp.flags = 0x8000;

    // 拷贝固定头
    np_copy(out, (const uint8_t*)&bp, sizeof(DhcpPacket));
    int o = sizeof(DhcpPacket);

    // Option 53: DHCPDISCOVER
    o = put_dhcp_opt_u8(out, o, DHCP_OPT_MSG_TYPE, DHCP_DISCOVER);
    // Option 55: 参数请求列表 (子网掩码/路由/DNS/域名)
    {
        uint8_t pl[] = { DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER,
                         DHCP_OPT_DNS, DHCP_OPT_DOMAIN_NAME };
        o = put_dhcp_opt(out, o, DHCP_OPT_PARAM_LIST, pl, 4);
    }
    // Option 255: END
    out[o++] = DHCP_OPT_END;
    return o;
}

int dhcp_build_request(uint8_t* out, int out_len,
                       const uint8_t* client_mac, uint32_t xid,
                       uint32_t requested_ip, uint32_t server_id) {
    if (!out || out_len < 300) return -1;
    DhcpPacket bp;
    init_bootp(&bp, DHCP_OP_BOOTREQUEST, client_mac, xid);
    bp.flags = 0x8000;

    np_copy(out, (const uint8_t*)&bp, sizeof(DhcpPacket));
    int o = sizeof(DhcpPacket);

    // Option 53: DHCPREQUEST
    o = put_dhcp_opt_u8(out, o, DHCP_OPT_MSG_TYPE, DHCP_REQUEST);
    // Option 50: 请求的 IP
    o = put_dhcp_opt_u32(out, o, DHCP_OPT_REQUEST_IP, requested_ip);
    // Option 54: 服务器标识
    o = put_dhcp_opt_u32(out, o, DHCP_OPT_SERVER_ID, server_id);
    // Option 55: 参数请求列表
    {
        uint8_t pl[] = { DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER,
                         DHCP_OPT_DNS, DHCP_OPT_DOMAIN_NAME };
        o = put_dhcp_opt(out, o, DHCP_OPT_PARAM_LIST, pl, 4);
    }
    out[o++] = DHCP_OPT_END;
    return o;
}

int dhcp_build_release(uint8_t* out, int out_len,
                       const uint8_t* client_mac, uint32_t xid,
                       uint32_t assigned_ip, uint32_t server_id) {
    if (!out || out_len < 300) return -1;
    DhcpPacket bp;
    init_bootp(&bp, DHCP_OP_BOOTREQUEST, client_mac, xid);
    bp.ciaddr = assigned_ip;   // 已分配的 IP

    np_copy(out, (const uint8_t*)&bp, sizeof(DhcpPacket));
    int o = sizeof(DhcpPacket);

    o = put_dhcp_opt_u8(out, o, DHCP_OPT_MSG_TYPE, DHCP_RELEASE);
    o = put_dhcp_opt_u32(out, o, DHCP_OPT_SERVER_ID, server_id);
    out[o++] = DHCP_OPT_END;
    return o;
}

int dhcp_parse_reply(const uint8_t* pkt, int len,
                     uint32_t& out_ip, uint32_t& out_server,
                     uint32_t& out_lease, uint32_t& out_mask,
                     uint32_t& out_gw) {
    if (!pkt || len < (int)sizeof(DhcpPacket)) return -1;
    const DhcpPacket* bp = (const DhcpPacket*)pkt;
    if (bp->op != DHCP_OP_BOOTREPLY) return -2;
    if (bp->magic != DHCP_MAGIC) return -3;

    out_ip = bp->yiaddr;
    out_server = 0;
    out_lease = 0;
    out_mask = 0;
    out_gw = 0;

    // 遍历选项
    int o = sizeof(DhcpPacket);
    int msg_type = 0;
    int steps = 0;
    while (o < len && steps < 256) {
        steps++;
        uint8_t code = pkt[o++];
        if (code == DHCP_OPT_END) break;
        if (code == 0) continue;   // 填充
        if (o >= len) break;
        uint8_t dlen = pkt[o++];
        if (o + dlen > len) break;
        const uint8_t* data = pkt + o;
        switch (code) {
            case DHCP_OPT_MSG_TYPE:
                if (dlen >= 1) msg_type = data[0];
                break;
            case DHCP_OPT_SERVER_ID:
                if (dlen >= 4) out_server = be32(data);
                break;
            case DHCP_OPT_LEASE_TIME:
                if (dlen >= 4) out_lease = be32(data);
                break;
            case DHCP_OPT_SUBNET_MASK:
                if (dlen >= 4) out_mask = be32(data);
                break;
            case DHCP_OPT_ROUTER:
                if (dlen >= 4) out_gw = be32(data);
                break;
            default:
                break;
        }
        o += dlen;
    }
    return msg_type;
}

const char* dhcp_msg_type_name(uint8_t t) {
    switch (t) {
        case DHCP_DISCOVER: return "DISCOVER";
        case DHCP_OFFER:    return "OFFER";
        case DHCP_REQUEST:  return "REQUEST";
        case DHCP_DECLINE:   return "DECLINE";
        case DHCP_ACK:       return "ACK";
        case DHCP_NAK:       return "NAK";
        case DHCP_RELEASE:   return "RELEASE";
        case DHCP_INFORM:    return "INFORM";
        default:             return "UNKNOWN";
    }
}

// 自检
static int g_fail = 0;
static void expect(const char* name, bool ok) {
    if (!ok) { g_fail++; }
}

int dhcp_self_test() {
    g_fail = 0;
    uint8_t buf[512];
    uint8_t mac[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };

    // 1) 构造 DISCOVER
    int n = dhcp_build_discover(buf, sizeof(buf), mac, 0x12345678);
    expect("dhcp discover len", n > 240 && n < 300);
    DhcpPacket* bp = (DhcpPacket*)buf;
    expect("dhcp discover op", bp->op == DHCP_OP_BOOTREQUEST);
    expect("dhcp discover xid", bp->xid == 0x12345678u);
    expect("dhcp discover magic", bp->magic == DHCP_MAGIC);
    expect("dhcp discover chaddr", bp->chaddr[0] == 0x00 &&
           bp->chaddr[5] == 0x55);

    // 2) 构造 REQUEST
    n = dhcp_build_request(buf, sizeof(buf), mac, 0x12345678,
                           0xC0A80105u, 0xC0A80101u);
    expect("dhcp request len", n > 240);

    // 3) 构造 RELEASE
    n = dhcp_build_release(buf, sizeof(buf), mac, 0x12345678,
                           0xC0A80105u, 0xC0A80101u);
    expect("dhcp release len", n > 240);
    bp = (DhcpPacket*)buf;
    expect("dhcp release ciaddr", bp->ciaddr == 0xC0A80105u);

    // 4) 解析 OFFER 响应
    {
        uint8_t resp[512];
        np_zero(resp, sizeof(resp));
        DhcpPacket* rp = (DhcpPacket*)resp;
        rp->op = DHCP_OP_BOOTREPLY;
        rp->htype = 1; rp->hlen = 6;
        rp->xid = 0x12345678;
        rp->yiaddr = 0xC0A80105u;
        rp->magic = DHCP_MAGIC;
        int o = sizeof(DhcpPacket);
        // Option 53: OFFER
        resp[o++] = DHCP_OPT_MSG_TYPE; resp[o++] = 1; resp[o++] = DHCP_OFFER;
        // Option 54: server id
        resp[o++] = DHCP_OPT_SERVER_ID; resp[o++] = 4;
        put_be32(resp + o, 0xC0A80101u); o += 4;
        // Option 51: lease 3600
        resp[o++] = DHCP_OPT_LEASE_TIME; resp[o++] = 4;
        put_be32(resp + o, 3600); o += 4;
        // Option 1: mask /24
        resp[o++] = DHCP_OPT_SUBNET_MASK; resp[o++] = 4;
        put_be32(resp + o, 0xFFFFFF00u); o += 4;
        // Option 3: router
        resp[o++] = DHCP_OPT_ROUTER; resp[o++] = 4;
        put_be32(resp + o, 0xC0A80101u); o += 4;
        resp[o++] = DHCP_OPT_END;

        uint32_t ip, srv, lease, mask, gw;
        int mt = dhcp_parse_reply(resp, o, ip, srv, lease, mask, gw);
        expect("dhcp offer type", mt == DHCP_OFFER);
        expect("dhcp offer ip", ip == 0xC0A80105u);
        expect("dhcp offer srv", srv == 0xC0A80101u);
        expect("dhcp offer lease", lease == 3600);
        expect("dhcp offer mask", mask == 0xFFFFFF00u);
        expect("dhcp offer gw", gw == 0xC0A80101u);
    }

    // 5) 解析 ACK
    {
        uint8_t resp[512];
        np_zero(resp, sizeof(resp));
        DhcpPacket* rp = (DhcpPacket*)resp;
        rp->op = DHCP_OP_BOOTREPLY;
        rp->htype = 1; rp->hlen = 6;
        rp->xid = 0x12345678;
        rp->yiaddr = 0xC0A80105u;
        rp->magic = DHCP_MAGIC;
        int o = sizeof(DhcpPacket);
        resp[o++] = DHCP_OPT_MSG_TYPE; resp[o++] = 1; resp[o++] = DHCP_ACK;
        resp[o++] = DHCP_OPT_SERVER_ID; resp[o++] = 4;
        put_be32(resp + o, 0xC0A80101u); o += 4;
        resp[o++] = DHCP_OPT_END;
        uint32_t ip, srv, lease, mask, gw;
        int mt = dhcp_parse_reply(resp, o, ip, srv, lease, mask, gw);
        expect("dhcp ack type", mt == DHCP_ACK);
        expect("dhcp ack ip", ip == 0xC0A80105u);
    }

    // 6) 错误情况
    {
        uint8_t bad[64];
        np_zero(bad, sizeof(bad));
        uint32_t ip, srv, lease, mask, gw;
        expect("dhcp short", dhcp_parse_reply(bad, 10, ip, srv, lease, mask, gw) < 0);
    }

    // 7) 类型名
    expect("dhcp name", strcmp(dhcp_msg_type_name(DHCP_ACK), "ACK") == 0);

    return g_fail;
}

} // namespace netproto
} // namespace nefu
