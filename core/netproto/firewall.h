// firewall.h - 简单包过滤防火墙 (ACL)
// 按五元组匹配规则，支持允许/拒绝
#ifndef NEFU_NETPROTO_FIREWALL_H
#define NEFU_NETPROTO_FIREWALL_H

#include <stdint.h>

namespace nefu {
namespace netproto {

// 动作
enum FwAction : uint8_t {
    FW_DENY  = 0,
    FW_ALLOW = 1,
};

// 方向
enum FwDir : uint8_t {
    FW_IN    = 0,
    FW_OUT   = 1,
    FW_BOTH  = 2,
};

// 协议
enum FwProto : uint8_t {
    FW_ANY  = 0,
    FW_TCP  = 6,
    FW_UDP  = 17,
    FW_ICMP = 1,
};

// 一条规则
struct FwRule {
    uint8_t  enabled;
    uint8_t  action;     // FwAction
    uint8_t  dir;       // FwDir
    uint8_t  proto;      // FwProto
    uint32_t src_ip;     // 0 = any
    uint32_t src_mask;   // 0 = any
    uint32_t dst_ip;
    uint32_t dst_mask;
    uint16_t src_port;   // 0 = any
    uint16_t dst_port;   // 0 = any
    int      hits;       // 命中计数
};

#define FW_MAX_RULES 32

// 防火墙
class Firewall {
public:
    Firewall();
    ~Firewall();

    // 添加规则，返回索引，<0 失败
    int add_rule(uint8_t action, uint8_t dir, uint8_t proto,
                 uint32_t src_ip, uint32_t src_mask,
                 uint32_t dst_ip, uint32_t dst_mask,
                 uint16_t src_port, uint16_t dst_port);

    // 删除规则
    void remove_rule(int idx);

    // 检查一个包，返回动作 (FW_ALLOW/FW_DENY)
    // dir: FW_IN/FW_OUT
    uint8_t check(uint8_t dir, uint8_t proto,
                  uint32_t src_ip, uint16_t src_port,
                  uint32_t dst_ip, uint16_t dst_port);

    // 统计
    int rule_count() const;
    int allow_count() const;
    int deny_count() const;

    // 默认策略 (无匹配时)
    void set_default(uint8_t a) { default_ = a; }
    uint8_t default_policy() const { return default_; }

    // 格式化一条规则
    void format_rule(int idx, char* buf, int buflen) const;

private:
    FwRule rules_[FW_MAX_RULES];
    uint8_t default_;
    int n_rules_;
};

// 自检
int firewall_self_test();

} // namespace netproto
} // namespace nefu

#endif // NEFU_NETPROTO_FIREWALL_H
