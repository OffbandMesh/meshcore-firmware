#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// MeshLog — the serial-capture tee sink (#393).
//
// mesh_log_line() is the single interception point that console producers
// (MESH_DEBUG_PRINTLN today; packet + boot lines in follow-ups) route through.
// When capture is enabled it timestamps the line and appends it to a static,
// non-retained CaptureRing for later download-to-app (#396). Capture is OFF by
// default, so on a stock build the sink early-outs on a single flag check and
// produces no output — preserving today's behaviour exactly.
//
// This declaration header is Arduino-free so MeshCore.h can include it
// unconditionally; the definition (MeshLog.cpp) is Arduino-coupled.

// Verbosity levels, lowest = most important. A line is captured only when its
// level is <= the active max level (default MLOG_DEBUG).
enum MeshLogLevel : uint8_t {
  MLOG_BOOT   = 0,
  MLOG_ERROR  = 1,
  MLOG_DEBUG  = 2,
  MLOG_PACKET = 3,
};

// #1069: level ceiling for the raw UART0 wire (#763) ONLY. The capture ring and
// the Serial mirror keep the runtime level (meshLogSetLevel); this caps what an
// OFFBAND_LOG_MIRROR_UART build puts on the always-on wire, so raising capture
// to `packet` from the client cannot flood a UART that costs ~87 us per byte.
// Build-time on purpose: the wire must behave the same on a board nobody has
// configured. A variant may override with -DOFFBAND_LOG_MIRROR_LEVEL=<n>.
//
// Default MLOG_DEBUG: a stock capture level is already DEBUG, so by default the
// wire carries exactly what it did before; only MLOG_PACKET is held back.
//
// An integer literal, not the enum name, so it also works in #if and matches
// what a -D override supplies. Out-of-range overrides fail the build below
// rather than silently wrapping (e.g. -1 would become 255 = no ceiling at all).
#ifndef OFFBAND_LOG_MIRROR_LEVEL
  #define OFFBAND_LOG_MIRROR_LEVEL 2  // MLOG_DEBUG
#endif
static_assert(MLOG_DEBUG == 2,
              "OFFBAND_LOG_MIRROR_LEVEL's default literal assumes MLOG_DEBUG == 2");
static_assert((OFFBAND_LOG_MIRROR_LEVEL) >= MLOG_BOOT &&
              (OFFBAND_LOG_MIRROR_LEVEL) <= MLOG_PACKET,
              "OFFBAND_LOG_MIRROR_LEVEL must be an MLOG_* level (0..3)");

inline bool meshLogUart0Admits(uint8_t level) {
  return level <= (OFFBAND_LOG_MIRROR_LEVEL);
}

// Which sinks one line reaches. mesh_log_line() routes through this, so the
// whole decision -- not just the ceiling -- is unit-testable natively.
//   capture: ring + Serial mirror. Needs the runtime level AND capture on.
//   wire:    raw UART0. Needs the runtime level AND the build-time ceiling, and
//            only exists in OFFBAND_LOG_MIRROR_UART builds. Capture-independent
//            by design (#763).
struct MeshLogRoute {
  bool capture;
  bool wire;
};
inline MeshLogRoute meshLogRoute(uint8_t level, uint8_t max_level,
                                 bool capture_on, bool uart0_built) {
  MeshLogRoute r = {false, false};
  if (level > max_level) return r;
  r.capture = capture_on;
  r.wire = uart0_built && meshLogUart0Admits(level);
  return r;
}

// Fast-path enable flag, read directly by the MESH_DEBUG_PRINTLN macro so that
// when capture is OFF the log arguments are never evaluated (short-circuit).
// Treat as read-only from producers; mutate only via meshLogSetEnabled().
extern volatile bool g_meshLogEnabled;

// Live-serial mirror flag (#411): when set, captured lines are also echoed to the
// live serial console. Set false at boot on a USB-serial companion (where Serial
// carries the framed protocol); true elsewhere. Set only via meshLogSetMirror().
extern volatile bool g_meshLogMirror;

