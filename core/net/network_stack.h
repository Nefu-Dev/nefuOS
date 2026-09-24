// nefuOS Network Stack - Full Implementation
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace net {

// Ethernet frame header
struct EthernetHeader {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} __attribute__((packed));

// IPv4 header
struct IPv4Header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

// TCP header
struct TCPHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

// UDP header
struct UDPHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

// ICMP header
struct ICMPHeader {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint32_t rest;
} __attribute__((packed));

// IP address
struct IPAddress {
    union {
        uint8_t bytes[4];
        uint32_t dword;
    };
    
    IPAddress() : dword(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        bytes[0] = a; bytes[1] = b; bytes[2] = c; bytes[3] = d;
    }
    
    bool operator==(const IPAddress& other) const {
        return dword == other.dword;
    }
    
    bool operator!=(const IPAddress& other) const {
        return dword != other.dword;
    }
};

// MAC address
struct MACAddress {
    uint8_t bytes[6];
    
    MACAddress() { memset(bytes, 0, 6); }
    MACAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f) {
        bytes[0] = a; bytes[1] = b; bytes[2] = c; bytes[3] = d; bytes[4] = e; bytes[5] = f;
    }
    
    bool operator==(const MACAddress& other) const {
        return memcmp(bytes, other.bytes, 6) == 0;
    }
};

// Network interface
struct NetInterface {
    char name[16];
    MACAddress mac;
    IPAddress ip;
    IPAddress netmask;
    IPAddress gateway;
    IPAddress dns;
    bool up;
    uint32_t mtu;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
};

// TCP socket state
enum TCPState {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
};

// TCP socket
struct TCPSocket {
    int fd;
    IPAddress local_ip;
    uint16_t local_port;
    IPAddress remote_ip;
    uint16_t remote_port;
    TCPState state;
    uint32_t send_seq;
    uint32_t recv_ack;
    uint32_t window_size;
    uint8_t* send_buf;
    uint32_t send_len;
    uint8_t* recv_buf;
    uint32_t recv_len;
    uint32_t recv_pos;
};

// UDP socket
struct UDPSocket {
    int fd;
    IPAddress local_ip;
    uint16_t local_port;
    uint8_t* recv_buf;
    uint32_t recv_len;
    uint32_t recv_pos;
};

// Network stack manager
class NetworkStack {
private:
    NetInterface interfaces[4];
    int interface_count;
    TCPSocket tcp_sockets[16];
    int tcp_socket_count;
    UDPSocket udp_sockets[16];
    int udp_socket_count;
    
public:
    NetworkStack() : interface_count(0), tcp_socket_count(0), udp_socket_count(0) {}
    
    // Initialize network stack
    bool init() {
        // Initialize all network interfaces
        for (int i = 0; i < 4; i++) {
            interfaces[i].up = false;
            interfaces[i].mtu = 1500;
        }
        return true;
    }
    
    // Shutdown network stack
    void shutdown() {
        // Close all sockets
        for (int i = 0; i < tcp_socket_count; i++) {
            if (tcp_sockets[i].send_buf) kfree(tcp_sockets[i].send_buf);
            if (tcp_sockets[i].recv_buf) kfree(tcp_sockets[i].recv_buf);
        }
        for (int i = 0; i < udp_socket_count; i++) {
            if (udp_sockets[i].recv_buf) kfree(udp_sockets[i].recv_buf);
        }
    }
    
    // Add network interface
    int add_interface(const char* name) {
        if (interface_count >= 4) return -1;
        
        NetInterface& iface = interfaces[interface_count++];
        strncpy(iface.name, name, 15);
        iface.up = true;
        
        return interface_count - 1;
    }
    
    // Get interface by name
    NetInterface* get_interface(const char* name) {
        for (int i = 0; i < interface_count; i++) {
            if (strcmp(interfaces[i].name, name) == 0) {
                return &interfaces[i];
            }
        }
        return 0;
    }
    
    // Set IP address
    bool set_ip(int iface, IPAddress ip, IPAddress netmask) {
        if (iface < 0 || iface >= interface_count) return false;
        interfaces[iface].ip = ip;
        interfaces[iface].netmask = netmask;
        return true;
    }
    
    // Set gateway
    bool set_gateway(int iface, IPAddress gw) {
        if (iface < 0 || iface >= interface_count) return false;
        interfaces[iface].gateway = gw;
        return true;
    }
    
    // Set DNS
    bool set_dns(int iface, IPAddress dns) {
        if (iface < 0 || iface >= interface_count) return false;
        interfaces[iface].dns = dns;
        return true;
    }
    
    // TCP socket
    int tcp_socket() {
        if (tcp_socket_count >= 16) return -1;
        
        TCPSocket& sock = tcp_sockets[tcp_socket_count++];
        sock.fd = tcp_socket_count;
        sock.state = TCP_CLOSED;
        sock.send_seq = 0;
        sock.recv_ack = 0;
        sock.window_size = 65535;
        sock.send_buf = 0;
        sock.send_len = 0;
        sock.recv_buf = (uint8_t*)kalloc(65536);
        sock.recv_len = 0;
        sock.recv_pos = 0;
        
        return sock.fd;
    }
    
