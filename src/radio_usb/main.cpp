/* airbridge_radio: USB radio backend for AirBridge32.
 *
 * Owns the RTL8821CU through libusb/WinUSB and speaks the radio_link v3
 * protocol (MIT) over a named pipe. It does not parse AWDL or AirDrop; those
 * live in the Dart application (docs/ARCHITECTURE.md).
 *
 * Protocol: 8-byte little-endian header (type, reserved, length) + payload.
 *   HELLO (radio -> client)   once on connect
 *   RX    (radio -> client)   radiotap + 802.11 frame
 *   TX    (client -> radio)   radiotap + 802.11 frame
 *   SET_CHANNEL (client)      one byte, real SetMonitorChannel
 *   SET_BSSID_FILTER (client) six bytes, filter on addr3
 *   SET_ADDRESS (client)      six bytes, own MAC for hardware ACK
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <libusb.h>

#include "DeviceConfig.h"
#include "IRadio.h"
#include "RxPacket.h"
#include "SelectedChannel.h"
#include "UsbOpen.h"
#include "WiFiDriver.h"
#include "logger.h"
#include "radio_link.h"

namespace {

constexpr int kRssiFloorDbm = -110;
constexpr size_t kRadiotapBytes = 10;
constexpr uint32_t kRadiotapPresent = (1u << 1) | (1u << 5);

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

class Pipe {
  public:
    ~Pipe() { close(); }

    bool create() {
        h_ = CreateNamedPipeA(
            RADIO_LINK_PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 1 << 20, 1 << 20, 0, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "radio: CreateNamedPipe failed (%lu)\n",
                         GetLastError());
            return false;
        }
        return true;
    }

    bool wait_for_client() {
        if (!ConnectNamedPipe(h_, nullptr)) {
            const DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                std::fprintf(stderr, "radio: ConnectNamedPipe failed (%lu)\n",
                             err);
                return false;
            }
        }
        return true;
    }

    bool read_exact(void *dst, size_t len) {
        auto *p = static_cast<uint8_t *>(dst);
        while (len > 0) {
            DWORD got = 0;
            if (!ReadFile(h_, p, static_cast<DWORD>(len), &got, nullptr)) {
                std::fprintf(stderr, "radio: ReadFile failed (%lu)\n",
                             GetLastError());
                return false;
            }
            if (got == 0) {
                std::fprintf(stderr, "radio: ReadFile returned 0\n");
                return false;
            }
            p += got;
            len -= got;
        }
        return true;
    }

    bool send(uint16_t type, const void *payload, size_t len) {
        if (len > RADIO_LINK_MAX_PAYLOAD) return false;
        radio_link_header header{type, 0, static_cast<uint32_t>(len)};
        std::lock_guard<std::mutex> lock(write_mu_);
        return write_all(&header, sizeof(header)) &&
               (len == 0 || write_all(payload, len));
    }

  private:
    bool write_all(const void *src, size_t len) {
        const auto *p = static_cast<const uint8_t *>(src);
        while (len > 0) {
            DWORD wrote = 0;
            if (!WriteFile(h_, p, static_cast<DWORD>(len), &wrote, nullptr)) {
                std::fprintf(stderr, "radio: WriteFile failed (%lu)\n",
                             GetLastError());
                return false;
            }
            if (wrote == 0) {
                std::fprintf(stderr, "radio: WriteFile returned 0\n");
                return false;
            }
            p += wrote;
            len -= wrote;
        }
        return true;
    }

    void close() {
        if (h_ != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(h_);
            DisconnectNamedPipe(h_);
            CloseHandle(h_);
            h_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE h_ = INVALID_HANDLE_VALUE;
    std::mutex write_mu_;
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
std::atomic<uint64_t> g_rx_sent{0};
std::atomic<uint64_t> g_rx_fail{0};
std::atomic<bool> g_rx_seen{false};
std::chrono::steady_clock::time_point g_start;

long long elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - g_start)
        .count();
}

void on_rx(const Packet &packet, Pipe &pipe, FilterState &state,
           const Logger_t &logger, std::atomic<bool> &client_ready) {
    const uint64_t call = g_rx_cb.fetch_add(1);
    g_rx_seen = true;
    if (!client_ready.load(std::memory_order_relaxed)) return;
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
    }

    std::vector<uint8_t> message(kRadiotapBytes + frame.size());
    message[2] = kRadiotapBytes;
    std::memcpy(&message[4], &kRadiotapPresent, sizeof(kRadiotapPresent));
    message[8] = packet.RxAtrib.fcs_present ? 0x10 : 0;
    const int dbm = kRssiFloorDbm + packet.RxAtrib.rssi[0];
    message[9] = static_cast<uint8_t>(static_cast<int8_t>(dbm));
    std::memcpy(message.data() + kRadiotapBytes, frame.data(), frame.size());
    if (message.size() > RADIO_LINK_MAX_PAYLOAD) {
        g_rx_small.fetch_add(1);
        return;
    }
    if (call < 3) {
        logger->info("radio: rx callback t={}ms frame={} bytes fcs={} crc={}",
                     elapsed_ms(), frame.size(),
                     packet.RxAtrib.fcs_present, packet.RxAtrib.crc_err);
    }
    if (pipe.send(RADIO_LINK_RX, message.data(), message.size())) {
        g_rx_sent.fetch_add(1);
    } else {
        g_rx_fail.fetch_add(1);
    }
}

int run(uint8_t channel, int width_mhz, bool skip_reset) {
    g_start = std::chrono::steady_clock::now();
    g_rx_seen = false;
    auto logger = std::make_shared<Logger>();
    logger->set_level(Logger::Level::Info);

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        logger->error("radio: libusb_init failed");
        return 1;
    }

    const uint16_t vid = env_hex16("AIRBRIDGE_VID", 0x0bda);
    const uint16_t pid = env_hex16("AIRBRIDGE_PID", 0xc820);
    libusb_device_handle *handle = libusb_open_device_with_vid_pid(ctx, vid, pid);
    if (!handle) {
        logger->error("radio: no device {:04x}:{:04x} (is MI_02 on WinUSB?)",
                      vid, pid);
        libusb_exit(ctx);
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
        return 3;
    }

    WiFiDriver driver(logger);
    devourer::DeviceConfig config;
    /* InitWrite is the TX bring-up; without this the RX filters stay closed
     * and StartRxLoop hears only a trickle of frames (the same setting the
     * upstream doctor uses for its RX smoke). */
    config.rx.enable_with_tx = true;
    auto radio = driver.CreateRadio(handle, ctx, usb_lock, config);
    if (!radio) {
        logger->error("radio: CreateRadio failed (chip support not built?)");
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        return 4;
    }

    const SelectedChannel selected{channel, 0, width_from_mhz(width_mhz), 0};
    try {
        radio->InitWrite(selected);
    } catch (const std::exception &error) {
        logger->error("radio: bring-up failed: {}", error.what());
        radio.reset();
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        return 5;
    }
    logger->info("radio: ready on channel {} width {} MHz, waiting for client",
                 channel, width_mhz);

    Pipe pipe;
    FilterState state;
    std::atomic<bool> rx_ended{false};
    std::atomic<bool> client_ready{false};
    std::thread rx_thread([&] {
        /* devourer's demos settle for a moment after InitWrite before starting
         * the RX worker; doing it immediately races the bring-up tail and the
         * first URB completion can be delayed by tens of seconds. */
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        try {
            radio->StartRxLoop([&](const Packet &packet) {
                on_rx(packet, pipe, state, logger, client_ready);
            });
        } catch (const std::exception &error) {
            logger->error("radio: RX loop died: {}", error.what());
        }
        rx_ended = true;
    });

    /* Warm up RX before accepting a client: the first URB completion after
     * InitWrite can take several seconds on this chip, and a client that
     * connects first would otherwise see an empty window. */
    for (int i = 0; i < 200 && !g_rx_seen.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    logger->info("radio: rx warmup seen={} at {} ms", g_rx_seen.load(),
                 elapsed_ms());

    if (!pipe.create() || !pipe.wait_for_client()) {        logger->error("radio: pipe setup failed");
        radio->StopRxLoop();
        if (rx_thread.joinable()) rx_thread.join();
        radio->Stop();
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        return 6;
    }

    const radio_link_hello hello{RADIO_LINK_VERSION, channel,
                                 static_cast<uint8_t>(width_mhz), {0, 0}};
    if (!pipe.send(RADIO_LINK_HELLO, &hello, sizeof(hello))) {
        logger->error("radio: HELLO write failed");
        radio->StopRxLoop();
        if (rx_thread.joinable()) rx_thread.join();
        radio->Stop();
        libusb_release_interface(handle, iface);
        libusb_close(handle);
        libusb_exit(ctx);
        return 7;
    }
    client_ready = true;  /* from here RX frames are forwarded */
    logger->info("radio: HELLO sent at {} ms", elapsed_ms());

    uint64_t tx_sent = 0, tx_failed = 0, rx_hint = 0;
    for (;;) {
        radio_link_header header{};
        if (!pipe.read_exact(&header, sizeof(header))) break;
        if (header.length > RADIO_LINK_MAX_PAYLOAD) {
            logger->error("radio: oversized client message ({} bytes)",
                          header.length);
            break;
        }
        std::vector<uint8_t> payload(header.length);
        if (header.length > 0 &&
            !pipe.read_exact(payload.data(), payload.size())) {
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
            SelectedChannel next = radio->GetSelectedChannel();
            next.Channel = payload[0];
            try {
                radio->SetMonitorChannel(next);
                channel = payload[0];
                logger->info("radio: channel -> {}", channel);
            } catch (const std::exception &error) {
                logger->error("radio: channel change failed: {}", error.what());
            }
        } else if (header.type == RADIO_LINK_SET_BSSID_FILTER &&
                   payload.size() == 6) {
            std::lock_guard<std::mutex> lock(state.mu);
            std::memcpy(state.bssid.data(), payload.data(), 6);
            state.has_bssid = true;
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
            /* HELLO is radio -> client only; ignore a stray one. */
        } else {
            logger->warn("radio: unknown message type {} length {}",
                         header.type, header.length);
        }
        if (rx_ended) break;
        (void)rx_hint;
    }

    logger->info("radio: client gone; tx sent {} failed {}; stopping",
                 tx_sent, tx_failed);
    logger->info("radio: rx cb={} crc={} small={} filtered={} sent={} fail={}",
                 g_rx_cb.load(), g_rx_crc.load(), g_rx_small.load(),
                 g_rx_filtered.load(), g_rx_sent.load(), g_rx_fail.load());
    radio->StopRxLoop();
    if (rx_thread.joinable()) rx_thread.join();
    radio->Stop();
    radio.reset();

    libusb_release_interface(handle, iface);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    const int channel_arg = argc > 1 ? std::atoi(argv[1]) : env_int("AIRBRIDGE_CHANNEL", 6);
    const int width_arg = argc > 2 ? std::atoi(argv[2]) : env_int("AIRBRIDGE_WIDTH", 20);
    if (channel_arg < 1 || channel_arg > 196 || width_arg <= 0) {
        std::fprintf(stderr, "usage: airbridge_radio <channel> [width_mhz]\n");
        return 2;
    }
    const bool skip_reset = env_flag("AIRBRIDGE_SKIP_RESET");
    return run(static_cast<uint8_t>(channel_arg), width_arg, skip_reset);
}
