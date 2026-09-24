// nefuOS 网络协议栈 —— HTTP/1.1 实现
#include "http.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace netproto {

// ---- 工具：大小写不敏感比较 ----
static bool eq_nocase(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

void HttpRequest::clear() {
    method = ""; path = ""; version = "";
    headers.clear(); body = "";
}

void HttpResponse::clear() {
    status = 0; reason = ""; version = "";
    headers.clear(); body = "";
}

String HttpRequest::get_header(const char* name) const {
    for (int i = 0; i < headers.size(); i++)
        if (eq_nocase(headers[i].name.c_str(), name)) return headers[i].value;
    return String("");
}

String HttpResponse::get_header(const char* name) const {
    for (int i = 0; i < headers.size(); i++)
        if (eq_nocase(headers[i].name.c_str(), name)) return headers[i].value;
    return String("");
}

void HttpRequest::set_header(const char* name, const char* value) {
    for (int i = 0; i < headers.size(); i++) {
        if (eq_nocase(headers[i].name.c_str(), name)) {
            headers[i].value = value;
            return;
        }
    }
    HttpHeader h; h.name = name; h.value = value;
    headers.push(h);
}

void HttpResponse::set_header(const char* name, const char* value) {
    for (int i = 0; i < headers.size(); i++) {
        if (eq_nocase(headers[i].name.c_str(), name)) {
            headers[i].value = value;
            return;
        }
    }
    HttpHeader h; h.name = name; h.value = value;
    headers.push(h);
}

// 找到 "\r\n\r\n" 的位置（头部与实体分隔）。找不到返回 -1。
static int find_header_end(const char* raw, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (raw[i] == '\r' && raw[i+1] == '\n' &&
            raw[i+2] == '\r' && raw[i+3] == '\n') return i;
    }
    return -1;
}

// 解析一段头部文本（header_start..header_end 之间），填入 headers。
// header_end 指向最后一个头部行的 \r（即 \r\n\r\n 的第一个 \r）。
static void parse_header_lines(const char* raw, int start, int end, List<HttpHeader>& headers) {
    int i = start;
    while (i < end) {
        // 找行尾 \r（行内容在 i..line_end）
        int line_end = i;
        while (line_end < end && raw[line_end] != '\r') line_end++;
        int line_len = line_end - i;
        if (line_len > 0) {
            // 找 ':'
            int colon = -1;
            for (int k = i; k < line_end; k++) if (raw[k] == ':') { colon = k; break; }
            if (colon > i) {
                HttpHeader h;
                h.name = String(raw + i, colon - i);
                // 跳过 ':' 后的空格
                int vstart = colon + 1;
                while (vstart < line_end && (raw[vstart] == ' ' || raw[vstart] == '\t')) vstart++;
                h.value = String(raw + vstart, line_end - vstart);
                headers.push(h);
            }
        }
        i = line_end + 2;   // 跳过 \r\n（\n 可能在 end 之外，无妨）
    }
}

int http_parse_request(const char* raw, int len, HttpRequest& out) {
    if (!raw || len < 4) return -1;
    out.clear();
    int hend = find_header_end(raw, len);
    if (hend < 0) return -1;

    // 第一行：METHOD SP PATH SP VERSION
    int sp1 = -1, sp2 = -1;
    for (int i = 0; i < hend; i++) {
        if (raw[i] == ' ' && sp1 < 0) sp1 = i;
        else if (raw[i] == ' ' && sp1 >= 0 && sp2 < 0) { sp2 = i; break; }
        else if (raw[i] == '\r') break;
    }
    if (sp1 < 0 || sp2 < 0) return -1;
    out.method  = String(raw, sp1);
    out.path    = String(raw + sp1 + 1, sp2 - sp1 - 1);
    // version 到行尾 \r
    int ve = sp2 + 1;
    while (ve < hend && raw[ve] != '\r') ve++;
    out.version = String(raw + sp2 + 1, ve - sp2 - 1);

    parse_header_lines(raw, sp2 + 1, hend, out.headers);

    // body 在 \r\n\r\n 之后
    int body_start = hend + 4;
    if (body_start < len) out.body = String(raw + body_start, len - body_start);
    return 0;
}

