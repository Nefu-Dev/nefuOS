// nefuOS DNS — RFC 1035 message codec implementation
#include "dns.h"
#include "../lib/hash.h"

namespace nefu {
namespace dns {

// ===================== name codec =====================

// encode "www.example.com" -> \3www\7example\3com\0
int encode_name(const char* name, uint8_t* out, int outsize) {
    if (!name || !out || outsize < 1) return -1;
    int o = 0;
    const char* p = name;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - p);
        if (len == 0) return -1;             // empty label
        if (len > 63) return -1;             // label too long
        if (o + len + 2 > outsize) return -1;
        out[o++] = (uint8_t)len;
        for (int i = 0; i < len; i++) out[o++] = (uint8_t)p[i];
        if (!*dot) break;
        p = dot + 1;
    }
    if (o + 1 > outsize) return -1;
    out[o++] = 0;
    return o;
}

// decode a name at p (which lies within data[0..len)); follows compression
// pointers with a jump budget. Returns bytes consumed at p (not counting
// jumps), or -1 on error.
int decode_name(const uint8_t* data, int len, const uint8_t* p, String& out) {
    out.clear();
    int consumed = 0;
    int jumps = 0;
    const uint8_t* q = p;
    bool jumped = false;
    for (;;) {
        if (q >= data + len) return -1;
        uint8_t b = *q;
        if (b == 0) {
            if (!jumped) consumed = (int)(q - p) + 1;
            break;
        }
        if ((b & 0xC0) == 0xC0) {
            if (q + 1 >= data + len) return -1;
            uint16_t off = (uint16_t)(((b & 0x3F) << 8) | q[1]);
            if (!jumped) consumed = (int)(q - p) + 2;
            if (off >= len) return -1;
            if (++jumps > 32) return -1;
            q = data + off;
            jumped = true;
            continue;
        }
        if ((b & 0xC0) != 0) return -1;      // reserved label type
        int l = b;
        if (q + 1 + l > data + len) return -1;
        q++;
        if (!out.empty()) out += '.';
        for (int i = 0; i < l; i++) out += (char)q[i];
        q += l;
        if (jumped && q == data + len) break;
    }
    if (consumed == 0 && !jumped) consumed = 1;
    return consumed;
}

// ===================== parse =====================

