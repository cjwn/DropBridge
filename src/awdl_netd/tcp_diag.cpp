#include "tcp_diag.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace tcp_diag {
namespace {
uint16_t be16(const uint8_t *p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t *p) { return (uint32_t(be16(p)) << 16) | be16(p + 2); }
bool after(uint32_t a, uint32_t b) { return a != b && uint32_t(a - b) < 0x80000000u; }
std::mutex mutex;
Snapshot state;
uint64_t next_id = 0;

Flow *find(const Key &key, uint64_t now) {
    for (auto &f : state.flows) if (f.id && f.key == key) return &f;
    Flow *slot = nullptr;
    for (auto &f : state.flows) {
        if (!f.id) { slot = &f; break; }
        if (!f.active && (!slot || f.touched < slot->touched)) slot = &f;
    }
    if (!slot) { ++state.capacity_skips; return nullptr; }
    if (slot->id) ++state.evicted;
    *slot = Flow{};
    slot->key = key;
    slot->id = ++next_id;
    slot->touched = now;
    return slot;
}
Flow *by_id(uint64_t id) {
    if (id) for (auto &f : state.flows) if (f.id == id) return &f;
    return nullptr;
}
long long age(uint64_t now, uint64_t then) {
    return then ? static_cast<long long>(now - then) : -1;
}
} // namespace

Kind parse(const uint8_t *data, size_t len, bool incoming, Packet &out) {
    out = Packet{};
    if (!data || len < 40 || (data[0] >> 4) != 6) return Kind::Malformed;
    const size_t payload = be16(data + 4);
    if (!payload) return data[6] == 59 ? Kind::Other : Kind::Unsupported; // No jumbograms.
    const size_t end = 40 + payload;
    if (end > len) return Kind::Malformed;
    size_t pos = 40;
    uint8_t next = data[6];
    // IPv6 extension lengths are bounded by the advertised packet boundary.
    while (next != 6) {
        if (next == 44) return end - pos >= 8 ? Kind::Fragment : Kind::Malformed;
        if (next == 50) return Kind::Unsupported; // Encrypted ESP cannot be inspected.
        if (next != 0 && next != 43 && next != 60 && next != 51) return Kind::Other;
        if (end - pos < 2) return Kind::Malformed;
        const size_t size = (size_t(data[pos + 1]) + (next == 51 ? 2 : 1)) * (next == 51 ? 4 : 8);
        if (size > end - pos) return Kind::Malformed;
        next = data[pos];
        pos += size;
    }
    if (end - pos < 20) return Kind::Malformed;
    const uint8_t *tcp = data + pos;
    const size_t header = size_t(tcp[12] >> 4) * 4;
    if (header < 20 || header > end - pos) return Kind::Malformed;
    out.seq = be32(tcp + 4);
    out.ack = be32(tcp + 8);
    out.flags = tcp[13];
    out.window = be16(tcp + 14);
    out.payload = static_cast<uint16_t>(end - pos - header);
    std::copy_n(data + (incoming ? 8 : 24), 16, out.key.remote.begin());
    std::copy_n(data + (incoming ? 24 : 8), 16, out.key.local.begin());
    out.key.remote_port = be16(tcp + (incoming ? 0 : 2));
    out.key.local_port = be16(tcp + (incoming ? 2 : 0));
    for (size_t i = 20; i < header;) {
        const uint8_t kind = tcp[i];
        if (!kind) break;
        if (kind == 1) { ++i; continue; }
        if (header - i < 2 || tcp[i + 1] < 2 || tcp[i + 1] > header - i) return Kind::Malformed;
        if (kind == 3) {
            if (tcp[i + 1] != 3) return Kind::Malformed;
            if (out.flags & 2) out.scale = std::min<int>(tcp[i + 2], 14);
        }
        i += tcp[i + 1];
    }
    return Kind::Tcp;
}

