#include "local_io.h"
#include "tcp_diag.h"

/* The C++ headers come first: lwIP's compat layer defines read/write/close as
 * function-like macros, which breaks the MSVC stream headers if they are
 * included afterwards. */
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

extern "C" {
#include "lwip/sockets.h"
}

// lwIP needs the netif index as the scope for a link-local destination.
extern "C" int awdl_netd_netif_index();

namespace {

constexpr int kReceiverPort = 8771;
constexpr int kMdnsLocalPort = 15353;

int proxy_send_all(int fd, intptr_t local, uint64_t id, bool up, const uint8_t *data, size_t len) {
    while (len > 0) {
        tcp_diag::proxy_phase(id, up, up ? "local_send" : "lwip_send");
        int n = up ? local_tcp_send_some(local, data, static_cast<int>(len)) : lwip_send(fd, data, len, 0);
        if (n <= 0) {
            if (up) std::fprintf(stderr, "awdl_netd: proxy flow=%llu local_send_failed error=%d\n",
                                 id, n < 0 ? local_socket_error() : 0);
            return -1;
        }
        tcp_diag::proxy_bytes(id, up, true, static_cast<size_t>(n));
        data += n;
        len -= static_cast<size_t>(n);
    }
    return 0;
}

void proxy_connection(int lwip_fd, tcp_diag::Key key) {
    const auto id = tcp_diag::proxy_open(key);
    intptr_t local = local_tcp_connect(kReceiverPort);
    if (local < 0) {
        tcp_diag::proxy_phase(id, true, "connect_error");
        tcp_diag::proxy_phase(id, false, "connect_error");
        tcp_diag::proxy_close(id);
        lwip_close(lwip_fd);
        return;
    }

    std::atomic<bool> stop{false};

    std::thread up([&] {
        uint8_t buffer[16384];
        for (;;) {
            if (stop.load()) {
                tcp_diag::proxy_phase(id, true, "cancelled");
                break;
            }
            tcp_diag::proxy_phase(id, true, "lwip_recv");
            int n = lwip_recv(lwip_fd, buffer, sizeof(buffer), 0);
            if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) continue;
            if (n <= 0) {
                tcp_diag::proxy_phase(id, true, n == 0 ? "lwip_eof" : "lwip_recv_error");
                // A normal FIN keeps the return path alive for the response.
                // A reset/error must also wake the local recv worker.
                if (n < 0) {
                    stop = true;
                    local_tcp_shutdown_both(local);
                }
                break;
            }
            tcp_diag::proxy_bytes(id, true, false, static_cast<size_t>(n));
            if (proxy_send_all(lwip_fd, local, id, true, buffer, static_cast<size_t>(n)) != 0) {
                tcp_diag::proxy_phase(id, true, "local_send_error");
                stop = true;
                local_tcp_shutdown_both(local);
                break;
            }
        }
        local_tcp_shutdown_send(local);
    });

    uint8_t buffer[16384];
    for (;;) {
        if (stop.load()) {
            tcp_diag::proxy_phase(id, false, "cancelled");
            break;
        }
        tcp_diag::proxy_phase(id, false, "local_recv");
        int n = local_tcp_recv(local, buffer, sizeof(buffer));
        if (n == -2) continue; // Check cancellation; healthy idle is not failure.
        if (n <= 0) {
            tcp_diag::proxy_phase(id, false, n == 0 ? "local_eof" : "local_recv_error");
            break;
        }
        tcp_diag::proxy_bytes(id, false, false, static_cast<size_t>(n));
        if (proxy_send_all(lwip_fd, local, id, false, buffer, static_cast<size_t>(n)) != 0) {
            tcp_diag::proxy_phase(id, false, "lwip_send_error");
            break;
        }
    }

    // Wake a local send before joining it. Keep handles valid until both
    // workers finish, including errors and the bounded local send timeout.
    stop = true;
    local_tcp_shutdown_both(local);
    up.join();
    // lwIP's current port does not enable concurrent close/shutdown. The
    // receive poll and local send timeout let the worker finish first.
    lwip_shutdown(lwip_fd, SHUT_RDWR);
    local_tcp_close(local);
    lwip_close(lwip_fd);
    tcp_diag::proxy_close(id);
}