// Level-name helpers for the `caplog` CLI (#395). Inline + Arduino-free so they
// are usable everywhere (including native unit tests) without linking MeshLog.cpp.
inline const char* meshLogLevelName(uint8_t level) {
  switch (level) {
    case MLOG_BOOT:   return "boot";
    case MLOG_ERROR:  return "error";
    case MLOG_DEBUG:  return "debug";
    case MLOG_PACKET: return "packet";
    default:          return "?";
  }
}
inline bool meshLogLevelFromName(const char* name, uint8_t* out_level) {
  if (!name || !out_level) return false;
  if (strcmp(name, "boot")   == 0) { *out_level = MLOG_BOOT;   return true; }
  if (strcmp(name, "error")  == 0) { *out_level = MLOG_ERROR;  return true; }
  if (strcmp(name, "debug")  == 0) { *out_level = MLOG_DEBUG;  return true; }
  if (strcmp(name, "packet") == 0) { *out_level = MLOG_PACKET; return true; }
  return false;
}

// Runtime control (wired to CLI verbs in #395).
void   meshLogSetEnabled(bool enabled);   // default false
bool   meshLogIsEnabled();
// Live-serial mirror (#411): main.cpp sets this at boot from the transport
// (false on a USB-serial companion where Serial is the protocol line).
void   meshLogSetMirror(bool on);         // default true
bool   meshLogMirrorEnabled();
void   meshLogSetLevel(uint8_t max_level); // capture lines with level <= this
uint8_t meshLogGetLevel();
void   meshLogClear();
size_t meshLogBytesUsed();
size_t meshLogCapacity();
// Copy up to out_cap captured bytes, oldest-first, into out, starting `offset`
// bytes in — lets the #396 download stream the buffer in chunks. Returns bytes
// copied (0 when offset >= bytesUsed()).
size_t meshLogSnapshot(uint8_t* out, size_t out_cap, size_t offset = 0);
// #561: drain oldest whole lines into out (removing them from the ring) under
// the sink lock; returns bytes copied. Lets a main-loop log forwarder pull new
// lines and ship them off-device (syslog/UDP) without offset-tracking across
// eviction. Network I/O by the caller happens OUTSIDE this lock.
size_t meshLogConsume(uint8_t* out, size_t out_cap);
// #1193: the non-destructive twin of meshLogConsume(), for a forwarder that must
// leave the capture intact so the app's download still has every line. Copies
// whole lines from the absolute position *cursor and advances it; removes
// nothing. If eviction has overtaken *cursor it skips to the oldest byte held
// and adds the gap to *lost (nullptr: skip without counting). Same short lock.
size_t meshLogReadFrom(uint64_t* cursor, uint8_t* out, size_t out_cap, uint64_t* lost);
// Stream the captured buffer to Serial in chunks (local-console `caplog dump`).
// Best-effort: stop capture first for a clean dump. Framed remote download is #396.
void   meshLogDumpSerial();

// The tee sink. printf-style; a "\n"-terminated line is expected. No-op (cheap
// early-out) when capture is disabled or the level is filtered out.
void mesh_log_line(uint8_t level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// #1211: for a line that has always gone to the serial console, such as
// SafeBoot's battery reading. It still goes there, and now also goes through
// mesh_log_line(): into the capture ring while capture is on, and onto the raw
// UART mirror where one is built, which is the wire the bench rig reads. When
// the capture echo is live, the echo is the console's copy, so the console gets
// the line once either way. Like Serial.print, it writes to the console even
// where the console carries the framed protocol, so use it only for lines that
// already did.
void mesh_log_print(uint8_t level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// Waits until the raw UART mirror has sent everything written so far, so a
// line logged just before a sleep or reset isn't cut off. No-op without it.
void meshLogDrainUart();
