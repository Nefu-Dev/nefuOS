// nefuOS 网络协议栈 —— WebSocket（RFC 6455）
//
// 帧结构：
//   0               1               2               3
//   FIN RSV1-3 opcode | MASK payload-len | [扩展长度] | [mask-key] | payload
//
//   opcode：0=continuation 1=text 2=binary 8=close 9=ping 10=pong
//   客户端发出的帧必须置 MASK 并用 4 字节密钥异或 payload。
//
// 本模块：帧封装（含 masking）、帧解析（含解掩码）、ping/pong/close。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

// 操作码
const uint8_t WS_OP_CONTINUATION = 0x0;
const uint8_t WS_OP_TEXT         = 0x1;
const uint8_t WS_OP_BINARY       = 0x2;
const uint8_t WS_OP_CLOSE        = 0x8;
const uint8_t WS_OP_PING         = 0x9;
const uint8_t WS_OP_PONG         = 0xA;

struct WsFrame {
    bool     fin;
    uint8_t  opcode;
    bool     masked;
    uint32_t payload_len;     // 解出的真实长度
    uint8_t  mask_key[4];
    const uint8_t* payload;   // 指向包内（已解掩码，若我们原地处理）
};

// 封装一个帧到 out。masked=true 时生成并写入掩码密钥（客户端必须如此）。
// payload_len 是 payload 字节数。返回整帧总长度（含头）。
int ws_encode_frame(uint8_t* out, uint8_t opcode, bool fin,
                   const uint8_t* payload, int payload_len, bool masked,
                   uint32_t* seed);

// 解析一个帧。pkt 指向整帧。成功返回头部长度（不含 payload），
// 并把 f->payload 指向 pkt 内的 payload（已解掩码，原地修改 pkt）。
// 失败返回 -1。
int ws_decode_frame(uint8_t* pkt, int len, WsFrame& f);

// 对一段数据做/解掩码（XOR 即可，对称）。
void ws_apply_mask(uint8_t* data, int len, const uint8_t key[4]);

// 操作码 -> 名字
const char* ws_opcode_name(uint8_t op);
// 关闭状态码 -> 原因短语
const char* ws_close_reason(uint16_t code);

// ===================== 握手（RFC 6455 §4）=====================
// 计算 Sec-WebSocket-Accept：base64( SHA1(client_key + GUID) )。
// key 是客户端发来的 Sec-WebSocket-Key（24 字节 base64）。
// out 至少 29 字节（28 字符 + NUL），返回 out。
char* ws_compute_accept(const char* client_key, char* out, int outsz);

// 构造客户端握手请求到 out。host/path 如 "example.com" "/ws"。
// key_buf 是 24 字节的随机 base64 密钥（由调用方提供/生成）。
// 返回写入长度。
int ws_build_client_handshake(const char* host, const char* path,
                              const char* key_buf, String& out);

// 解析服务端 101 响应，校验 Upgrade/Connection 并提取 Sec-WebSocket-Accept。
// 成功返回 0 并写入 accept_out（28+1 字节）。
int ws_parse_server_handshake(const char* raw, int len, char* accept_out, int outsz);

// ===================== 控制帧便捷构造 =====================
// close：状态码 2 字节 + 可选原因。out 至少 128 字节。
int ws_build_close(uint8_t* out, uint16_t code, const char* reason, bool masked,
                  uint32_t* seed);
// ping / pong：可带小负载。
int ws_build_ping(uint8_t* out, const uint8_t* payload, int len, bool masked,
                 uint32_t* seed);
int ws_build_pong(uint8_t* out, const uint8_t* payload, int len, bool masked,
                 uint32_t* seed);

int websocket_self_test();

} // namespace netproto
} // namespace nefu
