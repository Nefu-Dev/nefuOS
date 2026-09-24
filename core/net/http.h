// nefuOS HTTP Client Library
// Full-featured HTTP/1.1 client with GET, POST, headers, cookies, redirects
#pragma once

#include "../platform.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"

namespace nefu {
namespace net {

// ============================================
// HTTP Request
// ============================================

enum HttpMethod {
    HTTP_GET,
    HTTP_POST,
    HTTP_HEAD,
    HTTP_PUT,
    HTTP_DELETE
};

struct HttpRequest {
    HttpMethod method;
    char url[512];
    char host[256];
    char path[256];
    int port;
    char headers[16][128];  // up to 16 custom headers
    int header_count;
    char body[4096];
    int body_len;
    
    HttpRequest() : method(HTTP_GET), port(80), header_count(0), body_len(0) {
        url[0] = 0;
        host[0] = 0;
        path[0] = 0;
        body[0] = 0;
    }
    
    void add_header(const char* name, const char* value) {
        if (header_count >= 16) return;
        ksprintf(headers[header_count], 128, "%s: %s", name, value);
        header_count++;
    }
    
    void set_body(const char* data, int len) {
        if (len >= 4096) len = 4095;
        memcpy(body, data, len);
        body_len = len;
        body[len] = 0;
    }
};

// ============================================
// HTTP Response
// ============================================

struct HttpResponse {
    int status_code;
    char status_text[64];
    char headers[32][128];  // up to 32 response headers
    int header_count;
    char* body;
    int body_len;
    char content_type[128];
    int content_length;
    
    HttpResponse() : status_code(0), body(0), body_len(0), content_length(0) {
        status_text[0] = 0;
        content_type[0] = 0;
        header_count = 0;
    }
    
    ~HttpResponse() {
        if (body) kfree(body);
    }
    
    const char* get_header(const char* name) {
        for (int i = 0; i < header_count; i++) {
            if (strncasecmp(headers[i], name, strlen(name)) == 0) {
                return strchr(headers[i], ':') + 2;
            }
        }
        return "";
    }
    
    bool is_success() {
        return status_code >= 200 && status_code < 300;
    }
    
    bool is_redirect() {
        return status_code >= 300 && status_code < 400;
    }
    
    const char* get_redirect_url() {
        return get_header("Location");
    }
};

// ============================================
// URL Parser
// ============================================

struct Url {
    char protocol[16];
    char host[256];
    int port;
    char path[256];
    char query[256];
    
    Url() : port(80) {
        protocol[0] = 0;
        host[0] = 0;
        path[0] = 0;
        query[0] = 0;
    }
    
    bool parse(const char* url) {
        if (!url || !*url) return false;
        
        // Parse protocol
        const char* p = strstr(url, "://");
        if (p) {
            int proto_len = p - url;
            if (proto_len >= 16) proto_len = 15;
            memcpy(protocol, url, proto_len);
            protocol[proto_len] = 0;
            p += 3;
        } else {
            strcpy(protocol, "http");
            p = url;
        }
        
        // Parse host and port
        const char* slash = strchr(p, '/');
        int host_len = slash ? (slash - p) : strlen(p);
        if (host_len >= 256) host_len = 255;
        memcpy(host, p, host_len);
        host[host_len] = 0;
        
        // Check for port
        char* colon = strchr(host, ':');
        if (colon) {
            *colon = 0;
            port = atoi(colon + 1);
        } else if (strcmp(protocol, "https") == 0) {
            port = 443;
        } else {
            port = 80;
        }
        
        // Parse path and query
        if (slash) {
            const char* question = strchr(slash, '?');
            if (question) {
                int path_len = question - slash;
                if (path_len >= 256) path_len = 255;
                memcpy(path, slash, path_len);
                path[path_len] = 0;
                
                strncpy(query, question + 1, 255);
                query[255] = 0;
            } else {
                strncpy(path, slash, 255);
                path[255] = 0;
            }
        } else {
            strcpy(path, "/");
        }
        
        return true;
    }
};

// ============================================
// Cookie Jar
// ============================================

struct Cookie {
    char name[64];
    char value[128];
    char domain[128];
    char path[64];
    uint64_t expires;  // 0 = session cookie
};

struct CookieJar {
    Cookie cookies[64];
    int count;
    
    CookieJar() : count(0) {}
    
    void set(const char* name, const char* value, const char* domain, const char* path, uint64_t expires = 0) {
        if (count >= 64) return;
        strncpy(cookies[count].name, name, 63);
        strncpy(cookies[count].value, value, 127);
        strncpy(cookies[count].domain, domain, 127);
        strncpy(cookies[count].path, path, 63);
        cookies[count].expires = expires;
        count++;
    }
    
    const char* get(const char* name, const char* domain) {
        for (int i = 0; i < count; i++) {
            if (strcmp(cookies[i].name, name) == 0 && strstr(domain, cookies[i].domain)) {
                return cookies[i].value;
            }
        }
        return 0;
    }
    
    void clear() {
        count = 0;
    }
};

// ============================================
// HTTP Client
// ============================================

class HttpClient {
public:
    CookieJar cookie_jar;
    int max_redirects;
    int timeout_ms;
    bool follow_redirects;
    
    HttpClient() : max_redirects(5), timeout_ms(30000), follow_redirects(true) {}
    