namespace {

bool rd_u16(const uint8_t* data, int len, int& off, uint16_t& v) {
    if (off + 2 > len) return false;
    v = (uint16_t)((data[off] << 8) | data[off + 1]);
    off += 2;
    return true;
}

bool rd_u32(const uint8_t* data, int len, int& off, uint32_t& v) {
    if (off + 4 > len) return false;
    v = ((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
        ((uint32_t)data[off + 2] << 8) | (uint32_t)data[off + 3];
    off += 4;
    return true;
}

bool parse_question(const uint8_t* data, int len, int& off, Question& q) {
    int c = decode_name(data, len, data + off, q.name);
    if (c <= 0) return false;
    off += c;
    return rd_u16(data, len, off, q.type) && rd_u16(data, len, off, q.qclass);
}

} // namespace

bool parse_packet(const uint8_t* data, int len, Header& h,
                  List<Question>& questions, List<Record>& records) {
    questions.clear();
    records.clear();
    if (!data || len < 12) return false;
    int off = 0;
    rd_u16(data, len, off, h.id);
    rd_u16(data, len, off, h.flags);
    rd_u16(data, len, off, h.qd);
    rd_u16(data, len, off, h.an);
    rd_u16(data, len, off, h.ns);
    rd_u16(data, len, off, h.ar);
    for (int i = 0; i < h.qd; i++) {
        Question q;
        if (!parse_question(data, len, off, q)) return false;
        questions.push(q);
    }
    int total = h.an + h.ns + h.ar;
    for (int i = 0; i < total; i++) {
        Record r;
        int c = decode_name(data, len, data + off, r.name);
        if (c <= 0) return false;
        off += c;
        if (!rd_u16(data, len, off, r.type)) return false;
        if (!rd_u16(data, len, off, r.rclass)) return false;
        if (!rd_u32(data, len, off, r.ttl)) return false;
        uint16_t rdlen;
        if (!rd_u16(data, len, off, rdlen)) return false;
        if (off + rdlen > len) return false;
        r.rdata.clear();
        for (int k = 0; k < rdlen; k++) r.rdata += (char)data[off + k];
        off += rdlen;
        records.push(r);
    }
    return true;
}

// ===================== build =====================

namespace {

bool wr_u16(uint8_t* buf, int bufsize, int& o, uint16_t v) {
    if (o + 2 > bufsize) return false;
    buf[o++] = (uint8_t)(v >> 8);
    buf[o++] = (uint8_t)(v & 0xFF);
    return true;
}

bool wr_u32(uint8_t* buf, int bufsize, int& o, uint32_t v) {
    if (o + 4 > bufsize) return false;
    buf[o++] = (uint8_t)(v >> 24);
    buf[o++] = (uint8_t)((v >> 16) & 0xFF);
    buf[o++] = (uint8_t)((v >> 8) & 0xFF);
    buf[o++] = (uint8_t)(v & 0xFF);
    return true;
}

} // namespace

int build_query(uint16_t id, const char* name, uint16_t type,
                uint8_t* buf, int bufsize) {
    if (!buf || bufsize < 12) return -1;
    int o = 0;
    wr_u16(buf, bufsize, o, id);
    wr_u16(buf, bufsize, o, 0x0100);              // RD=1
    wr_u16(buf, bufsize, o, 1);                   // qd
    wr_u16(buf, bufsize, o, 0);
    wr_u16(buf, bufsize, o, 0);
    wr_u16(buf, bufsize, o, 0);
    int n = encode_name(name, buf + o, bufsize - o);
    if (n <= 0) return -1;
    o += n;
    if (!wr_u16(buf, bufsize, o, type)) return -1;
    if (!wr_u16(buf, bufsize, o, C_IN)) return -1;
    return o;
}

int build_response(const Header& h, const Question& q,
                   const List<Record>& answers,
                   uint8_t* buf, int bufsize) {
    if (!buf || bufsize < 12) return -1;
    int o = 0;
    wr_u16(buf, bufsize, o, h.id);
    wr_u16(buf, bufsize, o, h.flags);
    wr_u16(buf, bufsize, o, 1);                    // qd
    wr_u16(buf, bufsize, o, (uint16_t)answers.size());
    wr_u16(buf, bufsize, o, 0);
    wr_u16(buf, bufsize, o, 0);
    // question echo
    int n = encode_name(q.name.c_str(), buf + o, bufsize - o);
    if (n <= 0) return -1;
    o += n;
    if (!wr_u16(buf, bufsize, o, q.type)) return -1;
    if (!wr_u16(buf, bufsize, o, q.qclass)) return -1;
    // answers
    for (int i = 0; i < answers.size(); i++) {
        const Record& r = answers[i];
        n = encode_name(r.name.c_str(), buf + o, bufsize - o);
        if (n <= 0) return -1;
        o += n;
        if (!wr_u16(buf, bufsize, o, r.type)) return -1;
        if (!wr_u16(buf, bufsize, o, r.rclass)) return -1;
        if (!wr_u32(buf, bufsize, o, r.ttl)) return -1;
        if (!wr_u16(buf, bufsize, o, (uint16_t)r.rdata.len())) return -1;
        if (o + r.rdata.len() > bufsize) return -1;
        for (int k = 0; k < r.rdata.len(); k++) buf[o++] = (uint8_t)r.rdata[k];
    }
    return o;
}

// ===================== rdata helpers =====================

bool Record::rdata_a(uint8_t out[4]) const {
    if (rdata.len() != 4 || type != T_A) return false;
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)rdata[i];
    return true;
}

bool Record::rdata_aaaa(uint8_t out[16]) const {
    if (rdata.len() != 16 || type != T_AAAA) return false;
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)rdata[i];
    return true;
}

bool Record::rdata_name(String& out) const {
    if (type != T_CNAME && type != T_NS && type != T_PTR) return false;
    // rdata holds a wire-format name (possibly compressed relative to the
    // packet — without the packet we can only decode uncompressed names)
    const uint8_t* d = (const uint8_t*)rdata.c_str();
    return decode_name(d, rdata.len(), d, out) > 0;
}

bool Record::rdata_mx(uint16_t& pref, String& host) const {
    if (type != T_MX || rdata.len() < 3) return false;
    pref = (uint16_t)(((uint8_t)rdata[0] << 8) | (uint8_t)rdata[1]);
    const uint8_t* d = (const uint8_t*)rdata.c_str() + 2;
    return decode_name(d, rdata.len() - 2, d, host) > 0;
}

bool Record::rdata_txt(String& out) const {
    if (type != T_TXT) return false;
    out.clear();
    int i = 0;
    while (i < rdata.len()) {
        int l = (uint8_t)rdata[i++];
        if (i + l > rdata.len()) return false;
        for (int k = 0; k < l; k++) out += rdata[i + k];
        i += l;
    }
    return true;
}

