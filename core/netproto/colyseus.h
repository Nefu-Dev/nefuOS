/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS 网络协议栈 —— Colyseus 多人游戏协议（bloxd.io 后端）
//
// 定位：bloxd.io 的实时多人走 Colyseus，协议跑在 WebSocket 二进制帧之上。
// 每条 Colyseus 消息 = 一个 WebSocket binary 帧，其 payload 首字节为协议 opcode，
// 其余为消息体（可能是 JSON 文本，也可能是 @colyseus/schema 序列化的二进制状态）。
//
// 本模块先实现【离线的、可单测的】部分：
//   1) opcode 表与可读名；
//   2) 从已解的 WS 二进制 payload 拆出 (opcode, body)；
//   3) Colyseus 0.15 的变长整数（varint / 无符号 LEB128）编解码，以及
//      基于它的 JOIN_ROOM 帧编码——roomId / sessionId / options 三字段均为
//      “varint 长度前缀 + UTF-8 字节串”，与 @colyseus/schema 的 decode.string 一致。
//
// 已据文档化的 0.15 线协议实现并单测；真机与 bloxd 实测时的唯一不确定项是
// options 字段的取值（本模块按 JSON 字符串发送，与 bloxd matchmake 的
// joinOptions 对齐）。其余 opcode 帧体由 bloxd 页面的 SDK（JS）在应用层自行
// 反序列化，native 侧只需把二进制 WS 帧原样投递（已通）。
//
// 仍待真机抓包确认/实现（沙箱无网络、无 QEMU，无法端到端验证）：
//   * ROOM_STATE / ROOM_STATE_PATCH 的 @colyseus/schema 反射反序列化；
//   * 客户端上行消息（ROOM_DATA / ROOM_LEAVE / 重连）的封装。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

// Colyseus wire protocol opcode（每条二进制 WS 消息的首字节）。
// 取自 @colyseus/shared-types 的 Protocol 常量（房间相关码 10~18，跨
// 0.14/0.15/0.16 稳定；首字节高 3 位为 modifier，解码前需 & 0x1F）。
enum ColyseusOp : uint8_t {
    CO_ROOM_JOIN         = 10,  // C->S 加入请求（JOIN_ROOM）
    CO_ROOM_ERROR        = 11,  // S->C 错误；体 = [varint code][varint-len 串 msg]
    CO_ROOM_LEAVE        = 12,  // C->S 离开
    CO_ROOM_DATA         = 13,  // S<->C 自定义消息 / 请求-响应
    CO_ROOM_STATE        = 14,  // S->C 全量状态（schema）
    CO_ROOM_STATE_PATCH  = 15,  // S->C 增量状态补丁（schema）
    CO_ROOM_DATA_SCHEMA  = 16,  // 旧版：经 room.send 的 schema 实例
    CO_ROOM_DATA_BYTES   = 17,  // 二进制负载的自定义消息
    CO_ROOM_PING         = 18,  // 保活
};

// ERROR(11) 的已知错误码（@colyseus/shared-types 的 ErrorCode）。
enum ColyseusErrorCode : int {
    CE_AUTH_FAILED = 525,   // onAuth 拒绝；net-selftest 实测到的 "onAuth failed"
};

struct ColyseusMsg {
    uint8_t        op;          // 首字节 opcode
    const uint8_t* body;        // 指向 payload+1（已解 WS 掩码后）
    int            body_len;
};

// opcode -> 可读名（写入 buf，避免返回静态串的并发问题）。
void colyseus_op_name(uint8_t op, char* buf, int bufsz);

// 从一条已解 WS 掩码的二进制 payload 解析出 (opcode, body)。
// data 至少 1 字节。成功返回 0 并填 out；空帧/非法返回 -1。
int colyseus_parse(const uint8_t* data, int len, ColyseusMsg& out);

// ---- 变长整数（@colyseus/schema 用无符号 LEB128 varint）----
// 编码 v 到 p（最多 room 字节），返回写入字节数，空间不足返回 -1。
int colyseus_varint_enc(uint8_t* p, int room, uint64_t v);
// 从 p[off..len] 解码一个 varint 到 v，off 前进并返回消费字节数；越界/溢出返回 -1。
int colyseus_varint_dec(const uint8_t* p, int len, int& off, uint64_t& v);

// 编码一个字符串字段：[varint 长度][UTF-8 字节]；返回总字节数，空间不足返回 -1。
int colyseus_put_str(uint8_t* p, int room, const uint8_t* s, int slen);
// 解码一个字符串字段（与 colyseus_put_str 对称）；out 以 '\0' 结尾。
// 返回 0 成功；poff 前进到下一字段起点。
int colyseus_get_str(const uint8_t* body, int body_len, int& off,
                     char* out, int outsz);

// 解码 ERROR(11) 帧体：[varint code][varint-len string message]，与
// @colyseus/shared-types 的 Protocol.ERROR 编码一致（不是 JSON！）。
// *code 填错误码（如 525 = AUTH_FAILED），msg 填消息文本（可传 NULL 忽略）。
// 返回 0 成功；帧体过短/损坏返回 -1。
int colyseus_decode_error(const uint8_t* body, int body_len, int* code,
                          char* msg, int msgsz);

// 按 0.15 风格编码客户端 JOIN_ROOM 帧：
//   [op=0x0A][roomId: varint-len + str][sessionId: varint-len + str]
//            [options: varint-len + str(JSON)]
// options 传 JSON 字符串（如 "{}"）；传 NULL/0 则编码为空串。
// 返回整帧长度，失败返回 -1。
int colyseus_encode_join(uint8_t* out, int outsz,
                         const char* room_id, const char* session_id,
                         const uint8_t* options, int optlen);

int colyseus_self_test();

} // namespace netproto
} // namespace nefu