uint64_t now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void observe(const uint8_t *data, size_t len, bool incoming, bool accepted) {
    Packet p;
    const auto kind = parse(data, len, incoming, p);
    if (kind == Kind::Other) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (kind == Kind::Malformed) { ++state.malformed; return; }
    if (kind == Kind::Fragment) { ++state.fragments; return; }
    if (kind == Kind::Unsupported) { ++state.unsupported; return; }
    const auto now = now_ms();
    auto *f = find(p.key, now);
    if (!f) return;
    auto &s = incoming ? f->rx : f->tx;
    // A different initial sequence on an inactive tuple starts a new generation.
    if ((p.flags & 2) && s.have_syn && s.syn_seq != p.seq && !f->active) {
        const auto key = f->key;
        *f = Flow{};
        f->key = key;
        f->id = ++next_id;
    }
    f->touched = now;
    f->dirty = true;
    ++s.packets;
    s.payload += p.payload; // Includes repeated wire bytes; never call this unique file bytes.
    accepted ? ++s.accepted : ++s.failed;
    if (p.flags & 2) { ++s.syn; s.have_syn = true; s.syn_seq = p.seq; s.scale = p.scale; }
    if (p.flags & 1) ++s.fin;
    if (p.flags & 4) ++s.rst;
    if (!p.window) ++s.zero_window;
    const uint32_t span = p.payload + ((p.flags & 2) ? 1u : 0u) + ((p.flags & 1) ? 1u : 0u);
    if (span) {
        if (s.have_seq) {
            if (after(p.seq, s.next_seq)) ++s.gap;
            if (after(s.next_seq, p.seq)) ++s.overlap;
        }
        const uint32_t end = p.seq + span;
        if (!s.have_seq || after(end, s.next_seq)) s.next_seq = end;
        s.have_seq = true;
    }
    if (p.payload) s.last_data = now;
    if (p.flags & 16) {
        if (!s.have_ack || after(p.ack, s.ack_high)) {
            s.last_ack_progress = now;
            s.ack_high = p.ack;
        }
        if (s.have_ack && p.ack == s.ack && p.window == s.window && !span && !(p.flags & 4)) ++s.repeat_ack;
        s.have_ack = true;
        s.ack = p.ack;
    }
    s.seq = p.seq;
    s.window = p.window;
}

uint64_t proxy_open(const Key &key) {
    std::lock_guard<std::mutex> lock(mutex);
    auto *f = find(key, now_ms());
    if (!f) return 0;
    f->active = f->dirty = true;
    f->up_phase = "connect";
    f->down_phase = "connect";
    return f->id;
}
void proxy_phase(uint64_t id, bool up, const char *phase) {
    std::lock_guard<std::mutex> lock(mutex);
    if (auto *f = by_id(id)) {
        (up ? f->up_phase : f->down_phase) = phase;
        f->dirty = true;
    }
}
void proxy_bytes(uint64_t id, bool up, bool written, size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    if (auto *f = by_id(id)) {
        auto &count = up ? (written ? f->up_written : f->up_read) : (written ? f->down_written : f->down_read);
        count += bytes;
        f->touched = now_ms();
        f->dirty = true;
    }
}
void proxy_close(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    if (auto *f = by_id(id)) { f->active = false; f->dirty = true; f->touched = now_ms(); }
}
Snapshot snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return state;
}
void print() {
    Snapshot copy;
    {
        std::lock_guard<std::mutex> lock(mutex);
        copy = state;
        for (auto &f : state.flows) f.dirty = false;
    }
    const auto now = now_ms();
    std::fprintf(stderr, "awdl_netd: tcp diag t_ms=%llu malformed=%llu fragments=%llu unsupported=%llu capacity_skips=%llu evicted=%llu\n",
        now, copy.malformed, copy.fragments, copy.unsupported, copy.capacity_skips, copy.evicted);
    for (const auto &f : copy.flows) {
        if (!f.id || (!f.dirty && !f.active)) continue;
        for (bool incoming : {true, false}) {
            const auto &s = incoming ? f.rx : f.tx;
            std::fprintf(stderr, "awdl_netd: tcp flow=%llu t_ms=%llu dir=%s remote_port=%u local_port=%u "
                "packets=%llu payload=%llu accepted=%llu failed=%llu seq=%u ack=%u win_raw=%u ws_offer=%d "
                "syn=%llu fin=%llu rst=%llu zero_window=%llu gap=%llu overlap=%llu repeat_ack=%llu "
                "data_idle_ms=%lld ack_idle_ms=%lld\n",
                f.id, now, incoming ? "rx" : "tx", f.key.remote_port, f.key.local_port,
                s.packets, s.payload, s.accepted, s.failed, s.seq, s.ack, s.window, s.scale,
                s.syn, s.fin, s.rst, s.zero_window, s.gap, s.overlap, s.repeat_ack,
                age(now, s.last_data), age(now, s.last_ack_progress));
        }
        std::fprintf(stderr, "awdl_netd: proxy flow=%llu t_ms=%llu active=%d up_read=%llu up_written=%llu "
            "down_read=%llu down_written=%llu up_phase=%s down_phase=%s\n",
            f.id, now, int(f.active), f.up_read, f.up_written, f.down_read, f.down_written, f.up_phase, f.down_phase);
    }
}
} // namespace tcp_diag