int http_parse_response(const char* raw, int len, HttpResponse& out) {
    if (!raw || len < 4) return -1;
    out.clear();
    int hend = find_header_end(raw, len);
    if (hend < 0) return -1;

    // 状态行：VERSION SP STATUS SP REASON
    int sp1 = -1, sp2 = -1;
    for (int i = 0; i < hend; i++) {
        if (raw[i] == ' ' && sp1 < 0) sp1 = i;
        else if (raw[i] == ' ' && sp1 >= 0 && sp2 < 0) { sp2 = i; break; }
        else if (raw[i] == '\r') break;
    }
    if (sp1 < 0 || sp2 < 0) return -1;
    out.version = String(raw, sp1);
    // status 是整数
    int st = 0;
    for (int k = sp1 + 1; k < sp2; k++) if (raw[k] >= '0' && raw[k] <= '9')
        st = st * 10 + (raw[k] - '0');
    out.status = st;
    int re = sp2 + 1;
    while (re < hend && raw[re] != '\r') re++;
    out.reason = String(raw + sp2 + 1, re - sp2 - 1);

    parse_header_lines(raw, sp2 + 1, hend, out.headers);

    int body_start = hend + 4;
    if (body_start < len) out.body = String(raw + body_start, len - body_start);
    return 0;
}

int http_build_request(String& out, const char* method, const char* path,
                       const char* host, const char* body, int body_len) {
    out = "";
    out += method; out += ' '; out += path; out += " HTTP/1.1\r\n";
    out += "Host: "; out += host ? host : ""; out += "\r\n";
    out += "Accept: */*\r\n";
    out += "Connection: close\r\n";
    if (body && body_len > 0) {
        char hb[32];
        ksprintf(hb, sizeof(hb), "%d", body_len);
        out += "Content-Length: "; out += hb; out += "\r\n";
    }
    out += "\r\n";
    if (body && body_len > 0) out += String(body, body_len);
    return out.len();
}

int http_build_response(String& out, int status, const char* reason,
                        const char* content_type, const char* body, int body_len) {
    out = "";
    out += "HTTP/1.1 ";
    char sb[16]; ksprintf(sb, sizeof(sb), "%d", status);
    out += sb; out += ' '; out += reason ? reason : "OK"; out += "\r\n";
    out += "Content-Type: "; out += content_type ? content_type : "text/plain"; out += "\r\n";
    char hb[32]; ksprintf(hb, sizeof(hb), "%d", body_len);
    out += "Content-Length: "; out += hb; out += "\r\n";
    out += "Connection: close\r\n";
    out += "\r\n";
    if (body && body_len > 0) out += String(body, body_len);
    return out.len();
}

int http_decode_chunked(const char* in, int in_len, String& out) {
    out = "";
    if (!in || in_len <= 0) return -1;
    int i = 0;
    while (i < in_len) {
        // 读十六进制块大小
        int size = 0;
        int digits = 0;
        while (i < in_len) {
            char c = in[i];
            int v = -1;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else break;
            size = size * 16 + v;
            i++; digits++;
        }
        if (digits == 0) return -1;
        // 跳过分块扩展（;...）直到行尾
        while (i < in_len && in[i] != '\r') i++;
        if (i + 1 >= in_len) return -1;
        i += 2;   // 跳过 \r\n
        if (size == 0) {
            // 末尾：吞掉可能的 trailer 头直到空行
            // 简化处理：直接成功（调用方传入的 in 已到 0\r\n 为止）
            return out.len();
        }
        if (i + size > in_len) return -1;
        // 追加数据
        for (int k = 0; k < size; k++) out += in[i + k];
        i += size;
        // 跳过数据后的 \r\n
        if (i + 1 < in_len && in[i] == '\r' && in[i+1] == '\n') i += 2;
    }
    return out.len();
}

int http_encode_chunked(const uint8_t* data, int len, int chunk_size, String& out) {
    out = "";
    if (chunk_size <= 0) chunk_size = 1024;
    int off = 0;
    while (off < len) {
        int this_chunk = len - off;
        if (this_chunk > chunk_size) this_chunk = chunk_size;
        char hdr[16];
        int hn = ksprintf(hdr, sizeof(hdr), "%x\r\n", this_chunk);
        for (int i = 0; i < hn; i++) out += hdr[i];
        for (int i = 0; i < this_chunk; i++) out += (char)data[off + i];
        out += "\r\n";
        off += this_chunk;
    }
    out += "0\r\n\r\n";
    return out.len();
}

