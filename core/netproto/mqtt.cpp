// nefuOS 网络协议栈 —— MQTT 3.1.1 实现
#include "mqtt.h"
#include "netproto_common.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// ===================== 变长长度 =====================
int mqtt_encode_remaining_length(uint8_t* out, uint32_t len) {
    int o = 0;
    do {
        uint8_t b = (uint8_t)(len & 0x7Fu);
        len >>= 7;
        if (len > 0) b |= 0x80u;     // 后面还有字节
        out[o++] = b;
    } while (len > 0 && o < 4);
    return o;
}

int mqtt_decode_remaining_length(const uint8_t* pkt, int len,
                                 uint32_t* out_len, int* bytes_used) {
    uint32_t multiplier = 1;
    uint32_t value = 0;
    int i = 0;
    while (i < 4 && i < len) {
        uint8_t b = pkt[i];
        value += (uint32_t)(b & 0x7Fu) * multiplier;
        multiplier *= 128;
        i++;
        if ((b & 0x80u) == 0) {
            *out_len = value;
            *bytes_used = i;
            return 0;
        }
    }
    return -1;
}

// 写一个 MQTT 长度前缀字符串（2 字节大端长度 + 内容）
static int put_mqtt_str(uint8_t* out, int o, const char* s) {
    int l = (int)strlen(s);
    put_be16(out + o, (uint16_t)l); o += 2;
    np_copy(out + o, s, l); o += l;
    return o;
}

// 通用：把"可变头+payload"组装好后，在前面补固定头。
// body 从 out+reserved 开始写；返回整包长度。
static int finish_packet(uint8_t* out, int reserved, uint8_t type_flags, int body_end) {
    int remaining = body_end - reserved;
    uint8_t rl[4];
    int rln = mqtt_encode_remaining_length(rl, (uint32_t)remaining);
    // 把 body 从 reserved 挪到 1+rln
    if (1 + rln != reserved)
        nefu::memmove(out + 1 + rln, out + reserved, (size_t)remaining);
    out[0] = type_flags;
    for (int i = 0; i < rln; i++) out[1 + i] = rl[i];
    return 1 + rln + remaining;
}

int mqtt_build_connect(uint8_t* out, int outsz, const char* client_id,
                      uint16_t keepalive) {
    if (!out || !client_id || outsz < 16) return -1;
    int reserved = 5;                     // 最坏：1 类型 + 4 长度
    int o = reserved;
    // 可变头：协议名 "MQTT"
    put_be16(out + o, 4); o += 2;
    np_copy(out + o, "MQTT", 4); o += 4;
    out[o++] = 4;                         // 协议级别 4 = MQTT 3.1.1
    out[o++] = 0x02;                      // 连接标志：Clean Session
    put_be16(out + o, keepalive); o += 2;
    // 载荷：客户端 ID
    o = put_mqtt_str(out, o, client_id);
    return finish_packet(out, reserved, 0x10, o);   // CONNECT，标志 0
}

int mqtt_build_publish(uint8_t* out, int outsz, const char* topic,
                       const uint8_t* payload, int payload_len, uint8_t qos) {
    if (!out || !topic || outsz < 8) return -1;
    int reserved = 5;
    int o = reserved;
    o = put_mqtt_str(out, o, topic);
    // QoS>0 才带包 ID；这里 QoS 0 不带
    if (payload && payload_len > 0) { np_copy(out + o, payload, payload_len); o += payload_len; }
    uint8_t type_flags = (uint8_t)(MQTT_PUBLISH << 4);
    if (qos) type_flags |= (uint8_t)(qos << 1);
    return finish_packet(out, reserved, type_flags, o);
}

int mqtt_build_subscribe(uint8_t* out, int outsz, uint16_t packet_id,
                        const char* const* topics, const uint8_t* qos, int n) {
    if (!out || !topics || n <= 0) return -1;
    int reserved = 5;
    int o = reserved;
    put_be16(out + o, packet_id); o += 2;
    for (int i = 0; i < n; i++) {
        o = put_mqtt_str(out, o, topics[i]);
        out[o++] = qos[i];
    }
    // SUBSCRIBE 固定标志位必须为 0x2
    return finish_packet(out, reserved, (uint8_t)((MQTT_SUBSCRIBE << 4) | 0x2u), o);
}

