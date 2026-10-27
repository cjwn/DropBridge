/* SPDX-License-Identifier: MIT */
/* Wire protocol between the radio daemon and its client: a header, then
 * `length` payload bytes, little-endian. MIT, so both sides can include it.
 * This is the same v3 protocol WinAWDL uses; see docs/ARCHITECTURE.md. */
#pragma once
#include <stdint.h>

#define RADIO_LINK_PIPE_NAME "\\\\.\\pipe\\winawdl-radio"
#define RADIO_LINK_VERSION 3u
#define RADIO_LINK_MAX_PAYLOAD 16384u

enum radio_link_type {
  RADIO_LINK_HELLO = 1,
  RADIO_LINK_RX = 2,
  RADIO_LINK_TX = 3,
  RADIO_LINK_SET_CHANNEL = 4,
  RADIO_LINK_SET_BSSID_FILTER = 5,
  RADIO_LINK_SET_ADDRESS = 6,
};

struct radio_link_header {
  uint16_t type;
  uint16_t reserved;
  uint32_t length;
};

struct radio_link_hello {
  uint32_t version;
  uint8_t channel;
  uint8_t width_mhz;
  uint8_t reserved[2];
};

static_assert(sizeof(radio_link_header) == 8, "wire header must stay 8 bytes");
static_assert(sizeof(radio_link_hello) == 8, "HELLO must stay 8 bytes");
