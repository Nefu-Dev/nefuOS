// firewall.cpp - 简单包过滤防火墙实现
#include "firewall.h"
#include "netproto_common.h"
#include <string.h>

namespace nefu {
namespace netproto {

Firewall::Firewall() : default_(FW_ALLOW), n_rules_(0) {
    np_zero(rules_, sizeof(rules_));
}

Firewall::~Firewall() {}

int Firewall::add_rule(uint8_t action, uint8_t dir, uint8_t proto,
                       uint32_t src_ip, uint32_t src_mask,
                       uint32_t dst_ip, uint32_t dst_mask,
                       uint16_t src_port, uint16_t dst_port) {
    if (n_rules_ >= FW_MAX_RULES) return -1;
    int i = n_rules_++;
    FwRule* r = &rules_[i];
    r->enabled = 1;
    r->action = action;
    r->dir = dir;
    r->proto = proto;
    r->src_ip = src_ip;
    r->src_mask = src_mask;
    r->dst_ip = dst_ip;
    r->dst_mask = dst_mask;
    r->src_port = src_port;
    r->dst_port = dst_port;
    r->hits = 0;
    return i;
}

void Firewall::remove_rule(int idx) {
    if (idx < 0 || idx >= n_rules_) return;
    rules_[idx].enabled = 0;
}

static bool ip_match(uint32_t pkt, uint32_t rule, uint32_t mask) {
    if (mask == 0) return true;       // any
    return (pkt & mask) == (rule & mask);
}

uint8_t Firewall::check(uint8_t dir, uint8_t proto,
                        uint32_t src_ip, uint16_t src_port,
                        uint32_t dst_ip, uint16_t dst_port) {
    for (int i = 0; i < n_rules_; i++) {
        FwRule* r = &rules_[i];
        if (!r->enabled) continue;
        // 方向
        if (r->dir != FW_BOTH && r->dir != dir) continue;
        // 协议
        if (r->proto != FW_ANY && r->proto != proto) continue;
        // IP
        if (!ip_match(src_ip, r->src_ip, r->src_mask)) continue;
        if (!ip_match(dst_ip, r->dst_ip, r->dst_mask)) continue;
        // 端口
        if (r->src_port != 0 && r->src_port != src_port) continue;
        if (r->dst_port != 0 && r->dst_port != dst_port) continue;
        // 命中
        r->hits++;
        return r->action;
    }
    return default_;
}

int Firewall::rule_count() const { return n_rules_; }

int Firewall::allow_count() const {
    int n = 0;
    for (int i = 0; i < n_rules_; i++)
        if (rules_[i].enabled && rules_[i].action == FW_ALLOW) n++;
    return n;
}

int Firewall::deny_count() const {
    int n = 0;
    for (int i = 0; i < n_rules_; i++)
        if (rules_[i].enabled && rules_[i].action == FW_DENY) n++;
    return n;
}

void Firewall::format_rule(int idx, char* buf, int buflen) const {
    if (!buf || buflen < 32 || idx < 0 || idx >= n_rules_) return;
    const FwRule* r = &rules_[idx];
    const char* act = r->action == FW_ALLOW ? "ALLOW" : "DENY";
    const char* dir = r->dir == FW_IN ? "IN" :
                      r->dir == FW_OUT ? "OUT" : "ANY";
    ksprintf(buf, buflen, "[%d] %s %s hits=%d", idx, act, dir, r->hits);
}

// 自检
static int g_fail = 0;
static void expect(const char* name, bool ok) {
    if (!ok) g_fail++;
}

int firewall_self_test() {
    g_fail = 0;
    Firewall fw;

    // 默认策略 = 允许
    expect("fw default", fw.default_policy() == FW_ALLOW);

    // 1) 拒绝外部到本地 22 端口 (SSH)
    fw.add_rule(FW_DENY, FW_IN, FW_TCP,
                0, 0,                    // 任意源
                0xC0A80100u, 0xFFFFFF00u, // 目标 192.168.1.0/24
                0, 22);                   // 目标端口 22

    // 2) 允许 80 端口
    fw.add_rule(FW_ALLOW, FW_IN, FW_TCP,
                0, 0,
                0xC0A80100u, 0xFFFFFF00u,
                0, 80);

    // 3) 拒绝 ICMP (ping)
    fw.add_rule(FW_DENY, FW_BOTH, FW_ICMP,
                0, 0, 0, 0, 0, 0);

    // 测试
    // SSH 应被拒绝
    expect("fw deny ssh", fw.check(FW_IN, FW_TCP,
            0x08080808u, 12345, 0xC0A80105u, 22) == FW_DENY);
    // HTTP 应被允许
    expect("fw allow http", fw.check(FW_IN, FW_TCP,
            0x08080808u, 12345, 0xC0A80105u, 80) == FW_ALLOW);
    // 其他端口走默认策略 = 允许
    expect("fw default allow", fw.check(FW_IN, FW_TCP,
            0x08080808u, 12345, 0xC0A80105u, 443) == FW_ALLOW);
    // ICMP 应被拒绝
    expect("fw deny icmp", fw.check(FW_IN, FW_ICMP,
            0x08080808u, 0, 0xC0A80105u, 0) == FW_DENY);
    // ICMP OUT 也被拒绝 (FW_BOTH)
    expect("fw deny icmp out", fw.check(FW_OUT, FW_ICMP,
            0xC0A80105u, 0, 0x08080808u, 0) == FW_DENY);

    // 4) 统计
    expect("fw rules", fw.rule_count() == 3);
    expect("fw allows", fw.allow_count() == 1);
    expect("fw denys", fw.deny_count() == 2);

    // 5) 删除规则
    fw.remove_rule(2);   // 删 ICMP 规则
    expect("fw after remove", fw.check(FW_IN, FW_ICMP,
            0x08080808u, 0, 0xC0A80105u, 0) == FW_ALLOW);

    // 6) 格式化
    char line[64];
    fw.format_rule(0, line, sizeof(line));
    expect("fw fmt", strstr(line, "DENY") != 0);

    // 7) 设置默认策略为拒绝
    fw.set_default(FW_DENY);
    expect("fw default deny", fw.check(FW_IN, FW_TCP,
            0x08080808u, 12345, 0xC0A80105u, 443) == FW_DENY);

    return g_fail;
}

} // namespace netproto
} // namespace nefu