int mqtt_build_pingreq(uint8_t* out) {
    if (!out) return -1;
    out[0] = (uint8_t)(MQTT_PINGREQ << 4);   // 0xC0
    out[1] = 0x00;                            // remaining length = 0
    return 2;
}

int mqtt_build_disconnect(uint8_t* out) {
    if (!out) return -1;
    out[0] = (uint8_t)(MQTT_DISCONNECT << 4);  // 0xE0
    out[1] = 0x00;
    return 2;
}

int mqtt_build_puback(uint8_t* out, uint16_t packet_id) {
    if (!out) return -1;
    out[0] = (uint8_t)(MQTT_PUBACK << 4);     // 0x40
    out[1] = 2;                                // remaining length = 2
    put_be16(out + 2, packet_id);
    return 4;
}

int mqtt_build_pubrec(uint8_t* out, uint16_t packet_id) {
    if (!out) return -1;
    out[0] = (uint8_t)(MQTT_PUBREC << 4);   // 0x50
    out[1] = 2;
    put_be16(out + 2, packet_id);
    return 4;
}

int mqtt_build_pubrel(uint8_t* out, uint16_t packet_id) {
    if (!out) return -1;
    out[0] = 0x62;                               // PUBREL 固定标志 0x2
    out[1] = 2;
    put_be16(out + 2, packet_id);
    return 4;
}

int mqtt_build_pubcomp(uint8_t* out, uint16_t packet_id) {
    if (!out) return -1;
    out[0] = 0x70;                               // PUBCOMP
    out[1] = 2;
    put_be16(out + 2, packet_id);
    return 4;
}

int mqtt_build_unsubscribe(uint8_t* out, int outsz, uint16_t packet_id,
                          const char* const* topics, int n) {
    if (!out || !topics || n <= 0) return -1;
    int reserved = 5;
    int o = reserved;
    put_be16(out + o, packet_id); o += 2;
    for (int i = 0; i < n; i++) o = put_mqtt_str(out, o, topics[i]);
    // UNSUBSCRIBE 固定标志位 0x2
    return finish_packet(out, reserved, (uint8_t)((MQTT_UNSUBSCRIBE << 4) | 0x2u), o);
}

int mqtt_build_connect_will(uint8_t* out, int outsz, const char* client_id,
                            uint16_t keepalive, const char* will_topic,
                            const char* will_msg, uint8_t will_qos) {
    if (!out || !client_id || outsz < 16) return -1;
    int reserved = 5;
    int o = reserved;
    put_be16(out + o, 4); o += 2;
    np_copy(out + o, "MQTT", 4); o += 4;
    out[o++] = 4;                              // level
    uint8_t flags = 0x02;                      // clean session
    bool has_will = will_topic && will_topic[0] && will_msg;
    if (has_will) {
        flags |= 0x04;                         // will flag
        flags |= (uint8_t)((will_qos & 0x03) << 3);
    }
    out[o++] = flags;
    put_be16(out + o, keepalive); o += 2;
    o = put_mqtt_str(out, o, client_id);
    if (has_will) {
        o = put_mqtt_str(out, o, will_topic);
        int mlen = (int)strlen(will_msg);
        put_be16(out + o, (uint16_t)mlen); o += 2;
        np_copy(out + o, will_msg, mlen); o += mlen;
    }
    return finish_packet(out, reserved, 0x10, o);
}

// ===================== 解析 =====================
uint8_t mqtt_packet_type(const uint8_t* pkt) {
    return (uint8_t)((pkt[0] >> 4) & 0x0Fu);
}

int mqtt_parse_publish(const uint8_t* pkt, int len, MqttPublish& out) {
    if (!pkt || len < 2) return -1;
    uint8_t type = mqtt_packet_type(pkt);
    if (type != MQTT_PUBLISH) return -1;
    out.dup = (pkt[0] & 0x08u) != 0;
    out.qos = (uint8_t)((pkt[0] >> 1) & 0x03u);
    out.retain = (pkt[0] & 0x01u) != 0;

    uint32_t rl = 0; int used = 0;
    if (mqtt_decode_remaining_length(pkt + 1, len - 1, &rl, &used) != 0) return -1;
    int o = 1 + used;
    if (o + 2 > len) return -1;
    // topic
    uint16_t tlen = be16(pkt + o); o += 2;
    if (o + tlen > len) return -1;
    out.topic = String((const char*)(pkt + o), tlen); o += tlen;
    // QoS>0 跳过包 ID
    if (out.qos > 0) o += 2;
    out.payload = pkt + o;
    out.payload_len = len - o;
    return 0;
}

