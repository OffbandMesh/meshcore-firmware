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

// Fast-path enable flag, read directly by the MESH_DEBUG_PRINTLN macro so that
// when capture is OFF the log arguments are never evaluated (short-circuit).
// Treat as read-only from producers; mutate only via meshLogSetEnabled().
extern volatile bool g_meshLogEnabled;

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
void   meshLogSetLevel(uint8_t max_level); // capture lines with level <= this
uint8_t meshLogGetLevel();
void   meshLogClear();
size_t meshLogBytesUsed();
size_t meshLogCapacity();
// Copy up to out_cap captured bytes, oldest-first, into out, starting `offset`
// bytes in — lets the #396 download stream the buffer in chunks. Returns bytes
// copied (0 when offset >= bytesUsed()).
size_t meshLogSnapshot(uint8_t* out, size_t out_cap, size_t offset = 0);
// Stream the captured buffer to Serial in chunks (local-console `caplog dump`).
// Best-effort: stop capture first for a clean dump. Framed remote download is #396.
void   meshLogDumpSerial();

// The tee sink. printf-style; a "\n"-terminated line is expected. No-op (cheap
// early-out) when capture is disabled or the level is filtered out.
void mesh_log_line(uint8_t level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