bool Record::rdata_soa(String& out) const {
    if (type != T_SOA) return false;
    const uint8_t* d = (const uint8_t*)rdata.c_str();
    int rdlen = rdata.len();
    String mname, rname;
    int c = decode_name(d, rdlen, d, mname);
    if (c <= 0) return false;
    int c2 = decode_name(d, rdlen, d + c, rname);
    if (c2 <= 0) return false;
    int off = c + c2;
    uint32_t serial = 0, refresh = 0, retry = 0, expire = 0, minimum = 0;
    if (!rd_u32(d, rdlen, off, serial)) return false;
    if (!rd_u32(d, rdlen, off, refresh)) return false;
    if (!rd_u32(d, rdlen, off, retry)) return false;
    if (!rd_u32(d, rdlen, off, expire)) return false;
    if (!rd_u32(d, rdlen, off, minimum)) return false;
    char tmp[160];
    ksprintf(tmp, sizeof(tmp),
             "mname=%s rname=%s serial=%u refresh=%u retry=%u expire=%u minimum=%u",
             mname.c_str(), rname.c_str(), serial, refresh, retry, expire, minimum);
    out = tmp;
    return true;
}

// ===================== misc =====================

const char* flag_str(uint16_t f) {
    static char tmp[64];
    int n = 0;
    if (f & 0x8000) tmp[n++] = 'Q';
    if (f & 0x0400) tmp[n++] = 'A';
    if (f & 0x0200) tmp[n++] = 'T';
    if (f & 0x0100) tmp[n++] = 'R';
    if (f & 0x0080) tmp[n++] = 'D';
    if (n == 0) tmp[n++] = '-';
    tmp[n] = 0;
    return tmp;
}

// ===================== self test =====================

bool self_test() {
    // 1. encode/decode round trip
    uint8_t wire[256];
    int n = encode_name("www.example.com", wire, sizeof(wire));
    if (n != 17) return false;
    // \3www\7example\3com\0 = 4+8+4+1 = 17
    if (wire[0] != 3 || wire[4] != 7) return false;
    String back;
    if (decode_name(wire, n, wire, back) <= 0) return false;
    if (back != "www.example.com") return false;

    // 2. build a query and parse it back
    uint8_t qbuf[128];
    int qn = build_query(0x1234, "nefuos.local", T_A, qbuf, sizeof(qbuf));
    if (qn <= 0) return false;
    Header h;
    List<Question> qs;
    List<Record> rs;
    if (!parse_packet(qbuf, qn, h, qs, rs)) return false;
    if (h.id != 0x1234 || h.qd != 1 || qs.size() != 1) return false;
    if (qs[0].name != "nefuos.local" || qs[0].type != T_A) return false;

    // 3. canned response: A record 93.184.216.34
    Record a;
    a.name = "www.example.com";
    a.type = T_A;
    a.rclass = C_IN;
    a.ttl = 300;
    a.rdata = String("\x5D\xB8\xD8\x22", 4);
    List<Record> ans;
    ans.push(a);
    Header rh;
    rh.id = 0x1234;
    rh.flags = 0x8180;
    Question qq;
    qq.name = "www.example.com";
    qq.type = T_A;
    qq.qclass = C_IN;
    uint8_t rbuf[256];
    int rn = build_response(rh, qq, ans, rbuf, sizeof(rbuf));
    if (rn <= 0) return false;
    List<Question> qs2;
    List<Record> rs2;
    Header h2;
    if (!parse_packet(rbuf, rn, h2, qs2, rs2)) return false;
    if (h2.an != 1 || rs2.size() != 1) return false;
    if (rs2[0].type != T_A) return false;
    uint8_t ip4[4];
    if (!rs2[0].rdata_a(ip4)) return false;
    if (ip4[0] != 93 || ip4[3] != 34) return false;

    // 4. CNAME + MX decode
    Record cn;
    cn.type = T_CNAME;
    cn.rclass = C_IN;
    cn.ttl = 60;
    cn.rdata = String("\x07""example\x03""com\x00", 13);
    String target;
    if (!cn.rdata_name(target)) return false;
    if (target != "example.com") return false;
    Record mx;
    mx.type = T_MX;
    mx.rclass = C_IN;
    mx.ttl = 60;
    mx.rdata = String("\x00\x0A""\x05""mx1\x07""example\x03""com\x00", 19);
    uint16_t pref = 0;
    String host;
    if (!mx.rdata_mx(pref, host)) return false;
    if (pref != 10 || host != "mx1.example.com") return false;

    return true;
}

} // namespace dns
} // namespace nefu
