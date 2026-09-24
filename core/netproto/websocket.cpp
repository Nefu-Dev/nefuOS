// nefuOS 网络协议栈 —— WebSocket 实现
#include "websocket.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// ===================== 内嵌 SHA-1（FIPS 180-1）=====================
// 仅用于握手 Sec-WebSocket-Accept。纯整数，无 FPU。
static uint32_t rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

struct Sha1Ctx {
    uint32_t h[5];
    uint64_t total;       // 已处理字节数
    uint8_t  block[64];
    int      blen;
};

static void sha1_init(Sha1Ctx* c) {
    c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu; c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->total = 0; c->blen = 0;
}

static void sha1_block(Sha1Ctx* c, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = rotl32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a=c->h[0], b=c->h[1], cc=c->h[2], d=c->h[3], e=c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | ((~b) & d);        k = 0x5A827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ cc ^ d;                   k = 0xCA62C1D6u; }
        uint32_t t = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rotl32(b, 30); b = a; a = t;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e;
}

static void sha1_update(Sha1Ctx* c, const uint8_t* data, int len) {
    c->total += (uint64_t)len;
    while (len > 0) {
        int take = 64 - c->blen;
        if (take > len) take = len;
        for (int i = 0; i < take; i++) c->block[c->blen + i] = data[i];
        c->blen += take; data += take; len -= take;
        if (c->blen == 64) { sha1_block(c, c->block); c->blen = 0; }
    }
}

static void sha1_final(Sha1Ctx* c, uint8_t out[20]) {
    uint64_t bits = c->total * 8;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    uint8_t zero = 0;
    while (c->blen != 56) sha1_update(c, &zero, 1);
    // 追加 64 位大端长度
    uint8_t lenbytes[8];
    for (int i = 0; i < 8; i++) lenbytes[i] = (uint8_t)(bits >> (56 - i*8));
    sha1_update(c, lenbytes, 8);
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(c->h[i] >> 24);
        out[i*4+1] = (uint8_t)(c->h[i] >> 16);
        out[i*4+2] = (uint8_t)(c->h[i] >> 8);
        out[i*4+3] = (uint8_t)(c->h[i]);
    }
}

// 微型 base64（编码 20 字节摘要 -> 28 字符）
static void b64_encode_20(const uint8_t* in, char* out) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    // 20 字节 = 15 组完整 3 字节 + 2 字节尾 -> 输出 76 字符 + 1 个 '='
    int o = 0;
    int i = 0;
    for (; i + 3 <= 20; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = T[(v >> 6) & 63];
        out[o++] = T[v & 63];
    }
    // 剩余 2 字节
    uint32_t v = ((uint32_t)in[18] << 16) | ((uint32_t)in[19] << 8);
    out[o++] = T[(v >> 18) & 63];
    out[o++] = T[(v >> 12) & 63];
    out[o++] = T[(v >> 6) & 63];
    out[o++] = '=';
    out[o] = 0;
}

