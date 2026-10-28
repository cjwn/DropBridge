#include "tcp_diag.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool ok, const char *what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static void put16(std::vector<uint8_t> &v, size_t p, uint16_t n) { v[p] = uint8_t(n >> 8); v[p + 1] = uint8_t(n); }
static void put32(std::vector<uint8_t> &v, size_t p, uint32_t n) { put16(v, p, uint16_t(n >> 16)); put16(v, p + 2, uint16_t(n)); }
static std::vector<uint8_t> packet(uint32_t seq, uint32_t ack, uint8_t flags, size_t bytes = 0, bool rx = true) {
    std::vector<uint8_t> v(60 + bytes);
    v[0] = 0x60; v[6] = 6;
    put16(v, 4, uint16_t(v.size() - 40));
    v[23] = rx ? 2 : 1; v[39] = rx ? 1 : 2;
    put16(v, 40, rx ? 50000 : 8771); put16(v, 42, rx ? 8771 : 50000);
    put32(v, 44, seq); put32(v, 48, ack);
    v[52] = 0x50; v[53] = flags; put16(v, 54, 1000);
    return v;
}
int main() {
    using namespace tcp_diag;
    Packet p;
    auto v = packet(100, 0, 2);
    v.insert(v.end(), {3, 3, 20, 0}); v[52] = 0x60; put16(v, 4, 24);
    require(parse(v.data(), v.size(), true, p) == Kind::Tcp && p.scale == 14, "SYN window scale clamped to 14");
    const Key key = p.key;
    require(parse(v.data(), 39, true, p) == Kind::Malformed, "short IPv6");
    require(parse(v.data(), v.size() - 1, true, p) == Kind::Malformed, "truncated advertised body");
    v[61] = 0;
    require(parse(v.data(), v.size(), true, p) == Kind::Malformed, "zero length TCP option");
    v = packet(100, 0, 2);
    v.insert(v.begin() + 40, {6, 0, 0, 0, 0, 0, 0, 0}); v[6] = 60; put16(v, 4, 28);
    require(parse(v.data(), v.size(), true, p) == Kind::Tcp && p.seq == 100, "IPv6 destination options");
    v[6] = 44;
    require(parse(v.data(), v.size(), true, p) == Kind::Fragment, "fragments explicitly skipped");
    v[6] = 50;
    require(parse(v.data(), v.size(), true, p) == Kind::Unsupported, "ESP explicitly skipped");
    v = packet(0, 0, 16); v[52] = 0x40;
    require(parse(v.data(), v.size(), true, p) == Kind::Malformed, "TCP offset below minimum");
    v[52] = 0xf0;
    require(parse(v.data(), v.size(), true, p) == Kind::Malformed, "TCP offset beyond packet");
    std::puts("PASS: bounded IPv6/TCP/options parser, extensions and explicit fragment/ESP limits");

    auto observe_packet = [](std::vector<uint8_t> bytes, bool rx = true, bool accepted = true) {
        observe(bytes.data(), bytes.size(), rx, accepted);
    };
    observe_packet(packet(0xfffffff0u, 0, 2));
    auto id = proxy_open(key);
    observe_packet(packet(0xfffffff1u, 50, 16, 32)); // Cross the 32-bit sequence wrap.
    observe_packet(packet(0xfffffff1u, 50, 16, 32), true, false); // Duplicate/overlap, not unique bytes.
    observe_packet(packet(40, 50, 16, 5)); // Gap relative to high water 17.
    observe_packet(packet(17, 50, 16, 23)); // Late fill: overlap heuristic, not proof of retransmission.
    auto response = packet(50, 45, 16, 0, false);
    put16(response, 54, 0);
    observe_packet(response, false);
    observe_packet(response, false, false);
    proxy_phase(id, true, "local_send");
    proxy_bytes(id, true, false, 60); proxy_bytes(id, true, true, 40);
    proxy_bytes(id, false, false, 10); proxy_bytes(id, false, true, 8);
    auto snap = snapshot();
    const auto f = snap.flows[0];
    require(f.rx.packets == 5 && f.rx.payload == 92 && f.rx.accepted == 4 && f.rx.failed == 1, "wire bytes include duplicates, queue outcomes separated");
    require(f.rx.next_seq == 45 && f.rx.gap == 1 && f.rx.overlap == 2, "sequence wrap, gap and overlap indications");
    require(f.tx.packets == 2 && f.tx.zero_window == 2 && f.tx.repeat_ack == 1 && f.tx.failed == 1, "TX window/ACK and send outcomes");
    require(f.up_read == 60 && f.up_written == 40 && f.down_read == 10 && f.down_written == 8, "proxy read/write boundaries independent");
    proxy_close(id);
    observe_packet(packet(1000, 0, 2));
    require(snapshot().flows[0].id != id && snapshot().flows[0].rx.packets == 1, "new SYN resets inactive tuple generation");
    std::puts("PASS: wrap-safe gap/overlap hints, repeated ACK/raw zero window, generations and four proxy counters");

    // Hold all rows active; an extra flow cannot evict an active connection.
    for (size_t i = 0; i < kFlowLimit; ++i) {
        auto k = key; k.remote_port = uint16_t(1000 + i);
        require(proxy_open(k) != 0, "allocate bounded active row");
    }
    auto extra = key; extra.remote_port = 999;
    require(proxy_open(extra) == 0 && snapshot().capacity_skips == 1, "capacity is explicit and active rows survive");
    std::puts("PASS: bounded 32-flow storage; no active-flow eviction");
    print();
}
