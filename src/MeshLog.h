#pragma once
#include <stdint.h>
#include <stddef.h>

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

// Runtime control (wired to CLI verbs in #395).
void   meshLogSetEnabled(bool enabled);   // default false
bool   meshLogIsEnabled();
void   meshLogSetLevel(uint8_t max_level); // capture lines with level <= this
uint8_t meshLogGetLevel();
void   meshLogClear();
size_t meshLogBytesUsed();
size_t meshLogCapacity();
// Copy up to out_cap captured bytes, oldest-first, into out (for #396 download).
size_t meshLogSnapshot(uint8_t* out, size_t out_cap);

// The tee sink. printf-style; a "\n"-terminated line is expected. No-op (cheap
// early-out) when capture is disabled or the level is filtered out.
void mesh_log_line(uint8_t level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