// ===================== URL 解析 =====================
void Url::clear() {
    scheme = ""; host = ""; path = ""; query = "";
    port = 0; valid = false;
}

Url url_parse(const char* url) {
    Url u; u.clear();
    if (!url) return u;
    // scheme://
    const char* p = url;
    const char* slash2 = strstr(p, "://");
    if (!slash2) return u;
    u.scheme = String(p, (int)(slash2 - p));
    p = slash2 + 3;
    // host:port/path
    const char* slash = p;
    while (*slash && *slash != '/' && *slash != '?') slash++;
    const char* hostend = slash;
    // 分离 host 与 port
    const char* colon = p;
    while (colon < hostend && *colon != ':') colon++;
    if (colon < hostend) {
        u.host = String(p, (int)(colon - p));
        uint16_t port = 0;
        colon++;
        while (colon < hostend && *colon >= '0' && *colon <= '9') {
            port = (uint16_t)(port * 10 + (*colon - '0'));
            colon++;
        }
        u.port = port;
    } else {
        u.host = String(p, (int)(hostend - p));
        u.port = (u.scheme == "https") ? 443 : 80;
    }
    // path + query
    if (*slash == '/') {
        const char* qmark = slash;
        while (*qmark && *qmark != '?') qmark++;
        u.path = String(slash, (int)(qmark - slash));
        if (*qmark == '?') u.query = String(qmark + 1);
    } else {
        u.path = "/";
        if (*slash == '?') u.query = String(slash + 1);
    }
    u.valid = !u.host.empty();
    return u;
}

String query_get(const String& query, const char* key) {
    String r;
    if (query.empty() || !key) return r;
    int klen = (int)strlen(key);
    int i = 0;
    while (i < query.len()) {
        // 找一段 key=value
        int seg_start = i;
        while (i < query.len() && query[i] != '&') i++;
        int seg_end = i;
        // 在这段里找 '='
        int eq = seg_start;
        while (eq < seg_end && query[eq] != '=') eq++;
        if (eq - seg_start == klen) {
            bool match = true;
            for (int k = 0; k < klen; k++)
                if (query[seg_start + k] != key[k]) { match = false; break; }
            if (match) return String(query.c_str() + eq + 1, seg_end - eq - 1);
        }
        i = seg_end + 1;   // 跳过 '&'
    }
    return r;
}

bool http_is_success(int status)   { return status >= 200 && status <= 299; }
bool http_is_redirect(int status)  { return status==301||status==302||status==307||status==308; }
bool http_is_client_error(int status) { return status >= 400 && status <= 499; }
bool http_is_server_error(int status) { return status >= 500 && status <= 599; }

int http_build_request(char* out, int outsz, const char* method,
                       const char* host, const char* path) {
    if (!out || outsz < 64) return -1;
    return snprintf(out, (size_t)outsz,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: nefuOS\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        method ? method : "GET",
        path ? path : "/",
        host ? host : "localhost");
}

int http_build_response(char* out, int outsz, int status,
                        const char* body, int body_len,
                        const char* content_type) {
    if (!out || outsz < 64) return -1;
    int n = snprintf(out, (size_t)outsz,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Server: nefuOS\r\n"
        "\r\n",
        status, http_reason(status),
        content_type ? content_type : "text/plain",
        body_len);
    if (n < 0 || n >= outsz) return -1;
    if (body && body_len > 0) {
        for (int i = 0; i < body_len && n < outsz - 1; i++)
            out[n++] = body[i];
    }
    out[n] = 0;
    return n;
}

const char* http_reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 418: return "I'm a teapot";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default:  return "Unknown";
    }
}

