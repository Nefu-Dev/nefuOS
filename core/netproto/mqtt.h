// nefuOS 网络协议栈 —— MQTT 3.1.1（OASIS）
//
// 固定头：字节0 = (报文类型<<4) | 标志，随后是"剩余长度"
// （变长编码：每字节低 7 位为数据，高位为续位标志，最多 4 字节）。
//
// 报文类型：CONNECT=1 CONNACK=2 PUBLISH=3 PUBACK=4 SUBSCRIBE=8
//           SUBACK=9 UNSUBSCRIBE=10 PINGREQ=12 PINGRESP=13 DISCONNECT=14
//
// 本模块：CONNECT/PUBLISH/SUBSCRIBE/PINGREQ 封装，CONNACK/PUBLISH 解析，
// 变长长度编解码。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

const uint8_t MQTT_CONNECT     = 1;
const uint8_t MQTT_CONNACK     = 2;
const uint8_t MQTT_PUBLISH     = 3;
const uint8_t MQTT_PUBACK      = 4;
const uint8_t MQTT_PUBREC      = 5;
const uint8_t MQTT_PUBREL      = 6;
const uint8_t MQTT_PUBCOMP     = 7;
const uint8_t MQTT_SUBSCRIBE   = 8;
const uint8_t MQTT_SUBACK      = 9;
const uint8_t MQTT_UNSUBSCRIBE = 10;
const uint8_t MQTT_PINGREQ     = 12;
const uint8_t MQTT_PINGRESP    = 13;
const uint8_t MQTT_DISCONNECT  = 14;

// ---- 变长长度 ----
// 把 remaining length 编码到 out，返回写入字节数（1..4）。
int mqtt_encode_remaining_length(uint8_t* out, uint32_t len);
// 从 pkt 解码剩余长度。返回 0 成功，*out_len 为长度，*bytes_used 为占用字节数。
int mqtt_decode_remaining_length(const uint8_t* pkt, int len,
                                 uint32_t* out_len, int* bytes_used);

// ---- 封装 ----
// CONNECT：clean session，无遗嘱/用户名/密码。
int mqtt_build_connect(uint8_t* out, int outsz, const char* client_id,
                      uint16_t keepalive);

// PUBLISH：QoS 0（无包 ID）。
int mqtt_build_publish(uint8_t* out, int outsz, const char* topic,
                       const uint8_t* payload, int payload_len, uint8_t qos);

// SUBSCRIBE：订阅多个主题。topics/qos 数组长度 n。
int mqtt_build_subscribe(uint8_t* out, int outsz, uint16_t packet_id,
                        const char* const* topics, const uint8_t* qos, int n);

// PINGREQ：固定 2 字节。
int mqtt_build_pingreq(uint8_t* out);

// ---- 更多报文 ----
// DISCONNECT：客户端主动断开，固定 2 字节。
int mqtt_build_disconnect(uint8_t* out);
// PUBACK：对应 QoS1 PUBLISH。
int mqtt_build_puback(uint8_t* out, uint16_t packet_id);
// QoS2 报文：PUBREC/PUBREL/PUBCOMP。
int mqtt_build_pubrec(uint8_t* out, uint16_t packet_id);
int mqtt_build_pubrel(uint8_t* out, uint16_t packet_id);
int mqtt_build_pubcomp(uint8_t* out, uint16_t packet_id);
// UNSUBSCRIBE：取消订阅多个主题。
int mqtt_build_unsubscribe(uint8_t* out, int outsz, uint16_t packet_id,
                          const char* const* topics, int n);
// 构造一个带遗嘱（will）的 CONNECT。will_topic/will_msg 为空串表示无遗嘱。
int mqtt_build_connect_will(uint8_t* out, int outsz, const char* client_id,
                            uint16_t keepalive, const char* will_topic,
                            const char* will_msg, uint8_t will_qos);

// ---- 解析 ----
struct MqttPublish {
    String   topic;
    const uint8_t* payload;   // 指向包内
    int      payload_len;
    uint8_t  qos;
    bool     retain, dup;
};
int mqtt_parse_publish(const uint8_t* pkt, int len, MqttPublish& out);

// CONNACK：返回连接返回码（0=接受）。成功 0。
int mqtt_parse_connack(const uint8_t* pkt, int len, uint8_t* return_code);
// 构造一个 CONNACK（return_code=0 接受）。
int mqtt_build_connack(uint8_t* out, uint8_t return_code);

// PINGRESP：服务端对 PINGREQ 的回应，固定 2 字节（0xD0 0x00）。
int mqtt_build_pingresp(uint8_t* out);

// SUBACK：解析包 ID 与每个主题的授予 QoS。granted_out 写入 qos 数组，
// max 为其容量。成功返回授予个数；失败 -1。
int mqtt_parse_suback(const uint8_t* pkt, int len, uint16_t* packet_id,
                     uint8_t* granted_out, int max);

// 取报文类型（固定头高 4 位）
uint8_t mqtt_packet_type(const uint8_t* pkt);

const char* mqtt_type_name(uint8_t t);

int mqtt_self_test();

} // namespace netproto
} // namespace nefu
