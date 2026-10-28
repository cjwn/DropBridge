#include "local_io.h"
#include "tcp_diag.h"

extern "C" {
#include "lwip/sockets.h"
}

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

constexpr int kReceiverPort = 8771;
constexpr int kMdnsLocalPort = 15353;

int proxy_send_all(int fd, intptr_t local, uint64_t id, bool up, const uint8_t *data, size_t len) {
    while (len > 0) {
        tcp_diag::proxy_phase(id, up, up ? "local_send" : "lwip_send");
        int n = up ? local_tcp_send_some(local, data, static_cast<int>(len)) : lwip_send(fd, data, len, 0);
        if (n <= 0) return -1;
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

    std::thread up([&] {
        uint8_t buffer[16384];
        for (;;) {
            tcp_diag::proxy_phase(id, true, "lwip_recv");
            int n = lwip_recv(lwip_fd, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                tcp_diag::proxy_phase(id, true, n == 0 ? "lwip_eof" : "lwip_recv_error");
                break;
            }
            tcp_diag::proxy_bytes(id, true, false, static_cast<size_t>(n));
            if (proxy_send_all(lwip_fd, local, id, true, buffer, static_cast<size_t>(n)) != 0) {
                tcp_diag::proxy_phase(id, true, "local_send_error");
                break;
            }
        }
        local_tcp_shutdown_send(local);
    });

    uint8_t buffer[16384];
    for (;;) {
        tcp_diag::proxy_phase(id, false, "local_recv");
        int n = local_tcp_recv(local, buffer, sizeof(buffer));
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

    lwip_shutdown(lwip_fd, SHUT_RDWR);
    up.join();
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

void mdns_from_local() {
    uint8_t buffer[4096];
    for (;;) {
        uint32_t ip = 0;
        uint16_t port = 0;
        int n = local_udp_recv(g_mdns_local, buffer, sizeof(buffer), &ip, &port);
        if (n <= 0) return;
        {
            std::lock_guard<std::mutex> lock(g_peer_mu);
            g_peer_ip = ip;
            g_peer_port = port;
            g_have_peer = true;
        }
        sockaddr_in6 dst{};
        dst.sin6_family = AF_INET6;
        dst.sin6_port = htons(5353);
        inet_pton(AF_INET6, "ff02::fb", &dst.sin6_addr);
        lwip_sendto(g_mdns_fd, buffer, static_cast<size_t>(n), 0,
                    reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
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
        uint32_t ip = 0;
        uint16_t port = 0;
        {
            std::lock_guard<std::mutex> lock(g_peer_mu);
            if (!g_have_peer) continue;
            ip = g_peer_ip;
            port = g_peer_port;
        }
        local_udp_send(g_mdns_local, buffer, n, ip, port);
    }
}

}  // namespace

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
