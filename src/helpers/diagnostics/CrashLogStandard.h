// src/helpers/diagnostics/CrashLogStandard.h
//
// Uniform, role-agnostic CrashLog bring-up (#472 / Epic #350).
//
// CrashLog itself (CrashLog.h) is deliberately board-agnostic -- it takes a
// reset-reason *hook* rather than a board handle. This thin wrapper binds a
// role's MainBoard-derived `board` to that hook and gives every role an
// IDENTICAL one-line bring-up, so boot-survival is standard across ALL roles
// instead of hand-wired (and unevenly) per example.
//
// Usage (every role's main.cpp):
//   setup():  offband::crashLogStandardInit(board, "repeater");
//   loop():   offband::crashLogStandardTick(millis());
//
// Default-ON. Compiles to a no-op on the host build and when a board opts out
// with -D OFFBAND_CRASHLOG_DISABLED (reserved for boards that fail hardware
// testing -- per the #472 owner decision: on everywhere unless a tested board
// proves it can't). Platform storage (ESP32 RTC_NOINIT / nRF52 .noinit) and the
// nRF52 cross-reset retention caveat (#378) are handled/known inside CrashLog.
#pragma once
#include <stdint.h>
#include "CrashLog.h"   // superset header: also exposes the core API (crashLogf, crashLogBegin, ...)

namespace mesh { class MainBoard; }  // fwd; full definition in MeshCore.h

namespace offband {

// Call once at the TOP of setup(), after Serial.begin() and before any
// high-current peripheral rail / filesystem write. Wires
// board.getResetReasonString(board.getResetReason()) into CrashLog, dumps any
// previous-boot ring that survived a reset, and records a role-tagged boot
// breadcrumb. Idempotent (CrashLog's begin is idempotent).
void crashLogStandardInit(mesh::MainBoard& board, const char* role_tag);

// Call once per loop() iteration with millis(). Drives the deferred re-dump
// (#463) so a serial monitor that connects AFTER the reset still sees the
// previous-boot crash log.
void crashLogStandardTick(uint32_t now_ms);

}  // namespace offband
