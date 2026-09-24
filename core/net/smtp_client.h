// nefuOS SMTP/POP3 Client - Real Email Protocol
#pragma once

#include "../klib/klib.h"
#include "../stl/string.h"
#include "../stl/vector.h"

#ifdef NEFUOS_HOST
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace nefu {
namespace net {

// SMTP Client
class SMTPClient {
private:
    stl::string server;
    int port;
    stl::string username;
    stl::string password;
    bool connected;
    
#ifdef NEFUOS_HOST
    SOCKET sock;
#endif
    
public:
    SMTPClient() : port(587), connected(false) {
#ifdef NEFUOS_HOST
        sock = INVALID_SOCKET;
#endif
    }
    
    ~SMTPClient() {
        disconnect();
    }
    
    // Connect to SMTP server
    bool connect(const char* host, int smtp_port, const char* user, const char* pass) {
        server = host;
        port = smtp_port;
        username = user;
        password = pass;
        
#ifdef NEFUOS_HOST
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;
        
        struct hostent* he = gethostbyname(host);
        if (!he) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = *(unsigned long*)he->h_addr_list[0];
        
        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
        
        // Read greeting
        char buf[1024];
        recv(sock, buf, sizeof(buf) - 1, 0);
        
        // EHLO
        send_command("EHLO nefuos.local");
        
        // AUTH LOGIN
        send_command("AUTH LOGIN");
        send_command_base64(username.c_str());
        send_command_base64(password.c_str());
        
        connected = true;
        return true;
#else
        return false;
#endif
    }
    
    // Disconnect
    void disconnect() {
        if (connected) {
#ifdef NEFUOS_HOST
            send_command("QUIT");
            if (sock != INVALID_SOCKET) {
                closesocket(sock);
                sock = INVALID_SOCKET;
            }
            WSACleanup();
#endif
            connected = false;
        }
    }
    
    // Send email
    bool send_email(const char* to, const char* subject, const char* body) {
        if (!connected) return false;
        
#ifdef NEFUOS_HOST
        send_command(("MAIL FROM:" + username).c_str());
        send_command(("RCPT TO:" + stl::string(to)).c_str());
        send_command("DATA");
        
        // Send email content
        stl::string content = "From: " + username + "\r\n";
        content += "To: " + stl::string(to) + "\r\n";
        content += "Subject: " + stl::string(subject) + "\r\n";
        content += "MIME-Version: 1.0\r\n";
        content += "Content-Type: text/plain; charset=utf-8\r\n";
        content += "\r\n";
        content += body;
        content += "\r\n.\r\n";
        
        send(sock, content.c_str(), content.length(), 0);
        
        // Read response
        char buf[1024];
        recv(sock, buf, sizeof(buf) - 1, 0);
        
        return true;
#else
        return false;
#endif
    }
    
    bool is_connected() const { return connected; }
    
private:
    void send_command(const char* cmd) {
#ifdef NEFUOS_HOST
        stl::string line = cmd;
        line += "\r\n";
        send(sock, line.c_str(), line.length(), 0);
        
        char buf[1024];
        recv(sock, buf, sizeof(buf) - 1, 0);
#endif
    }
    
    void send_command_base64(const char* str) {
        // Simple base64 encoding
        stl::string b64;
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        int len = strlen(str);
        for (int i = 0; i < len; i += 3) {
            unsigned char c1 = str[i];
            unsigned char c2 = (i + 1 < len) ? str[i + 1] : 0;
            unsigned char c3 = (i + 2 < len) ? str[i + 2] : 0;
            
            b64 += chars[c1 >> 2];
            b64 += chars[((c1 & 0x03) << 4) | (c2 >> 4)];
            b64 += (i + 1 < len) ? chars[((c2 & 0x0F) << 2) | (c3 >> 6)] : '=';
            b64 += (i + 2 < len) ? chars[c3 & 0x3F] : '=';
        }
        
        send_command(b64.c_str());
    }
};

// POP3 Client
class POP3Client {
private:
    stl::string server;
    int port;
    stl::string username;
    stl::string password;
    bool connected;
    
#ifdef NEFUOS_HOST
    SOCKET sock;
#endif
    
public:
    POP3Client() : port(110), connected(false) {
#ifdef NEFUOS_HOST
        sock = INVALID_SOCKET;
#endif
    }
    
    ~POP3Client() {
        disconnect();
    }
    
    // Connect to POP3 server
    bool connect(const char* host, int pop_port, const char* user, const char* pass) {
        server = host;
        port = pop_port;
        username = user;
        password = pass;
        
#ifdef NEFUOS_HOST
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;
        
        struct hostent* he = gethostbyname(host);
        if (!he) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = *(unsigned long*)he->h_addr_list[0];
        
        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
        
        // Read greeting
        char buf[1024];
        recv(sock, buf, sizeof(buf) - 1, 0);
        
        // USER
        send_command(("USER " + username).c_str());
        
        // PASS
        send_command(("PASS " + password).c_str());
        
        connected = true;
        return true;
#else
        return false;
#endif
    }
    
    // Disconnect
    void disconnect() {
        if (connected) {
#ifdef NEFUOS_HOST
            send_command("QUIT");
            if (sock != INVALID_SOCKET) {
                closesocket(sock);
                sock = INVALID_SOCKET;
            }
            WSACleanup();
#endif
            connected = false;
        }
    }
    
    // Get message count
    int get_message_count() {
        if (!connected) return 0;
        
#ifdef NEFUOS_HOST
        send_command("STAT");
        char buf[1024];
        recv(sock, buf, sizeof(buf) - 1, 0);
        
        int count = 0;
        sscanf(buf, "+OK %d", &count);
        return count;
#else
        return 0;
#endif
    }
    
    // Get message list
    stl::vector<stl::string> list_messages() {
        stl::vector<stl::string> list;
        if (!connected) return list;
        
#ifdef NEFUOS_HOST
        send_command("LIST");
        char buf[4096];
        int total = recv(sock, buf, sizeof(buf) - 1, 0);
        buf[total] = 0;
        
        // Parse list
        char* line = strtok(buf, "\n");
        while (line) {
            if (strstr(line, "+OK") || strstr(line, ".")) break;
            list.push_back(stl::string(line));
            line = strtok(0, "\n");
        }
#endif
        
        return list;
    }
    
    // Retrieve message
    stl::string retrieve_message(int index) {
        if (!connected) return "";
        
#ifdef NEFUOS_HOST
        char cmd[64];
        ksprintf(cmd, sizeof(cmd), "RETR %d", index);
        send_command(cmd);
        
        char buf[8192];
        int total = recv(sock, buf, sizeof(buf) - 1, 0);
        buf[total] = 0;
        
        return stl::string(buf);
#else
        return "";
#endif
    }
    
    bool is_connected() const { return connected; }
    
private:
    void send_command(const char* cmd) {
#ifdef NEFUOS_HOST
        stl::string line = cmd;
        line += "\r\n";
        send(sock, line.c_str(), line.length(), 0);
        
        char buf[1024];
        recv(sock, buf, sizeof(buf) - 1, 0);
#endif
    }
};

// Global clients
SMTPClient g_smtp;
POP3Client g_pop3;

} // namespace net
} // namespace nefu
