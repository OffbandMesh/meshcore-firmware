#pragma once

// Pad ID beacon (#1210): a diag-only proof of which wire lands where.
//
// Once a period, sends one short line on each of a variant's spare pads in turn,
// "PAD GPIO34 P1.02 #17", by briefly re-pointing the log mirror's UARTE at each
// pad. A sniffer that tags every line with the pin it arrived on can then show
// which pad reaches which of its pins, and a silent pin shows a pad that doesn't.
//
// Build flags:
//   OFFBAND_PAD_BEACON        enables it (diag envs only)
//   OFFBAND_PAD_BEACON_PADS   the variant's pad table: {{nrf_pin, "name"}, ...}
//   OFFBAND_PAD_BEACON_MS     period, default 1000
//
// nRF52 only, and only alongside OFFBAND_LOG_MIRROR_UART, whose UARTE it borrows.
// The line format below is plain C++ so the host tests can cover it.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace offband {

struct PadBeaconPad {
  uint8_t nrf_pin;    // port * 32 + pin, the same number PSEL takes
  const char* name;   // what the pad is called on the board, e.g. "GPIO34 P1.02"
};

// "PAD <name> #<seq>\r\n" into out. Returns its length, or 0 with out emptied when
// it does not fit: a truncated ID would name the wrong pad, so it is never sent.
inline size_t formatPadBeaconLine(char* out, size_t n, const char* name, uint32_t seq) {
  if (out == nullptr || n == 0) return 0;
  out[0] = '\0';
  if (name == nullptr) return 0;
  const int len = snprintf(out, n, "PAD %s #%lu\r\n", name, (unsigned long)seq);
  if (len < 0 || (size_t)len >= n) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)len;
}

}  // namespace offband

#if defined(OFFBAND_PAD_BEACON) && \
    (defined(NRF52_PLATFORM) || defined(NRF52840_XXAA) || defined(NRF52832_XXAA))

#include "LogMirrorUart.h"

#if !defined(OFFBAND_LOG_MIRROR_ACTIVE)
  #error "OFFBAND_PAD_BEACON borrows the log mirror's UARTE: build it with OFFBAND_LOG_MIRROR_UART=1."
#endif
#ifndef OFFBAND_PAD_BEACON_PADS
  #error "OFFBAND_PAD_BEACON needs the variant's OFFBAND_PAD_BEACON_PADS table."
#endif
#ifndef OFFBAND_PAD_BEACON_MS
  #define OFFBAND_PAD_BEACON_MS 1000
#endif

namespace offband {

inline void padBeaconTick(uint32_t now_ms) {
  static const PadBeaconPad kPads[] = OFFBAND_PAD_BEACON_PADS;
  static uint32_t next_ms = 0;
  static uint32_t seq = 0;
  static bool pads_ready = false;

  if ((int32_t)(now_ms - next_ms) < 0) return;
  next_ms = now_ms + OFFBAND_PAD_BEACON_MS;
  if (!offband_log_mirror_ok()) return;   // the mirror gave up after repeated timeouts

  NRF_UARTE_Type* u = OFFBAND_LOG_MIRROR_NRF_DEV;
  offband_log_mirror_nrf_init();          // configured once, on the mirror's own pin
  const uint32_t mirror_pin = u->PSEL.TXD;

  if (!pads_ready) {
    // Every pad idles HIGH, the UART idle level, while the UARTE is on another one.
    // nrfx's UARTE driver sets its TXD pin up the same way before enabling.
    for (const PadBeaconPad& p : kPads) {
      NRF_GPIO_Type* port = (p.nrf_pin >= 32) ? NRF_P1 : NRF_P0;
      const uint32_t bit = 1UL << (p.nrf_pin & 31);
      port->OUTSET = bit;
      port->DIRSET = bit;
    }
    pads_ready = true;
  }

  seq++;
  char line[32];

  // Other tasks log through this same UARTE: MeshLog from any task, CrashLog from a
  // BLE callback. While PSEL points at a spare pad their output would land there,
  // so the scheduler is held for the few ms this takes. Interrupts stay on: masking
  // them that long would starve the SoftDevice, which LogMirrorUart.h rejects too.
  vTaskSuspendAll();
  for (const PadBeaconPad& p : kPads) {
    const size_t len = formatPadBeaconLine(line, sizeof(line), p.name, seq);
    if (len == 0) continue;
    // PSEL is only safe to change while the UARTE is disabled. Each putc has waited
    // for its own ENDTX, so nothing is in flight; flush() stops the transmitter and
    // disables it, and the next putc re-enables it on the new pad.
    offband_log_mirror_flush();
    u->PSEL.TXD = p.nrf_pin;
    offband_log_mirror_write(line, len);
  }
  offband_log_mirror_flush();
  u->PSEL.TXD = mirror_pin;               // the next log line re-enables it here
  xTaskResumeAll();
}

}  // namespace offband

#endif  // OFFBAND_PAD_BEACON on nRF52
