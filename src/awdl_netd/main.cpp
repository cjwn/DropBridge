/* awdl_netd: user-space AWDL data plane.
 *
 * awdld sends raw IPv6 packets over 127.0.0.1:49877. lwIP terminates
 * IPv6/NDP and exposes blocking sockets; TCP 8771 is proxied to receiver.py,
 * mDNS 5353 to the Python announcer. The Windows IPv6 stack is not involved.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

extern "C" {
#include "lwip/ip6.h"
#include "lwip/ip6_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
}

#include <cstdint>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include "tcp_diag.h"

extern "C" void awdl_netd_start_tcp_proxy();
extern "C" bool awdl_netd_start_mdns_proxy();
extern "C" void awdl_netd_start_native_mdns(const uint8_t address[16],
                                             uint8_t ifindex);

namespace {

constexpr uint16_t kIpcPort = 49877;
constexpr size_t kMaxFrame = 2048;
constexpr DWORD kIpcSendTimeoutMs = 5000;
// How long the IPC reader waits for lwIP to accept one frame before treating
// it as lost. Blocking this thread is the intended backpressure: awdld's send
// then stalls instead of netd silently discarding frames the phone would have
// to retransmit.
constexpr int kInputRetrySleepMs = 1;
constexpr int kInputRetryLimitMs = 500;

SOCKET g_ipc = INVALID_SOCKET;
SOCKET g_listen = INVALID_SOCKET;
netif g_netif;
uint8_t g_address[16] = {0};
std::atomic<bool> g_ipc_failed{false};
struct {
    std::atomic<unsigned long long> rx_seen{0}, rx_bytes{0}, rx_queued{0};
    std::atomic<unsigned long long> alloc_fail{0}, copy_fail{0}, input_fail{0};
    std::atomic<unsigned long long> input_retry{0};
    std::atomic<unsigned long long> tx_frames{0}, tx_bytes{0}, tx_fail{0};
} g_stats;

void print_stats() {
    std::fprintf(stderr, "awdl_netd: ipc stats rx_seen=%llu rx_bytes=%llu rx_queued=%llu "
        "alloc_fail=%llu copy_fail=%llu input_fail=%llu input_retry=%llu "
        "tx_frames=%llu tx_bytes=%llu tx_fail=%llu\n",
        g_stats.rx_seen.load(), g_stats.rx_bytes.load(), g_stats.rx_queued.load(),
        g_stats.alloc_fail.load(), g_stats.copy_fail.load(), g_stats.input_fail.load(),
        g_stats.input_retry.load(),
        g_stats.tx_frames.load(), g_stats.tx_bytes.load(), g_stats.tx_fail.load());
    tcp_diag::print();
}

void ipc_failed(const char *operation, int error) {
    if (!g_ipc_failed.exchange(true)) {
        std::fprintf(stderr, "awdl_netd: IPC failed op=%s error=%d\n", operation, error);
        // A partial write cannot be retried as a new framed packet. Wake both
        // directions and let main exit so the existing supervisor sees failure.
        if (g_ipc != INVALID_SOCKET) shutdown(g_ipc, SD_BOTH);
    }
}

bool send_all(SOCKET s, const uint8_t *data, size_t len) {
    while (len > 0) {
        int n = ::send(s, reinterpret_cast<const char *>(data),
                       static_cast<int>(len), 0);
        if (n <= 0) {
            ipc_failed("send", n < 0 ? WSAGetLastError() : 0);
            return false;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool ipc_send_frame(const uint8_t *data, size_t len) {
    if (g_ipc == INVALID_SOCKET || g_ipc_failed.load()) {
        // No peer during initial startup is not a broken established link.
        // The caller still reports ERR_IF instead of pretending delivery.
        return false;
    }
    if (len == 0 || len > kMaxFrame) return false;
    uint8_t header[2] = {static_cast<uint8_t>(len & 0xff),
                         static_cast<uint8_t>((len >> 8) & 0xff)};
    return send_all(g_ipc, header, sizeof(header)) &&
           send_all(g_ipc, data, len);
}

err_t netif_output_ip6(struct netif *, struct pbuf *p, const ip6_addr_t *) {
    if (!p || p->tot_len == 0 || p->tot_len > kMaxFrame) {
        ++g_stats.tx_fail;
        return ERR_VAL;
    }
    uint8_t buffer[kMaxFrame];
    if (pbuf_copy_partial(p, buffer, p->tot_len, 0) != p->tot_len) {
        ++g_stats.tx_fail;
        return ERR_BUF;
    }
    if (!ipc_send_frame(buffer, p->tot_len)) {
        tcp_diag::observe(buffer, p->tot_len, false, false);
        ++g_stats.tx_fail;
        return ERR_IF;
    }
    ++g_stats.tx_frames;
    tcp_diag::observe(buffer, p->tot_len, false, true);
    g_stats.tx_bytes += p->tot_len;
    return ERR_OK;
}

err_t awdl_netif_init(struct netif *netif) {
    netif->name[0] = 'a';
    netif->name[1] = 'w';
    netif->mtu = 1450;
    netif->hwaddr_len = 6;
    netif->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP |
                   NETIF_FLAG_BROADCAST | NETIF_FLAG_MLD6;
    netif->output_ip6 = netif_output_ip6;
    return ERR_OK;
}

void configure_address(const uint8_t address[16]) {
    memcpy(g_address, address, 16);
    memcpy(g_netif.hwaddr, address + 8, 6);
    g_netif.hwaddr[0] ^= 0x02;

    ip6_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    memcpy(addr.addr, address, 16);
    netif_ip6_addr_set(&g_netif, 0, &addr);
    netif_ip6_addr_set_state(&g_netif, 0, IP6_ADDR_PREFERRED);
}

void write_address_file(const uint8_t address[16]) {
    const char *path = std::getenv("AWDL_NETD_ADDR_FILE");
    if (!path || !*path) path = "awdl_netd.addr";
    FILE *f = std::fopen(path, "w");
    if (!f) return;
    char text[64] = {0};
    inet_ntop(AF_INET6, address, text, sizeof(text));
    std::fprintf(f, "%s\n", text);
    std::fclose(f);
}

void deliver_ip_packet(const uint8_t *packet, u16_t len) {
    ++g_stats.rx_seen;
    g_stats.rx_bytes += len;
    pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (!p) {
        ++g_stats.alloc_fail;
        tcp_diag::observe(packet, len, true, false);
        return;
    }
    if (pbuf_take(p, packet, len) != ERR_OK) {
        ++g_stats.copy_fail;
        tcp_diag::observe(packet, len, true, false);
        pbuf_free(p);
        return;
    }
    // tcpip_input() reports ERR_MEM when its inbound message pool or mailbox
    // is momentarily full and leaves the pbuf owned by us. A drop here is
    // self-inflicted loss: the radio already delivered the frame and only the
    // phone's retransmission can recover it. Wait briefly instead; this reader
    // thread is the only producer, so blocking it pushes back on awdld.
    for (int waited = 0;; waited += kInputRetrySleepMs) {
        const err_t err = g_netif.input(p, &g_netif);
        if (err == ERR_OK) {
            ++g_stats.rx_queued; // Queued to lwIP, not proof of TCP/file delivery.
            tcp_diag::observe(packet, len, true, true);
            return;
        }
        if (err != ERR_MEM || g_ipc_failed.load() || waited >= kInputRetryLimitMs) {
            ++g_stats.input_fail;
            tcp_diag::observe(packet, len, true, false);
            pbuf_free(p);
            return;
        }
        ++g_stats.input_retry;
        std::this_thread::sleep_for(std::chrono::milliseconds(kInputRetrySleepMs));
    }
}

void ipc_reader() {
    std::vector<uint8_t> buffer;
    uint8_t chunk[4096];
    for (;;) {
        int n = recv(g_ipc, reinterpret_cast<char *>(chunk), sizeof(chunk), 0);
        if (n <= 0) {
            const int error = n < 0 ? WSAGetLastError() : 0;
            std::fprintf(stderr, "awdl_netd: IPC read ended pending_bytes=%zu\n", buffer.size());
            ipc_failed("recv", error);
            return;
        }
        buffer.insert(buffer.end(), chunk, chunk + n);
        for (;;) {
            if (buffer.size() < 2) break;
            size_t len = buffer[0] | (static_cast<size_t>(buffer[1]) << 8);
            if (len < 40) { // Raw IPv6 must contain its fixed header.
                ipc_failed("invalid_frame_length", static_cast<int>(len));
                return;
            }
            if (buffer.size() < 2 + len) break;
            deliver_ip_packet(buffer.data() + 2, static_cast<u16_t>(len));
            buffer.erase(buffer.begin(), buffer.begin() + 2 + len);
        }
    }
}

bool accept_awdld() {
    sockaddr_in addr{};
    int len = sizeof(addr);
    g_ipc = accept(g_listen, reinterpret_cast<sockaddr *>(&addr), &len);
    if (g_ipc == INVALID_SOCKET) return false;
    if (setsockopt(g_ipc, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char *>(&kIpcSendTimeoutMs), sizeof(kIpcSendTimeoutMs)) != 0)
        return false;
    uint8_t hello[20];
    size_t got = 0;
    while (got < sizeof(hello)) {
        int n = recv(g_ipc, reinterpret_cast<char *>(hello + got),
                     static_cast<int>(sizeof(hello) - got), 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    if (memcmp(hello, "AWDL", 4) != 0) return false;
    configure_address(hello + 4);
    write_address_file(hello + 4);
    return true;
}

}  // namespace

#ifndef AIRBRIDGE_NETD_TEST
int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) return 1;
    BOOL exclusive = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kIpcPort);
    if (bind(g_listen, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        listen(g_listen, 1) != 0) {
        std::fprintf(stderr, "awdl_netd: cannot listen on %u\n", kIpcPort);
        return 1;
    }
    std::fprintf(stderr, "awdl_netd: waiting for awdld on 127.0.0.1:%u\n",
                 kIpcPort);

    tcpip_init(nullptr, nullptr);
    netif_add(&g_netif, nullptr, awdl_netif_init, tcpip_input);
    netif_set_hostname(&g_netif, "AirBridge32");
    netif_set_default(&g_netif);

    if (!accept_awdld()) {
        std::fprintf(stderr, "awdl_netd: awdld handshake failed\n");
        return 1;
    }
    netif_set_up(&g_netif);
    netif_set_link_up(&g_netif);
    std::thread(ipc_reader).detach();

    if (std::getenv("AWDL_NETD_NATIVE_MDNS")) {
        awdl_netd_start_native_mdns(g_address, g_netif.num);
    }

    if (std::getenv("AWDL_NETD_MDNS_PROXY")) {
        if (!awdl_netd_start_mdns_proxy()) return 1;
    }
    awdl_netd_start_tcp_proxy();
    std::fprintf(stderr, "awdl_netd: AWDL data plane up\n");
    auto next_stats = GetTickCount64() + 5000;
    while (!g_ipc_failed.load()) {
        Sleep(100);
        if (GetTickCount64() >= next_stats) {
            print_stats();
            next_stats = GetTickCount64() + 5000;
        }
    }
    print_stats();
    std::fprintf(stderr, "awdl_netd: data plane failed; exiting 2\n");
    std::fflush(stderr);
    // Workers are detached; do not destroy shared globals underneath them.
    std::_Exit(2);
}
#endif
