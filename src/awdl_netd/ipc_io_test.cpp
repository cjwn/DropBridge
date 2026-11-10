// Exercises the production functions with real loopback sockets and lwIP pools.
// No radio, AWDL daemon, device or external network is involved.
#define AIRBRIDGE_NETD_TEST
#include "main.cpp"
extern "C" {
#include "lwip/init.h"
}
#include <stdexcept>
#include <utility>

static void require(bool ok, const char *what) {
    if (!ok) throw std::runtime_error(what);
}

static std::pair<SOCKET, SOCKET> socket_pair() {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(listener != INVALID_SOCKET, "listener socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0, "bind loopback");
    int size = sizeof(addr);
    require(getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &size) == 0, "ephemeral port");
    require(listen(listener, 1) == 0, "listen");
    SOCKET peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(connect(peer, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0, "connect");
    SOCKET server = accept(listener, nullptr, nullptr);
    closesocket(listener);
    require(server != INVALID_SOCKET, "accept");
    return {server, peer};
}

static std::vector<std::vector<uint8_t>> received;
static err_t accept_packet(pbuf *p, netif *) {
    received.emplace_back(p->tot_len);
    pbuf_copy_partial(p, received.back().data(), p->tot_len, 0);
    pbuf_free(p); // Successful input owns the buffer.
    return ERR_OK;
}
static err_t reject_packet(pbuf *, netif *) { return ERR_MEM; }
static int busy_rejects = 0;
static err_t busy_then_accept(pbuf *p, netif *n) {
    if (busy_rejects > 0) {
        --busy_rejects;
        return ERR_MEM; // A momentarily full tcpip mailbox; the pbuf stays ours.
    }
    return accept_packet(p, n);
}

static void receive_exact(SOCKET s, uint8_t *out, size_t size) {
    while (size) {
        int n = recv(s, reinterpret_cast<char *>(out), static_cast<int>(size), 0);
        require(n > 0, "receive framed output");
        size -= n;
        out += n;
    }
}

static std::vector<pbuf *> hold_pool() {
    std::vector<pbuf *> held;
    while (auto *p = pbuf_alloc(PBUF_RAW, 1, PBUF_POOL)) {
        held.push_back(p);
        require(held.size() <= 4096, "pool must be bounded");
    }
    return held;
}
static void release_pool(const std::vector<pbuf *> &held) {
    for (auto *p : held) pbuf_free(p);
}

int main() {
    try {
        WSADATA wsa{};
        require(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "WSAStartup");
        HANDLE ready = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        require(ready != nullptr, "init event");
        tcpip_init([](void *event) { SetEvent(static_cast<HANDLE>(event)); }, ready);
        require(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0, "lwIP core ready");
        CloseHandle(ready);
        uint8_t packet[64] = {0x60};
        g_netif.input = accept_packet;

        auto held = hold_pool();
        const size_t capacity = held.size();
        require(capacity > 0, "pool capacity");
        deliver_ip_packet(packet, sizeof(packet));
        require(g_stats.alloc_fail == 1 && g_stats.rx_queued == 0, "allocation failure counted");
        release_pool(held);
        for (int i = 0; i < 100; ++i) deliver_ip_packet(packet, sizeof(packet));
        g_netif.input = reject_packet;
        // No retry budget: count outcomes without paying 500 ms per packet.
        g_input_retry_limit_ms = 0;
        for (int i = 0; i < 100; ++i) deliver_ip_packet(packet, sizeof(packet));
        require(g_stats.rx_queued == 100 && g_stats.input_fail == 100 && g_stats.input_retry == 0,
                "input outcomes counted separately");
        // Persistent ERR_MEM: retried for the wall-clock budget, then dropped.
        g_input_retry_limit_ms = 100;
        const auto retry_began = GetTickCount64();
        deliver_ip_packet(packet, sizeof(packet));
        const auto retry_elapsed = GetTickCount64() - retry_began;
        require(g_stats.input_fail == 101 && g_stats.input_retry > 0, "persistent ERR_MEM retried then dropped");
        require(retry_elapsed >= 50 && retry_elapsed < 1000, "ERR_MEM retry bounded by wall-clock budget");
        // Transient ERR_MEM: the same pbuf is offered again until lwIP takes it.
        g_netif.input = busy_then_accept;
        busy_rejects = 3;
        const unsigned long long retries_before = g_stats.input_retry.load();
        deliver_ip_packet(packet, sizeof(packet));
        require(busy_rejects == 0 && g_stats.rx_queued == 101 && g_stats.input_fail == 101 &&
                g_stats.input_retry == retries_before + 3, "transient ERR_MEM retried until queued");
        held = hold_pool();
        require(held.size() == capacity, "no leaked pbuf on success, retry or rejection");
        release_pool(held);
        std::puts("PASS: real pool exhaustion, input rejection, ERR_MEM retry and buffer ownership");

        auto pair = socket_pair();
        g_ipc = pair.first;
        auto *p = pbuf_alloc(PBUF_RAW, sizeof(packet), PBUF_RAM);
        require(p && pbuf_take(p, packet, sizeof(packet)) == ERR_OK, "output pbuf");
        require(netif_output_ip6(nullptr, p, nullptr) == ERR_OK, "successful output");
        uint8_t wire[66];
        receive_exact(pair.second, wire, sizeof(wire));
        require(wire[0] == 64 && wire[1] == 0 && memcmp(wire + 2, packet, 64) == 0, "wire header and payload");
        ++p->tot_len; // Deliberately inconsistent chain must not send garbage.
        require(netif_output_ip6(nullptr, p, nullptr) == ERR_BUF, "short pbuf copy rejected");
        --p->tot_len;
        shutdown(pair.first, SD_SEND);
        require(netif_output_ip6(nullptr, p, nullptr) == ERR_IF, "send failure not reported as success");
        require(g_ipc_failed && g_stats.tx_frames == 1 && g_stats.tx_fail == 2, "terminal send failure recorded");
        pbuf_free(p);
        closesocket(pair.first); closesocket(pair.second);
        std::puts("PASS: actual output framing, copy bounds and failed send => ERR_IF");

        pair = socket_pair();
        g_ipc = pair.first;
        g_ipc_failed = false;
        const DWORD timeout = 100;
        int no_buffer = 0, receive_buffer = 1024;
        require(setsockopt(pair.first, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char *>(&timeout), sizeof(timeout)) == 0, "test send timeout");
        require(setsockopt(pair.first, SOL_SOCKET, SO_SNDBUF,
            reinterpret_cast<const char *>(&no_buffer), sizeof(no_buffer)) == 0, "test send buffer");
        require(setsockopt(pair.second, SOL_SOCKET, SO_RCVBUF,
            reinterpret_cast<const char *>(&receive_buffer), sizeof(receive_buffer)) == 0, "test receive buffer");
        std::vector<uint8_t> pressure(4 * 1024 * 1024);
        const auto began = GetTickCount64();
        require(!send_all(pair.first, pressure.data(), pressure.size()) && g_ipc_failed,
                "a peer that never reads must not block forever");
        require(GetTickCount64() - began < 3000, "send timeout was not bounded");
        closesocket(pair.first); closesocket(pair.second);
        std::puts("PASS: non-reading local peer terminates a blocked send");

        pair = socket_pair();
        g_ipc = pair.first;
        g_ipc_failed = false;
        g_netif.input = accept_packet;
        received.clear();
        std::vector<uint8_t> large(60000);
        for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<uint8_t>(i);
        large[0] = 0x60;
        std::thread reader(ipc_reader);
        const uint8_t small_header[] = {64, 0};
        require(send_all(pair.second, small_header, 1), "split header byte 1");
        require(send_all(pair.second, small_header + 1, 1), "split header byte 2");
        require(send_all(pair.second, packet, 17), "split payload 1");
        require(send_all(pair.second, packet + 17, sizeof(packet) - 17), "split payload 2");
        const uint8_t big_header[] = {static_cast<uint8_t>(large.size()), static_cast<uint8_t>(large.size() >> 8)};
        require(send_all(pair.second, big_header, 2) && send_all(pair.second, large.data(), large.size()), "large chained pbuf input");
        shutdown(pair.second, SD_SEND);
        reader.join();
        require(g_ipc_failed && received.size() == 2, "EOF is terminal after two frames");
        require(received[0] == std::vector<uint8_t>(packet, packet + 64) && received[1] == large, "fragmented/batched IPC preserves bytes");
        closesocket(pair.first); closesocket(pair.second);
        std::puts("PASS: split IPC, 60 KB input, orderly EOF and exact bytes");
        print_stats();
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        std::fflush(stderr);
        std::_Exit(1);
    }
}