char* ws_compute_accept(const char* client_key, char* out, int outsz) {
    if (!out || outsz < 29) { if (outsz > 0 && out) out[0] = 0; return out; }
    Sha1Ctx c; sha1_init(&c);
    sha1_update(&c, (const uint8_t*)client_key, (int)strlen(client_key));
    sha1_update(&c, (const uint8_t*)"258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
    uint8_t digest[20];
    sha1_final(&c, digest);
    b64_encode_20(digest, out);
    return out;
}

int ws_build_client_handshake(const char* host, const char* path,
                              const char* key_buf, String& out) {
    out = "";
    out += "GET "; out += path ? path : "/"; out += " HTTP/1.1\r\n";
    out += "Host: "; out += host ? host : ""; out += "\r\n";
    out += "Upgrade: websocket\r\n";
    out += "Connection: Upgrade\r\n";
    out += "Sec-WebSocket-Key: "; out += key_buf ? key_buf : ""; out += "\r\n";
    out += "Sec-WebSocket-Version: 13\r\n";
    out += "\r\n";
    return out.len();
}

int ws_parse_server_handshake(const char* raw, int len, char* accept_out, int outsz) {
    if (!raw || len < 16) return -1;
    // 必须是 101
    if (strncmp(raw, "HTTP/1.1 101", 12) != 0) return -1;
    // 找 Sec-WebSocket-Accept:（共 21 字节）
    const char* needle = "Sec-WebSocket-Accept:";
    const int nlen = 21;
    const char* p = raw;
    const char* end = raw + len;
    while (p + nlen < end) {
        if (memcmp(p, needle, nlen) == 0) {
            p += nlen;
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            int o = 0;
            while (p < end && *p != '\r' && *p != '\n' && o < outsz - 1)
                accept_out[o++] = *p++;
            accept_out[o] = 0;
            return 0;
        }
        p++;
    }
    return -1;
}

void ws_apply_mask(uint8_t* data, int len, const uint8_t key[4]) {
    for (int i = 0; i < len; i++) data[i] ^= key[i & 3];
}

int ws_encode_frame(uint8_t* out, uint8_t opcode, bool fin,
                   const uint8_t* payload, int payload_len, bool masked,
                   uint32_t* seed) {
    if (!out) return -1;
    int o = 0;
    out[o++] = (uint8_t)((fin ? 0x80 : 0) | (opcode & 0x0F));

    uint8_t mbit = masked ? 0x80 : 0;
    if (payload_len < 126) {
        out[o++] = (uint8_t)(mbit | payload_len);
    } else if (payload_len < 65536) {
        out[o++] = (uint8_t)(mbit | 126);
        put_be16(out + o, (uint16_t)payload_len); o += 2;
    } else {
        out[o++] = (uint8_t)(mbit | 127);
        // 64 位长度：高 32 位写 0，低 32 位写实际长度（本库不处理 >4G 帧）
        put_be32(out + o, 0); o += 4;
        put_be32(out + o, (uint32_t)payload_len); o += 4;
    }

    uint8_t key[4] = {0, 0, 0, 0};
    if (masked) {
        // 由 seed 派生一个确定性掩码密钥（LCG），便于自测复现
        uint32_t s = seed ? *seed : 0;
        for (int i = 0; i < 4; i++) {
            s = s * 1103515245u + 12345u;
            key[i] = (uint8_t)(s >> 16);
        }
        if (seed) *seed = s;
        np_copy(out + o, key, 4); o += 4;
    }

    if (payload && payload_len > 0) {
        np_copy(out + o, payload, payload_len);
        if (masked) ws_apply_mask(out + o, payload_len, key);
        o += payload_len;
    }
    return o;
}

int ws_decode_frame(uint8_t* pkt, int len, WsFrame& f) {
    if (!pkt || len < 2) return -1;
    np_zero(&f, sizeof(f));
    f.fin = (pkt[0] & 0x80) != 0;
    f.opcode = pkt[0] & 0x0Fu;
    f.masked = (pkt[1] & 0x80) != 0;
    uint32_t payload_len = pkt[1] & 0x7Fu;
    int o = 2;
    if (payload_len == 126) {
        if (len < o + 2) return -1;
        payload_len = be16(pkt + o); o += 2;
    } else if (payload_len == 127) {
        if (len < o + 8) return -1;
        uint32_t hi = be32(pkt + o); o += 4;
        payload_len = be32(pkt + o); o += 4;
        if (hi != 0) return -1;   // >4G 帧本库不支持
    }
    if (f.masked) {
        if (len < o + 4) return -1;
        for (int i = 0; i < 4; i++) f.mask_key[i] = pkt[o + i];
        o += 4;
    }
    if ((int)(o + payload_len) > len) return -1;
    f.payload = pkt + o;
    f.payload_len = payload_len;
    // 原地解掩码
    if (f.masked && payload_len > 0) ws_apply_mask(pkt + o, payload_len, f.mask_key);
    return o;
}

const char* ws_close_reason(uint16_t code) {
    switch (code) {
    case 1000: return "Normal";
    case 1001: return "Going away";
    case 1002: return "Protocol error";
    case 1003: return "Unsupported data";
    case 1006: return "Abnormal closure";
    case 1007: return "Invalid payload";
    case 1008: return "Policy violation";
    case 1009: return "Message too big";
    case 1011: return "Internal error";
    default:   return "Unknown";
    }
}

const char* ws_opcode_name(uint8_t op) {
    switch (op) {
    case WS_OP_CONTINUATION: return "cont";
    case WS_OP_TEXT:         return "text";
    case WS_OP_BINARY:       return "binary";
    case WS_OP_CLOSE:        return "close";
    case WS_OP_PING:         return "ping";
    case WS_OP_PONG:         return "pong";
    default:                 return "?";
    }
}

int ws_build_close(uint8_t* out, uint16_t code, const char* reason, bool masked,
                  uint32_t* seed) {
    uint8_t payload[128];
    put_be16(payload + 0, code);
    int pl = 2;
    if (reason) {
        int rlen = (int)strlen(reason);
        if (rlen > 120) rlen = 120;
        for (int i = 0; i < rlen; i++) payload[pl++] = (uint8_t)reason[i];
    }
    return ws_encode_frame(out, WS_OP_CLOSE, true, payload, pl, masked, seed);
}

int ws_build_ping(uint8_t* out, const uint8_t* payload, int len, bool masked,
                 uint32_t* seed) {
    return ws_encode_frame(out, WS_OP_PING, true, payload, len, masked, seed);
}

int ws_build_pong(uint8_t* out, const uint8_t* payload, int len, bool masked,
                 uint32_t* seed) {
    return ws_encode_frame(out, WS_OP_PONG, true, payload, len, masked, seed);
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [ws] FAIL: %s\n", what); }
}
} // namespace

