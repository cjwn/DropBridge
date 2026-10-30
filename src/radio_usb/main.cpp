/* airbridge_radio: USB radio backend for AirBridge32.
 *
 * Owns the RTL8821CU through libusb/WinUSB and speaks the radio_link v3
 * protocol (MIT) over a localhost TCP socket. It does not parse AWDL or
 * AirDrop; those live in the Dart application (docs/ARCHITECTURE.md).
 *
 * The transport is TCP rather than a Windows named pipe because Dart's
 * dart:io has no named-pipe API; the framing is unchanged.
 *
 * Protocol: 8-byte little-endian header (type, reserved, length) + payload.
 *   HELLO (radio -> client)   once on connect
 *   RX    (radio -> client)   radiotap + 802.11 frame
 *   TX    (client -> radio)   radiotap + 802.11 frame
 *   SET_CHANNEL (client)      one byte; FastRetune inside one band, full
 *                             SetMonitorChannel across bands
 *   SET_BSSID_FILTER (client) six bytes, filter on addr3
 *   SET_ADDRESS (client)      six bytes, own MAC for hardware ACK
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <libusb.h>

#include "DeviceConfig.h"
#include "IRadio.h"
#include "RadiotapBuilder.h"
#include "RxPacket.h"
#include "SelectedChannel.h"
#include "UsbOpen.h"
#include "WiFiDriver.h"
#include "logger.h"
#include "radio_link.h"
#include "peer_telemetry.h"

namespace {

constexpr int kRssiFloorDbm = -110;
constexpr size_t kRadiotapBytes = 10;
constexpr uint32_t kRadiotapPresent = (1u << 1) | (1u << 5);
constexpr uint16_t kDefaultPort = 49876;

int env_int(const char *name, int fallback) {
    const char *v = std::getenv(name);
    return v ? std::atoi(v) : fallback;
}

bool env_flag(const char *name) {
    const char *v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

uint16_t env_hex16(const char *name, uint16_t fallback) {
    const char *v = std::getenv(name);
    return v ? static_cast<uint16_t>(std::strtoul(v, nullptr, 0)) : fallback;
}

ChannelWidth_t width_from_mhz(int mhz) {
    switch (mhz) {
        case 5: return CHANNEL_WIDTH_5;
        case 10: return CHANNEL_WIDTH_10;
        case 40: return CHANNEL_WIDTH_40;
        case 80: return CHANNEL_WIDTH_80;
        default: return CHANNEL_WIDTH_20;
    }
}

/* channel.ChannelOffset is the primary-channel index inside the wide channel
 * ({0,+6,+2,-2,-6} for 80 MHz). Passing 0 for an 80 MHz channel made the
 * 8821C tune its centre to the primary channel, so an 80 MHz tune came up
 * deaf. Map the common 5 GHz blocks; 20/40 MHz keep offset 0. */
uint8_t channel_offset_for(uint8_t channel, int width_mhz) {
    if (width_mhz != 80) return 0;
    static const uint8_t blocks[] = {36, 52, 100, 116, 132, 149};
    for (uint8_t block : blocks) {
        if (channel >= block && channel < block + 16) {
            const int pos = channel - block;
            if (pos < 4) return 1;
            if (pos < 8) return 2;
            if (pos < 12) return 3;
            return 4;
        }
    }
    return 0;
}

