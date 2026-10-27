/* awdl_netd: user-space AWDL data plane.
 *
 * awdld hands us raw IPv6 packets over a local TCP socket instead of writing
 * them into a Windows Wintun adapter. We terminate IPv6/NDP/TCP/mDNS in lwIP
 * and proxy AirDrop's TCP 8771 to the local receiver, so v2ray TUN (which only
 * sees the Windows IP stack) cannot intercept AWDL traffic.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

extern "C" {
#include "lwip/init.h"
#include "lwip/ip6.h"
#include "lwip/ip6_addr.h"
#include "lwip/netif.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "lwip/apps/mdns.h"
}

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kIpcPort = 49877;
constexpr uint16_t kReceiverPort = 8771;
constexpr size_t kMaxFrame = 2048;
constexpr uint16_t kLocalPort = 8771;

SOCKET g_ipc = INVALID_SOCKET;
SOCKET g_listen = INVALID_SOCKET;
netif g_netif;
std::vector<uint8_t> g_ipc_rx;

struct ProxyConn {
    tcp_pcb *pcb = nullptr;
    SOCKET local = INVALID_SOCKET;
    bool connecting = false;
    bool remote_closed = false;
    bool local_closed = false;
    std::vector<uint8_t> to_local;
    size_t to_local_off = 0;
    std::vector<uint8_t> from_local;
    size_t from_local_off = 0;
};

std::vector<ProxyConn *> g_conns;
bool g_mdns_enabled = false;
u32_t g_next_announce = 0;

SOCKET g_mdns_local = INVALID_SOCKET;
sockaddr_in g_mdns_peer{};
bool g_has_mdns_peer = false;
udp_pcb *g_mdns_pcb = nullptr;

uint64_t g_proxy_rx = 0;
uint64_t g_proxy_local = 0;
uint64_t g_proxy_tx = 0;

void write_address_file(const uint8_t address[16]);

extern "C" u32_t sys_now(void) {
    return static_cast<u32_t>(GetTickCount64());
}

bool send_all(SOCKET s, const uint8_t *data, size_t len) {
    while (len > 0) {
        int n = ::send(s, reinterpret_cast<const char *>(data),
                       static_cast<int>(len), 0);
        if (n > 0) {
            data += n;
            len -= static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            Sleep(1);
            continue;
        }
        return false;
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
    if (std::getenv("AWDL_NETD_DUMP") && p->tot_len >= 48 &&
        buffer[6] == 17) {
        char src[64] = {0};
        char dst[64] = {0};
        inet_ntop(AF_INET6, buffer + 8, src, sizeof(src));
        inet_ntop(AF_INET6, buffer + 24, dst, sizeof(dst));
        const uint16_t sport = (uint16_t)((buffer[40] << 8) | buffer[41]);
        const uint16_t dport = (uint16_t)((buffer[42] << 8) | buffer[43]);
        char text[512];
        size_t ti = 0;
        for (size_t i = 48; i < p->tot_len && ti + 1 < sizeof(text); i++) {
            const uint8_t c = buffer[i];
            text[ti++] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        text[ti] = 0;
        std::fprintf(stderr,
                     "tx udp %s:%u -> %s:%u len=%u flags=%02x%02x qd=%u an=%u "
                     "text=%s\n",
                     src, sport, dst, dport, p->tot_len, buffer[50], buffer[51],
                     (unsigned)((buffer[52] << 8) | buffer[53]),
                     (unsigned)((buffer[54] << 8) | buffer[55]), text);
    }
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

err_t awdl_input(struct pbuf *p, struct netif *netif) {
    return ip6_input(p, netif);
}

void configure_address(const uint8_t address[16]) {
    memcpy(g_netif.hwaddr, address + 8, 6);
    g_netif.hwaddr[0] ^= 0x02;  // EUI-64 -> MAC (flip the U/L bit)

    ip6_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    memcpy(addr.addr, address, 16);
    netif_ip6_addr_set(&g_netif, 0, &addr);
    netif_ip6_addr_set_state(&g_netif, 0, IP6_ADDR_PREFERRED);
}

void mdns_txt(struct mdns_service *service, void *) {
    const char *txt = "flags=0x3fb";
    mdns_resp_add_service_txtitem(service,
                                  const_cast<char *>(txt),
                                  static_cast<u8_t>(strlen(txt)));
}

void close_conn(ProxyConn *conn) {
    if (!conn) return;
    if (conn->local != INVALID_SOCKET) {
        closesocket(conn->local);
        conn->local = INVALID_SOCKET;
    }
    if (conn->pcb) {
        tcp_arg(conn->pcb, nullptr);
        tcp_recv(conn->pcb, nullptr);
        tcp_err(conn->pcb, nullptr);
        tcp_poll(conn->pcb, nullptr, 0);
        tcp_sent(conn->pcb, nullptr);
        tcp_close(conn->pcb);
        conn->pcb = nullptr;
    }
    g_conns.erase(std::remove(g_conns.begin(), g_conns.end(), conn),
                  g_conns.end());
    delete conn;
}

err_t proxy_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    auto *conn = static_cast<ProxyConn *>(arg);
    if (!conn) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        if (p) pbuf_free(p);
        close_conn(conn);
        return ERR_OK;
    }
    if (!p) {
        conn->remote_closed = true;
        return ERR_OK;
    }
    if (p->tot_len > 0) {
        size_t old = conn->to_local.size();
        conn->to_local.resize(old + p->tot_len);
        pbuf_copy_partial(p, conn->to_local.data() + old, p->tot_len, 0);
        tcp_recved(pcb, p->tot_len);
        g_proxy_rx += p->tot_len;
        if (std::getenv("AWDL_NETD_TRACE") && (g_proxy_rx % 32768) < p->tot_len) {
            std::fprintf(stderr, "proxy rx=%llu local=%llu tx=%llu\n",
                         (unsigned long long)g_proxy_rx,
                         (unsigned long long)g_proxy_local,
                         (unsigned long long)g_proxy_tx);
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

void proxy_err(void *arg, err_t) {
    close_conn(static_cast<ProxyConn *>(arg));
}

err_t proxy_poll(void *arg, struct tcp_pcb *) {
    auto *conn = static_cast<ProxyConn *>(arg);
    if (!conn) return ERR_OK;
    if ((conn->remote_closed || conn->local_closed) &&
        conn->to_local_off >= conn->to_local.size() &&
        conn->from_local_off >= conn->from_local.size()) {
        close_conn(conn);
    }
    return ERR_OK;
}

err_t proxy_accept(void *, struct tcp_pcb *newpcb, err_t) {
    auto *conn = new ProxyConn();
    conn->pcb = newpcb;
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, proxy_recv);
    tcp_err(newpcb, proxy_err);
    tcp_poll(newpcb, proxy_poll, 2);
    tcp_nagle_disable(newpcb);

    conn->local = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn->local == INVALID_SOCKET) {
        close_conn(conn);
        return ERR_ABRT;
    }
    u_long nonblocking = 1;
    ioctlsocket(conn->local, FIONBIO, &nonblocking);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kReceiverPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int rc = connect(conn->local, reinterpret_cast<sockaddr *>(&addr),
                     sizeof(addr));
    if (rc == 0) {
        conn->connecting = false;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        conn->connecting = true;
    } else {
        close_conn(conn);
        return ERR_ABRT;
    }
    g_conns.push_back(conn);
    return ERR_OK;
}

void pump_proxy(ProxyConn *conn) {
    if (!conn || conn->local == INVALID_SOCKET) return;

    if (conn->connecting) {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(conn->local, &writefds);
        timeval tv{0, 0};
        int rc = select(0, nullptr, &writefds, nullptr, &tv);
        if (rc <= 0) return;
        int error = 0;
        int len = sizeof(error);
        getsockopt(conn->local, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char *>(&error), &len);
        if (error != 0) {
            close_conn(conn);
            return;
        }
        conn->connecting = false;
    }

    while (conn->to_local_off < conn->to_local.size()) {
        int n = ::send(conn->local,
                       reinterpret_cast<const char *>(conn->to_local.data() +
                                                      conn->to_local_off),
                       static_cast<int>(conn->to_local.size() -
                                        conn->to_local_off),
                       0);
        if (n > 0) {
            conn->to_local_off += static_cast<size_t>(n);
            g_proxy_local += n;
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
        conn->local_closed = true;
        break;
    }
    if (conn->to_local_off >= conn->to_local.size()) {
        conn->to_local.clear();
        conn->to_local_off = 0;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(conn->local, &readfds);
    timeval tv{0, 0};
    int rc = select(0, &readfds, nullptr, nullptr, &tv);
    if (rc > 0 && FD_ISSET(conn->local, &readfds)) {
        uint8_t buffer[16384];
        int n = recv(conn->local, reinterpret_cast<char *>(buffer),
                     sizeof(buffer), 0);
        if (n > 0) {
            size_t old = conn->from_local.size();
            conn->from_local.resize(old + static_cast<size_t>(n));
            memcpy(conn->from_local.data() + old, buffer,
                   static_cast<size_t>(n));
            g_proxy_tx += n;
        } else if (n == 0) {
            conn->local_closed = true;
        } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
            conn->local_closed = true;
        }
    }

    while (conn->from_local_off < conn->from_local.size() && conn->pcb) {
        u16_t space = tcp_sndbuf(conn->pcb);
        if (space == 0) break;
        size_t remaining = conn->from_local.size() - conn->from_local_off;
        size_t amount = std::min<size_t>(remaining, space);
        err_t err = tcp_write(conn->pcb,
                              conn->from_local.data() + conn->from_local_off,
                              static_cast<u16_t>(amount), TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            conn->from_local_off += amount;
            tcp_output(conn->pcb);
        } else if (err == ERR_MEM) {
            break;
        } else {
            close_conn(conn);
            return;
        }
    }
    if (conn->from_local_off >= conn->from_local.size()) {
        conn->from_local.clear();
        conn->from_local_off = 0;
    }

    if (conn->local_closed && conn->from_local.empty() && conn->pcb) {
        tcp_shutdown(conn->pcb, 0, 1);
    }
}

void ipc_feed(const uint8_t *data, size_t len) {
    if (len == 0 || len > kMaxFrame) return;
    if (std::getenv("AWDL_NETD_DUMP") && len >= 48 && data[6] == 17) {
        const uint16_t sport = (uint16_t)((data[40] << 8) | data[41]);
        const uint16_t dport = (uint16_t)((data[42] << 8) | data[43]);
        if (sport == 5353 || dport == 5353) {
            char src[64] = {0};
            char dst[64] = {0};
            inet_ntop(AF_INET6, data + 8, src, sizeof(src));
            inet_ntop(AF_INET6, data + 24, dst, sizeof(dst));
            char text[512];
            size_t ti = 0;
            for (size_t i = 48; i < len && ti + 1 < sizeof(text); i++) {
                const uint8_t c = data[i];
                text[ti++] = (c >= 32 && c < 127) ? (char)c : '.';
            }
            text[ti] = 0;
            std::fprintf(stderr,
                         "rx udp %s:%u -> %s:%u len=%u flags=%02x%02x text=%s\n",
                         src, sport, dst, dport, (unsigned)len, data[50],
                         data[51], text);
        }
    }
    struct pbuf *p = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(len), PBUF_POOL);
    if (!p) return;
    pbuf_take(p, data, static_cast<u16_t>(len));
    if (g_netif.input(p, &g_netif) != ERR_OK) pbuf_free(p);
}

void ipc_readable() {
    uint8_t buffer[4096];
    int n = recv(g_ipc, reinterpret_cast<char *>(buffer), sizeof(buffer), 0);
    if (n <= 0) return;
    g_ipc_rx.insert(g_ipc_rx.end(), buffer, buffer + n);
    for (;;) {
        if (g_ipc_rx.size() < 2) return;
        size_t len = g_ipc_rx[0] | (static_cast<size_t>(g_ipc_rx[1]) << 8);
        if (g_ipc_rx.size() < 2 + len) return;
        ipc_feed(g_ipc_rx.data() + 2, len);
        g_ipc_rx.erase(g_ipc_rx.begin(), g_ipc_rx.begin() + 2 + len);
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
    u_long nonblocking = 1;
    ioctlsocket(g_ipc, FIONBIO, &nonblocking);
    return true;
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

void mdns_udp_recv(void *, udp_pcb *, struct pbuf *p, const ip_addr_t *,
                   u16_t) {
    if (!p) return;
    if (g_has_mdns_peer) {
        uint8_t buffer[4096];
        if (p->tot_len <= sizeof(buffer)) {
            pbuf_copy_partial(p, buffer, p->tot_len, 0);
            sendto(g_mdns_local, reinterpret_cast<const char *>(buffer),
                   p->tot_len, 0,
                   reinterpret_cast<const sockaddr *>(&g_mdns_peer),
                   sizeof(g_mdns_peer));
        }
    }
    pbuf_free(p);
}

void mdns_local_readable() {
    uint8_t buffer[4096];
    sockaddr_in from{};
    int from_len = sizeof(from);
    int n = recvfrom(g_mdns_local, reinterpret_cast<char *>(buffer),
                     sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from),
                     &from_len);
    if (n <= 0) return;
    g_mdns_peer = from;
    g_has_mdns_peer = true;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(n),
                                PBUF_RAM);
    if (!p) return;
    pbuf_take(p, buffer, static_cast<u16_t>(n));

    ip6_addr_t dst6;
    ip6addr_aton("ff02::fb", &dst6);
    ip_addr_t dst;
    memset(&dst, 0, sizeof(dst));
    memcpy(&dst, &dst6, sizeof(dst6));
    udp_sendto(g_mdns_pcb, p, &dst, 5353);
    pbuf_free(p);
}

bool start_mdns_proxy() {
    g_mdns_local = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_mdns_local == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(15353);
    if (bind(g_mdns_local, reinterpret_cast<sockaddr *>(&addr),
             sizeof(addr)) != 0) {
        std::fprintf(stderr, "awdl_netd: mDNS proxy bind failed (%d)\n",
                     WSAGetLastError());
        return false;
    }
    u_long nonblocking = 1;
    ioctlsocket(g_mdns_local, FIONBIO, &nonblocking);

    g_mdns_pcb = udp_new_ip_type(IPADDR_TYPE_V6);
    if (!g_mdns_pcb) return false;
    ip_addr_t any;
    memset(&any, 0, sizeof(any));
    IP_SET_TYPE_VAL(any, IPADDR_TYPE_V6);
    if (udp_bind(g_mdns_pcb, &any, 5353) != ERR_OK) return false;
    udp_recv(g_mdns_pcb, mdns_udp_recv, nullptr);
    std::fprintf(stderr,
                 "awdl_netd: mDNS proxy 127.0.0.1:15353 <-> ff02::fb:5353\n");
    return true;
}

void start_mdns() {
    static char id[13];
    snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x",
             g_netif.hwaddr[0], g_netif.hwaddr[1], g_netif.hwaddr[2],
             g_netif.hwaddr[3], g_netif.hwaddr[4], g_netif.hwaddr[5]);
    mdns_resp_init();
    err_t netif_result = mdns_resp_add_netif(&g_netif, id);
    s8_t slot = mdns_resp_add_service(&g_netif, id, "_airdrop",
                                      DNSSD_PROTO_TCP, kLocalPort, mdns_txt,
                                      nullptr);
    std::fprintf(stderr, "awdl_netd: mdns id=%s netif=%d service_slot=%d\n",
                 id, (int)netif_result, (int)slot);
    mdns_resp_announce(&g_netif);
    g_mdns_enabled = true;
    g_next_announce = sys_now() + 2000;
}

bool start_tcp_proxy() {
    tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V6);
    if (!pcb) return false;
    ip_addr_t any;
    memset(&any, 0, sizeof(any));
    IP_SET_TYPE_VAL(any, IPADDR_TYPE_V6);
    if (tcp_bind(pcb, &any, kLocalPort) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }
    tcp_pcb *listener = tcp_listen(pcb);
    if (!listener) {
        tcp_close(pcb);
        return false;
    }
    tcp_accept(listener, proxy_accept);
    return true;
}

}  // namespace

int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "awdl_netd: WSAStartup failed\n");
        return 1;
    }

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
    std::printf("awdl_netd: waiting for awdld on 127.0.0.1:%u\n", kIpcPort);

    lwip_init();
    netif_add(&g_netif, nullptr, awdl_netif_init, awdl_input);
    netif_set_hostname(&g_netif, "AirBridge32");
    netif_set_default(&g_netif);

    if (!accept_awdld()) {
        std::fprintf(stderr, "awdl_netd: awdld handshake failed\n");
        return 1;
    }
    std::fprintf(stderr, "awdl_netd: timeout slots=%d\n",
                 (int)MEMP_NUM_SYS_TIMEOUT);
    netif_set_up(&g_netif);
    std::fprintf(stderr, "awdl_netd: netif up\n");
    netif_set_link_up(&g_netif);
    if (std::getenv("AWDL_NETD_MDNS_PROXY")) {
        if (!start_mdns_proxy()) return 1;
    }
    if (std::getenv("AWDL_NETD_TCP") && !start_tcp_proxy()) {
        std::fprintf(stderr, "awdl_netd: cannot listen on TCP 8771\n");
        return 1;
    }
    std::printf("awdl_netd: AWDL data plane up, TCP 8771 -> 127.0.0.1:%u\n",
                kReceiverPort);

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_ipc, &readfds);
        if (g_mdns_local != INVALID_SOCKET) FD_SET(g_mdns_local, &readfds);
        for (auto *conn : g_conns) {
            if (conn->local != INVALID_SOCKET) FD_SET(conn->local, &readfds);
        }
        timeval tv{0, 10 * 1000};
        int rc = select(0, &readfds, nullptr, nullptr, &tv);
        if (rc > 0) {
            if (FD_ISSET(g_ipc, &readfds)) ipc_readable();
            if (g_mdns_local != INVALID_SOCKET &&
                FD_ISSET(g_mdns_local, &readfds)) {
                mdns_local_readable();
            }
            for (auto *conn : g_conns) {
                if (conn->local != INVALID_SOCKET &&
                    FD_ISSET(conn->local, &readfds)) {
                    pump_proxy(conn);
                }
            }
        }
        for (auto *conn : g_conns) pump_proxy(conn);
        sys_check_timeouts();
        if (g_mdns_enabled && (int32_t)(sys_now() - g_next_announce) >= 0) {
            mdns_resp_announce(&g_netif);
            g_next_announce = sys_now() + 2000;
        }
    }

    return 0;
}