int mqtt_build_connack(uint8_t* out, uint8_t return_code) {
    if (!out) return -1;
    out[0] = (uint8_t)(MQTT_CONNACK << 4);
    out[1] = 2;
    out[2] = 0;          // 连接会话标志
    out[3] = return_code;
    return 4;
}

int mqtt_parse_connack(const uint8_t* pkt, int len, uint8_t* return_code) {
    if (!pkt || len < 4) return -1;
    if (mqtt_packet_type(pkt) != MQTT_CONNACK) return -1;
    // 剩余长度应为 2：[ack flags][return code]
    *return_code = pkt[3];
    return 0;
}

int mqtt_build_pingresp(uint8_t* out) {
    if (!out) return -1;
    out[0] = (uint8_t)(MQTT_PINGRESP << 4);   // 0xD0
    out[1] = 0x00;
    return 2;
}

int mqtt_parse_suback(const uint8_t* pkt, int len, uint16_t* packet_id,
                     uint8_t* granted_out, int max) {
    if (!pkt || len < 3) return -1;
    if (mqtt_packet_type(pkt) != MQTT_SUBACK) return -1;
    uint32_t rlen = 0; int used = 0;
    if (mqtt_decode_remaining_length(pkt + 1, len - 1, &rlen, &used) != 0) return -1;
    int o = 1 + used;
    if (o + 2 > len) return -1;
    if (packet_id) *packet_id = be16(pkt + o);
    o += 2;
    int n = 0;
    while (o < len && n < max) {
        granted_out[n++] = pkt[o++];
    }
    return n;
}

const char* mqtt_type_name(uint8_t t) {
    switch (t) {
    case MQTT_CONNECT:     return "CONNECT";
    case MQTT_CONNACK:     return "CONNACK";
    case MQTT_PUBLISH:     return "PUBLISH";
    case MQTT_PUBACK:      return "PUBACK";
    case MQTT_PUBREC:      return "PUBREC";
    case MQTT_PUBREL:      return "PUBREL";
    case MQTT_PUBCOMP:     return "PUBCOMP";
    case MQTT_SUBSCRIBE:   return "SUBSCRIBE";
    case MQTT_SUBACK:      return "SUBACK";
    case MQTT_UNSUBSCRIBE: return "UNSUBSCRIBE";
    case MQTT_PINGREQ:     return "PINGREQ";
    case MQTT_PINGRESP:    return "PINGRESP";
    case MQTT_DISCONNECT:  return "DISCONNECT";
    default:               return "?";
    }
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [mqtt] FAIL: %s\n", what); }
}
} // namespace