char* http_date(uint32_t unix_sec, char* out, int outsz) {
    // 简化：按 1970-01-01 起算，整数推算年月日时分秒
    uint32_t days = unix_sec / 86400u;
    uint32_t rem  = unix_sec % 86400u;
    int hh = (int)(rem / 3600u);
    int mm = (int)((rem % 3600u) / 60u);
    int ss = (int)(rem % 60u);
    // 从 1970-01-01（周四）累加天数到年/月
    uint32_t y = 1970;
    for (;;) {
        uint32_t year_days = ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366u : 365u);
        if (days < year_days) break;
        days -= year_days;
        y++;
    }
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t dim = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29u : 28u;
    uint32_t month = 1; uint32_t dom = days;
    uint32_t mlen = mdays[0];
    for (uint32_t m = 0; m < 12; m++) {
        uint32_t L = (m == 1) ? dim : mdays[m];
        if (dom < L) { month = m + 1; break; }
        dom -= L;
    }
    // 星期：1970-01-01 是周四（index 4=Thu, 从周日=0）
    const char* wd[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
    const char* mo[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec" };
    int wday = (int)((unix_sec / 86400u + 4u) % 7u);
    snprintf(out, (size_t)outsz, "%s, %02u %s %u %02d:%02d:%02d GMT",
             wd[wday], dom + 1, mo[month - 1], y, hh, mm, ss);
    return out;
}

int http_html_escape(const char* in, char* out, int outsz) {
    if (!out || outsz < 1) return 0;
    int o = 0;
    while (*in && o + 6 < outsz) {
        const char* ent = 0;
        switch (*in) {
        case '<': ent = "&lt;"; break;
        case '>': ent = "&gt;"; break;
        case '&': ent = "&amp;"; break;
        case '"': ent = "&quot;"; break;
        default:  out[o++] = *in; break;
        }
        if (ent) {
            while (*ent && o < outsz - 1) out[o++] = *ent++;
        }
        in++;
    }
    out[o] = 0;
    return o;
}

bool http_get_cookie(const HttpRequest& r, const char* name, char* out, int outsz) {
    if (!out || outsz < 1) return false;
    out[0] = 0;
    String c = r.get_header("Cookie");
    if (strlen(c.c_str()) == 0 || !name) return false;
    // 形如 "a=1; session=xyz; b=2"
    const char* s = c.c_str();
    int nlen = (int)strlen(name);
    while (*s) {
        // 跳过空格/分号
        while (*s == ' ' || *s == ';' || *s == '\t') s++;
        // 匹配 name=
        if (strncmp(s, name, nlen) == 0 && s[nlen] == '=') {
            s += nlen + 1;
            int o = 0;
            while (*s && *s != ';' && o < outsz - 1) out[o++] = *s++;
            out[o] = 0;
            return true;
        }
        // 跳到下一个分号
        while (*s && *s != ';') s++;
    }
    return false;
}

// ===================== 自测 =====================
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) { g_fails++; printf("  [http] FAIL: %s\n", what); }
}
} // namespace

