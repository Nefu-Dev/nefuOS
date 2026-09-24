// nefuOS 网络协议栈 —— 公共原语实现（校验和）
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// RFC 1071 累加：按 16 位大端字求和。
// 注意：这里用显式移位拼装 16 位字，而不是把 data 强转成 uint16_t*，
// 这样既不受主机字节序影响，也不会触发对齐访问问题（裸机/x86 均可）。
uint32_t csum_add(uint32_t sum, const uint8_t* data, int len) {
    int i = 0;
    // 主体：每两字节拼成一个 16 位字（大端）
    while (len >= 2) {
        uint32_t w = ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1];
        sum += w;
        // 提前吸收进位，避免 sum 持续膨胀后还要多次折叠
        if (sum & 0x10000u) sum = (sum & 0xFFFFu) + (sum >> 16);
        i += 2;
        len -= 2;
    }
    // 奇数长度：最后一个字节按高 8 位补零对齐
    if (len == 1) {
        sum += (uint32_t)data[i] << 8;
        if (sum & 0x10000u) sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return sum;
}

uint16_t csum_fold(uint32_t sum) {
    // 反复把高 16 位折到低 16 位，直到只剩 16 位
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

uint16_t csum_of(const uint8_t* data, int len) {
    return csum_fold(csum_add(0, data, len));
}

uint16_t transport_csum(const uint8_t* segment, int layer_len,
                       uint32_t src_ip, uint32_t dst_ip, uint8_t proto) {
    // 伪首部共 12 字节：src_ip(4) dst_ip(4) zero(1) proto(1) length(2)
    uint32_t sum = 0;
    // src_ip / dst_ip 已是主机序的 32 位，按字节拆成两个 16 位大端字累加
    uint8_t ipbuf[12];
    put_be32(ipbuf + 0, src_ip);
    put_be32(ipbuf + 4, dst_ip);
    ipbuf[8] = 0;
    ipbuf[9] = proto;
    put_be16(ipbuf + 10, (uint16_t)layer_len);
    sum = csum_add(sum, ipbuf, 12);
    // 再叠加真正的传输层报文
    sum = csum_add(sum, segment, layer_len);
    return csum_fold(sum);
}

// ===================== Q16.16 定点 =====================
float Fx::to_float() const {
    // host 调试用；bare 环境不链接 libc 浮点，故不内联。
    return (float)v / 65536.0f;
}

// ===================== hex dump =====================
char* np_hex_dump(const uint8_t* data, int n, char* out, int outsz) {
    if (!out || outsz < 2) { if (out && outsz > 0) out[0] = 0; return out; }
    static const char* H = "0123456789ABCDEF";
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0 && o + 4 < outsz) out[o++] = ' ';
        if (o + 3 >= outsz) break;
        out[o++] = H[(data[i] >> 4) & 0xF];
        out[o++] = H[data[i] & 0xF];
    }
    out[o] = 0;
    return out;
}

// 单字符转 nibble
static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int np_parse_hex(const char* s, uint8_t* out, int max_bytes, char sep) {
    if (!s || !out || max_bytes <= 0) return -1;
    int n = 0;
    const char* p = s;
    while (*p && n < max_bytes) {
        int hi = hex_nibble(p[0]);
        int lo = hex_nibble(p[1]);
        if (hi < 0 || lo < 0) return -2;
        out[n++] = (uint8_t)((hi << 4) | lo);
        p += 2;
        if (sep && *p == sep) p++;
    }
    return n;
}

char* np_format_hex(const uint8_t* data, int n, char sep, char* out, int outsz) {
    if (!out || outsz < 2) { if (out && outsz > 0) out[0] = 0; return out; }
    static const char* H = "0123456789ABCDEF";
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0 && sep) { if (o + 2 >= outsz) break; out[o++] = sep; }
        if (o + 3 >= outsz) break;
        out[o++] = H[(data[i] >> 4) & 0xF];
        out[o++] = H[data[i] & 0xF];
    }
    out[o] = 0;
    return out;
}
// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [common] FAIL: %s\n", what); }
}
} // namespace

const char* netproto_version() { return "nefuOS netproto 0.1.0"; }
int netproto_module_count() { return 14; }

int netproto_common_self_test() {
    g_fails = 0;
    uint8_t buf[16];
    for (int i = 0; i < 16; i++) buf[i] = 0;

    // 1) be16/be32 往返
    put_be16(buf, 0x1234);
    expect("be16 roundtrip", be16(buf) == 0x1234);
    put_be32(buf, 0xDEADBEEFu);
    expect("be32 roundtrip", be32(buf) == 0xDEADBEEFu);
    expect("be32 bytes", buf[0] == 0xDE && buf[3] == 0xEFu);

    // 2) 校验和：全零数据的校验和为 0xFFFF（~0）
    uint8_t z[8] = {0,0,0,0,0,0,0,0};
    expect("csum zero", csum_of(z, 8) == 0xFFFFu);

    // 3) 校验和互补：标准做法是先把校验和字段清零，算出 c，写回，再验证得 0
    uint8_t pkt[20];
    for (int i = 0; i < 20; i++) pkt[i] = (uint8_t)(i * 31);
    pkt[0] = 0; pkt[1] = 0;                 // 校验和字段先清零
    uint16_t c = csum_of(pkt, 20);
    pkt[0] = (uint8_t)(c >> 8);             // 大端写回
    pkt[1] = (uint8_t)(c & 0xFFu);
    expect("csum complementary", csum_of(pkt, 20) == 0);

    // 4) Q16.16
    Fx a = Fx::from_int(3);
    Fx b = Fx::from_int(2);
    expect("fx add", (a + b).to_int() == 5);
    expect("fx sub", (a - b).to_int() == 1);
    expect("fx mul", (a * b).to_int() == 6);
    expect("fx div", (a / b).to_int() == 1);   // 3/2 = 1.5 -> 1

    // 5) hex dump
    uint8_t hd[3] = {0xAB, 0x0C, 0x1F};
    char ob[16];
    np_hex_dump(hd, 3, ob, sizeof(ob));
    expect("hex dump", strcmp(ob, "AB 0C 1F") == 0);

    // 5b) hex parse/format
    uint8_t mb[6];
    int mn = np_parse_hex("00:11:22:33:44:55", mb, 6, ':');
    expect("hex parse", mn == 6 && mb[0] == 0 && mb[5] == 0x55);
    char hb[20];
    np_format_hex(mb, 6, ':', hb, sizeof(hb));
    expect("hex format", strcmp(hb, "00:11:22:33:44:55") == 0);


    expect("version", strstr(netproto_version(), "netproto") != 0);
    expect("module count", netproto_module_count() >= 12);

    // 6) 校验和：奇数长度
    uint8_t odd[5] = {0x00, 0x01, 0x02, 0x03, 0x04};
    uint16_t oc = csum_of(odd, 5);
    expect("csum odd len", oc != 0);
    {
        uint8_t p2[6];
        np_copy(p2, odd, 5);
        p2[0] = 0; p2[1] = 0;
        uint16_t c2 = csum_of(p2, 5);
        p2[0] = (uint8_t)(c2 >> 8);
        p2[1] = (uint8_t)(c2 & 0xFF);
        expect("csum odd recheck", csum_of(p2, 5) == 0);
    }

    // 7) Fx 小数与负数
    Fx half = Fx::from_int(1) / Fx::from_int(2);
    expect("fx half", half.to_int() == 0 && half.to_float() > 0.4f && half.to_float() < 0.6f);
    Fx neg = Fx::from_int(0) - Fx::from_int(5);
    expect("fx neg", neg.to_int() == -5);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
