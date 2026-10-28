#pragma once

#include <array>
#include <cstring>
#include <mutex>
#include "RxPacket.h"

// Only unicast payload addressed to us measures an upload. Management,
// firmware reports and absent PHY reports must not dilute that measurement.
struct PeerSample {
    uint64_t frames = 0, bytes = 0, signal_samples = 0;
    int64_t rssi_sum = 0;
    uint16_t rate = 0;
    uint8_t bw = 0, sgi = 0;
};

class PeerTelemetry {
  public:
    void observe(const Packet &p, const std::array<uint8_t, 6> &own) {
        const auto f = p.Data;
        if (p.RxAtrib.pkt_rpt_type != RX_PACKET_TYPE::NORMAL_RX ||
            p.RxAtrib.crc_err || f.size() < 24 ||
            (f[0] & 0x0c) != 0x08 || (f[0] & 0x40) != 0 ||
            std::memcmp(f.data() + 4, own.data(), own.size()) != 0)
            return;
        std::lock_guard<std::mutex> lock(mu_);
        ++sample_.frames;
        sample_.bytes += f.size();
        sample_.rate = p.RxAtrib.data_rate;
        sample_.bw = p.RxAtrib.bw;
        sample_.sgi = p.RxAtrib.sgi;
        if (p.RxAtrib.physt && p.RxAtrib.drvinfo_sz >= 28 && p.RxAtrib.rssi[0]) {
            ++sample_.signal_samples;
            sample_.rssi_sum += p.RxAtrib.rssi[0];
        }
    }

    PeerSample take() {
        std::lock_guard<std::mutex> lock(mu_);
        const auto result = sample_;
        sample_ = {};
        return result;
    }

  private:
    std::mutex mu_;
    PeerSample sample_;
};
