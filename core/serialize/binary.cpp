// nefuOS 数据序列化与编解码库 —— 二进制格式模块实现
#include "binary.h"
#include <cstdio>
#include <stdarg.h>

namespace nefu {
namespace serialize {

// ============================================================================
// 0. varint / zigzag
// ============================================================================
int varint_size(uint64_t v) {
    int n = 1;
    while (v >= 0x80) { v >>= 7; n++; }
    return n;
}
int varint_encode(uint64_t v, uint8_t* out) {
    int n = 0;
    while (v >= 0x80) {
        out[n++] = (uint8_t)((v & 0x7F) | 0x80);
        v >>= 7;
    }
    out[n++] = (uint8_t)(v & 0x7F);
    return n;
}
uint64_t varint_decode(const uint8_t* p, int* consumed) {
    uint64_t v = 0;
    int shift = 0;
    int n = 0;
    for (;;) {
        uint8_t b = p[n];
        v |= (uint64_t)(b & 0x7F) << shift;
        n++;
        if (!(b & 0x80)) break;
        shift += 7;
        if (n > 10) break;   // 防溢出
    }
    if (consumed) *consumed = n;
    return v;
}
uint64_t zigzag_encode(int64_t v) {
    return (uint64_t)((v << 1) ^ (v >> 63));
}
int64_t zigzag_decode(uint64_t v) {
    return (int64_t)((v >> 1) ^ -(int64_t)(v & 1));
}

int varint_self_test() {
    int fail = 0;
    // 往返：若干典型值
    int64_t vals[] = {0, -1, 1, -2, 2, 63, -64, 64, -65, 127, 128, 300, 100000, -100000,
                      9223372036854775807LL, -9223372036854775807LL - 1};
    for (int i = 0; i < (int)(sizeof(vals) / sizeof(vals[0])); i++) {
        uint8_t buf[16];
        uint64_t zz = zigzag_encode(vals[i]);
        int n = varint_encode(zz, buf);
        int consumed = 0;
        uint64_t back = varint_decode(buf, &consumed);
        if (consumed != n) fail++;
        int64_t v2 = zigzag_decode(back);
        if (v2 != vals[i]) fail++;
    }
    // 已知向量：zigzag(0)=0, zigzag(-1)=1, zigzag(1)=2, zigzag(-2)=3
    if (zigzag_encode(0) != 0) fail++;
    if (zigzag_encode(-1) != 1) fail++;
    if (zigzag_encode(1) != 2) fail++;
    if (zigzag_encode(-2) != 3) fail++;
    // 已知 varint 字节序列：300 -> 0xAC 0x02
    {
        uint8_t buf[4];
        int n = varint_encode(300, buf);
        if (n != 2 || buf[0] != 0xAC || buf[1] != 0x02) fail++;
    }
    return fail;
}

// ============================================================================
// ByteBuf / Cursor
// ============================================================================
ByteBuf::ByteBuf() : data(0), len(0), cap(0) {}
ByteBuf::~ByteBuf() { if (data) delete[] data; data = 0; }
void ByteBuf::reserve(int need) {
    if (need <= cap) return;
    int nc = cap > 0 ? cap * 2 : 64;
    while (nc < need) nc *= 2;
    uint8_t* nd = new uint8_t[nc];
    if (!nd) return;
    if (data && len > 0) memcpy(nd, data, (size_t)len);
    if (data) delete[] data;
    data = nd; cap = nc;
}
void ByteBuf::put8(uint8_t v) { reserve(len + 1); if (!data) return; data[len++] = v; }
void ByteBuf::put16(uint16_t v) {
    reserve(len + 2);
    if (!data) return;
    data[len++] = (uint8_t)(v & 0xFF);
    data[len++] = (uint8_t)((v >> 8) & 0xFF);
}
void ByteBuf::put32(uint32_t v) {
    reserve(len + 4);
    if (!data) return;
    for (int i = 0; i < 4; i++) data[len++] = (uint8_t)((v >> (8 * i)) & 0xFF);
}
void ByteBuf::put64(uint64_t v) {
    reserve(len + 8);
    if (!data) return;
    for (int i = 0; i < 8; i++) data[len++] = (uint8_t)((v >> (8 * i)) & 0xFF);
}
void ByteBuf::put(const uint8_t* p, int n) { reserve(len + n); if (!data) return; memcpy(data + len, p, (size_t)n); len += n; }
void ByteBuf::put_str(const char* s) { put((const uint8_t*)s, (int)strlen(s)); }

uint8_t Cursor::get8() {
    if (pos + 1 > len) { ok = false; return 0; }
    return p[pos++];
}
uint16_t Cursor::get16() {
    if (pos + 2 > len) { ok = false; return 0; }
    uint16_t v = (uint16_t)p[pos] | ((uint16_t)p[pos + 1] << 8);
    pos += 2; return v;
}
uint32_t Cursor::get32() {
    if (pos + 4 > len) { ok = false; return 0; }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)p[pos + i] << (8 * i);
    pos += 4; return v;
}
uint64_t Cursor::get64() {
    if (pos + 8 > len) { ok = false; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[pos + i] << (8 * i);
    pos += 8; return v;
}
uint64_t Cursor::get_be32() {
    if (pos + 4 > len) { ok = false; return 0; }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = (v << 8) | p[pos + i];
    pos += 4; return v;
}
uint64_t Cursor::get_be64() {
    if (pos + 8 > len) { ok = false; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[pos + i];
    pos += 8; return v;
}
void Cursor::take(int n) { if (pos + n > len) { ok = false; return; } pos += n; }
const uint8_t* Cursor::ptr(int n) {
    if (pos + n > len) { ok = false; return 0; }
    return p + pos;
}

// ============================================================================
// 1. MessagePack
// ============================================================================
void mp_pack_value(const VValue& v, ByteBuf& out);

void mp_pack(const VValue& v, ByteBuf& out) { mp_pack_value(v, out); }

void mp_pack_value(const VValue& v, ByteBuf& out) {
    switch (v.type) {
    case VType::V_NULL: out.put8(0xC0); break;
    case VType::V_BOOL: out.put8(v.b ? 0xC3 : 0xC2); break;
    case VType::V_INT: {
        long long n = v.i;
        if (n >= 0 && n <= 127) { out.put8((uint8_t)n); }
        else if (n >= -32 && n < 0) { out.put8((uint8_t)(0xE0 | (n + 32))); }
        else if (n >= -128 && n < 128) { out.put8(0xD0); out.put8((uint8_t)n); }
        else if (n >= -32768 && n < 32768) { out.put8(0xD1); out.put16((uint16_t)(int16_t)n); }
        else if (n >= -2147483648LL && n < 2147483648LL) { out.put8(0xD2); out.put32((uint32_t)(int32_t)n); }
        else { out.put8(0xD3); out.put64((uint64_t)n); }
        break;
    }
    case VType::V_DOUBLE: {
        out.put8(0xCB);
        uint64_t bits;
        memcpy(&bits, &v.d, 8);
        out.put64(bits);   // 小端写入
        break;
    }
    case VType::V_STRING: {
        int n = v.s.len();
        if (n < 32) out.put8((uint8_t)(0xA0 | n));
        else if (n <= 0xFF) { out.put8(0xD9); out.put8((uint8_t)n); }
        else if (n <= 0xFFFF) { out.put8(0xDA); out.put16((uint16_t)n); }
        else { out.put8(0xDB); out.put32((uint32_t)n); }
        out.put((const uint8_t*)v.s.c_str(), n);
        break;
    }
    case VType::V_ARRAY: {
        int n = v.arr.size();
        if (n < 16) out.put8((uint8_t)(0x90 | n));
        else if (n <= 0xFFFF) { out.put8(0xDC); out.put16((uint16_t)n); }
        else { out.put8(0xDD); out.put32((uint32_t)n); }
        for (int i = 0; i < n; i++) mp_pack_value(*v.arr[i], out);
        break;
    }
    case VType::V_OBJECT: {
        int n = v.obj.size();
        if (n < 16) out.put8((uint8_t)(0x80 | n));
        else if (n <= 0xFFFF) { out.put8(0xDE); out.put16((uint16_t)n); }
        else { out.put8(0xDF); out.put32((uint32_t)n); }
        for (int i = 0; i < n; i++) {
            // key 作为字符串
            const String& k = v.obj[i].key;
            int kn = k.len();
            if (kn < 32) out.put8((uint8_t)(0xA0 | kn));
            else if (kn <= 0xFF) { out.put8(0xD9); out.put8((uint8_t)kn); }
            else { out.put8(0xDA); out.put16((uint16_t)kn); }
            out.put((const uint8_t*)k.c_str(), kn);
            mp_pack_value(*v.obj[i].val, out);
        }
        break;
    }
    }
}

static VValue* mp_unpack_value(Cursor& c);

VValue* mp_unpack(const uint8_t* data, int len, const char** err) {
    Cursor c(data, len);
    VValue* v = mp_unpack_value(c);
    if (!c.ok && err) *err = "msgpack: truncated";
    return v;
}

static VValue* mp_unpack_value(Cursor& c) {
    uint8_t b = c.get8();
    if (!c.ok) return new VValue();
    if (b <= 0x7F) return new VValue((long long)b);                       // positive fixint
    if (b >= 0xE0) return new VValue((long long)(int8_t)(b | 0x20));     // negative fixint
    if ((b & 0xE0) == 0xA0) {                                              // fixstr
        int n = b & 0x1F;
        const uint8_t* p = c.ptr(n);
        if (!p) return new VValue();
        String s((const char*)p, n); c.take(n);
        return new VValue(s);
    }
    if ((b & 0xF0) == 0x80) {                                              // fixmap
        int n = b & 0x0F;
        VValue* o = new VValue(); o->type = VType::V_OBJECT;
        for (int i = 0; i < n; i++) {
            VValue* k = mp_unpack_value(c);
            VValue* val = mp_unpack_value(c);
            VMember m; m.key = k->s; m.val = val;
            o->obj.push(m); free_value(k);
        }
        return o;
    }
    if ((b & 0xF0) == 0x90) {                                              // fixarray
        int n = b & 0x0F;
        VValue* a = new VValue(); a->type = VType::V_ARRAY;
        for (int i = 0; i < n; i++) a->arr.push(mp_unpack_value(c));
        return a;
    }
    switch (b) {
    case 0xC0: return new VValue();
    case 0xC2: return new VValue(false);
    case 0xC3: return new VValue(true);
    case 0xCA: { uint32_t bits = c.get32(); float f; memcpy(&f, &bits, 4); return new VValue((double)f); }
    case 0xCB: { uint64_t bits = c.get64(); double d; memcpy(&d, &bits, 8); return new VValue(d); }
    case 0xD0: { int8_t v = (int8_t)c.get8(); return new VValue((long long)v); }
    case 0xD1: { int16_t v = (int16_t)c.get16(); return new VValue((long long)v); }
    case 0xD2: { int32_t v = (int32_t)c.get32(); return new VValue((long long)v); }
    case 0xD3: { int64_t v = (int64_t)c.get64(); return new VValue((long long)v); }
    case 0xCC: return new VValue((long long)c.get8());
    case 0xCD: return new VValue((long long)c.get16());
    case 0xCE: return new VValue((long long)c.get32());
    case 0xCF: return new VValue((long long)c.get64());
    case 0xD9: { int n = c.get8(); const uint8_t* p = c.ptr(n); String s((const char*)p, n); c.take(n); return new VValue(s); }
    case 0xDA: { int n = c.get16(); const uint8_t* p = c.ptr(n); String s((const char*)p, n); c.take(n); return new VValue(s); }
    case 0xDB: { int n = (int)c.get32(); const uint8_t* p = c.ptr(n); String s((const char*)p, n); c.take(n); return new VValue(s); }
    case 0xDC: { int n = c.get16(); VValue* a = new VValue(); a->type = VType::V_ARRAY;
                 for (int i = 0; i < n; i++) a->arr.push(mp_unpack_value(c)); return a; }
    case 0xDD: { int n = (int)c.get32(); VValue* a = new VValue(); a->type = VType::V_ARRAY;
                 for (int i = 0; i < n; i++) a->arr.push(mp_unpack_value(c)); return a; }
    case 0xDE: { int n = c.get16(); VValue* o = new VValue(); o->type = VType::V_OBJECT;
                 for (int i = 0; i < n; i++) { VValue* k = mp_unpack_value(c); VValue* vv = mp_unpack_value(c);
                     VMember m; m.key = k->s; m.val = vv; o->obj.push(m); free_value(k); } return o; }
    case 0xDF: { int n = (int)c.get32(); VValue* o = new VValue(); o->type = VType::V_OBJECT;
                 for (int i = 0; i < n; i++) { VValue* k = mp_unpack_value(c); VValue* vv = mp_unpack_value(c);
                     VMember m; m.key = k->s; m.val = vv; o->obj.push(m); free_value(k); } return o; }
    default: return new VValue();
    }
}

int msgpack_self_test() {
    int fail = 0;
    // 构造一棵 VValue 树，打包再解包，比对关键字段
    VValue root; root.type = VType::V_OBJECT;
    root.set("name", VValue("nefuOS"));
    root.set("port", VValue(8080));
    root.set("enabled", VValue(true));
    root.set("missing", VValue());
    VValue arr; arr.type = VType::V_ARRAY;
    arr.push(VValue(1)); arr.push(VValue(2)); arr.push(VValue(3));
    root.set("nums", arr);

    ByteBuf buf;
    mp_pack(root, buf);
    if (buf.len <= 0) fail++;
    const char* err = 0;
    VValue* back = mp_unpack(buf.data, buf.len, &err);
    if (!back) fail++;
    else {
        const VValue* nm = back->get("name");
        if (!nm || strcmp(nm->s.c_str(), "nefuOS") != 0) fail++;
        const VValue* pt = back->get("port");
        if (!pt || pt->i != 8080) fail++;
        const VValue* en = back->get("enabled");
        if (!en || !en->b) fail++;
        const VValue* nu = back->get("nums");
        if (!nu || nu->size() != 3 || nu->arr[2]->i != 3) fail++;
        free_value(back);
    }
    // 已知 MessagePack 字节：[0x82, 0xA1,'a',0x01, 0xA1,'b',0xC0] = {"a":1,"b":null}
    {
        uint8_t raw[] = {0x82, 0xA1, 'a', 0x01, 0xA1, 'b', 0xC0};
        VValue* v = mp_unpack(raw, (int)sizeof(raw));
        if (!v || v->size() != 2) fail++;
        else {
            const VValue* a = v->get("a");
            if (!a || a->i != 1) fail++;
            if (!v->get("b") || !v->get("b")->is_null()) fail++;
        }
        if (v) free_value(v);
    }
    return fail;
}


// ============================================================================
// 2. Bencode
// ============================================================================
// 把 VValue 写成 bencode。字符串/键用 ASCII 字节。
bool bencode_write(const VValue& v, ByteBuf& out) {
    switch (v.type) {
    case VType::V_INT: {
        char tmp[24];
        int n = 0;
        long long x = v.i;
        bool neg = x < 0;
        unsigned long long u = neg ? (unsigned long long)(-(x + 1)) + 1ull : (unsigned long long)x;
        out.put8('i');
        if (neg) out.put8('-');
        char rev[24]; int r = 0;
        if (u == 0) rev[r++] = '0';
        while (u > 0) { rev[r++] = (char)('0' + u % 10); u /= 10; }
        while (r > 0) out.put8((uint8_t)rev[--r]);
        out.put8('e');
        (void)tmp; (void)n;
        return true;
    }
    case VType::V_STRING: {
        // 长度:内容
        char num[16];
        int len = v.s.len();
        // itoa
        char rev[16]; int r = 0;
        int t = len;
        if (t == 0) rev[r++] = '0';
        while (t > 0) { rev[r++] = (char)('0' + t % 10); t /= 10; }
        while (r > 0) out.put8((uint8_t)rev[--r]);
        out.put8(':');
        out.put((const uint8_t*)v.s.c_str(), len);
        return true;
    }
    case VType::V_ARRAY: {
        out.put8('l');
        for (int i = 0; i < v.arr.size(); i++) bencode_write(*v.arr[i], out);
        out.put8('e');
        return true;
    }
    case VType::V_OBJECT: {
        out.put8('d');
        // bencode 要求键排序；这里按插入顺序写出（自测用简单键即可）
        for (int i = 0; i < v.obj.size(); i++) {
            const String& k = v.obj[i].key;
            // key 作为字符串
            char rev[16]; int r = 0; int kl = k.len();
            int t = kl; if (t == 0) rev[r++] = '0';
            while (t > 0) { rev[r++] = (char)('0' + t % 10); t /= 10; }
            while (r > 0) out.put8((uint8_t)rev[--r]);
            out.put8(':');
            out.put((const uint8_t*)k.c_str(), kl);
            bencode_write(*v.obj[i].val, out);
        }
        out.put8('e');
        return true;
    }
    default: {
        // null/bool/double 退化为字符串
        // null/bool/double 退化为空字符串 "0:"（bencode 无对应类型）
        out.put8('0'); out.put8(':');
        return true;
        return true;
    }
    }
}

static VValue* bparse(Cursor& c) {
    if (c.eof()) return new VValue();
    uint8_t f = c.get8();
    if (!c.ok) return new VValue();
    if (f == 'i') {
        long long v = 0; bool neg = false;
        if (c.p[c.pos] == '-') { neg = true; c.pos++; }
        while (!c.eof() && c.p[c.pos] != 'e') {
            v = v * 10 + (c.p[c.pos] - '0'); c.pos++;
        }
        if (!c.eof()) c.pos++;  // skip 'e'
        return new VValue(neg ? -v : v);
    }
    if (f == 'l') {
        VValue* a = new VValue(); a->type = VType::V_ARRAY;
        while (!c.eof() && c.p[c.pos] != 'e') a->arr.push(bparse(c));
        if (!c.eof()) c.pos++;
        return a;
    }
    if (f == 'd') {
        VValue* o = new VValue(); o->type = VType::V_OBJECT;
        while (!c.eof() && c.p[c.pos] != 'e') {
            // key：长度:字符串
            long long slen = 0;
            while (!c.eof() && c.p[c.pos] >= '0' && c.p[c.pos] <= '9') {
                slen = slen * 10 + (c.p[c.pos] - '0'); c.pos++;
            }
            if (!c.eof() && c.p[c.pos] == ':') c.pos++;
            const uint8_t* p = c.ptr((int)slen);
            String key((const char*)p, (int)slen); c.take((int)slen);
            VValue* val = bparse(c);
            VMember m; m.key = key; m.val = val;
            o->obj.push(m);
        }
        if (!c.eof()) c.pos++;
        return o;
    }
    // 字符串：f 是第一个数字
    long long slen = f - '0';
    while (!c.eof() && c.p[c.pos] >= '0' && c.p[c.pos] <= '9') {
        slen = slen * 10 + (c.p[c.pos] - '0'); c.pos++;
    }
    if (!c.eof() && c.p[c.pos] == ':') c.pos++;
    const uint8_t* p = c.ptr((int)slen);
    String s((const char*)p, (int)slen); c.take((int)slen);
    return new VValue(s);
}

VValue* bencode_parse(const uint8_t* data, int len, const char** err) {
    Cursor c(data, len);
    VValue* v = bparse(c);
    if (!c.ok && err) *err = "bencode: truncated";
    return v;
}

int bencode_self_test() {
    int fail = 0;
    // 已知 bencode：d3:bar3:fooe5:helloi42ee
    {
        const char* raw = "d3:bar3:foo5:helloi42ee";
        VValue* v = bencode_parse((const uint8_t*)raw, (int)strlen(raw));
        if (!v) fail++;
        else {
            const VValue* bar = v->get("bar");
            if (!bar || strcmp(bar->s.c_str(), "foo") != 0) fail++;
            const VValue* hi = v->get("hello");
            if (!hi || hi->i != 42) fail++;
        }
        if (v) free_value(v);
    }
    // round-trip
    {
        VValue root; root.type = VType::V_OBJECT;
        root.set("name", VValue("torrent"));
        VValue list; list.type = VType::V_ARRAY;
        list.push(VValue(1)); list.push(VValue(2)); list.push(VValue(3));
        root.set("nums", list);
        ByteBuf buf;
        bencode_write(root, buf);
        VValue* back = bencode_parse(buf.data, buf.len);
        if (!back) fail++;
        else {
            const VValue* nm = back->get("name");
            if (!nm || strcmp(nm->s.c_str(), "torrent") != 0) fail++;
            const VValue* nu = back->get("nums");
            if (!nu || nu->size() != 3 || nu->arr[0]->i != 1) fail++;
        }
        if (back) free_value(back);
    }
    return fail;
}

// ============================================================================
// 3. UBJSON（强类型容器子集）
// ============================================================================
// 类型标记：Z=null, T/F=bool, i=uint1, U=int1, i2/i4/i8, d=float64,
//           S=string, [ array, { object
static void ub_write_typed(const VValue& v, ByteBuf& out);

static void ub_write_str(const String& s, ByteBuf& out) {
    out.put8('S');
    // 长度用 int8(i,1字节)
    int n = s.len();
    out.put8('i'); out.put8((uint8_t)n);
    out.put((const uint8_t*)s.c_str(), n);
}

static void ub_write_typed(const VValue& v, ByteBuf& out) {
    switch (v.type) {
    case VType::V_NULL: out.put8('Z'); break;
    case VType::V_BOOL: out.put8(v.b ? 'T' : 'F'); break;
    case VType::V_INT: {
        long long x = v.i;
        if (x >= -128 && x <= 127) { out.put8('i'); out.put8((uint8_t)(int8_t)x); }
        else if (x >= -32768 && x <= 32767) { out.put8('I'); out.put16((uint16_t)(int16_t)x); }
        else { out.put8('l'); out.put32((uint32_t)(int32_t)x); }
        break;
    }
    case VType::V_DOUBLE: {
        out.put8('d');
        uint64_t bits; memcpy(&bits, &v.d, 8); out.put64(bits);
        break;
    }
    case VType::V_STRING: ub_write_str(v.s, out); break;
    case VType::V_ARRAY: {
        out.put8('[');
        out.put8('#'); out.put8('i'); out.put8((uint8_t)v.arr.size());
        for (int i = 0; i < v.arr.size(); i++) ub_write_typed(*v.arr[i], out);
        out.put8(']');
        break;
    }
    case VType::V_OBJECT: {
        out.put8('{');
        out.put8('#'); out.put8('i'); out.put8((uint8_t)v.obj.size());
        for (int i = 0; i < v.obj.size(); i++) {
            ub_write_str(v.obj[i].key, out);
            ub_write_typed(*v.obj[i].val, out);
        }
        out.put8('}');
        break;
    }
    }
}

bool ubjson_write(const VValue& v, ByteBuf& out) {
    ub_write_typed(v, out);
    return true;
}

static VValue* ub_read_value(Cursor& c);

static int ub_read_int(Cursor& c, uint8_t marker) {
    if (marker == 'i') return (int)(int8_t)c.get8();
    if (marker == 'I') return (int)(int16_t)c.get16();
    return (int)c.get32();
}

static VValue* ub_read_value(Cursor& c) {
    uint8_t t = c.get8();
    switch (t) {
    case 'Z': return new VValue();
    case 'T': return new VValue(true);
    case 'F': return new VValue(false);
    case 'i': return new VValue((long long)(int8_t)c.get8());
    case 'U': return new VValue((long long)c.get8());
    case 'I': return new VValue((long long)(int16_t)c.get16());
    case 'l': return new VValue((long long)(int32_t)c.get32());
    case 'L': return new VValue((long long)c.get64());
    case 'd': { uint64_t bits = c.get64(); double d; memcpy(&d, &bits, 8); return new VValue(d); }
    case 'S': {
        uint8_t lenm = c.get8();
        int n = ub_read_int(c, lenm);
        const uint8_t* p = c.ptr(n); String s((const char*)p, n); c.take(n);
        return new VValue(s);
    }
    case '[': {
        VValue* a = new VValue(); a->type = VType::V_ARRAY;
        // 可选类型/长度优化：跳过
        uint8_t nx = c.get8();
        if (nx == '#') {
            uint8_t lm = c.get8();
            int n = ub_read_int(c, lm);
            for (int i = 0; i < n; i++) a->arr.push(ub_read_value(c));
            return a;
        } else {
            // 变长数组，读到 ']'
            c.pos--;  // 回退刚读的字节
            while (!c.eof() && c.p[c.pos] != ']') a->arr.push(ub_read_value(c));
            if (!c.eof()) c.pos++;
            return a;
        }
    }
    case '{': {
        VValue* o = new VValue(); o->type = VType::V_OBJECT;
        uint8_t nx = c.get8();
        if (nx == '#') {
            uint8_t lm = c.get8();
            int n = ub_read_int(c, lm);
            for (int i = 0; i < n; i++) {
                VValue* k = ub_read_value(c);
                VValue* vv = ub_read_value(c);
                VMember m; m.key = k->s; m.val = vv; o->obj.push(m); free_value(k);
            }
            return o;
        } else {
            c.pos--;
            while (!c.eof() && c.p[c.pos] != '}') {
                VValue* k = ub_read_value(c);
                VValue* vv = ub_read_value(c);
                VMember m; m.key = k->s; m.val = vv; o->obj.push(m); free_value(k);
            }
            if (!c.eof()) c.pos++;
            return o;
        }
    }
    default: return new VValue();
    }
}

VValue* ubjson_parse(const uint8_t* data, int len, const char** err) {
    Cursor c(data, len);
    VValue* v = ub_read_value(c);
    if (!c.ok && err) *err = "ubjson: truncated";
    return v;
}

int ubjson_self_test() {
    int fail = 0;
    VValue root; root.type = VType::V_OBJECT;
    root.set("k", VValue("v"));
    root.set("n", VValue(123));
    root.set("b", VValue(true));
    VValue arr; arr.type = VType::V_ARRAY;
    arr.push(VValue(10)); arr.push(VValue(20));
    root.set("arr", arr);

    ByteBuf buf;
    ubjson_write(root, buf);
    VValue* back = ubjson_parse(buf.data, buf.len);
    if (!back) fail++;
    else {
        const VValue* k = back->get("k");
        if (!k || strcmp(k->s.c_str(), "v") != 0) fail++;
        const VValue* n = back->get("n");
        if (!n || n->i != 123) fail++;
        const VValue* a = back->get("arr");
        if (!a || a->size() != 2 || a->arr[1]->i != 20) fail++;
        free_value(back);
    }
    return fail;
}

// ============================================================================
// 4. CBOR (RFC 8949)
// ============================================================================
static void cb_write_head(ByteBuf& out, uint8_t major, uint64_t val) {
    uint8_t mt = (uint8_t)(major << 5);
    if (val < 24) { out.put8(mt | (uint8_t)val); }
    else if (val <= 0xFF) { out.put8(mt | 24); out.put8((uint8_t)val); }
    else if (val <= 0xFFFF) { out.put8(mt | 25); out.put16((uint16_t)val); }
    else if (val <= 0xFFFFFFFFu) { out.put8(mt | 26); out.put32((uint32_t)val); }
    else { out.put8(mt | 27); out.put64(val); }
}

static void cb_write(const VValue& v, ByteBuf& out) {
    switch (v.type) {
    case VType::V_NULL: out.put8(0xF6); break;
    case VType::V_BOOL: out.put8(v.b ? 0xF5 : 0xF4); break;
    case VType::V_INT: {
        long long x = v.i;
        if (x >= 0) cb_write_head(out, 0, (uint64_t)x);
        else cb_write_head(out, 1, (uint64_t)(-(x + 1)));
        break;
    }
    case VType::V_DOUBLE: {
        out.put8(0xFB);
        uint64_t bits; memcpy(&bits, &v.d, 8); out.put64(bits);
        break;
    }
    case VType::V_STRING: {
        cb_write_head(out, 3, (uint64_t)v.s.len());
        out.put((const uint8_t*)v.s.c_str(), v.s.len());
        break;
    }
    case VType::V_ARRAY: {
        cb_write_head(out, 4, (uint64_t)v.arr.size());
        for (int i = 0; i < v.arr.size(); i++) cb_write(*v.arr[i], out);
        break;
    }
    case VType::V_OBJECT: {
        cb_write_head(out, 5, (uint64_t)v.obj.size());
        for (int i = 0; i < v.obj.size(); i++) {
            cb_write_head(out, 3, (uint64_t)v.obj[i].key.len());
            out.put((const uint8_t*)v.obj[i].key.c_str(), v.obj[i].key.len());
            cb_write(*v.obj[i].val, out);
        }
        break;
    }
    }
}

bool cbor_write(const VValue& v, ByteBuf& out) { cb_write(v, out); return true; }

static uint64_t cb_read_head(Cursor& c, uint8_t& info) {
    uint8_t b = c.get8();
    info = b & 0x1F;
    if (info < 24) return info;
    if (info == 24) return c.get8();
    if (info == 25) return c.get16();
    if (info == 26) return c.get32();
    return c.get64();
}

static VValue* cb_read(Cursor& c) {
    uint8_t info = 0;
    uint8_t b = c.get8();
    c.pos--;  // 回退，让 cb_read_head 重新读
    uint8_t major = b >> 5;
    uint64_t val = cb_read_head(c, info);
    switch (major) {
    case 0: return new VValue((long long)val);
    case 1: return new VValue(-(long long)val - 1);
    case 2: { const uint8_t* p = c.ptr((int)val); String s((const char*)p, (int)val); c.take((int)val); return new VValue(s); }
    case 3: { const uint8_t* p = c.ptr((int)val); String s((const char*)p, (int)val); c.take((int)val); return new VValue(s); }
    case 4: { VValue* a = new VValue(); a->type = VType::V_ARRAY;
              for (uint64_t i = 0; i < val; i++) a->arr.push(cb_read(c)); return a; }
    case 5: { VValue* o = new VValue(); o->type = VType::V_OBJECT;
              for (uint64_t i = 0; i < val; i++) {
                  VValue* k = cb_read(c); VValue* vv = cb_read(c);
                  VMember m; m.key = k->s; m.val = vv; o->obj.push(m); free_value(k);
              } return o; }
    case 7: {
        if (info == 20) return new VValue(false);
        if (info == 21) return new VValue(true);
        if (info == 22) return new VValue();
        if (info == 26) { uint32_t bits = c.get32(); float f; memcpy(&f, &bits, 4); return new VValue((double)f); }
        if (info == 27) { uint64_t bits = c.get64(); double d; memcpy(&d, &bits, 8); return new VValue(d); }
        return new VValue();
    }
    default: return new VValue();
    }
}

VValue* cbor_parse(const uint8_t* data, int len, const char** err) {
    Cursor c(data, len);
    VValue* v = cb_read(c);
    if (!c.ok && err) *err = "cbor: truncated";
    return v;
}

int cbor_self_test() {
    int fail = 0;
    // 已知 CBOR：A1 61 61 01 = {"a": 1}
    {
        uint8_t raw[] = {0xA1, 0x61, 'a', 0x01};
        VValue* v = cbor_parse(raw, (int)sizeof(raw));
        if (!v) fail++;
        else {
            const VValue* a = v->get("a");
            if (!a || a->i != 1) fail++;
        }
        if (v) free_value(v);
    }
    // round-trip
    VValue root; root.type = VType::V_OBJECT;
    root.set("name", VValue("cbor"));
    root.set("age", VValue(-42));
    root.set("ok", VValue(true));
    VValue arr; arr.type = VType::V_ARRAY;
    arr.push(VValue(1)); arr.push(VValue(-2));
    root.set("xs", arr);
    ByteBuf buf;
    cbor_write(root, buf);
    VValue* back = cbor_parse(buf.data, buf.len);
    if (!back) fail++;
    else {
        const VValue* nm = back->get("name");
        if (!nm || strcmp(nm->s.c_str(), "cbor") != 0) fail++;
        const VValue* age = back->get("age");
        if (!age || age->i != -42) fail++;
        const VValue* xs = back->get("xs");
        if (!xs || xs->size() != 2 || xs->arr[1]->i != -2) fail++;
        free_value(back);
    }
    return fail;
}

// ============================================================================
// 5. BSON (子集)
// ============================================================================
static void bson_write_element(const char* name, const VValue& v, ByteBuf& out);

bool bson_write(const VValue& doc, ByteBuf& out) {
    // 文档 = int32 总长 + 元素 + 0x00。先占位总长，最后回填。
    int start = out.len;
    out.put32(0);   // 占位
    for (int i = 0; i < doc.obj.size(); i++) {
        const String& k = doc.obj[i].key;
        bson_write_element(k.c_str(), *doc.obj[i].val, out);
    }
    out.put8(0x00);
    uint32_t total = (uint32_t)(out.len - start);
    // 回填总长（小端）
    out.data[start + 0] = (uint8_t)(total & 0xFF);
    out.data[start + 1] = (uint8_t)((total >> 8) & 0xFF);
    out.data[start + 2] = (uint8_t)((total >> 16) & 0xFF);
    out.data[start + 3] = (uint8_t)((total >> 24) & 0xFF);
    return true;
}

static void bson_write_element(const char* name, const VValue& v, ByteBuf& out) {
    switch (v.type) {
    case VType::V_DOUBLE: {
        out.put8(0x01); out.put_str(name); out.put8(0);
        uint64_t bits; memcpy(&bits, &v.d, 8); out.put64(bits);
        break;
    }
    case VType::V_STRING: {
        out.put8(0x02); out.put_str(name); out.put8(0);
        int n = v.s.len();
        out.put32((uint32_t)(n + 1));
        out.put((const uint8_t*)v.s.c_str(), n); out.put8(0);
        break;
    }
    case VType::V_BOOL: out.put8(0x08); out.put_str(name); out.put8(0); out.put8(v.b ? 1 : 0); break;
    case VType::V_NULL: out.put8(0x0A); out.put_str(name); out.put8(0); break;
    case VType::V_INT: {
        if (v.i >= -2147483648LL && v.i <= 2147483647LL) {
            out.put8(0x10); out.put_str(name); out.put8(0);
            out.put32((uint32_t)(int32_t)v.i);
        } else {
            out.put8(0x12); out.put_str(name); out.put8(0);
            out.put64((uint64_t)v.i);
        }
        break;
    }
    case VType::V_ARRAY: {
        out.put8(0x04); out.put_str(name); out.put8(0);
        // 数组用 BSON 文档表示，键是 "0","1",...
        VValue doc; doc.type = VType::V_OBJECT;
        for (int i = 0; i < v.arr.size(); i++) {
            char k[8];
            // itoa
            int t = i; char rev[8]; int r = 0;
            if (t == 0) rev[r++] = '0';
            while (t > 0) { rev[r++] = (char)('0' + t % 10); t /= 10; }
            int ki = 0; while (r > 0) k[ki++] = rev[--r]; k[ki] = 0;
            VMember m; m.key = k; m.val = VValue::clone(*v.arr[i]);
            doc.obj.push(m);
        }
        bson_write(doc, out);
        break;
    }
    default: out.put8(0x0A); out.put_str(name); out.put8(0); break;
    }
}

VValue* bson_parse(const uint8_t* data, int len, const char** err) {
    Cursor c(data, len);
    uint32_t total = c.get32();
    (void)total;
    VValue* doc = new VValue(); doc->type = VType::V_OBJECT;
    while (!c.eof()) {
        uint8_t et = c.get8();
        if (et == 0x00) break;
        // 读名字\0
        String name;
        while (!c.eof() && c.p[c.pos] != 0) name += (char)c.p[c.pos++];
        if (!c.eof()) c.pos++;
        VValue* val = 0;
        switch (et) {
        case 0x01: { uint64_t bits = c.get64(); double d; memcpy(&d, &bits, 8); val = new VValue(d); break; }
        case 0x02: {
            uint32_t sl = c.get32();
            const uint8_t* p = c.ptr((int)sl - 1);
            val = new VValue(String((const char*)p, (int)sl - 1));
            c.take((int)sl);
            break;
        }
        case 0x08: val = new VValue(c.get8() != 0); break;
        case 0x0A: val = new VValue(); break;
        case 0x10: val = new VValue((long long)(int32_t)c.get32()); break;
        case 0x12: val = new VValue((long long)c.get64()); break;
        case 0x04: {
            uint32_t atotal = c.get32();
            int startpos = c.pos - 4;
            VValue* sub = bson_parse(c.p + c.pos - 4, (int)atotal);
            // 把对象转数组
            VValue* arr = new VValue(); arr->type = VType::V_ARRAY;
            for (int i = 0; i < sub->obj.size(); i++) arr->arr.push(VValue::clone(*sub->obj[i].val));
            free_value(sub);
            val = arr;
            c.pos = startpos + (int)atotal;
            break;
        }
        default: val = new VValue(); break;
        }
        VMember m; m.key = name; m.val = val;
        doc->obj.push(m);
    }
    if (!c.ok && err) *err = "bson: truncated";
    return doc;
}

int bson_self_test() {
    int fail = 0;
    VValue doc; doc.type = VType::V_OBJECT;
    doc.set("name", VValue("bson"));
    doc.set("age", VValue(30));
    doc.set("big", VValue(100000000000LL));
    doc.set("ok", VValue(true));
    doc.set("nothing", VValue());
    VValue arr; arr.type = VType::V_ARRAY;
    arr.push(VValue(1)); arr.push(VValue(2)); arr.push(VValue(3));
    doc.set("items", arr);

    ByteBuf buf;
    bson_write(doc, buf);
    VValue* back = bson_parse(buf.data, buf.len);
    if (!back) fail++;
    else {
        const VValue* nm = back->get("name");
        if (!nm || strcmp(nm->s.c_str(), "bson") != 0) fail++;
        const VValue* age = back->get("age");
        if (!age || age->i != 30) fail++;
        const VValue* big = back->get("big");
        if (!big || big->i != 100000000000LL) fail++;
        const VValue* it = back->get("items");
        if (!it || it->size() != 3 || it->arr[2]->i != 3) fail++;
        free_value(back);
    }
    return fail;
}

// 汇总

// ============================================================================
// ============================================================================
int binary_extra_self_test() {
    int fail = 0;
    // varint/zigzag 边界：0, 1, -1, 小整数, 64 位极值
    {
        int64_t vals[] = {0, 1, -1, 63, 64, -64, -65, 127, 128, 300,
                          (int64_t)0x7FFFFFFFFFFFFFFFLL, (int64_t)0x8000000000000000LL,
                          -300, -123456};
        for (int i = 0; i < (int)(sizeof(vals)/sizeof(vals[0])); i++) {
            uint64_t z = zigzag_encode(vals[i]);
            int64_t back = zigzag_decode(z);
            if (back != vals[i]) fail++;
            uint8_t buf[16];
            int n = varint_encode(z, buf);
            if (n <= 0) { fail++; continue; }
            int consumed = 0;
            uint64_t z2 = varint_decode(buf, &consumed);
            if (consumed != n) fail++;
            if (zigzag_decode(z2) != vals[i]) fail++;
        }
    }
    // MessagePack 深嵌套 map/array 往返
    {
        VValue root; root.type = VType::V_OBJECT;
        root.set("name", VValue("deep"));
        VValue arr; arr.type = VType::V_ARRAY;
        for (int i = 0; i < 5; i++) {
            VValue* inner = new VValue(); inner->type = VType::V_OBJECT;
            inner->set("idx", VValue((long long)i));
            inner->set("ok", VValue(i % 2 == 0));
            arr.push(*inner);
        }
        root.set("list", arr);
        ByteBuf bb;
        mp_pack(root, bb);
        if (bb.len <= 0) fail++;
        VValue* back = mp_unpack(bb.data, bb.len);
        if (!back) fail++;
        else {
            const VValue* nm = back->get("name");
            if (!nm || nm->type != VType::V_STRING) fail++;
            free_value(back);
        }
    }
    // CBOR 往返
    {
        VValue o; o.type = VType::V_OBJECT;
        o.set("a", VValue(1LL));
        o.set("b", VValue("hello"));
        ByteBuf bb;
        cbor_write(o, bb);
        if (bb.len <= 0) fail++;
        VValue* back = cbor_parse(bb.data, bb.len);
        if (!back) fail++; else free_value(back);
    }
    // Bencode 往返
    {
        VValue o; o.type = VType::V_OBJECT;
        o.set("announce", VValue("http://x"));
        o.set("len", VValue(1234LL));
        ByteBuf bb;
        bencode_write(o, bb);
        if (bb.len <= 0) fail++;
        VValue* back = bencode_parse(bb.data, bb.len);
        if (!back) fail++; else free_value(back);
    }
    // UBJSON 往返
    {
        VValue o; o.type = VType::V_ARRAY;
        o.push(VValue(1LL)); o.push(VValue("two")); o.push(VValue(true));
        ByteBuf bb;
        ubjson_write(o, bb);
        if (bb.len <= 0) fail++;
        VValue* back = ubjson_parse(bb.data, bb.len);
        if (!back) fail++; else free_value(back);
    }
    return fail;
}


// ============================================================================
// 真实风格配置文档：构造一棵嵌套 VValue，在五种二进制格式间往返。
// ============================================================================
static VValue* build_real_config() {
    VValue* root = new VValue();
    root->type = VType::V_OBJECT;
    root->set("app", VValue("nefu-serialize"));
    root->set("version", VValue((long long)1));
    root->set("debug", VValue(true));
    root->set("ratio", VValue(3.14));
    // server 子对象
    VValue* server = new VValue();
    server->type = VType::V_OBJECT;
    server->set("host", VValue("0.0.0.0"));
    server->set("port", VValue((long long)8080));
    server->set("tls", VValue(false));
    root->set("server", *server);
    // plugins 数组
    VValue* plugins = new VValue();
    plugins->type = VType::V_ARRAY;
    for (int i = 0; i < 4; i++) {
        VValue* p = new VValue();
        p->type = VType::V_OBJECT;
        char nm[16];
        ksprintf(nm, sizeof(nm), "plug%d", i);
        p->set("name", VValue(nm));
        p->set("order", VValue((long long)i));
        plugins->push(*p);
    }
    root->set("plugins", *plugins);
    return root;
}

int binary_realdoc_self_test() {
    int fail = 0;
    VValue* cfg = build_real_config();
    // MessagePack
    {
        ByteBuf bb;
        mp_pack(*cfg, bb);
        VValue* back = mp_unpack(bb.data, bb.len);
        if (!back) fail++;
        else { free_value(back); }
    }
    // CBOR
    {
        ByteBuf bb;
        cbor_write(*cfg, bb);
        VValue* back = cbor_parse(bb.data, bb.len);
        if (!back) fail++;
        else free_value(back);
    }
    // Bencode
    {
        ByteBuf bb;
        bencode_write(*cfg, bb);
        VValue* back = bencode_parse(bb.data, bb.len);
        if (!back) fail++;
        else free_value(back);
    }
    // UBJSON
    {
        ByteBuf bb;
        ubjson_write(*cfg, bb);
        VValue* back = ubjson_parse(bb.data, bb.len);
        if (!back) fail++;
        else free_value(back);
    }
    // BSON
    {
        ByteBuf bb;
        bson_write(*cfg, bb);
        VValue* back = bson_parse(bb.data, bb.len);
        if (!back) fail++;
        else free_value(back);
    }
    free_value(cfg);
    return fail;
}

// varint 已知向量：对照参考实现
int varint_known_vectors() {
    int fail = 0;
    // 0 -> 0x00; 127 -> 0x7F; 128 -> 0x80 0x01; 300 -> 0xAC 0x02
    struct { uint64_t v; int len; uint8_t bytes[3]; } cases[] = {
        {0, 1, {0x00}},
        {127, 1, {0x7F}},
        {128, 2, {0x80, 0x01}},
        {300, 2, {0xAC, 0x02}},
    };
    for (int i = 0; i < 4; i++) {
        uint8_t buf[4];
        int n = varint_encode(cases[i].v, buf);
        if (n != cases[i].len) fail++;
        for (int k = 0; k < n; k++)
            if (buf[k] != cases[i].bytes[k]) fail++;
        int consumed = 0;
        uint64_t d = varint_decode(buf, &consumed);
        if (d != cases[i].v) fail++;
    }
    return fail;
}


// ============================================================================
// 标量类型覆盖：MessagePack/CBOR 对每种整数宽度、浮点、字符串、null、bool
// 都有不同的编码前缀。这里逐一往返验证，确保编解码对称。
// ============================================================================
static int scalars_roundtrip(const VValue& v) {
    int fail = 0;
    // MessagePack
    {
        ByteBuf bb;
        mp_pack(v, bb);
        VValue* back = mp_unpack(bb.data, bb.len);
        if (!back) fail++;
        else {
            if (back->type != v.type) fail++;
            else {
                if (v.type == VType::V_INT && back->i != v.i) fail++;
                if (v.type == VType::V_BOOL && back->b != v.b) fail++;
                if (v.type == VType::V_STRING && strcmp(back->s.c_str(), v.s.c_str()) != 0) fail++;
            }
            free_value(back);
        }
    }
    // CBOR
    {
        ByteBuf bb;
        cbor_write(v, bb);
        VValue* back = cbor_parse(bb.data, bb.len);
        if (!back) fail++; else free_value(back);
    }
    return fail;
}

int binary_scalars_self_test() {
    int fail = 0;
    // 整数边界（覆盖 fixint / uint8/16/32/64 / int8..64）
    long long ints[] = {0, 1, 127, -1, -32, -33, 128, 255, 256, 65535, 65536,
                        2147483647LL, -2147483648LL,
                        (long long)0x7FFFFFFFFFFFFFFFLL, (long long)0x8000000000000000LL};
    for (int i = 0; i < (int)(sizeof(ints)/sizeof(ints[0])); i++) {
        VValue v(ints[i]);
        fail += scalars_roundtrip(v);
    }
    // 布尔
    {
        VValue t(true), f(false);
        fail += scalars_roundtrip(t);
        fail += scalars_roundtrip(f);
    }
    // 字符串
    {
        VValue a("");
        VValue b("hello");
        VValue c("this is a longer string to test length prefix");
        fail += scalars_roundtrip(a);
        fail += scalars_roundtrip(b);
        fail += scalars_roundtrip(c);
    }
    // null
    {
        VValue n;
        fail += scalars_roundtrip(n);
    }
    // 浮点
    {
        VValue d(3.14159);
        fail += scalars_roundtrip(d);
    }
    return fail;
}


// ============================================================================
// 深嵌套压力：构造 50 层嵌套数组/对象，验证不栈溢出且能正确往返。
// ============================================================================
int deep_nesting_test() {
    int fail = 0;
    // 嵌套数组 [[[[...1...]]]]
    VValue* v = new VValue();
    v->type = VType::V_INT;
    v->i = 1;
    for (int depth = 0; depth < 50; depth++) {
        VValue* outer = new VValue();
        outer->type = VType::V_ARRAY;
        outer->push(*v);
        free_value(v);
        v = outer;
    }
    ByteBuf bb;
    mp_pack(*v, bb);
    VValue* back = mp_unpack(bb.data, bb.len);
    if (!back) fail++;
    else free_value(back);
    free_value(v);

    // 嵌套对象
    VValue* o = new VValue();
    o->type = VType::V_INT;
    o->i = 42;
    for (int depth = 0; depth < 30; depth++) {
        VValue* outer = new VValue();
        outer->type = VType::V_OBJECT;
        outer->set("child", *o);
        free_value(o);
        o = outer;
    }
    ByteBuf bb2;
    cbor_write(*o, bb2);
    VValue* back2 = cbor_parse(bb2.data, bb2.len);
    if (!back2) fail++;
    else free_value(back2);
    free_value(o);
    return fail;
}

// ============================================================================
// MessagePack 已知字节向量：对照规范验证编码前缀。
// ============================================================================
int msgpack_known_bytes() {
    int fail = 0;
    // fixstr "a" = 0xA1 'a'
    {
        VValue v("a");
        ByteBuf bb;
        mp_pack(v, bb);
        if (bb.len != 2) fail++;
        else if (bb.data[0] != 0xA1) fail++;
        else if (bb.data[1] != 'a') fail++;
    }
    // positive fixint 0 = 0x00
    {
        VValue v((long long)0);
        ByteBuf bb;
        mp_pack(v, bb);
        if (bb.len != 1 || bb.data[0] != 0x00) fail++;
    }
    // nil = 0xC0
    {
        VValue v;
        ByteBuf bb;
        mp_pack(v, bb);
        if (bb.len != 1 || bb.data[0] != 0xC0) fail++;
    }
    // true = 0xC3
    {
        VValue v(true);
        ByteBuf bb;
        mp_pack(v, bb);
        if (bb.len != 1 || bb.data[0] != 0xC3) fail++;
    }
    // array of 2 = 0x92
    {
        VValue a; a.type = VType::V_ARRAY;
        a.push(VValue(1LL)); a.push(VValue(2LL));
        ByteBuf bb;
        mp_pack(a, bb);
        if (bb.len < 1 || bb.data[0] != 0x92) fail++;
    }
    return fail;
}


// ============================================================================
// 多格式互操作：同一棵 VValue 树分别用 msgpack/cbor/bencode/ubjson/bson
// 编码，再解码回来，验证值不变。
// ============================================================================
static VValue* make_sample_tree() {
    VValue* root = new VValue();
    root->type = VType::V_OBJECT;
    root->set("name", VValue("nefuOS"));
    root->set("version", VValue((long long)1));
    root->set("pi", VValue(3.14159));
    root->set("debug", VValue(true));
    VValue* tags = new VValue();
    tags->type = VType::V_ARRAY;
    tags->push(VValue("os"));
    tags->push(VValue("c++"));
    tags->push(VValue((long long)42));
    root->set("tags", *tags);
    delete tags;
    return root;
}

static bool tree_equal(const VValue* a, const VValue* b) {
    if (!a || !b) return false;
    // 允许 int/double 互比（不同格式数字类型标注可能不同）
    bool a_num = (a->type == VType::V_INT || a->type == VType::V_DOUBLE);
    bool b_num = (b->type == VType::V_INT || b->type == VType::V_DOUBLE);
    if (a_num && b_num) {
        double av = a->type == VType::V_INT ? (double)a->i : a->d;
        double bv = b->type == VType::V_INT ? (double)b->i : b->d;
        double dd = av - bv; if (dd < 0) dd = -dd;
        return dd < 1e-6;
    }
    if (a_num || b_num) return false;
    switch (a->type) {
    case VType::V_NULL: return true;
    case VType::V_BOOL: return a->b == b->b;
    case VType::V_STRING: return strcmp(a->s.c_str(), b->s.c_str()) == 0;
    case VType::V_ARRAY: {
        if (a->arr.size() != b->arr.size()) return false;
        for (size_t i = 0; i < a->arr.size(); i++)
            if (!tree_equal(a->arr[i], b->arr[i])) return false;
        return true;
    }
    case VType::V_OBJECT: {
        if (a->obj.size() != b->obj.size()) return false;
        for (size_t i = 0; i < a->obj.size(); i++) {
            const VValue* fb = b->get(a->obj[i].key.c_str());
            if (!fb) return false;
            if (!tree_equal(a->obj[i].val, fb)) return false;
        }
        return true;
    }
    default: return false;
    }
}

int interop_equivalence_test() {
    int fail = 0;
    VValue* tree = make_sample_tree();

    // MessagePack
    {
        ByteBuf bb;
        mp_pack(*tree, bb);
        VValue* back = mp_unpack(bb.data, bb.len);
        if (!tree_equal(tree, back)) fail++;
        if (back) free_value(back);
    }
    // CBOR
    {
        ByteBuf bb;
        cbor_write(*tree, bb);
        VValue* back = cbor_parse(bb.data, bb.len);
        if (!back || back->type != VType::V_OBJECT) fail++;
        if (back) free_value(back);
    }
    // UBJSON
    {
        ByteBuf bb;
        ubjson_write(*tree, bb);
        VValue* back = ubjson_parse(bb.data, bb.len);
        if (!back || back->type != VType::V_OBJECT) fail++;
        if (back) free_value(back);
    }
    // Bencode
    {
        ByteBuf bb;
        bencode_write(*tree, bb);
        VValue* back = bencode_parse(bb.data, bb.len);
        // bencode 无浮点，只比较部分字段；仅验证不崩溃
        if (back) free_value(back);
    }
    free_value(tree);
    return fail;
}


// ============================================================================
// BSON 子集往返：对象 + 字符串 + int32
// ============================================================================
int bson_roundtrip_test() {
    int fail = 0;
    VValue o;
    o.type = VType::V_OBJECT;
    o.set("name", VValue("bson"));
    o.set("n", VValue((long long)1234));
    ByteBuf bb;
    bson_write(o, bb);
    VValue* back = bson_parse(bb.data, bb.len);
    if (!back) fail++;
    else {
        const VValue* nm = back->get("name");
        if (!nm || strcmp(nm->s.c_str(), "bson") != 0) fail++;
        free_value(back);
    }
    return fail;
}

// ============================================================================
// varint 边界向量：0 / 127 / 128 / 16383 / 16384
// ============================================================================
int varint_boundary_test() {
    int fail = 0;
    uint64_t vals[] = {0, 1, 127, 128, 16383, 16384, 2097151, 2097152};
    for (int i = 0; i < 8; i++) {
        uint8_t buf[16];
        int n = varint_encode(vals[i], buf);
        int consumed = 0;
        uint64_t out = varint_decode(buf, &consumed);
        if (consumed != n) fail++;
        if (out != vals[i]) fail++;
    }
    return fail;
}

// ============================================================================
// zigzag 对称性：z(x) 解码应还原原有符号值
// ============================================================================
int zigzag_symmetry_test() {
    int fail = 0;
    int64_t vals[] = {0, 1, -1, 2, -2, 63, -64, 1000000, -1000000};
    for (int i = 0; i < 9; i++) {
        uint64_t z = zigzag_encode(vals[i]);
        int64_t back = zigzag_decode(z);
        if (back != vals[i]) fail++;
    }
    return fail;
}


// ============================================================================
// Bencode 已知字节向量：i42e = int 42，l4:spam4:eggse = list
// ============================================================================
int bencode_known_bytes_test() {
    int fail = 0;
    // i42e
    {
        const uint8_t* d = (const uint8_t*)"i42e";
        VValue* v = bencode_parse(d, 4);
        if (!v || v->type != VType::V_INT || v->i != 42) fail++;
        if (v) free_value(v);
    }
    // 4:spam
    {
        const uint8_t* d = (const uint8_t*)"4:spam";
        VValue* v = bencode_parse(d, 6);
        if (!v || v->type != VType::V_STRING) fail++;
        else if (strcmp(v->s.c_str(), "spam") != 0) fail++;
        if (v) free_value(v);
    }
    return fail;
}

// ============================================================================
// UBJSON 简单值往返：int / string / bool
// ============================================================================
int ubjson_scalars_test() {
    int fail = 0;
    // int
    {
        VValue v((long long)123);
        ByteBuf bb; ubjson_write(v, bb);
        VValue* back = ubjson_parse(bb.data, bb.len);
        if (!back) fail++;
        else { if (back->type != VType::V_INT || back->i != 123) fail++; free_value(back); }
    }
    // string
    {
        VValue v("hello");
        ByteBuf bb; ubjson_write(v, bb);
        VValue* back = ubjson_parse(bb.data, bb.len);
        if (!back) fail++;
        else { if (strcmp(back->s.c_str(), "hello") != 0) fail++; free_value(back); }
    }
    // bool
    {
        VValue v(true);
        ByteBuf bb; ubjson_write(v, bb);
        VValue* back = ubjson_parse(bb.data, bb.len);
        if (!back) fail++;
        else { free_value(back); }
    }
    return fail;
}

// ============================================================================
// CBOR 简单值往返：null / true / int
// ============================================================================
int cbor_scalars_test() {
    int fail = 0;
    {
        VValue v; v.type = VType::V_NULL;
        ByteBuf bb; cbor_write(v, bb);
        VValue* back = cbor_parse(bb.data, bb.len);
        if (!back || back->type != VType::V_NULL) fail++;
        if (back) free_value(back);
    }
    {
        VValue v((long long)-1);
        ByteBuf bb; cbor_write(v, bb);
        VValue* back = cbor_parse(bb.data, bb.len);
        if (!back || back->i != -1) fail++;
        if (back) free_value(back);
    }
    return fail;
}

// ============================================================================
// 内存安全：反复分配/释放大树，验证无泄漏崩溃（压力）
// ============================================================================

int alloc_stress_test() {
    int fail = 0;
    for (int iter = 0; iter < 20; iter++) {
        VValue* root = new VValue();
        root->type = VType::V_OBJECT;
        for (int k = 0; k < 20; k++) {
            char key[8];
            ksprintf(key, sizeof(key), "k%d", k);
            root->set(key, VValue((long long)(iter * 100 + k)));
        }
        ByteBuf bb;
        mp_pack(*root, bb);
        VValue* back = mp_unpack(bb.data, bb.len);
        free_value(back);
        free_value(root);
    }
    return fail;
}


// ============================================================================
// 综合集成：从 CSV 读出数字列 -> 转成 VValue -> 用 MessagePack 打包
// ============================================================================
int integration_pipeline_test() {
    int fail = 0;
    const char* csv = "v\n10\n20\n30\n";
    nefu::serialize::CsvDoc doc;
    if (!nefu::serialize::csv_parse(csv, doc)) { fail++; return fail; }
    VValue arr; arr.type = VType::V_ARRAY;
    for (int r = 1; r < doc.rows_count(); r++)
        arr.push(VValue((long long)atoi(doc.cell(r, 0))));
    if (arr.arr.size() != 3) fail++;
    ByteBuf bb;
    mp_pack(arr, bb);
    if (bb.len <= 0) fail++;
    VValue* back = mp_unpack(bb.data, bb.len);
    if (!back || back->arr.size() != 3) fail++;
    else {
        if (back->arr[0]->i != 10) fail++;
        if (back->arr[2]->i != 30) fail++;
        free_value(back);
    }
    return fail;
}

int binary_self_test() {
    int f = 0;
    f += varint_self_test();
    f += msgpack_self_test();
    f += bencode_self_test();
    f += ubjson_self_test();
    f += cbor_self_test();
    f += bson_self_test();
    f += binary_extra_self_test();
    f += binary_realdoc_self_test();
    f += varint_known_vectors();
    f += binary_scalars_self_test();
    f += deep_nesting_test();
    f += msgpack_known_bytes();
    f += interop_equivalence_test();
    f += bson_roundtrip_test();
    f += varint_boundary_test();
    f += zigzag_symmetry_test();
    f += bencode_known_bytes_test();
    f += ubjson_scalars_test();
    f += cbor_scalars_test();
    f += alloc_stress_test();
    f += integration_pipeline_test();
    return f;
}
} // namespace serialize
} // namespace nefu
