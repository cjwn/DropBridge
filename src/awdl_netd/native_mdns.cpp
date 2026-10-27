/* Native AirDrop mDNS responder for awdl_netd.
 *
 * The record set mirrors upstream/winawdl/airdrop/mdns.py exactly:
 *   PTR  _airdrop._tcp.local -> <id>._airdrop._tcp.local
 *   SRV  <id>._airdrop._tcp.local -> <id>.local:8771
 *   TXT  <id>._airdrop._tcp.local -> flags=0x3fb
 *   AAAA <id>.local -> AWDL link-local
 *   NSEC <id>.local -> AAAA/NSEC
 * It runs on lwIP sockets and does not touch the Windows IPv6 stack.
 */
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "lwip/sockets.h"
}

namespace {

constexpr uint16_t kPort = 8771;
constexpr uint16_t kMdnsPort = 5353;

void put16(std::vector<uint8_t> &out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void put32(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

std::vector<uint8_t> encode_name(const std::string &name) {
    std::vector<uint8_t> out;
    size_t start = 0;
    while (start < name.size()) {
        size_t dot = name.find('.', start);
        if (dot == std::string::npos) dot = name.size();
        const size_t len = dot - start;
        out.push_back(static_cast<uint8_t>(len));
        out.insert(out.end(), name.begin() + start, name.begin() + dot);
        start = dot + 1;
    }
    out.push_back(0);
    return out;
}

void append_record(std::vector<uint8_t> &out, const std::string &owner,
                   uint16_t type, uint16_t klass, uint32_t ttl,
                   const std::vector<uint8_t> &rdata) {
    const auto name = encode_name(owner);
    out.insert(out.end(), name.begin(), name.end());
    put16(out, type);
    put16(out, klass);
    put32(out, ttl);
    put16(out, static_cast<uint16_t>(rdata.size()));
    out.insert(out.end(), rdata.begin(), rdata.end());
}

std::string id_from_address(const uint8_t address[16]) {
    char id[13];
    std::snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", address[8],
                  address[9], address[10], address[11], address[12],
                  address[13]);
    return id;
}

std::vector<uint8_t> build_response(const uint8_t address[16]) {
    const std::string id = id_from_address(address);
    const std::string service = "_airdrop._tcp.local";
    const std::string instance = id + "." + service;
    const std::string host = id + ".local";

    std::vector<uint8_t> out = {0, 0, 0x84, 0, 0, 0, 0, 5, 0, 0, 0, 0};

    std::vector<uint8_t> ptr = encode_name(instance);
    append_record(out, service, 12, 1, 4500, ptr);

    std::vector<uint8_t> srv;
    put16(srv, 0);
    put16(srv, 0);
    put16(srv, kPort);
    auto target = encode_name(host);
    srv.insert(srv.end(), target.begin(), target.end());
    append_record(out, instance, 33, 0x8001, 120, srv);

    const char *txt_text = "flags=0x3fb";
    std::vector<uint8_t> txt;
    txt.push_back(static_cast<uint8_t>(std::strlen(txt_text)));
    txt.insert(txt.end(), txt_text, txt_text + std::strlen(txt_text));
    append_record(out, instance, 16, 0x8001, 4500, txt);

    append_record(out, host, 28, 0x8001, 120,
                  std::vector<uint8_t>(address, address + 16));

    std::vector<uint8_t> nsec = encode_name(host);
    nsec.insert(nsec.end(), {0, 6, 0, 0, 0, 0x08, 0, 0x01});
    append_record(out, host, 47, 0x8001, 120, nsec);
    return out;
}

bool looks_like_query(const uint8_t *data, int len) {
    if (len < 12) return false;
    const uint16_t flags = static_cast<uint16_t>((data[2] << 8) | data[3]);
    if (flags & 0x8000) return false;  // a response
    const uint16_t qd = static_cast<uint16_t>((data[4] << 8) | data[5]);
    if (qd == 0) return false;
    const char needle[] = "_airdrop";
    for (int i = 12; i + static_cast<int>(sizeof(needle)) <= len; i++) {
        if (std::memcmp(data + i, needle, sizeof(needle) - 1) == 0) return true;
    }
    return false;
}

}  // namespace

extern "C" void awdl_netd_start_native_mdns(const uint8_t address[16]) {
    std::thread([address] {
        std::vector<uint8_t> addr(address, address + 16);
        int fd = lwip_socket(AF_INET6, SOCK_DGRAM, 0);
        if (fd < 0) {
            std::fprintf(stderr, "awdl_netd: native mDNS socket failed\n");
            return;
        }
        sockaddr_in6 bind_addr{};
        bind_addr.sin6_family = AF_INET6;
        bind_addr.sin6_port = htons(kMdnsPort);
        if (lwip_bind(fd, reinterpret_cast<sockaddr *>(&bind_addr),
                      sizeof(bind_addr)) != 0) {
            std::fprintf(stderr, "awdl_netd: native mDNS bind failed\n");
            lwip_close(fd);
            return;
        }
        ipv6_mreq group{};
        inet_pton(AF_INET6, "ff02::fb", &group.ipv6mr_multiaddr);
        group.ipv6mr_interface = 1;
        lwip_setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group,
                        sizeof(group));
        int nonblocking = 1;
        lwip_ioctl(fd, FIONBIO, &nonblocking);

        sockaddr_in6 dst{};
        dst.sin6_family = AF_INET6;
        dst.sin6_port = htons(kMdnsPort);
        dst.sin6_scope_id = 1;
        inet_pton(AF_INET6, "ff02::fb", &dst.sin6_addr);

        const std::vector<uint8_t> response = build_response(addr.data());
        lwip_sendto(fd, response.data(), response.size(), 0,
                    reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
        std::fprintf(stderr, "awdl_netd: native mDNS up for %s.local\n",
                     id_from_address(addr.data()).c_str());

        auto now_ms = [] {
            return static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        };
        int64_t last_send = now_ms();
        for (;;) {
            uint8_t packet[4096];
            sockaddr_in6 from{};
            socklen_t from_len = sizeof(from);
            int n = lwip_recvfrom(fd, packet, sizeof(packet), 0,
                                  reinterpret_cast<sockaddr *>(&from),
                                  &from_len);
            if (n > 0 && looks_like_query(packet, n)) {
                lwip_sendto(fd, response.data(), response.size(), 0,
                            reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
            }
            const int64_t now = now_ms();
            if (now - last_send >= 2000) {
                lwip_sendto(fd, response.data(), response.size(), 0,
                            reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
                last_send = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
}