    // Perform HTTP GET request
    HttpResponse* get(const char* url) {
        HttpRequest req;
        req.method = HTTP_GET;
        strncpy(req.url, url, 511);
        
        return execute(&req);
    }
    
    // Perform HTTP POST request
    HttpResponse* post(const char* url, const char* body, int body_len) {
        HttpRequest req;
        req.method = HTTP_POST;
        strncpy(req.url, url, 511);
        req.set_body(body, body_len);
        req.add_header("Content-Type", "application/x-www-form-urlencoded");
        
        return execute(&req);
    }
    
    // Download file to local path
    bool download(const char* url, const char* local_path) {
        HttpResponse* resp = get(url);
        if (!resp || !resp->is_success()) {
            if (resp) delete resp;
            return false;
        }
        
        FSNode* f = g_vfs->create(local_path, FS_REGULAR);
        if (!f) {
            delete resp;
            return false;
        }
        
        g_vfs->write_file(f, (const uint8_t*)resp->body, resp->body_len);
        
        delete resp;
        return true;
    }
    
private:
    HttpResponse* execute(HttpRequest* req) {
        // Parse URL
        Url url;
        if (!url.parse(req->url)) {
            return 0;
        }
        
        strncpy(req->host, url.host, 255);
        strncpy(req->path, url.path, 255);
        req->port = url.port;
        
        // Build request string
        char request[2048];
        int req_len = 0;
        
        // Request line
        const char* method_str = "GET";
        if (req->method == HTTP_POST) method_str = "POST";
        else if (req->method == HTTP_HEAD) method_str = "HEAD";
        
        req_len += ksprintf(request + req_len, sizeof(request) - req_len,
                           "%s %s HTTP/1.1\r\n", method_str, req->path);
        
        // Host header
        req_len += ksprintf(request + req_len, sizeof(request) - req_len,
                           "Host: %s\r\n", req->host);
        
        // User-Agent
        req_len += ksprintf(request + req_len, sizeof(request) - req_len,
                           "User-Agent: nefuOS/1.0\r\n");
        
        // Accept
        req_len += ksprintf(request + req_len, sizeof(request) - req_len,
                           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n");
        
        // Connection
        req_len += ksprintf(request + req_len, sizeof(request) - req_len,
                           "Connection: close\r\n");
        
        // Custom headers
        for (int i = 0; i < req->header_count; i++) {
            req_len += ksprintf(request + req_len, sizeof(request) - req_len,
                               "%s\r\n", req->headers[i]);
        }
        
        // Body (for POST)
        if (req->method == HTTP_POST && req->body_len > 0) {
            req_len += ksprintf(request + req_len, sizeof(request) - req_len,
                               "Content-Length: %d\r\n", req->body_len);
        }
        
        // End of headers
        req_len += ksprintf(request + req_len, sizeof(request) - req_len, "\r\n");
        
        // Body
        if (req->body_len > 0) {
            memcpy(request + req_len, req->body, req->body_len);
            req_len += req->body_len;
        }
        
        // Send request via platform HTTP
        uint8_t* response_data = 0;
        uint32_t response_len = 0;
        
        if (!platform_http_get(req->url, &response_data, &response_len)) {
            return 0;
        }
        
        // Parse response
        HttpResponse* resp = new HttpResponse();
        parse_response((char*)response_data, response_len, resp);
        
        kfree(response_data);
        
        // Handle redirects
        if (follow_redirects && resp->is_redirect() && max_redirects > 0) {
            const char* redirect_url = resp->get_redirect_url();
            if (redirect_url && *redirect_url) {
                delete resp;
                max_redirects--;
                return get(redirect_url);
            }
        }
        
        return resp;
    }
    
    void parse_response(char* data, int len, HttpResponse* resp) {
        // Find end of headers
        char* header_end = strstr(data, "\r\n\r\n");
        if (!header_end) return;
        
        *header_end = 0;
        
        // Parse status line
        char* line = data;
        char* nl = strstr(line, "\r\n");
        if (nl) *nl = 0;
        
        // HTTP/1.1 200 OK
        int code;
        char status[64];
        if (sscanf(line, "HTTP/%*d.%*d %d %63[^\r\n]", &code, status) >= 1) {
            resp->status_code = code;
            strncpy(resp->status_text, status, 63);
        }
        
        // Parse headers
        line = nl + 2;
        while (line && *line && resp->header_count < 32) {
            nl = strstr(line, "\r\n");
            if (nl) *nl = 0;
            
            if (*line) {
                strncpy(resp->headers[resp->header_count], line, 127);
                resp->header_count++;
            }
            
            if (!nl) break;
            line = nl + 2;
        }
        
        // Get content length
        const char* cl = resp->get_header("Content-Length");
        if (*cl) {
            resp->content_length = atoi(cl);
        }
        
        // Get content type
        strncpy(resp->content_type, resp->get_header("Content-Type"), 127);
        
        // Body
        char* body_start = header_end + 4;
        int body_len = len - (body_start - data);
        
        if (body_len > 0) {
            resp->body = (char*)kalloc(body_len + 1);
            memcpy(resp->body, body_start, body_len);
            resp->body[body_len] = 0;
            resp->body_len = body_len;
        }
    }
};

// ============================================
// Global HTTP client instance
// ============================================

static HttpClient g_http_client;

HttpClient* http_client() {
    return &g_http_client;
}

} // namespace net
} // namespace nefu
