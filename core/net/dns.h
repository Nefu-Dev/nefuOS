// nefuOS DNS — RFC 1035 message codec (parse + build), no sockets needed.
// Handles header, name compression, question/answer parsing for A, AAAA,
// CNAME, PTR, NS, MX, TXT, SOA records, and query packet building.
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace dns {

// resource record types
enum {
    T_A    = 1,
    T_NS   = 2,
    T_CNAME= 5,
    T_SOA  = 6,
    T_PTR  = 12,
    T_MX   = 15,
    T_TXT  = 16,
    T_AAAA = 28
};

// classes
enum { C_IN = 1 };

struct Header {
    uint16_t id;
    uint16_t flags;
    uint16_t qd, an, ns, ar;
    Header() : id(0), flags(0), qd(0), an(0), ns(0), ar(0) {}
};

struct Question {
    String name;
    uint16_t type;
    uint16_t qclass;
};

struct Record {
    String name;
    uint16_t type;
    uint16_t rclass;
    uint32_t ttl;
    String rdata;      // raw rdata bytes
    // decoded helpers
    bool rdata_a(uint8_t out[4]) const;
    bool rdata_aaaa(uint8_t out[16]) const;
    // domain-name rdata (CNAME/NS/PTR) — decoded from raw
    bool rdata_name(String& out) const;
    bool rdata_mx(uint16_t& pref, String& host) const;
    // TXT: join strings; SOA: decode to readable text
    bool rdata_txt(String& out) const;
    bool rdata_soa(String& out) const;
};

// ---- packet parse ----
// Parse a raw DNS packet. Fills header, questions and records.
// Returns true on success. Names are decompressed (jump-loop safe).
bool parse_packet(const uint8_t* data, int len, Header& h,
                  List<Question>& questions, List<Record>& records);

// ---- packet build ----
// Build a query packet for <name>/<type> into buf. Returns size (>0) or -1.
int build_query(uint16_t id, const char* name, uint16_t type,
                uint8_t* buf, int bufsize);

// Build a response packet given header fields and a question + answer records.
// Used by tests / future local DNS cache. Returns size or -1.
int build_response(const Header& h, const Question& q,
                   const List<Record>& answers,
                   uint8_t* buf, int bufsize);

// ---- helpers ----
// Compress a domain name into DNS wire format (with label length bytes).
// Returns bytes written or -1. Handles trailing dot.
int encode_name(const char* name, uint8_t* out, int outsize);
// Decode one name starting at <p> inside packet <data>, following pointers.
// Returns bytes consumed from p (excluding pointer jumps) or -1.
int decode_name(const uint8_t* data, int len, const uint8_t* p, String& out);

// printable flag word (for diagnostics)
const char* flag_str(uint16_t flags);

// ---- self test (no network) ----
bool self_test();

} // namespace dns
} // namespace nefu
