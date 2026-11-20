/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS 网络协议栈 —— Colyseus 多人游戏协议实现
//
// 见 colyseus.h 顶部说明：本模块实现可离线单测的 opcode 拆分与 JOIN_ROOM 编码，
// 并与现有 WebSocket 帧层做端到端 round-trip 自测，作为 bloxd.io 进游戏前的
// 协议地基。schema 状态反序列化等需真机抓包的部分留作 TODO。
#include "colyseus.h"
#include "websocket.h"   // ws_encode_frame / ws_decode_frame：验证端到端路径
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

void colyseus_op_name(uint8_t op, char* buf, int bufsz) {
    if (!buf || bufsz < 2) { if (buf && bufsz) buf[0] = 0; return; }
    const char* s = "UNKNOWN";
    switch (op) {
        case CO_ROOM_JOIN:        s = "ROOM_JOIN(10)"; break;
        case CO_ROOM_ERROR:       s = "ERROR(11)"; break;
        case CO_ROOM_LEAVE:       s = "LEAVE_ROOM(12)"; break;
        case CO_ROOM_DATA:        s = "ROOM_DATA(13)"; break;
        case CO_ROOM_STATE:       s = "ROOM_STATE(14)"; break;
        case CO_ROOM_STATE_PATCH: s = "ROOM_STATE_PATCH(15)"; break;
        case CO_ROOM_DATA_SCHEMA: s = "ROOM_DATA_SCHEMA(16)"; break;
        case CO_ROOM_DATA_BYTES:  s = "ROOM_DATA_BYTES(17)"; break;
        case CO_ROOM_PING:        s = "PING(18)"; break;
        default:                  s = "UNKNOWN"; break;
    }
    int n = 0;
    while (s[n] && n + 1 < bufsz) { buf[n] = s[n]; n++; }
    buf[n] = 0;
}

int colyseus_parse(const uint8_t* data, int len, ColyseusMsg& out) {
    if (!data || len < 1) return -1;
    out.op = data[0];
    out.body = data + 1;
    out.body_len = len - 1;
    return 0;
}

// ---- 变长整数：@colyseus/schema 使用无符号 LEB128 varint ----
int colyseus_varint_enc(uint8_t* p, int room, uint64_t v) {
    if (!p || room < 1) return -1;
    int n = 0;
    do {
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v) b |= 0x80;
        p[n++] = b;
        if (n >= room) return (v == 0) ? n : -1;   // 空间不足
    } while (v);
    return n;
}

int colyseus_varint_dec(const uint8_t* p, int len, int& off, uint64_t& v) {
    if (!p || off < 0 || off >= len) return -1;
    uint64_t result = 0;
    int shift = 0;
    int n = 0;
    for (;;) {
        if (off >= len) return -1;
        uint8_t b = p[off++];
        n++;
        result |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) { v = result; return n; }
        shift += 7;
        if (shift > 63) return -1;   // 溢出
    }
}

int colyseus_put_str(uint8_t* p, int room, const uint8_t* s, int slen) {
    if (!p || room < 1 || slen < 0) return -1;
    int vl = colyseus_varint_enc(p, room, (uint64_t)slen);
    if (vl < 0) return -1;
    if (slen > 0 && s) {
        if (room - vl < slen) return -1;
        memcpy(p + vl, s, slen);
    }
    return vl + slen;
}

int colyseus_get_str(const uint8_t* body, int body_len, int& poff,
                     char* out, int outsz) {
    if (!body || !out || outsz < 1) return -1;
    uint64_t len = 0;
    int c = colyseus_varint_dec(body, body_len, poff, len);
    if (c < 0) return -1;
    if ((int64_t)poff + (int64_t)len > (int64_t)body_len) return -1;
    int l = (int)len;
    if (l + 1 > outsz) return -1;
    if (l > 0) memcpy(out, body + poff, l);
    out[l] = 0;
    poff += l;
    return 0;
}

int colyseus_decode_error(const uint8_t* body, int body_len, int* code,
                          char* msg, int msgsz) {
    if (!body || body_len < 1) return -1;
    int off = 0;
    uint64_t c = 0;
    if (colyseus_varint_dec(body, body_len, off, c) < 0) return -1;
    if (code) *code = (int)c;
    if (msg && msgsz > 0) {
        msg[0] = 0;
        if (off < body_len) {
            // 尾部是 varint-len + UTF-8 消息；解码失败则保留空串
            colyseus_get_str(body, body_len, off, msg, msgsz);
        }
    }
    return 0;
}

