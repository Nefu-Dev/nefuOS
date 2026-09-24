// nefuOS 网络协议栈 —— HTTP/1.1（RFC 7230/7231）
//
// 请求行：  METHOD SP PATH SP "HTTP/1.1" CRLF
// 状态行：  "HTTP/1.1" SP STATUS SP Reason CRLF
// 头部：    Name ": " Value CRLF，空行结束
// 实体：    可选 body；或 Transfer-Encoding: chunked 的分块编码。
//
// 本模块：请求/响应文本解析、头部键值管理、chunked 解码、报文构造。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../klib/klib.h"

namespace nefu {
namespace netproto {

struct HttpHeader {
    String name;
    String value;
};

struct HttpRequest {
    String method;        // GET / POST ...
    String path;         // /index.html
    String version;       // HTTP/1.1
    List<HttpHeader> headers;
    String body;

    void clear();
    String get_header(const char* name) const;          // 大小写不敏感
    void   set_header(const char* name, const char* value);
    bool   has_header(const char* name) const { return !get_header(name).empty(); }
};

struct HttpResponse {
    int    status;        // 200
    String reason;        // OK
    String version;       // HTTP/1.1
    List<HttpHeader> headers;
    String body;

    void clear();
    String get_header(const char* name) const;
    void   set_header(const char* name, const char* value);
};

// 解析请求报文（raw 不一定以 \0 结尾，len 给出长度）。成功 0。
int  http_parse_request(const char* raw, int len, HttpRequest& out);
// 解析响应报文。成功 0。
int  http_parse_response(const char* raw, int len, HttpResponse& out);

// 构造一个 HTTP/1.1 响应到 out。status=200/404...，body 可为空。
// 返回写入长度。自动写 Content-Length。
// 构造一个 HTTP/1.1 GET 请求到 out。host/path 如 "example.com" "/"。
int  http_build_request(char* out, int outsz, const char* method,
                        const char* host, const char* path);
int  http_build_response(char* out, int outsz, int status,
                         const char* body, int body_len,
                         const char* content_type);

// 构造请求报文到 out（追加式）。返回输出总长度。
int  http_build_request(String& out, const char* method, const char* path,
                        const char* host, const char* body, int body_len);

// 构造响应报文到 out。
int  http_build_response(String& out, int status, const char* reason,
                        const char* content_type, const char* body, int body_len);

// chunked 解码：把分块编码的 in 解码到 out。返回解码后长度；失败 -1。
// 支持 "SIZE\r\nDATA\r\n" 重复，直到 "0\r\n"，并吞掉尾部 CRLF。
int  http_decode_chunked(const char* in, int in_len, String& out);

// chunked 编码：把一段数据按 chunk_size 切成分块写入 out，结尾加 "0\r\n\r\n"。
// 返回输出总长度。
int  http_encode_chunked(const uint8_t* data, int len, int chunk_size, String& out);

// ===================== URL 解析 =====================
struct Url {
    String   scheme;     // http / https
    String   host;
    uint16_t port;       // 0 = 按 scheme 默认（http=80）
    String   path;       // 含前导 '/'
    String   query;      // '?' 之后的部分（不含 '?'）
    bool     valid;
    void clear();
};

// 解析 "http://host:port/path?query=1"。失败时 valid=false。
Url  url_parse(const char* url);
// 从 query 串 "a=1&b=2" 取 key 的值（URL 解码未做，按原文返回）。
String query_get(const String& query, const char* key);

// 状态码分类
bool http_is_success(int status);   // 200..299
bool http_is_redirect(int status);  // 301/302/307/308
bool http_is_client_error(int status);
bool http_is_server_error(int status);

// 状态码 -> 标准原因短语（"OK"/"Not Found"/...）。未知返回 "Unknown"。
const char* http_reason(int status);

// 把 unix 秒数格式化成 RFC 1123 日期头："Sun, 06 Nov 1994 08:49:37 GMT"。
// 仅用整数（无 FPU）。out 至少 30 字节。
char* http_date(uint32_t unix_sec, char* out, int outsz);

// HTML 转义：把 < > & " 转成 &lt; &gt; &amp; &quot;。写入 out。
int   http_html_escape(const char* in, char* out, int outsz);
// 从 Cookie 头取某个 name 的值（值拷到 out）。找到返回 true。
bool  http_get_cookie(const HttpRequest& r, const char* name, char* out, int outsz);

int  http_self_test();

} // namespace netproto
} // namespace nefu