int http_self_test() {
    g_fails = 0;

    // 1) 解析一个请求
    const char* req =
        "GET /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Accept: text/html\r\n"
        "\r\n";
    HttpRequest r;
    int n = http_parse_request(req, (int)strlen(req), r);
    expect("http req parse", n == 0);
    expect("http req method", r.method == "GET");
    expect("http req path", r.path == "/index.html");
    expect("http req version", r.version == "HTTP/1.1");
    expect("http req host", r.get_header("Host") == "example.com");
    expect("http req accept", r.get_header("Accept") == "text/html");
    // 大小写不敏感
    expect("http req case-insensitive", r.get_header("HOST") == "example.com");

    // 2) 解析一个响应
    const char* resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    HttpResponse s;
    n = http_parse_response(resp, (int)strlen(resp), s);
    expect("http resp parse", n == 0);
    expect("http resp status", s.status == 200);
    expect("http resp reason", s.reason == "OK");
    expect("http resp ct", s.get_header("Content-Type") == "text/plain");
    expect("http resp body", s.body == "hello");

    // 3) chunked 解码：RFC 例子
    const char* chunked = "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    String decoded;
    int dl = http_decode_chunked(chunked, (int)strlen(chunked), decoded);
    expect("http chunked len", dl == 9);
    expect("http chunked body", decoded == "Wikipedia");

    // 另一个：空块结尾
    const char* chunked2 = "3\r\nabc\r\n0\r\n\r\n";
    dl = http_decode_chunked(chunked2, (int)strlen(chunked2), decoded);
    expect("http chunked2", dl == 3 && decoded == "abc");

    // 4) 构造请求再解析回来
    String built;
    http_build_request(built, "POST", "/api", "api.example.com", "data=1", 6);
    HttpRequest r2;
    n = http_parse_request(built.c_str(), built.len(), r2);
    expect("http build roundtrip", n == 0 && r2.method == "POST" &&
           r2.path == "/api" && r2.get_header("Host") == "api.example.com" &&
           r2.body == "data=1");

    // 5) set_header 覆盖
    r.set_header("X-Test", "one");
    expect("http set hdr", r.get_header("X-Test") == "one");
    r.set_header("X-Test", "two");
    expect("http set hdr overwrite", r.get_header("X-Test") == "two");

    // 6) URL 解析
    Url u = url_parse("http://example.com:8080/path/page?id=42&x=abc");
    expect("url valid", u.valid);
    expect("url scheme", u.scheme == "http");
    expect("url host", u.host == "example.com");
    expect("url port", u.port == 8080);
    expect("url path", u.path == "/path/page");
    expect("url query", u.query == "id=42&x=abc");
    expect("query id", query_get(u.query, "id") == "42");
    expect("query x", query_get(u.query, "x") == "abc");
    expect("query missing", query_get(u.query, "nope").empty());
    // 默认端口
    Url u2 = url_parse("https://nefu.os/index");
    expect("url default port", u2.port == 443 && u2.path == "/index");

    // 7) 状态分类
    expect("status 200", http_is_success(200));
    expect("status 302", http_is_redirect(302));
    expect("status 404", http_is_client_error(404));
    expect("status 500", http_is_server_error(500));
    expect("reason 200", strcmp(http_reason(200), "OK") == 0);
    expect("reason 404", strcmp(http_reason(404), "Not Found") == 0);
    expect("reason 500", strcmp(http_reason(500), "Internal Server Error") == 0);

    // 构造并回解析
    char outbuf[256];
    int rlen = http_build_response(outbuf, sizeof(outbuf), 200, "hello", 5, "text/plain");
    expect("build resp", rlen > 0 && strstr(resp, "200 OK") != 0);
    HttpResponse rr;
    http_parse_response(outbuf, rlen, rr);
    expect("build parse", rr.status == 200 && strcmp(rr.body.c_str(), "hello") == 0);

    // 构造一个 GET 请求并回解析
    char reqbuf[256];
    int rq = http_build_request(reqbuf, sizeof(reqbuf), "GET", "example.com", "/index.html");
    expect("build req", rq > 0 && strstr(req, "Host: example.com") != 0);
    HttpRequest hr;
    http_parse_request(reqbuf, rq, hr);
    // Cookie 解析
    HttpRequest rc;
    http_parse_request("GET / HTTP/1.1\r\nHost: x\r\nCookie: a=1; session=xyz\r\n\r\n", 53, rc);
    char cv[32];
    expect("cookie get", http_get_cookie(rc, "session", cv, sizeof(cv)) && strcmp(cv, "xyz") == 0);
    expect("cookie miss", !http_get_cookie(rc, "nope", cv, sizeof(cv)));
    expect("build req parse", strcmp(hr.method.c_str(), "GET") == 0 &&
           strcmp(hr.path.c_str(), "/index.html") == 0);

    // 9) 日期与转义
    {
        char d[40];
        http_date(0, d, sizeof(d));   // 1970-01-01 00:00:00 UTC
        expect("http date epoch", strstr(d, "1970") != 0 && strstr(d, "GMT") != 0);
        char esc[64];
        http_html_escape("<a>&\"x\"</a>", esc, sizeof(esc));
        expect("html escape", strstr(esc, "&lt;a&gt;") != 0 && strstr(esc, "&amp;") != 0);
    }

    // 8) chunked 编码再解码应往返
    {
        const char* body = "Wikipedia in 9 bytes!";
        String enc;
        http_encode_chunked((const uint8_t*)body, (int)strlen(body), 8, enc);
        expect("chunked encode head", strstr(enc.c_str(), "8\r\nWikipedi") != 0);
        String dec;
        int dl2 = http_decode_chunked(enc.c_str(), enc.len(), dec);
        expect("chunked roundtrip", dl2 == (int)strlen(body) && dec == body);
    }

    return g_fails;
}

} // namespace netproto
} // namespace nefu
