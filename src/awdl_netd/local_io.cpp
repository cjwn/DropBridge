#include "local_io.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

namespace {
SOCKET to_socket(intptr_t handle) {
    return static_cast<SOCKET>(handle);
}
}  // namespace

extern "C" {

intptr_t local_tcp_connect(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return -1;
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char *>(&one), sizeof(one));
    return static_cast<intptr_t>(s);
}

int local_tcp_send(intptr_t handle, const void *data, int len) {
    const char *p = static_cast<const char *>(data);
    int remaining = len;
    while (remaining > 0) {
        int n = send(to_socket(handle), p, remaining, 0);
        if (n <= 0) return -1;
        p += n;
        remaining -= n;
    }
    return 0;
}

int local_tcp_recv(intptr_t handle, void *data, int len) {
    return recv(to_socket(handle), static_cast<char *>(data), len, 0);
}

void local_tcp_shutdown_send(intptr_t handle) {
    shutdown(to_socket(handle), SD_SEND);
}

void local_tcp_close(intptr_t handle) {
    closesocket(to_socket(handle));
}

intptr_t local_udp_bind(int port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));
    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return -1;
    }
    return static_cast<intptr_t>(s);
}

int local_udp_recv(intptr_t handle, void *data, int len, uint32_t *peer_ip,
                   uint16_t *peer_port) {
    sockaddr_in from{};
    int from_len = sizeof(from);
    int n = recvfrom(to_socket(handle), static_cast<char *>(data), len, 0,
                     reinterpret_cast<sockaddr *>(&from), &from_len);
    if (n > 0) {
        if (peer_ip) *peer_ip = from.sin_addr.s_addr;
        if (peer_port) *peer_port = from.sin_port;
    }
    return n;
}

int local_udp_send(intptr_t handle, const void *data, int len, uint32_t peer_ip,
                   uint16_t peer_port) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = peer_ip;
    to.sin_port = peer_port;
    return sendto(to_socket(handle), static_cast<const char *>(data), len, 0,
                  reinterpret_cast<const sockaddr *>(&to), sizeof(to));
}

void local_udp_close(intptr_t handle) {
    closesocket(to_socket(handle));
}

}  // extern "C"
