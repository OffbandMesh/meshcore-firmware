#pragma once

#include "Mesh.h"

#if defined(ARDUINO) && defined(ESP_PLATFORM)
  // #358: heap_caps_* live here. Included explicitly rather than relying on it
  // arriving transitively via Mesh.h -> Arduino.h.
  #include <esp_heap_caps.h>
#endif

class StatsFormatHelper {
public:
  // #358: heap fields are appended ESP32-only (see below), so the JSON is built
  // in two steps -- base fields without the closing brace, then the optional
  // heap pair, then the brace.
  //
  // Buffer budget: all three callers (repeater / room-server / sensor) pass
  // char reply[160]. Worst case here is ~130 chars incl. terminator, leaving
  // ~30 bytes of margin. sprintf is unbounded, matching every other reply
  // formatter in this codebase -- the CLI reply chain never plumbs a buffer
  // size down to the formatters, so bounding it properly is a codebase-wide
  // change tracked separately, NOT something to half-fix here with a
  // hardcoded size that a future caller could silently violate.
  static void formatCoreStats(char* reply,
                             mesh::MainBoard& board,
                             mesh::MillisecondClock& ms,
                             uint16_t err_flags,
                             mesh::PacketManager* mgr) {
    int n = sprintf(reply,
      "{\"battery_mv\":%u,\"uptime_secs\":%u,\"errors\":%u,\"queue_len\":%u",
      board.getBattMilliVolts(),
      ms.getMillis() / 1000,
      err_flags,
      mgr->getOutboundTotal()
    );
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    // Heap headroom (#358). ESP32-only: the nRF52 builds have no equivalent for
    // these APIs, so the keys are simply absent there rather than reported as a
    // misleading zero. Additive keys -- existing fields are unchanged.
    //
    // INTERNAL DRAM specifically (MALLOC_CAP_INTERNAL), not ESP.getFreeHeap():
    // boards/heltec_v4.json sets BOARD_HAS_PSRAM (2 MB qspi), and the aggregate
    // free-heap figure can fold PSRAM in with internal DRAM. That would overstate
    // the headroom that actually constrains us, since mbedTLS/TLS session buffers
    // need internal DRAM -- the exact number Epic #308 is trying to establish.
    //
    // *_min is the minimum-ever free (low-water mark): it captures peak memory
    // pressure, which is what matters for headroom. The instantaneous figure can
    // look healthy right after a spike has already passed.
    n += sprintf(reply + n,
      ",\"heap_int_free\":%u,\"heap_int_min\":%u",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)
    );
#endif
    // Single char + terminator; no need to pay for a sprintf call here.
    reply[n]     = '}';
    reply[n + 1] = 0;
  }

  template<typename RadioDriverType>
  static void formatRadioStats(char* reply,
                              mesh::Radio* radio,
                              RadioDriverType& driver,
                              uint32_t total_air_time_ms,
                              uint32_t total_rx_air_time_ms) {
    sprintf(reply, 
      "{\"noise_floor\":%d,\"last_rssi\":%d,\"last_snr\":%.2f,\"tx_air_secs\":%u,\"rx_air_secs\":%u}",
      (int16_t)radio->getNoiseFloor(),
      (int16_t)driver.getLastRSSI(),
      driver.getLastSNR(),
      total_air_time_ms / 1000,
      total_rx_air_time_ms / 1000
    );
  }

  template<typename RadioDriverType>
  static void formatPacketStats(char* reply,
                               RadioDriverType& driver,
                               uint32_t n_sent_flood,
                               uint32_t n_sent_direct,
                               uint32_t n_recv_flood,
                               uint32_t n_recv_direct) {
    sprintf(reply, 
      "{\"recv\":%u,\"sent\":%u,\"flood_tx\":%u,\"direct_tx\":%u,\"flood_rx\":%u,\"direct_rx\":%u,\"recv_errors\":%u}",
      driver.getPacketsRecv(),
      driver.getPacketsSent(),
      n_sent_flood,
      n_sent_direct,
      n_recv_flood,
      n_recv_direct,
      driver.getPacketsRecvErrors()
    );
  }
};
