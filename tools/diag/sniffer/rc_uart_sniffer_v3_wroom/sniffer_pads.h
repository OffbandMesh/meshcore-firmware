#pragma once

// Tagged lines and the RMT-decoded pads for the WROOM sniffer (#1199).
//
// Kept out of the .ino on purpose: Arduino inserts its generated prototypes before
// the sketch's first function, where a type declared further down is not yet
// visible, so these functions would not compile there. Headers are not scanned.
//
// Included by the sketch after its PIN MAP, which supplies TAG_A..TAG_D,
// PIN_RMT_C/PIN_RMT_D and SNIFF_BAUD.

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Tagged lines. Each line a pad delivers prints as "[<pin>] <text>", so the log
// shows which sniffer pin every wire lands on. Bytes outside printable ASCII print
// as \xHH, so noise on an idle pad reads as noise and cannot garble the log. A line
// that never ends is printed after TAG_FLUSH_MS of quiet, and says so.
// ---------------------------------------------------------------------------
#define TAG_FLUSH_MS 50

struct TagLine {
  const char* tag;
  char buf[160];
  uint16_t len;
  uint32_t last_ms;
};

static void tagFlush(TagLine& t, bool ended) {
  if (t.len == 0) return;
  Serial.print('[');
  Serial.print(t.tag);
  Serial.print("] ");
  Serial.write((const uint8_t*)t.buf, t.len);
  if (!ended) Serial.print("  <no line end>");
  Serial.println();
  t.len = 0;
}

static void tagByte(TagLine& t, uint8_t b, uint32_t now) {
  t.last_ms = now;
  if (b == '\n') { tagFlush(t, true); return; }
  if (b == '\r') return;
  const bool printable = (b >= 0x20 && b < 0x7F);
  const uint16_t need = printable ? 1 : 4;               // "\xHH" is four characters
  if (t.len + need > sizeof(t.buf)) tagFlush(t, false);  // make room first, then append
  if (printable) {
    t.buf[t.len++] = (char)b;
  } else {
    char esc[5];
    snprintf(esc, sizeof(esc), "\\x%02X", b);
    memcpy(t.buf + t.len, esc, 4);                       // buf is length-counted, no NUL
    t.len += 4;
  }
}

static void tagIdle(TagLine& t, uint32_t now) {
  if (t.len != 0 && (uint32_t)(now - t.last_ms) >= TAG_FLUSH_MS) tagFlush(t, false);
}

static TagLine tag_a = {TAG_A}, tag_b = {TAG_B};

#if SNIFF_RMT_PADS
// ---------------------------------------------------------------------------
// Pads C and D. The ESP32's three UARTs are all taken -- the console, pad A and
// pad B -- so the RMT times every pulse on these pins in hardware, and
// uart_rmt_decode.h turns the timings back into bytes. Each capture is re-armed as
// soon as it is read, so both pads are read continuously.
// ---------------------------------------------------------------------------
#include "uart_rmt_decode.h"

#define RMT_TICK_HZ      10000000UL   // 100 ns per tick
#define RMT_IDLE_TICKS   10000        // 1 ms of quiet closes a capture
#define RMT_FILTER_TICKS 10           // pulses under 1 us are noise
#define RMT_PAD_SYMBOLS  (2 * RMT_SYMBOLS_PER_CHANNEL_BLOCK)

struct RmtPad {
  uint8_t pin;
  TagLine line;
  rmt_data_t sym[RMT_PAD_SYMBOLS];
  size_t nsym;
  uint32_t bytes, bad, full;
  bool ok;
};

static RmtPad rmt_c = {PIN_RMT_C, {TAG_C}}, rmt_d = {PIN_RMT_D, {TAG_D}};

static bool rmtPadArm(RmtPad& p) {
  p.nsym = RMT_PAD_SYMBOLS;
  p.ok = rmtReadAsync(p.pin, p.sym, &p.nsym);
  return p.ok;
}

static void rmtPadBegin(RmtPad& p, const char* name) {
  p.ok = rmtInit(p.pin, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_2, RMT_TICK_HZ) &&
         rmtSetRxMinThreshold(p.pin, RMT_FILTER_TICKS) &&
         rmtSetRxMaxThreshold(p.pin, RMT_IDLE_TICKS);
  if (p.ok) {
    gpio_pullup_en((gpio_num_t)p.pin);   // an idle pad rests high instead of floating
    rmtPadArm(p);
  }
  Serial.printf("=== pad %s GPIO%d: RMT decode %s\n", name, (int)p.pin,
                p.ok ? "running" : "FAILED TO START -- this pad is not being read");
}

static void rmtPadService(RmtPad& p, uint32_t now) {
  if (p.ok && rmtReceiveCompleted(p.pin)) {
    // Copy the capture out, then re-arm at once: the next line may be close behind.
    static rmtuart::Run runs[2 * RMT_PAD_SYMBOLS];
    const size_t n = p.nsym;
    size_t nr = 0;
    for (size_t i = 0; i < n; i++) {
      runs[nr++] = {(uint8_t)p.sym[i].level0, (uint32_t)p.sym[i].duration0};
      if (p.sym[i].duration0 == 0) break;
      runs[nr++] = {(uint8_t)p.sym[i].level1, (uint32_t)p.sym[i].duration1};
      if (p.sym[i].duration1 == 0) break;
    }
    if (n >= RMT_PAD_SYMBOLS) p.full++;   // the capture filled up and lost its tail
    if (!rmtPadArm(p)) {
      Serial.printf("=== pad GPIO%d: RMT re-arm FAILED -- this pad is no longer read\n",
                    (int)p.pin);
    }
    static uint8_t bytes[RMT_PAD_SYMBOLS];
    const size_t nb = rmtuart::decodeFrame(runs, nr, rmtuart::bitX100(RMT_TICK_HZ, SNIFF_BAUD),
                                           bytes, sizeof(bytes), &p.bad);
    p.bytes += nb;
    for (size_t i = 0; i < nb; i++) tagByte(p.line, bytes[i], now);
  }
  tagIdle(p.line, now);
}
#endif  // SNIFF_RMT_PADS