int g_mdns_fd = -1;
intptr_t g_mdns_local = -1;
std::mutex g_peer_mu;
uint32_t g_peer_ip = 0;
uint16_t g_peer_port = 0;
bool g_have_peer = false;
// The local endpoint that last asked a question. Answers go there instead of to
// whoever spoke last, so a browser and the announcer can share the proxy.
uint32_t g_querier_ip = 0;
uint16_t g_querier_port = 0;
bool g_have_querier = false;

bool mdns_is_query(const uint8_t *data, int len) {
    if (len < 12) return false;
    const uint16_t flags = static_cast<uint16_t>((data[2] << 8) | data[3]);
    const uint16_t questions = static_cast<uint16_t>((data[4] << 8) | data[5]);
    return (flags & 0x8000u) == 0 && questions > 0;
}

bool mdns_debug() {
    static const bool on = std::getenv("AWDL_NETD_MDNS_DEBUG") != nullptr;
    return on;
}

void mdns_from_local() {
    uint8_t buffer[4096];
    for (;;) {
        uint32_t ip = 0;
        uint16_t port = 0;
        int n = local_udp_recv(g_mdns_local, buffer, sizeof(buffer), &ip, &port);
        // -2: a per-datagram error (e.g. the old receiver's port is gone).
        // Leaving here would stop all local->AWDL mDNS until a full restart.
        if (n == -2 || n == 0) continue;
        if (n < 0) {
            std::fprintf(stderr, "awdl_netd: mDNS local recv failed (%d); forwarding stopped\n",
                         local_socket_error());
            return;
        }
        const bool query = mdns_is_query(buffer, n);
        {
            std::lock_guard<std::mutex> lock(g_peer_mu);
            g_peer_ip = ip;
            g_peer_port = port;
            g_have_peer = true;
            if (query) {
                g_querier_ip = ip;
                g_querier_port = port;
                g_have_querier = true;
            }
        }
        if (mdns_debug()) {
            std::fprintf(stderr, "awdl_netd: mdns local->awdl %d bytes from port %u (%s)\n",
                         n, static_cast<unsigned>(port), query ? "query" : "announce");
        }
        sockaddr_in6 dst{};
        dst.sin6_family = AF_INET6;
        dst.sin6_port = htons(5353);
        inet_pton(AF_INET6, "ff02::fb", &dst.sin6_addr);
        const int sent = lwip_sendto(g_mdns_fd, buffer, static_cast<size_t>(n), 0,
                                     reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
        if (mdns_debug() && sent < 0) {
            std::fprintf(stderr, "awdl_netd: mdns sendto failed errno=%d\n", errno);
        }
    }
}

void mdns_from_awdl() {
    uint8_t buffer[4096];
    for (;;) {
        sockaddr_in6 from{};
        socklen_t from_len = sizeof(from);
        int n = lwip_recvfrom(g_mdns_fd, buffer, sizeof(buffer), 0,
                              reinterpret_cast<sockaddr *>(&from), &from_len);
        if (n <= 0) continue;
        const bool answer = n >= 4 && (buffer[2] & 0x80u) != 0;
        uint32_t ip = 0;
        uint16_t port = 0;
        bool routed = false;
        {
            std::lock_guard<std::mutex> lock(g_peer_mu);
            if (answer && g_have_querier) {
                ip = g_querier_ip;
                port = g_querier_port;
                routed = true;
            } else if (g_have_peer) {
                ip = g_peer_ip;
                port = g_peer_port;
                routed = true;
            }
        }
        if (mdns_debug()) {
            char source[64] = {0};
            inet_ntop(AF_INET6, &from.sin6_addr, source, sizeof(source));
            std::fprintf(stderr,
                         "awdl_netd: mdns awdl->local %d bytes (%s) from %s routed=%d port=%u\n",
                         n, answer ? "answer" : "query", source, routed ? 1 : 0,
                         static_cast<unsigned>(port));
            // The first 96 bytes are enough to read every question and most
            // answers; hex keeps the log greppable without parsing here.
            char hex[3 * 96 + 1] = {0};
            const int shown = n < 96 ? n : 96;
            for (int index = 0; index < shown; ++index) {
                std::snprintf(hex + index * 3, 4, "%02x ", buffer[index]);
            }
            std::fprintf(stderr, "awdl_netd: mdns hex %s\n", hex);
        }
        if (!routed) continue;
        local_udp_send(g_mdns_local, buffer, n, ip, port);
    }
}

/* Outbound proxy (PC -> phone). A local client sends one text line
 *     CONNECT <ipv6> <port>\n
 * and everything after it is pumped verbatim to a new lwIP connection, so the
 * protocol above (TLS, HTTP) stays end to end. The header never reaches the
 * peer. Kept in this file because it needs both socket families at once. */
constexpr int kConnectHeaderLimit = 256;
constexpr int kConnectHeaderMs = 5000;

bool parse_connect_line(const char *line, size_t len, uint8_t address[16],
                        uint16_t *port) {
    if (len == 0 || len > kConnectHeaderLimit) return false;
    char text[kConnectHeaderLimit + 1] = {0};
    std::memcpy(text, line, len);
    char address_text[64] = {0};
    unsigned value = 0;
    if (std::sscanf(text, "CONNECT %63s %u", address_text, &value) != 2) return false;
    if (value == 0 || value > 65535) return false;
    // A zone suffix belongs to the Windows API, not to lwIP: the netif is
    // already fixed for this stack.
    if (char *zone = std::strchr(address_text, '%')) *zone = 0;
    if (inet_pton(AF_INET6, address_text, address) != 1) return false;
    *port = static_cast<uint16_t>(value);
    return true;
}

void outbound_connection(intptr_t local) {
    char line[kConnectHeaderLimit + 1] = {0};
    size_t used = 0;
    bool complete = false;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kConnectHeaderMs);
    while (used < kConnectHeaderLimit && std::chrono::steady_clock::now() < deadline) {
        int n = local_tcp_recv(local, line + used, 1);
        if (n == -2) continue; // idle wait, not a failure
        if (n <= 0) break;
        ++used;
        if (line[used - 1] == '\n') {
            complete = true;
            break;
        }
    }
    uint8_t address[16] = {0};
    uint16_t port = 0;
    const size_t text_length = complete ? used - 1 : used;
    if (!complete || !parse_connect_line(line, text_length, address, &port)) {
        std::fprintf(stderr, "awdl_netd: outbound request rejected (no CONNECT line)\n");
        local_tcp_close(local);
        return;
    }

    int fd = lwip_socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "awdl_netd: outbound lwip_socket failed\n");
        local_tcp_close(local);
        return;
    }
    sockaddr_in6 target{};
    target.sin6_family = AF_INET6;
    target.sin6_port = htons(port);
    std::memcpy(&target.sin6_addr, address, 16);
    target.sin6_scope_id = static_cast<uint32_t>(awdl_netd_netif_index());
    if (lwip_connect(fd, reinterpret_cast<sockaddr *>(&target), sizeof(target)) != 0) {
        std::fprintf(stderr, "awdl_netd: outbound connect failed (port %u)\n",
                     static_cast<unsigned>(port));
        lwip_close(fd);
        local_tcp_close(local);
        return;
    }
    timeval poll{1, 0};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &poll, sizeof(poll));

    std::atomic<bool> stop{false};
    std::thread up([&] { // local bytes -> lwIP
        uint8_t buffer[16384];
        for (;;) {
            if (stop.load()) break;
            int n = local_tcp_recv(local, buffer, sizeof(buffer));
            if (n == -2) continue;
            if (n <= 0) {
                stop = true;
                lwip_shutdown(fd, SHUT_RDWR);
                break;
            }
            size_t sent = 0;
            while (sent < static_cast<size_t>(n)) {
                int written = lwip_send(fd, buffer + sent, n - sent, 0);
                if (written <= 0) {
                    stop = true;
                    local_tcp_shutdown_both(local);
                    return;
                }
                sent += static_cast<size_t>(written);
            }
        }
    });

    uint8_t buffer[16384];
    for (;;) { // lwIP bytes -> local
        if (stop.load()) break;
        int n = lwip_recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) continue;
        if (n <= 0) break;
        size_t sent = 0;
        while (sent < static_cast<size_t>(n)) {
            int written = local_tcp_send_some(local, buffer + sent,
                                              static_cast<int>(n - sent));
            if (written <= 0) {
                stop = true;
                break;
            }
            sent += static_cast<size_t>(written);
        }
        if (stop.load()) break;
    }

    stop = true;
    local_tcp_shutdown_both(local);
    up.join();
    lwip_shutdown(fd, SHUT_RDWR);
    lwip_close(fd);
    local_tcp_close(local);
}

}  // namespace

