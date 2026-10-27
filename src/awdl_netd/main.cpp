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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" void awdl_netd_start_tcp_proxy();
extern "C" bool awdl_netd_start_mdns_proxy();
extern "C" void awdl_netd_start_native_mdns(const uint8_t address[16],
                                             uint8_t ifindex);

namespace {

constexpr uint16_t kIpcPort = 49877;
constexpr size_t kMaxFrame = 2048;

SOCKET g_ipc = INVALID_SOCKET;
SOCKET g_listen = INVALID_SOCKET;
netif g_netif;
uint8_t g_address[16] = {0};

bool send_all(SOCKET s, const uint8_t *data, size_t len) {
    while (len > 0) {
        int n = ::send(s, reinterpret_cast<const char *>(data),
                       static_cast<int>(len), 0);
        if (n <= 0) return false;
        data += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool ipc_send_frame(const uint8_t *data, size_t len) {
    if (g_ipc == INVALID_SOCKET || len == 0 || len > kMaxFrame) return false;
    uint8_t header[2] = {static_cast<uint8_t>(len & 0xff),
                         static_cast<uint8_t>((len >> 8) & 0xff)};
    return send_all(g_ipc, header, sizeof(header)) &&
           send_all(g_ipc, data, len);
}

err_t netif_output_ip6(struct netif *, struct pbuf *p, const ip6_addr_t *) {
    if (!p || p->tot_len == 0 || p->tot_len > kMaxFrame) return ERR_VAL;
    uint8_t buffer[kMaxFrame];
    pbuf_copy_partial(p, buffer, p->tot_len, 0);
    ipc_send_frame(buffer, p->tot_len);
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

void ipc_reader() {
    std::vector<uint8_t> buffer;
    uint8_t chunk[4096];
    for (;;) {
        int n = recv(g_ipc, reinterpret_cast<char *>(chunk), sizeof(chunk), 0);
        if (n <= 0) return;
        buffer.insert(buffer.end(), chunk, chunk + n);
        for (;;) {
            if (buffer.size() < 2) break;
            size_t len = buffer[0] | (static_cast<size_t>(buffer[1]) << 8);
            if (buffer.size() < 2 + len) break;
            pbuf *p = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(len),
                                 PBUF_POOL);
            if (p) {
                pbuf_take(p, buffer.data() + 2, static_cast<u16_t>(len));
                if (g_netif.input(p, &g_netif) != ERR_OK) pbuf_free(p);
            }
            buffer.erase(buffer.begin(), buffer.begin() + 2 + len);
        }
    }
}

bool accept_awdld() {
    sockaddr_in addr{};
    int len = sizeof(addr);
    g_ipc = accept(g_listen, reinterpret_cast<sockaddr *>(&addr), &len);
    if (g_ipc == INVALID_SOCKET) return false;
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
    for (;;) Sleep(1000);
    return 0;
}
