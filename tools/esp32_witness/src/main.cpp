/* DropBridge 2.4 GHz witness for the ESP32-S3-DevKitC-1.
 *
 * Passive only: initializes Wi-Fi in station mode WITHOUT scanning, connecting
 * or transmitting, then puts the radio into promiscuous mode on a fixed
 * channel and prints one JSON line per matching 802.11 frame over serial.
 *
 * Build/flash (PlatformIO, already installed on this machine):
 *   pio run -d tools/esp32_witness -t upload --upload-port COM17
 *
 * Default match: frames whose transmitter address (addr2) is the canonical
 * devourer TX address 57:42:75:05:d6:00. Channel 6, 20 MHz.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>

namespace {

constexpr uint8_t kChannel = 6;
constexpr uint8_t kFilter[6] = {0x57, 0x42, 0x75, 0x05, 0xd6, 0x00};

volatile uint32_t g_total = 0;
volatile uint32_t g_matched = 0;
uint32_t g_last_stats_ms = 0;

uint32_t fnv1a(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

void print_mac(const uint8_t *m) {
  Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3],
                m[4], m[5]);
}

void sniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *pkt =
      static_cast<const wifi_promiscuous_pkt_t *>(buf);
  const uint8_t *frame = pkt->payload;
  const int len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;
  g_total++;

  const uint8_t fc0 = frame[0];
  const uint8_t version = fc0 & 0x03;
  const uint8_t ftype = (fc0 >> 2) & 0x03;
  const uint8_t subtype = (fc0 >> 4) & 0x0f;
  /* addr2 is at offset 10 for management and data frames. */
  const uint8_t *addr2 = frame + 10;

  if (version != 0) return;
  if (ftype != 0 && ftype != 2) return; /* management or data only */
  if (memcmp(addr2, kFilter, 6) != 0) return;
  g_matched++;

  const uint16_t seq = (static_cast<uint16_t>(frame[22]) |
                        (static_cast<uint16_t>(frame[23]) << 8)) >>
                       4;
  const uint32_t hash = fnv1a(frame, len < 64 ? len : 64);

  Serial.printf("{\"ev\":\"witness.frame\",\"t_ms\":%lu,\"len\":%d,"
                "\"rssi\":%d,\"type\":%u,\"sub\":%u,\"seq\":%u,"
                "\"hash\":\"%08lx\",\"addr2\":\"",
                static_cast<unsigned long>(millis()), len,
                static_cast<int>(pkt->rx_ctrl.rssi), ftype, subtype, seq,
                static_cast<unsigned long>(hash));
  print_mac(addr2);
  Serial.println("\"}");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("{\"ev\":\"witness.boot\",\"fw\":\"dbw1\",\"chip\":\"esp32s3\"}");

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask =
      WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_promiscuous_rx_cb(&sniffer);
  esp_wifi_set_promiscuous(true);

  Serial.printf("{\"ev\":\"witness.ready\",\"ch\":%u,\"filter\":\"", kChannel);
  print_mac(kFilter);
  Serial.println("\",\"tx\":false}");
}

void loop() {
  const uint32_t now = millis();
  if (now - g_last_stats_ms >= 5000) {
    g_last_stats_ms = now;
    Serial.printf(
        "{\"ev\":\"witness.stats\",\"t_ms\":%lu,\"total\":%lu,\"matched\":%lu}\n",
        static_cast<unsigned long>(now),
        static_cast<unsigned long>(g_total),
        static_cast<unsigned long>(g_matched));
  }
  delay(20);
}
