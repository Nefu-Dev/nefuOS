// nefuOS 网络协议栈 —— 聚合自测实现 (汇总全部模块 self_test)
// 由 netproto_self_test() 统一调用各模块自测
#include "netproto_all.h"
#include <stdio.h>

namespace nefu {
namespace netproto {

int netproto_self_test() {
    int total = 0;
    int f;

    printf("== nefu::netproto self tests ==\n");

    f = netproto_common_self_test();
    printf("  common   : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = ethernet_self_test();
    printf("  ethernet : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = arp_self_test();
    printf("  arp      : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = ip_self_test();
    printf("  ip       : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = icmp_self_test();
    printf("  icmp     : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = udp_self_test();
    printf("  udp      : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = tcp_self_test();
    printf("  tcp      : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = dns_self_test();
    printf("  dns      : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = http_self_test();
    printf("  http     : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = websocket_self_test();
    printf("  websocket: %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = mqtt_self_test();
    printf("  mqtt     : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = dhcp_self_test();
    printf("  dhcp     : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = socket_self_test();
    printf("  socket   : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = firewall_self_test();
    printf("  firewall : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);

    total += f;

    f = ntp_self_test();
    printf("  ntp      : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = netif_self_test();
    printf("  netif    : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = pktlog_self_test();
    printf("  pktlog   : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = netstack_self_test();
    printf("  netstack : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    f = pkt_dissect_self_test();
    printf("  dissect  : %s (%d)\n", f == 0 ? "OK" : "FAIL", f);
    total += f;

    printf("== total failures: %d ==\n", total);
    return total;
}

} // namespace netproto
} // namespace nefu