int mqtt_self_test() {
    g_fails = 0;
    uint8_t buf[256];

    // 1) 变长长度编解码
    uint8_t rl[4]; int n; uint32_t v; int used;
    n = mqtt_encode_remaining_length(rl, 0);
    expect("rl 0", n == 1 && rl[0] == 0);
    expect("rl 0 decode", mqtt_decode_remaining_length(rl, 1, &v, &used) == 0 && v == 0 && used == 1);

    n = mqtt_encode_remaining_length(rl, 127);
    expect("rl 127", n == 1 && rl[0] == 127);
    expect("rl 127 decode", mqtt_decode_remaining_length(rl, 1, &v, &used) == 0 && v == 127);

    n = mqtt_encode_remaining_length(rl, 128);
    expect("rl 128 bytes", n == 2 && rl[0] == 0x80 && rl[1] == 0x01);
    expect("rl 128 decode", mqtt_decode_remaining_length(rl, 2, &v, &used) == 0 && v == 128);

    n = mqtt_encode_remaining_length(rl, 16384);
    expect("rl 16384", n == 3 && rl[2] == 0x01);
    expect("rl 16384 decode", mqtt_decode_remaining_length(rl, 3, &v, &used) == 0 && v == 16384);

    // 2) PINGREQ
    n = mqtt_build_pingreq(buf);
    expect("mqtt pingreq", n == 2 && buf[0] == 0xC0 && buf[1] == 0x00);

    // 3) CONNECT
    n = mqtt_build_connect(buf, sizeof(buf), "nefu-client", 60);
    expect("mqtt connect type", mqtt_packet_type(buf) == MQTT_CONNECT);
    // 找到 "MQTT" 协议名
    bool found = false;
    for (int i = 1; i + 4 < n; i++)
        if (buf[i] == 'M' && buf[i+1] == 'Q' && buf[i+2] == 'T' && buf[i+3] == 'T') found = true;
    expect("mqtt connect proto", found);

    // 4) PUBLISH
    const char* topic = "sensors/temp";
    const char* payload = "23.5";
    n = mqtt_build_publish(buf, sizeof(buf), topic, (const uint8_t*)payload, 4, 0);
    expect("mqtt publish type", mqtt_packet_type(buf) == MQTT_PUBLISH);
    MqttPublish p;
    int r = mqtt_parse_publish(buf, n, p);
    expect("mqtt publish parse", r == 0);
    expect("mqtt publish topic", p.topic == topic);
    expect("mqtt publish payload", p.payload_len == 4 && memcmp(p.payload, payload, 4) == 0);

    // 5) SUBSCRIBE
    const char* tops[2] = { "a/#", "b/+" };
    uint8_t qss[2] = { 0, 1 };
    n = mqtt_build_subscribe(buf, sizeof(buf), 0x0001, tops, qss, 2);
    expect("mqtt subscribe type", mqtt_packet_type(buf) == MQTT_SUBSCRIBE);

    // 6) CONNACK：构造并解析
    uint8_t ca[4];
    mqtt_build_connack(ca, 0);
    uint8_t rc = 0xFF;
    r = mqtt_parse_connack(ca, 4, &rc);
    expect("mqtt connack", r == 0 && rc == 0);
    mqtt_build_connack(ca, 5);   // 未授权
    r = mqtt_parse_connack(ca, 4, &rc);
    expect("mqtt connack denied", r == 0 && rc == 5);

    // 7) 类型名
    expect("mqtt type name", strcmp(mqtt_type_name(MQTT_PUBLISH), "PUBLISH") == 0);

    // 8) DISCONNECT / PUBACK
    n = mqtt_build_disconnect(buf);
    expect("mqtt disconnect", n == 2 && buf[0] == 0xE0);
    n = mqtt_build_puback(buf, 0x0007);
    expect("mqtt puback", n == 4 && mqtt_packet_type(buf) == MQTT_PUBACK &&
           be16(buf + 2) == 0x0007);

    // 9) UNSUBSCRIBE
    n = mqtt_build_unsubscribe(buf, sizeof(buf), 0x0002, tops, 2);
    expect("mqtt unsubscribe", mqtt_packet_type(buf) == MQTT_UNSUBSCRIBE);

    // 10) 带遗嘱的 CONNECT
    n = mqtt_build_connect_will(buf, sizeof(buf), "dev1", 30, "lwt/offline", "bye", 0);
    expect("mqtt will connect", mqtt_packet_type(buf) == MQTT_CONNECT && n > 10);
    // 遗嘱位应置位
    bool found_will = false;
    for (int i = 1; i + 4 < n; i++)
        if (buf[i] == 'M' && buf[i+1] == 'Q' && buf[i+2] == 'T' && buf[i+3] == 'T') found_will = true;
    expect("mqtt will proto", found_will);

    // 11) PINGRESP
    n = mqtt_build_pingresp(buf);
    expect("mqtt pingresp", n == 2 && buf[0] == 0xD0 && buf[1] == 0x00);

    // 12) SUBACK 解析：90 04 [pid hi lo] [qos0 qos1]
    uint8_t sa[6] = { 0x90, 0x04, 0x00, 0x01, 0x00, 0x01 };
    uint16_t pid = 0; uint8_t granted[4];
    int gn = mqtt_parse_suback(sa, 6, &pid, granted, 4);
    // 13) QoS2 握手
    n = mqtt_build_pubrec(buf, 0x0009);
    expect("mqtt pubrec", mqtt_packet_type(buf) == MQTT_PUBREC && be16(buf+2) == 9);
    n = mqtt_build_pubrel(buf, 0x0009);
    expect("mqtt pubrel", mqtt_packet_type(buf) == MQTT_PUBREL);
    n = mqtt_build_pubcomp(buf, 0x0009);
    expect("mqtt pubcomp", mqtt_packet_type(buf) == MQTT_PUBCOMP);

    expect("mqtt suback", gn == 2 && pid == 0x0001 &&
           granted[0] == 0 && granted[1] == 1);

    return g_fails;
}

} // namespace netproto
} // namespace nefu
