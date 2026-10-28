#include "peer_telemetry.h"
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    PeerTelemetry telemetry;
    const std::array<uint8_t, 6> own{2, 3, 4, 5, 6, 7};
    std::vector<uint8_t> frame(100, 0);
    frame[0] = 0x88; // QoS data
    std::memcpy(frame.data() + 4, own.data(), own.size());
    Packet p{};
    p.Data = frame;
    p.RxAtrib.pkt_rpt_type = RX_PACKET_TYPE::NORMAL_RX;
    p.RxAtrib.physt = true;
    p.RxAtrib.drvinfo_sz = 32;
    p.RxAtrib.rssi[0] = 70;
    telemetry.observe(p, own);
    p.RxAtrib.physt = false; // no measurement, NOT zero signal
    p.RxAtrib.rssi[0] = 0;
    telemetry.observe(p, own);
    frame[0] = 0xd0; // action frame is not upload payload
    telemetry.observe(p, own);
    frame[0] = 0xc8; // QoS null is not upload payload
    telemetry.observe(p, own);
    frame[0] = 0x88;
    frame[4] ^= 1; // multicast/another destination
    telemetry.observe(p, own);
    frame[4] ^= 1;
    p.RxAtrib.pkt_rpt_type = RX_PACKET_TYPE::C2H_PACKET;
    telemetry.observe(p, own);
    auto sample = telemetry.take();
    if (sample.frames != 2 || sample.bytes != 200 ||
        sample.signal_samples != 1 || sample.rssi_sum != 70) return 1;
    p.RxAtrib.pkt_rpt_type = RX_PACKET_TYPE::NORMAL_RX;
    // Sampling while RX runs must not lose increments at a window boundary.
    std::thread producer([&] { for (int i = 0; i < 10000; ++i) telemetry.observe(p, own); });
    uint64_t total = 0;
    for (int i = 0; i < 1000; ++i) total += telemetry.take().frames;
    producer.join();
    total += telemetry.take().frames;
    if (total != 10000) return 2;
    std::puts("PASS: payload classification, absent PHY report, concurrent window sampling");
}