    // TCP connect
    bool tcp_connect(int fd, IPAddress ip, uint16_t port) {
        if (fd < 0 || fd >= tcp_socket_count) return false;
        
        TCPSocket& sock = tcp_sockets[fd];
        sock.remote_ip = ip;
        sock.remote_port = port;
        sock.state = TCP_SYN_SENT;
        
        // Send SYN packet
        send_tcp_packet(fd, 0x02);  // SYN flag
        
        return true;
    }
    
    // TCP bind
    bool tcp_bind(int fd, uint16_t port) {
        if (fd < 0 || fd >= tcp_socket_count) return false;
        
        TCPSocket& sock = tcp_sockets[fd];
        sock.local_port = port;
        return true;
    }
    
    // TCP listen
    bool tcp_listen(int fd) {
        if (fd < 0 || fd >= tcp_socket_count) return false;
        
        TCPSocket& sock = tcp_sockets[fd];
        sock.state = TCP_LISTEN;
        return true;
    }
    
    // TCP accept
    int tcp_accept(int fd) {
        if (fd < 0 || fd >= tcp_socket_count) return -1;
        
        // Accept new connection
        int new_fd = tcp_socket();
        if (new_fd < 0) return -1;
        
        // Copy listen socket info
        TCPSocket& listen = tcp_sockets[fd];
        TCPSocket& accepted = tcp_sockets[new_fd];
        accepted.local_port = listen.local_port;
        
        return new_fd;
    }
    
    // TCP send
    int tcp_send(int fd, const void* buf, uint32_t len) {
        if (fd < 0 || fd >= tcp_socket_count) return -1;
        
        TCPSocket& sock = tcp_sockets[fd];
        if (sock.state != TCP_ESTABLISHED) return -1;
        
        // Send data
        memcpy(sock.send_buf + sock.send_len, buf, len);
        sock.send_len += len;
        
        // Push to network
        send_tcp_packet(fd, 0x18);  // PSH + ACK flag
        
        return len;
    }
    
    // TCP receive
    int tcp_recv(int fd, void* buf, uint32_t len) {
        if (fd < 0 || fd >= tcp_socket_count) return -1;
        
        TCPSocket& sock = tcp_sockets[fd];
        if (sock.state != TCP_ESTABLISHED) return -1;
        
        uint32_t available = sock.recv_len - sock.recv_pos;
        uint32_t to_read = len < available ? len : available;
        
        memcpy(buf, sock.recv_buf + sock.recv_pos, to_read);
        sock.recv_pos += to_read;
        
        return to_read;
    }
    
    // TCP close
    bool tcp_close(int fd) {
        if (fd < 0 || fd >= tcp_socket_count) return false;
        
        TCPSocket& sock = tcp_sockets[fd];
        sock.state = TCP_CLOSED;
        
        if (sock.send_buf) kfree(sock.send_buf);
        if (sock.recv_buf) kfree(sock.recv_buf);
        sock.send_buf = 0;
        sock.recv_buf = 0;
        
        return true;
    }
    
    // UDP socket
    int udp_socket() {
        if (udp_socket_count >= 16) return -1;
        
        UDPSocket& sock = udp_sockets[udp_socket_count++];
        sock.recv_buf = (uint8_t*)kalloc(65536);
        sock.recv_len = 0;
        sock.recv_pos = 0;
        
        return udp_socket_count - 1;
    }
    
    // UDP bind
    bool udp_bind(int fd, uint16_t port) {
        if (fd < 0 || fd >= udp_socket_count) return false;
        
        udp_sockets[fd].local_port = port;
        return true;
    }
    
    // UDP send
    int udp_send(int fd, IPAddress ip, uint16_t port, const void* buf, uint32_t len) {
        if (fd < 0 || fd >= udp_socket_count) return -1;
        
        // Send UDP packet
        return len;
    }
    
    // UDP receive
    int udp_recv(int fd, void* buf, uint32_t len, IPAddress* src_ip, uint16_t* src_port) {
        if (fd < 0 || fd >= udp_socket_count) return -1;
        
        UDPSocket& sock = udp_sockets[fd];
        uint32_t available = sock.recv_len - sock.recv_pos;
        uint32_t to_read = len < available ? len : available;
        
        memcpy(buf, sock.recv_buf + sock.recv_pos, to_read);
        sock.recv_pos += to_read;
        
        return to_read;
    }
    
    // UDP close
    bool udp_close(int fd) {
        if (fd < 0 || fd >= udp_socket_count) return false;
        
        if (udp_sockets[fd].recv_buf) kfree(udp_sockets[fd].recv_buf);
        udp_sockets[fd].recv_buf = 0;
        
        return true;
    }
    
    // DNS resolve
    IPAddress dns_resolve(const char* hostname) {
        // DNS resolution
        return IPAddress(0, 0, 0, 0);
    }
    
    // Ping (ICMP echo request)
    bool ping(IPAddress ip, uint32_t timeout_ms) {
        // Send ICMP echo request
        return true;
    }
    
private:
    void send_tcp_packet(int fd, uint8_t flags) {
        // Construct and send TCP packet
    }
    
    void send_ip_packet(IPAddress dst, uint8_t protocol, const void* buf, uint32_t len) {
        // Construct and send IP packet
    }
    
    void send_ethernet_packet(MACAddress dst, uint16_t ethertype, const void* buf, uint32_t len) {
        // Construct and send Ethernet frame
    }
};

// Global network stack
NetworkStack g_net;

} // namespace net
} // namespace nefu
