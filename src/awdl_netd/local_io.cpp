#include "local_io.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <cstring>

namespace {
SOCKET to_socket(intptr_t handle) {
    return static_cast<SOCKET>(handle);
}

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

// Bound both directions of a local TCP stream: a peer that stops reading must
// not block send() forever, and recv() wakes every poll_ms (returning -2 from
// local_tcp_recv) so callers can check their stop flags and deadlines.
bool set_stream_timeouts(SOCKET s) {
    const DWORD timeout_ms = 5000;
    const DWORD poll_ms = 1000;
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char *>(&timeout_ms), sizeof(timeout_ms)) == 0 &&
           setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&poll_ms), sizeof(poll_ms)) == 0;
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
    // Bound a receiver that stops reading. Partial writes are counted by the
    // caller; any later failure aborts this connection instead of retrying bytes.
    if (!set_stream_timeouts(s)) {
        closesocket(s);
        return -1;
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char *>(&one), sizeof(one));
    return static_cast<intptr_t>(s);
}

int local_tcp_send_some(intptr_t handle, const void *data, int len) {
    return send(to_socket(handle), static_cast<const char *>(data), len, 0);
}

intptr_t local_tcp_listen(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port));
    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        listen(s, 4) != 0) {
        closesocket(s);
        return -1;
    }
    return static_cast<intptr_t>(s);
}

intptr_t local_tcp_accept(intptr_t listener) {
    // Poll first so the accept loop can notice shutdown instead of blocking.
    fd_set set;
    FD_ZERO(&set);
    FD_SET(to_socket(listener), &set);
    timeval poll{1, 0};
    int ready = select(0, &set, nullptr, nullptr, &poll);
    if (ready <= 0) return ready == 0 ? -2 : -1;
    SOCKET client = accept(to_socket(listener), nullptr, nullptr);
    if (client == INVALID_SOCKET) return -1;
    // Same bounds as local_tcp_connect: without them the outbound proxy's
    // CONNECT-header deadline and stop checks never ran, and an idle client
    // pinned a thread plus one of lwIP's few netconns forever.
    if (!set_stream_timeouts(client)) {
        closesocket(client);
        return -1;
    }
    return static_cast<intptr_t>(client);
}

int local_tcp_recv(intptr_t handle, void *data, int len) {
    int n = recv(to_socket(handle), static_cast<char *>(data), len, 0);
    return n < 0 && WSAGetLastError() == WSAETIMEDOUT ? -2 : n;
}

int local_socket_error() { return WSAGetLastError(); }

void local_tcp_shutdown_send(intptr_t handle) {
    shutdown(to_socket(handle), SD_SEND);
}

void local_tcp_shutdown_both(intptr_t handle) {
    shutdown(to_socket(handle), SD_BOTH);
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
    // Windows reports an ICMP port-unreachable for an earlier sendto() as
    // WSAECONNRESET on the next recvfrom(). That happens whenever the receiver
    // restarts on a new port; it is not a socket failure, so turn it off.
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned,
             nullptr, nullptr);
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
    if (n < 0) {
        // Per-datagram errors leave the socket usable: report them as -2 so
        // the caller keeps reading (belt and braces with SIO_UDP_CONNRESET).
        const int error = WSAGetLastError();
        if (error == WSAECONNRESET || error == WSAEMSGSIZE || error == WSAENETRESET) {
            return -2;
        }
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