int colyseus_encode_join(uint8_t* out, int outsz,
                         const char* room_id, const char* session_id,
                         const uint8_t* options, int optlen) {
    if (!out || outsz < 4) return -1;
    const uint8_t* r = (const uint8_t*)room_id;
    const uint8_t* s = (const uint8_t*)session_id;
    int rl = room_id ? (int)strlen(room_id) : 0;
    int sl = session_id ? (int)strlen(session_id) : 0;
    int ol = (options && optlen > 0) ? optlen : 0;
    // 最坏情况：每个字段 1 字节 varint 长度
    int need = 1 + (1 + rl) + (1 + sl) + (1 + ol);
    if (need > outsz) return -1;

    int off = 0;
    out[off++] = CO_ROOM_JOIN;                                  // 0x0A
    int n;
    n = colyseus_put_str(out + off, outsz - off, r, rl); if (n < 0) return -1; off += n;
    n = colyseus_put_str(out + off, outsz - off, s, sl); if (n < 0) return -1; off += n;
    n = colyseus_put_str(out + off, outsz - off, options, ol); if (n < 0) return -1; off += n;
    return off;
}

// ---- 自测 ----
static int expect(const char* name, int cond) {
    printf("    %-40s %s\n", name, cond ? "[OK]" : "[FAIL]");
    return cond ? 0 : 1;
}

