#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Metadata only: no TCP payload, TLS records or device addresses are logged.
namespace tcp_diag {
constexpr size_t kFlowLimit = 32;
struct Key {
    std::array<uint8_t, 16> remote{}, local{};
    uint16_t remote_port = 0, local_port = 0;
    bool operator==(const Key &) const = default;
};
enum class Kind { Other, Tcp, Malformed, Fragment, Unsupported };
struct Packet {
    Key key;
    uint32_t seq = 0, ack = 0;
    uint16_t window = 0, payload = 0;
    uint8_t flags = 0;
    int scale = -1;
};
Kind parse(const uint8_t *data, size_t len, bool incoming, Packet &out);
struct Side {
    uint64_t packets = 0, payload = 0, accepted = 0, failed = 0;
    uint64_t syn = 0, fin = 0, rst = 0, zero_window = 0;
    uint64_t gap = 0, overlap = 0, repeat_ack = 0;
    uint64_t last_data = 0, last_ack_progress = 0;
    uint32_t seq = 0, ack = 0, ack_high = 0, next_seq = 0, syn_seq = 0;
    uint16_t window = 0;
    int scale = -1;
    bool have_seq = false, have_ack = false, have_syn = false;
};
struct Flow {
    Key key;
    uint64_t id = 0, touched = 0;
    Side rx, tx;
    bool active = false, dirty = false;
    uint64_t up_read = 0, up_written = 0, down_read = 0, down_written = 0;
    const char *up_phase = "unaccepted", *down_phase = "unaccepted";
};
struct Snapshot {
    std::array<Flow, kFlowLimit> flows{};
    uint64_t malformed = 0, fragments = 0, unsupported = 0, capacity_skips = 0, evicted = 0;
};
uint64_t now_ms();
void observe(const uint8_t *data, size_t len, bool incoming, bool accepted);
uint64_t proxy_open(const Key &key);
void proxy_phase(uint64_t id, bool up, const char *phase);
void proxy_bytes(uint64_t id, bool up, bool written, size_t bytes);
void proxy_close(uint64_t id);
Snapshot snapshot();
void print();
} // namespace tcp_diag