/* PC -> phone. The local side names its target with one text line before any
 * bytes of its own protocol:
 *     CONNECT <ipv6> <port>\n
 * The proxy then opens a fresh lwIP connection to that address and pumps both
 * directions verbatim, so TLS above it is end to end. Only started when the
 * supervisor asks for it (AWDL_NETD_OUTBOUND), so the receive-only product
 * path keeps no extra listener. */
extern "C" void awdl_netd_start_outbound_proxy(int port) {
    std::thread([port] {
        intptr_t listener = local_tcp_listen(port);
        if (listener < 0) {
            std::fprintf(stderr, "awdl_netd: outbound proxy cannot listen on %d\n", port);
            return;
        }
        std::fprintf(stderr,
                     "awdl_netd: outbound proxy on 127.0.0.1:%d "
                     "(CONNECT <ipv6> <port> then raw bytes)\n",
                     port);
        for (;;) {
            intptr_t local = local_tcp_accept(listener);
            if (local == -2) continue;
            if (local < 0) continue;
            std::thread(outbound_connection, local).detach();
        }
    }).detach();
}

extern "C" void awdl_netd_start_tcp_proxy() {
    std::thread([] {
        int listener = lwip_socket(AF_INET6, SOCK_STREAM, 0);
        if (listener < 0) {
            std::fprintf(stderr, "awdl_netd: lwip_socket TCP failed\n");
            return;
        }
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(kReceiverPort);
        if (lwip_bind(listener, reinterpret_cast<sockaddr *>(&addr),
                      sizeof(addr)) != 0 ||
            lwip_listen(listener, 8) != 0) {
            std::fprintf(stderr, "awdl_netd: TCP 8771 listen failed\n");
            lwip_close(listener);
            return;
        }
        std::fprintf(stderr, "awdl_netd: TCP 8771 -> 127.0.0.1:%d ready\n",
                     kReceiverPort);
        for (;;) {
            sockaddr_in6 peer{};
            socklen_t peer_len = sizeof(peer);
            int fd = lwip_accept(listener, reinterpret_cast<sockaddr *>(&peer),
                                 &peer_len);
            if (fd < 0) continue;
            int one = 1;
            lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            timeval poll{1, 0};
            if (lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &poll, sizeof(poll)) != 0) {
                lwip_close(fd);
                continue;
            }
            sockaddr_in6 local_addr{};
            socklen_t local_len = sizeof(local_addr);
            if (lwip_getsockname(fd, reinterpret_cast<sockaddr *>(&local_addr), &local_len) != 0) {
                lwip_close(fd);
                continue;
            }
            tcp_diag::Key key;
            std::memcpy(key.remote.data(), &peer.sin6_addr, 16);
            std::memcpy(key.local.data(), &local_addr.sin6_addr, 16);
            key.remote_port = ntohs(peer.sin6_port);
            key.local_port = ntohs(local_addr.sin6_port);
            std::thread(proxy_connection, fd, key).detach();
        }
    }).detach();
}

extern "C" bool awdl_netd_start_mdns_proxy() {
    g_mdns_local = local_udp_bind(kMdnsLocalPort);
    if (g_mdns_local < 0) {
        std::fprintf(stderr, "awdl_netd: mDNS local bind failed\n");
        return false;
    }
    g_mdns_fd = lwip_socket(AF_INET6, SOCK_DGRAM, 0);
    if (g_mdns_fd < 0) return false;
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(5353);
    if (lwip_bind(g_mdns_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) !=
        0) {
        std::fprintf(stderr, "awdl_netd: mDNS lwIP bind failed\n");
        return false;
    }
    std::fprintf(stderr,
                 "awdl_netd: mDNS proxy 127.0.0.1:%d <-> ff02::fb:5353\n",
                 kMdnsLocalPort);
    std::thread(mdns_from_local).detach();
    std::thread(mdns_from_awdl).detach();
    return true;
}
