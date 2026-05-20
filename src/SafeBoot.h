#pragma once

/**
 * SafeBoot: pre-init power guard for battery/solar nodes.
 *
 * Runs very early in setup(), before any high-current rail or
 * filesystem write. Reads Vbat with a lightweight ADC sample and:
 *   - if stable above the wake threshold, returns and boot continues;
 *   - otherwise puts the MCU back into deep sleep with hysteresis and
 *     exponential backoff. Wake sources: timer (ESP32), hardware
 *     comparator (nRF52 LPCOMP), or button.
 *
 * State across attempts is kept in non-flash retention memory
 * (ESP32: RTC_NOINIT_ATTR; nRF52: GPREGRET2 with a bit-7 ownership
 * marker so SafeBoot's state can coexist with MeshCore's existing
 * NRF52Board shutdown-reason codes -- see SafeBoot.cpp).
 *
 * Configurability (highest priority first):
 *   1) DEFAULT_SAFE_BOOT_* in variant.h / platformio.ini (per hardware)
 *   2) Compile-time fallbacks in src/SafeBoot.cpp (LiIon single cell)
 *
 * No-op when ESP32 / NRF52_PLATFORM are not defined, PIN_VBAT_READ is
 * undefined, or SAFE_BOOT_DISABLED is set.
 *
 * Ported from Meshtastic PR #10391 (commit 4c7e1ee8) by Mickyleitor.
 * See meshcore-fork/docs/safeboot-maintenance.md for branch model
 * and upstream-rebase workflow.
 */

#include <stdint.h>

namespace SafeBoot
{

/**
 * Run the SafeBoot pre-init power guard.
 *
 * Must be called as early as possible in setup(), right after
 * Serial.begin() and BEFORE board.begin() (which enables high-current
 * peripheral rails), before any I2C scan, before LittleFS writes,
 * before radio/BLE init.
 *
 * If the guard decides the node has insufficient energy to boot
 * safely, this function does NOT return: it places the MCU into
 * deep sleep with a retry timer (and optional hardware-comparator
 * wake on nRF52). Otherwise it returns and normal boot proceeds.
 */
void checkAndMaybeSleep();

/**
 * True iff SafeBoot has decided the node is healthy enough to
 * keep running.
 */
bool isSettled();

/** True iff this boot was a recovery from a SafeBoot-induced sleep. */
bool wokeFromSafeBoot();

/**
 * True iff the previous reset reason indicated a brownout, watchdog
 * or other unclean condition. Consumers may use this to enable
 * defensive behavior (e.g. retry filesystem reads).
 */
bool lastResetWasUnclean();

} // namespace SafeBoot