int colyseus_self_test() {
    int fails = 0;
    printf("== colyseus self tests ==\n");

    // 1) opcode 名可读（对照 @colyseus/shared-types 的 Protocol 常量）
    {
        char buf[32];
        colyseus_op_name(CO_ROOM_JOIN, buf, sizeof(buf));
        fails += expect("op_name(ROOM_JOIN) carries 10", strstr(buf, "10") != 0);
        colyseus_op_name(CO_ROOM_ERROR, buf, sizeof(buf));
        fails += expect("op_name(ERROR) carries 11", strstr(buf, "11") != 0);
        colyseus_op_name(CO_ROOM_STATE, buf, sizeof(buf));
        fails += expect("op_name(ROOM_STATE) carries 14", strstr(buf, "14") != 0);
        colyseus_op_name(200, buf, sizeof(buf));
        fails += expect("op_name(UNKNOWN) safe", buf[0] != 0 && strstr(buf, "UNKNOWN") != 0);
    }

    // 2) 解析最简帧：op + 空体
    {
        uint8_t frame[1] = { CO_ROOM_JOIN };
        ColyseusMsg m;
        int rc = colyseus_parse(frame, 1, m);
        fails += expect("parse empty-body frame rc=0", rc == 0);
        fails += expect("parse empty-body op=10", rc == 0 && m.op == CO_ROOM_JOIN);
        fails += expect("parse empty-body body_len=0", rc == 0 && m.body_len == 0);
    }

    // 3) ERROR(11) 帧体：[varint code][varint-len string message]（非 JSON）
    {
        uint8_t frame[64];
        int p = 0;
        frame[p++] = CO_ROOM_ERROR;                       // 11
        p += colyseus_varint_enc(frame + p, (int)sizeof(frame) - p,
                                 (uint64_t)CE_AUTH_FAILED);   // 525
        const char* m = "onAuth failed";
        p += colyseus_put_str(frame + p, (int)sizeof(frame) - p,
                              (const uint8_t*)m, (int)strlen(m));
        ColyseusMsg cm;
        int rc = colyseus_parse(frame, p, cm);
        fails += expect("parse error frame rc=0", rc == 0);
        fails += expect("parse error op=11", rc == 0 && cm.op == CO_ROOM_ERROR);
        int ecode = 0; char emsg[64];
        int dr = colyseus_decode_error(cm.body, cm.body_len, &ecode, emsg, sizeof(emsg));
        fails += expect("error code=525", dr == 0 && ecode == 525);
        fails += expect("error msg=onAuth failed", dr == 0 && strcmp(emsg, m) == 0);
    }

    // 4) varint（无符号 LEB128）编解码：含单字节与多字节
    {
        uint8_t vbuf[8];
        int n = colyseus_varint_enc(vbuf, sizeof(vbuf), 0x2A);
        fails += expect("varint enc 42 -> 1 byte", n == 1 && vbuf[0] == 0x2A);
        uint64_t out = 0; int off = 0;
        int c = colyseus_varint_dec(vbuf, n, off, out);
        fails += expect("varint dec 42", c == 1 && out == 0x2A && off == 1);

        // 0xAC,0x02 == 300 (LEB128)
        n = colyseus_varint_enc(vbuf, sizeof(vbuf), 300);
        fails += expect("varint enc 300 -> 2 bytes", n == 2 && vbuf[0] == 0xAC && vbuf[1] == 0x02);
        off = 0; out = 0;
        c = colyseus_varint_dec(vbuf, n, off, out);
        fails += expect("varint dec 300", c == 2 && out == 300 && off == 2);

        // 多字节字符串长度前缀：造一个 200 字符的 room 名
        char big[256];
        for (int i = 0; i < 200; i++) big[i] = (char)('a' + (i % 26));
        big[200] = 0;
        uint8_t jb[320];
        int jn = colyseus_encode_join(jb, sizeof(jb), big, "s", (const uint8_t*)"", 0);
        fails += expect("encode_join big-room ok", jn > 0 && jb[0] == CO_ROOM_JOIN);
        // 反解：首字段长度应占 2 字节（200 -> 0xC8,0x01）
        int p = 0; uint64_t rl = 0;
        int rc2 = colyseus_varint_dec(jb + 1, jn - 1, p, rl);
        fails += expect("big-room varint len 2 bytes", rc2 == 2 && rl == 200);
        fails += expect("big-room str matches",
                        rl == 200 && memcmp(jb + 1 + p, big, 200) == 0);
    }

    // 5) JOIN_ROOM 编码 + 反解 round-trip（用 schema 风格字符串解码器）
    {
        uint8_t buf[256];
        const char* room = "social_updates_21";
        const char* sess = "abc123session";
        const char* opt = "{\"metricsCookies\":{},\"hidden\":false}";
        int n = colyseus_encode_join(buf, sizeof(buf), room, sess,
                                     (const uint8_t*)opt, (int)strlen(opt));
        fails += expect("encode_join produces frame", n > 0);
        ColyseusMsg m;
        int rc = colyseus_parse(buf, n, m);
        fails += expect("encode_join round-trip op=10", rc == 0 && m.op == CO_ROOM_JOIN);
        if (rc == 0) {
            int p = 0; char rd[256], sd[256], od[256];
            int r1 = colyseus_get_str(m.body, m.body_len, p, rd, sizeof(rd));
            int r2 = colyseus_get_str(m.body, m.body_len, p, sd, sizeof(sd));
            int r3 = colyseus_get_str(m.body, m.body_len, p, od, sizeof(od));
            fails += expect("round-trip room str", r1 == 0 && strcmp(rd, room) == 0);
            fails += expect("round-trip session str", r2 == 0 && strcmp(sd, sess) == 0);
            fails += expect("round-trip options str", r3 == 0 && strcmp(od, opt) == 0);
            fails += expect("round-trip consumed all bytes", p == m.body_len);
        }
    }

    // 5) 端到端：Colyseus 消息 -> WS binary 帧 -> 解帧 -> Colyseus 解析
    {
        uint8_t co[256];
        int cn = colyseus_encode_join(co, sizeof(co), "roomX", "sessY", (const uint8_t*)"", 0);
        uint8_t pkt[512];
        uint32_t seed = 0x1234;
        int flen = ws_encode_frame(pkt, WS_OP_BINARY, true, co, cn, true, &seed);
        fails += expect("ws wrap colyseus frame", flen > 0);
        WsFrame f;
        int hl = ws_decode_frame(pkt, flen, f);
        fails += expect("ws decode header", hl > 0 && f.opcode == WS_OP_BINARY);
        ColyseusMsg m;
        int rc = colyseus_parse(f.payload, f.payload_len, m);
        fails += expect("end-to-end colyseus op=10", rc == 0 && m.op == CO_ROOM_JOIN);
        fails += expect("end-to-end body preserved",
                        rc == 0 && m.body_len == cn - 1);
    }

    printf("== colyseus failures: %d ==\n", fails);
    return fails;
}

} // namespace netproto
} // namespace nefu