bool send_all(SOCKET sock, const void *src, size_t len) {    const auto *p = static_cast<const char *>(src);
    while (len > 0) {
        const int n = ::send(sock, p, static_cast<int>(len), 0);
        if (n <= 0) return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool recv_exact(SOCKET sock, void *dst, size_t len) {
    auto *p = static_cast<char *>(dst);
    while (len > 0) {
        const int n = ::recv(sock, p, static_cast<int>(len), 0);
        if (n <= 0) return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

/* Bounded queue between the RX callback and the socket writer. The callback
 * must never block on a slow client: dropping the oldest frame is better than
 * stalling the USB RX ring. */
class OutQueue {
  public:
    bool push(std::vector<uint8_t> message) {
        std::lock_guard<std::mutex> lock(mu_);
        const bool dropped = queue_.size() >= kMaxQueued;
        if (dropped) queue_.pop_front();
        queue_.push_back(std::move(message));
        cv_.notify_one();
        return dropped;
    }

    bool pop(std::vector<uint8_t> &message) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        message = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
        cv_.notify_all();
    }

  private:
    static constexpr size_t kMaxQueued = 256;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::vector<uint8_t>> queue_;
    bool stop_ = false;
};

struct FilterState {
    std::mutex mu;
    std::array<uint8_t, 6> bssid{};
    bool has_bssid = false;
    std::array<uint8_t, 6> own{};
    bool has_own = false;
};

std::atomic<uint64_t> g_rx_cb{0};
std::atomic<uint64_t> g_rx_crc{0};
std::atomic<uint64_t> g_rx_small{0};
std::atomic<uint64_t> g_rx_filtered{0};
std::atomic<uint64_t> g_rx_queued{0};
std::atomic<uint64_t> g_rx_dropped{0};
// Accepted data frames only: what the peer's rate control is doing is the
// difference between a 900 KB/s and a 250 KB/s transfer with identical loss.
PeerTelemetry g_peer;
std::chrono::steady_clock::time_point g_start;

long long elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - g_start)
        .count();
}

void on_rx(const Packet &packet, OutQueue &queue, FilterState &state,
           std::atomic<bool> &client_ready) {
    g_rx_cb.fetch_add(1);
    if (!client_ready.load(std::memory_order_relaxed)) return;
    if (packet.RxAtrib.pkt_rpt_type != RX_PACKET_TYPE::NORMAL_RX) return;
    if (packet.RxAtrib.crc_err) {
        g_rx_crc.fetch_add(1);
        return;
    }
    const std::span<uint8_t> frame = packet.Data;
    if (frame.size() < 24) {
        g_rx_small.fetch_add(1);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state.mu);
        if (state.has_bssid &&
            std::memcmp(frame.data() + 16, state.bssid.data(), 6) != 0) {
            g_rx_filtered.fetch_add(1);
            return;
        }
        if (state.has_own) g_peer.observe(packet, state.own);
    }

    std::vector<uint8_t> message(sizeof(radio_link_header) + kRadiotapBytes +
                                 frame.size());
    radio_link_header header{RADIO_LINK_RX, 0,
                             static_cast<uint32_t>(kRadiotapBytes +
                                                   frame.size())};
    std::memcpy(message.data(), &header, sizeof(header));
    uint8_t *body = message.data() + sizeof(header);
    body[2] = kRadiotapBytes;
    std::memcpy(&body[4], &kRadiotapPresent, sizeof(kRadiotapPresent));
    body[8] = packet.RxAtrib.fcs_present ? 0x10 : 0;
    const int dbm = kRssiFloorDbm + packet.RxAtrib.rssi[0];
    body[9] = static_cast<uint8_t>(static_cast<int8_t>(dbm));
    std::memcpy(body + kRadiotapBytes, frame.data(), frame.size());
    if (message.size() > sizeof(header) + RADIO_LINK_MAX_PAYLOAD) {
        g_rx_small.fetch_add(1);
        return;
    }
    if (queue.push(std::move(message))) g_rx_dropped.fetch_add(1);
    g_rx_queued.fetch_add(1);
}