int websocket_self_test() {
    g_fails = 0;
    uint8_t buf[512];

    // 1) 编码一个 masked text 帧 "Hello"
    const char* msg = "Hello";
    uint32_t seed = 0xABCD;
    int n = ws_encode_frame(buf, WS_OP_TEXT, true,
                            (const uint8_t*)msg, 5, true, &seed);
    expect("ws text frame len", n >= 2 + 4 + 5);
    expect("ws fin/opcode", (buf[0] & 0x0F) == WS_OP_TEXT && (buf[0] & 0x80));
    expect("ws mask bit", buf[1] & 0x80);
    expect("ws len short", (buf[1] & 0x7F) == 5);

    // 2) 解码回读（会原地解掩码）
    uint8_t copy[512]; np_copy(copy, buf, n);
    WsFrame f;
    int hlen = ws_decode_frame(copy, n, f);
    expect("ws decode hdr", hlen > 0);
    expect("ws decode opcode", f.opcode == WS_OP_TEXT);
    expect("ws decode fin", f.fin);
    expect("ws decode len", f.payload_len == 5);
    expect("ws decode payload", memcmp(f.payload, "Hello", 5) == 0);

    // 3) ping / pong
    seed = 42;
    n = ws_encode_frame(buf, WS_OP_PING, true, (const uint8_t*)"abc", 3, true, &seed);
    copy[0] = 0; np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws ping", f.opcode == WS_OP_PING && f.payload_len == 3 &&
           memcmp(f.payload, "abc", 3) == 0);

    n = ws_encode_frame(buf, WS_OP_PONG, true, (const uint8_t*)"abc", 3, true, &seed);
    np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws pong", f.opcode == WS_OP_PONG);

    // 4) close 帧（空 payload）
    n = ws_encode_frame(buf, WS_OP_CLOSE, true, 0, 0, true, &seed);
    np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws close", f.opcode == WS_OP_CLOSE && f.payload_len == 0);

    // 5) 长 payload（>125）走 16 位扩展长度
    uint8_t big[300];
    for (int i = 0; i < 300; i++) big[i] = (uint8_t)(i & 0xFF);
    seed = 7;
    n = ws_encode_frame(buf, WS_OP_BINARY, true, big, 300, true, &seed);
    expect("ws big frame uses ext", (buf[1] & 0x7F) == 126);
    np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws big decode len", f.payload_len == 300);
    bool ok = true;
    for (int i = 0; i < 300; i++) if (f.payload[i] != big[i]) ok = false;
    expect("ws big payload match", ok);

    // 6) 服务器->客户端不掩码帧
    n = ws_encode_frame(buf, WS_OP_TEXT, true, (const uint8_t*)"hi", 2, false, &seed);
    expect("ws unmasked bit off", !(buf[1] & 0x80));
    np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws unmasked decode", f.opcode == WS_OP_TEXT && f.payload_len == 2 &&
           memcmp(f.payload, "hi", 2) == 0);

    // 7) 握手：RFC 6455 标准向量
    //    key "dGhlIHNhbXBsZSBub25jZQ==" -> accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    char accept[32];
    ws_compute_accept("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept));
    expect("ws accept vector", strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

    // 8) 构造并解析握手
    String hs;
    ws_build_client_handshake("example.com", "/chat", "dGhlIHNhbXBsZSBub25jZQ==", hs);
    expect("ws handshake has upgrade", strstr(hs.c_str(), "Upgrade: websocket") != 0);
    const char* srv =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n";
    char srv_accept[32];
    int hr = ws_parse_server_handshake(srv, (int)strlen(srv), srv_accept, sizeof(srv_accept));
    expect("ws parse handshake", hr == 0 && strcmp(srv_accept, accept) == 0);
    // 非 101 应拒绝
    expect("ws handshake reject", ws_parse_server_handshake("HTTP/1.1 404 Not Found\r\n\r\n", 25, srv_accept, 32) == -1);

    // 9) 便捷控制帧构造
    seed = 99;
    n = ws_build_close(buf, 1000, "going away", true, &seed);
    np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws build close", f.opcode == WS_OP_CLOSE && f.payload_len >= 2);
    expect("ws close code", be16(f.payload) == 1000);

    seed = 1;
    n = ws_build_ping(buf, (const uint8_t*)"t", 1, true, &seed);
    np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws build ping", f.opcode == WS_OP_PING && f.payload_len == 1);

    n = ws_build_pong(buf, (const uint8_t*)"t", 1, true, &seed);
    np_copy(copy, buf, n);
    hlen = ws_decode_frame(copy, n, f);
    expect("ws build pong", f.opcode == WS_OP_PONG);


    expect("ws close reason", strcmp(ws_close_reason(1000), "Normal") == 0 &&
           strcmp(ws_close_reason(1003), "Unsupported data") == 0);
    return g_fails;
}

} // namespace netproto
} // namespace nefu
