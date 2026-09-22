#pragma once

// 8N1 UART decoding from RMT pulse runs (#1199).
//
// The WROOM sniffer has three UARTs, and all three are taken: the console to the
// PC, pad A and pad B. Pads C and D are read with the RMT peripheral instead, which
// times every pulse in hardware; this turns those timings back into bytes.
//
// Plain C++ with no Arduino dependency, so the host tests can drive it with
// synthetic pulse trains (test/test_sniffer_rmt_decode).

#include <stddef.h>
#include <stdint.h>

namespace rmtuart {

// One stretch of constant level, as the RMT reports it. A zero-length run is the
// RMT's end-of-frame marker: the line then stayed at that level until the frame
// was closed by the idle timeout.
struct Run {
  uint8_t level;    // 0 low, 1 high; the line idles high
  uint32_t ticks;
};

// Bit length in hundredths of a tick: 10 MHz ticks at 115200 baud gives 8681.
inline uint32_t bitX100(uint32_t tick_hz, uint32_t baud) {
  return (uint32_t)(((uint64_t)tick_hz * 100u + baud / 2) / baud);
}

// Decodes one frame of 8N1, least significant bit first. Writes at most cap bytes
// to out and returns how many it wrote. Adds to *bad each byte whose stop bit was
// low, and a byte cut off mid-way by the end of the frame; neither is ever emitted.
inline size_t decodeFrame(const Run* runs, size_t n, uint32_t bit_x100,
                          uint8_t* out, size_t cap, uint32_t* bad) {
  enum State { IDLE, DATA, STOP, WAIT_HIGH };
  State st = IDLE;
  uint8_t cur = 0, nbits = 0;
  size_t w = 0;
  uint32_t errors = 0;

  auto feed = [&](uint8_t bit) {
    switch (st) {
      case IDLE:
        if (bit == 0) { st = DATA; cur = 0; nbits = 0; }   // start bit
        break;
      case DATA:
        cur |= (uint8_t)(bit << nbits);
        if (++nbits == 8) st = STOP;
        break;
      case STOP:
        if (bit) {
          if (w < cap) out[w++] = cur;
          st = IDLE;
        } else {
          errors++;           // framing error: resync on the next high-to-low edge
          st = WAIT_HIGH;
        }
        break;
      case WAIT_HIGH:
        if (bit) st = IDLE;
        break;
    }
  };

  for (size_t i = 0; i < n; i++) {
    uint32_t bits;
    if (runs[i].ticks == 0) {
      bits = runs[i].level ? 1 : 0;   // end marker: idle high completes a pending stop bit
    } else {
      bits = (uint32_t)(((uint64_t)runs[i].ticks * 100u + bit_x100 / 2) / bit_x100);
      if (bits > 64) bits = 64;       // a long idle or break teaches nothing more
    }
    for (uint32_t b = 0; b < bits; b++) feed(runs[i].level ? 1 : 0);
  }
  if (st == STOP) feed(1);            // the frame closed on idle, which is a high stop bit
  else if (st == DATA) errors++;      // cut off mid-byte: count it, never guess the rest

  if (bad) *bad += errors;
  return w;
}

}  // namespace rmtuart