int run(uint8_t channel, int width_mhz, uint16_t port, bool skip_reset) {
    g_start = std::chrono::steady_clock::now();
    auto logger = std::make_shared<Logger>();
    const char *log_level = std::getenv("AIRBRIDGE_LOG_LEVEL");
    logger->set_level(log_level && std::strcmp(log_level, "warn") == 0
                          ? Logger::Level::Warn
                          : Logger::Level::Info);

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logger->error("radio: WSAStartup failed");
        return 1;
    }

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        logger->error("radio: libusb_init failed");
        WSACleanup();
        return 1;
    }

    const uint16_t vid = env_hex16("AIRBRIDGE_VID", 0x0bda);
    const uint16_t pid = env_hex16("AIRBRIDGE_PID", 0xc820);
    libusb_device_handle *handle = libusb_open_device_with_vid_pid(ctx, vid, pid);
    if (!handle) {
        logger->error("radio: no device {:04x}:{:04x} (is MI_02 on WinUSB?)",
                      vid, pid);
        libusb_exit(ctx);
        WSACleanup();
        return 2;
    }

    int iface = devourer::find_wifi_interface(handle);
    std::shared_ptr<devourer::UsbDeviceLock> usb_lock;
    int rc = 0;
    if (skip_reset) {
        rc = devourer::claim_interface_then_reset(handle, iface, logger, false,
                                                  usb_lock);
    } else {
        rc = devourer::claim_interface_reset_reopen(ctx, handle, logger, true,
                                                    usb_lock);
        iface = devourer::find_wifi_interface(handle);
    }
    if (rc != 0) {
        logger->error("radio: claim/reset failed: {}", rc);
        libusb_close(handle);
        libusb_exit(ctx);
        WSACleanup();
        return 3;
    }

    WiFiDriver driver(logger);
    devourer::DeviceConfig config;
    config.rx.enable_with_tx = true;
    config.rx.keep_corrupted = true;
    int retry_limit = env_int("AIRBRIDGE_RETRY_LIMIT", 7);
    if (retry_limit < 0) retry_limit = 0;
    if (retry_limit > 63) retry_limit = 63;
    config.tx.retry_limit = retry_limit;
    // AWDL sequences mix 5 GHz and 2.4 GHz slots. With the firmware fast path
    // (H2C 0x1D, Jaguar2) a cross-band hop costs ~2 ms instead of ~90 ms, so
    // the per-superframe social-channel slot becomes affordable.
    config.tuning.fastretune_fw = env_int("AIRBRIDGE_FASTRETUNE_FW", 0);
    config.tuning.fastretune_wait_ms = 20; // RX C2H + one post-completion RF check.
    auto radio = driver.CreateRadio(handle, ctx, usb_lock, config);
    if (!radio) {
        logger->error("radio: CreateRadio failed (chip support not built?)");
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        WSACleanup();
        return 4;
    }

    const SelectedChannel selected{channel,
                                   channel_offset_for(channel, width_mhz),
                                   width_from_mhz(width_mhz), 0};
    logger->info("radio: channel {} width {} MHz offset {}", channel,
                 width_mhz, (int)selected.ChannelOffset);
    try {
        radio->InitWrite(selected);
    } catch (const std::exception &error) {
        logger->error("radio: bring-up failed: {}", error.what());
        radio.reset();
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        WSACleanup();
        return 5;
    }
    logger->info("radio: ready on channel {} width {} MHz", channel, width_mhz);
    logger->info("radio: tx retry limit {}", retry_limit);

    /* The demos always set a TX mode before injecting; a rate-less frame with
     * no mode can fall back to an unusable default and never reach the air.
     * Match txdemo's default (1M legacy) unless the caller overrides it. */
    const char *rate_env = std::getenv("AIRBRIDGE_TX_RATE");
    const std::string rate = rate_env && *rate_env ? rate_env : "1M";
    radio->SetTxMode(devourer::parse_tx_mode_str(rate));
    logger->info("radio: tx mode {}", rate);

    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        logger->error("radio: socket failed");
        radio->Stop();
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        WSACleanup();
        return 6;
    }
    /* SO_REUSEADDR on Windows lets another process bind the same port and
     * steal connections; SO_EXCLUSIVEADDRUSE is the correct local-server
     * option here. */
    const BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        listen(listener, 1) != 0) {
        logger->error("radio: bind/listen 127.0.0.1:{} failed", port);
        closesocket(listener);
        radio->Stop();
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        WSACleanup();
        return 6;
    }
    logger->info("radio: listening on 127.0.0.1:{}", port);

    // The supervisor stops children with TerminateProcess, which leaves the
    // RTL8821CU in the middle of a USB transfer and wedges the chip until it is
    // physically replugged (measured repeatedly tonight: 0.03 s CPU for minutes,
    // then "no device" or a hung init). A named event lets it ask for a clean
    // stop instead: closing the listener makes accept() fail, which takes the
    // ordinary teardown path (StopRxLoop, device Stop, libusb release), so the
    // next start works without touching the cable.
    HANDLE stop_event = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\DropBridgeRadioStop");
    if (stop_event != nullptr) {
        std::thread([listener, stop_event]() {
            WaitForSingleObject(stop_event, INFINITE);
            closesocket(listener);
        }).detach();
    }
    /* Always on stderr so the GUI can detect readiness even in quiet mode. */
    std::fprintf(stderr, "radio: listening on 127.0.0.1:%u\n", port);

    /* Start RX before accepting a client: TX from the pipe thread must not
     * race the RX worker's first URBs. Frames heard before a client is ready
     * are dropped by on_rx's client_ready gate. */
    /* Accept clients forever: the Dart app may disconnect/reconnect without
     * paying the 40-120 s bring-up again. Per-client state is recreated each
     * iteration; the radio stays initialized. */
    for (;;) {
        g_rx_cb = 0;
        g_rx_crc = 0;
        g_rx_small = 0;
        g_rx_filtered = 0;
        g_rx_queued = 0;
        g_rx_dropped = 0;
        (void)g_peer.take();

        OutQueue queue;
    FilterState state;
    std::atomic<bool> client_ready{false};
    std::atomic<bool> send_failed{false};
    std::atomic<bool> rx_ended{false};
    const bool no_rx = env_flag("AIRBRIDGE_NO_RX");
    std::thread rx_thread;
    if (!no_rx) {
        rx_thread = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            try {
                radio->StartRxLoop([&](const Packet &packet) {
                    on_rx(packet, queue, state, client_ready);
                });
            } catch (const std::exception &error) {
                logger->error("radio: RX loop died: {}", error.what());
            }
            rx_ended = true;
        });
    } else {
        logger->info("radio: AIRBRIDGE_NO_RX=1, RX loop skipped");
    }

    const SOCKET client = accept(listener, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
        logger->error("radio: accept failed");
        radio->StopRxLoop();
        if (rx_thread.joinable()) rx_thread.join();
        queue.stop();
        break;
    }
    const BOOL nodelay = TRUE;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));

    const radio_link_hello hello{RADIO_LINK_VERSION, channel,
                                 static_cast<uint8_t>(width_mhz), {0, 0}};
    const radio_link_header hello_header{RADIO_LINK_HELLO, 0,
                                         static_cast<uint32_t>(sizeof(hello))};
    if (!send_all(client, &hello_header, sizeof(hello_header)) ||
        !send_all(client, &hello, sizeof(hello))) {
        logger->error("radio: HELLO write failed");
        closesocket(client);
        radio->StopRxLoop();
        if (rx_thread.joinable()) rx_thread.join();
        queue.stop();
        break;
    }
    client_ready = true;
    logger->info("radio: client connected, HELLO sent at {} ms", elapsed_ms());

    std::thread writer([&] {
        std::vector<uint8_t> message;
        while (queue.pop(message)) {
            if (!send_all(client, message.data(), message.size())) {
                send_failed = true;
                return;
            }
        }
    });

    std::atomic<uint64_t> tx_sent{0}, tx_failed{0};

    // Periodic link counters: a transfer's CRC share must be visible while it
    // runs, not only in the teardown line. AIRBRIDGE_LINK_STATS keeps them at
    // warn level so the supervisor can watch the rate without the radio's
    // per-frame "bulk_send" noise that info level produces.
    const bool stats_at_warn = env_flag("AIRBRIDGE_LINK_STATS");
    std::atomic<bool> stats_stop{false};
    std::thread stats_thread([&] {
        auto previous_sample = std::chrono::steady_clock::now();
        while (!stats_stop.load()) {
            for (int tick = 0; tick < 50 && !stats_stop.load(); tick++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (stats_stop.load()) break;
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - previous_sample).count();
            previous_sample = now;
            const auto sample = g_peer.take();
            const int rssi = sample.signal_samples
                ? static_cast<int>(sample.rssi_sum / sample.signal_samples) : 0;
            // Chip temperature meter rides along: the slow-transfer events have
            // shown an RSSI collapse, and heat is the first thing to rule out.
            devourer::ThermalStatus thermal;
            try {
                thermal = radio->GetThermalStatus();
            } catch (const std::exception &error) {
                logger->warn("radio: thermal sample unavailable: {}", error.what());
            }
            if (stats_at_warn) {
                logger->warn(
                    "radio: stats cb={} crc={} small={} filtered={} queued={} drop={} tx={} failed={}",
                    g_rx_cb.load(), g_rx_crc.load(), g_rx_small.load(),
                    g_rx_filtered.load(), g_rx_queued.load(),
                    g_rx_dropped.load(), tx_sent.load(), tx_failed.load());
                logger->warn(
                    "radio: peer frames={} fps={:.1f} rssi={} rssi_n={} bytes={} rate={} bw={} sgi={} thermal={}/{} thermal_valid={}",
                    sample.frames, sample.frames / seconds, rssi, sample.signal_samples,
                    sample.bytes, sample.rate, sample.bw, sample.sgi,
                    static_cast<unsigned>(thermal.raw), thermal.delta, thermal.valid);
            } else {
                logger->info(
                    "radio: stats cb={} crc={} small={} filtered={} queued={} drop={} tx={} failed={}",
                    g_rx_cb.load(), g_rx_crc.load(), g_rx_small.load(),
                    g_rx_filtered.load(), g_rx_queued.load(),
                    g_rx_dropped.load(), tx_sent.load(), tx_failed.load());
                logger->info(
                    "radio: peer frames={} fps={:.1f} rssi={} rssi_n={} bytes={} rate={} bw={} sgi={} thermal={}/{} thermal_valid={}",
                    sample.frames, sample.frames / seconds, rssi, sample.signal_samples,
                    sample.bytes, sample.rate, sample.bw, sample.sgi,
                    static_cast<unsigned>(thermal.raw), thermal.delta, thermal.valid);
            }
        }
    });
    for (;;) {
        radio_link_header header{};
        if (!recv_exact(client, &header, sizeof(header))) break;
        if (header.length > RADIO_LINK_MAX_PAYLOAD) {
            logger->error("radio: oversized client message ({} bytes)",
                          header.length);
            break;
        }
        std::vector<uint8_t> payload(header.length);
        if (header.length > 0 &&
            !recv_exact(client, payload.data(), payload.size())) {
            break;
        }

        if (header.type == RADIO_LINK_TX) {
            if (radio->send_packet(payload.data(), payload.size())) {
                ++tx_sent;
            } else {
                ++tx_failed;
            }
        } else if (header.type == RADIO_LINK_SET_CHANNEL &&
                   payload.size() == 1) {
            const uint8_t previous = radio->GetSelectedChannel().Channel;
            const uint8_t target = payload[0];
            const auto started = std::chrono::steady_clock::now();
            const auto elapsed_us = [&started] {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started).count();
            };
            try {
                if (target == previous) {
                    logger->debug("radio: channel {} unchanged", target);
                } else {
                    // FastRetune keeps the firmware fast path when it is
                    // configured (cross-band included) and falls back to the
                    // full SetMonitorChannel itself otherwise.
                    radio->FastRetune(target);
                    channel = target;
                    logger->warn("radio: channel {} -> {} ({} us, confirmed retune)",
                                 previous, target, elapsed_us());
                }
            } catch (const std::exception &error) {
                logger->error("radio: channel change failed: {}", error.what());
                // Hardware/software channel state is uncertain. Disconnect the
                // chain instead of continuing to inject on an assumed channel.
                shutdown(client, SD_BOTH);
                break;
            }
        } else if (header.type == RADIO_LINK_SET_BSSID_FILTER &&
                   payload.size() == 6) {
            const bool all_zero =
                std::all_of(payload.begin(), payload.end(),
                            [](uint8_t b) { return b == 0; });
            {
                std::lock_guard<std::mutex> lock(state.mu);
                if (all_zero) {
                    state.has_bssid = false;
                } else {
                    std::memcpy(state.bssid.data(), payload.data(), 6);
                    state.has_bssid = true;
                }
            }
            if (all_zero) {
                logger->info("radio: BSSID filter cleared");
            } else {
                logger->info("radio: BSSID filter {:02x}:{:02x}:{:02x}:"
                             "{:02x}:{:02x}:{:02x}",
                             payload[0], payload[1], payload[2], payload[3],
                             payload[4], payload[5]);
            }
        } else if (header.type == RADIO_LINK_SET_ADDRESS &&
                   payload.size() == 6) {
            std::array<uint8_t, 6> own{};
            bool have_bssid = false;
            std::array<uint8_t, 6> bssid{};
            {
                std::lock_guard<std::mutex> lock(state.mu);
                std::memcpy(state.own.data(), payload.data(), 6);
                state.has_own = true;
                own = state.own;
                have_bssid = state.has_bssid;
                bssid = state.bssid;
            }
            logger->info("radio: own address {:02x}:{:02x}:{:02x}:{:02x}:"
                         "{:02x}:{:02x}",
                         own[0], own[1], own[2], own[3], own[4], own[5]);
            if (have_bssid) {
                devourer::MacAddr own_mac;
                devourer::MacAddr bssid_mac;
                own_mac.bytes = own;
                bssid_mac.bytes = bssid;
                if (!radio->SetStationIdentity(own_mac, bssid_mac)) {
                    logger->warn("radio: SetStationIdentity refused");
                }
            }
        } else if (header.type == RADIO_LINK_HELLO) {
            /* radio -> client only */
        } else {
            logger->warn("radio: unknown message type {} length {}",
                         header.type, header.length);
        }
        if (send_failed || rx_ended) break;
    }

    stats_stop = true;
    if (stats_thread.joinable()) stats_thread.join();
    logger->info("radio: client gone; tx sent {} failed {}; stopping",
                 tx_sent.load(), tx_failed.load());
    logger->info("radio: rx cb={} crc={} small={} filtered={} queued={} drop={}",
                 g_rx_cb.load(), g_rx_crc.load(), g_rx_small.load(),
                 g_rx_filtered.load(), g_rx_queued.load(), g_rx_dropped.load());

    client_ready = false;
    radio->StopRxLoop();
    if (rx_thread.joinable()) rx_thread.join();
    queue.stop();
    if (writer.joinable()) writer.join();

    shutdown(client, SD_BOTH);
    closesocket(client);
    logger->info("radio: waiting for next client");
    }

    radio->Stop();
    radio.reset();
    closesocket(listener);
    libusb_release_interface(handle, iface);
    libusb_close(handle);
    libusb_exit(ctx);
    WSACleanup();
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    const int channel_arg =
        argc > 1 ? std::atoi(argv[1]) : env_int("AIRBRIDGE_CHANNEL", 6);
    const int width_arg =
        argc > 2 ? std::atoi(argv[2]) : env_int("AIRBRIDGE_WIDTH", 20);
    const int port_arg =
        argc > 3 ? std::atoi(argv[3]) : env_int("AIRBRIDGE_PORT", kDefaultPort);
    if (channel_arg < 1 || channel_arg > 196 || width_arg <= 0 || port_arg <= 0 ||
        port_arg > 65535) {
        std::fprintf(stderr,
                     "usage: airbridge_radio <channel> [width_mhz] [port]\n");
        return 2;
    }
    const bool skip_reset = env_flag("AIRBRIDGE_SKIP_RESET");
    return run(static_cast<uint8_t>(channel_arg), width_arg,
               static_cast<uint16_t>(port_arg), skip_reset);
}
