# Task 2 — Assessment of the prior session's autobaud efforts

**Issue:** OffbandMesh/meshcore-firmware#218 (Epic #216) · **Citadel:** Crosswire-r87
**Date:** 2026-06-27 · Read from branch `feat/gps-state-query` (worktree `meshcore-firmware-gpsquery`) + dirty primary clone.
Claims tagged `[verified: ref]` / `[hypothesis: untested]`.

---

## Headline (plain grounding) — this reframes the Epic

The prior session's **autobaud logic is actually sound** — a non-blocking, NMEA-checksum-validated state machine that matches the Meshtastic anti-wedge principles from Task 1. **It worked**: it read the M100 at 115200 with a valid fix `[verified: prior 2026-06-26 gps-jam log: "fix=1 baud=115200 with valid coordinates"]`.

**What "blew up" was not the autobaud** — it was a **serial-logging ↔ BLE-jam interaction on the ESP32-S3 USB-Serial-JTAG (HWCDC)**, and that was most likely **misattributed to the GPS work**. The real driver is the env's `-D BLE_DEBUG_LOGGING=1` flooding the HWCDC per BLE frame; the GPS code's 5 s `[GPS]` status line and the `setTxTimeoutMs(0)` "fix" are secondary, and the root cause was **never confirmed at the failing layer** (no clean serial capture isolating BLE_DEBUG_LOGGING from the GPS line). That is the one thing to settle before trusting any of it.

So we are **not** starting from zero. We have a salvageable ESP32 autobaud + 0xC1 query, and the real open work is (a) confirm/fix the BLE-jam root cause *properly*, (b) extend to nRF52/RAK (prior work is ESP32-only), (c) the M100 command-dialect question, (d) baud persistence.

## Where the work lives

- Branch **`feat/gps-state-query`** @ 7efa0807, 2 commits past `firmware-base`, tracked under **#149** (not a dedicated autobaud issue — this is the "erroneously untracked" part):
  - `14b1c570 feat(#149): non-blocking GPS auto-baud + 0xC1 status query` ← the substance
  - `7efa0807 chore(#149): stamp build version on serial at boot` ← minor
- `sleeptest` worktree (detached `8dd1734d`): **no commits ahead of firmware-base** `[verified: git log empty]` — irrelevant to autobaud.

## Artifact-by-artifact verdict

| Artifact | What it is | Verdict | Rationale |
|---|---|---|---|
| `autoBaudStep()` + `nmeaScanByte()` (`EnvironmentSensorManager.cpp`) | Non-blocking baud SM: candidate window `{GPS_BAUD_RATE?,115200,9600}`, reads only buffered bytes, validates by incremental NMEA checksum, 1.5 s/candidate, fallback+lock | **SALVAGE** | Sound, non-blocking, no spin/wedge `[verified: 14b1c570]`. Matches Meshtastic principles. Checksum-validation is *better* than substring match for the generic case. |
| `CMD_OFFBAND_GPS` 0xC1 query (`MyMesh.cpp`) + `getGpsStatusText()` | ASCII status reply (enabled/detected/active/fix/baud/lat/lon/alt/sats/time), fork-only 0xC1, NUL-terminated; integer units to dodge newlib-nano `%f` | **SALVAGE** | Directly serves Epic criteria 3 (fw reports via 0xC1) + client criterion 1. nRF52-`%f`-aware. Client mirror = meshcore-client #135. |
| Toggle decouple (remove `gps_detected &&` gates in getNumSettings/Name/Value/setSettingValue) | Always expose the `gps` toggle; presence reported via gps_detected/0xC1 instead | **SALVAGE** | Fixes a real chicken-egg (couldn't enable GPS to detect it) `[verified: 14b1c570]`. |
| 5 s serial `[GPS]` status line (`loop()`) | `Serial.print("[GPS] …")` every 5 s, self-throttled | **REWORK** | Useful for bench, but unconditional `Serial` writes in a shipping path are a USB-flood risk (rule #10). Gate behind a debug flag or remove for release. |
| `Serial.setTxTimeoutMs(0)` in `setup()` (`main.cpp`) | Make HWCDC writes non-blocking (drop instead of block) | **SCRUTINIZE — riskiest piece** | Has a *documented* failure mode: with it, "phone app stuck on Loading, sync never completes" `[verified: prior gps-jam log Build B]`. Whether to keep depends on confirming the BLE-jam root cause on-device. Do NOT trust as-is. |
| `heltec_v4/platformio.ini` (+5 comment lines) | Documents auto-detect; no `GPS_BAUD_RATE` hardcode | **SALVAGE** | Correct: one image reads either modem. |
| `test-builds/*.bin` (4 files, primary clone, untracked) | Throwaway test binaries | **DISCARD from git** | Build artifacts — gitignore, never commit. Keep locally for flash A/B if useful. |
| `scripts/_cap_serial.py` (primary clone, untracked) | Serial-capture helper | **KEEP** | Diagnostic tool; the layer-appropriate capture we need for the BLE-jam confirmation. Adopt into the branch (or scripts/). |
| `docs/diagnostics/2026-06-24/25-*` (primary clone, untracked) | RAK3401 BLE / pwrsave logs | **NOT autobaud** | Belongs to #208 power-save work — leave for that effort. |
| `scripts/pio-flash.py`, `variants/rak3401/platformio.ini` (primary-clone dirty diffs) | Flash tooling + RAK3401 variant | **NOT autobaud [hypothesis]** | Likely #211 (RAK3401_NO_GPS) / flash work, not this Epic. Leave untouched; confirm owner before any action. |

## Critical scope gap — prior work is ESP32-only

`autoBaudStep` is wrapped in `#if defined(ESP32)` and relies on `Serial1.updateBaudRate()` `[verified: 14b1c570 header + cpp]`. **The Adafruit nRF52 core has no `updateBaudRate()`**, so on RAK/nRF52 there is **no autobaud** — it falls back to a bounded byte-presence detect at the build-configured rate. The Epic's future RAK/Repeater scope needs the **`Serial1.end()/begin(newBaud)` re-init** pattern Meshtastic uses for nRF52 `[verified: meshtastic GPS.cpp:1278-1279]`. This is net-new, not salvage.

## Design forks surfaced (for the design Task, not decided here)

1. **Passive-detect vs active-probe.** Prior work is *passive* — it only listens for auto-spewed NMEA and locks onto it. It never *sends* a command, so it cannot prove a silent modem's baud nor reconfigure a modem (e.g., force a standard rate). Meshtastic is *active* (sends `$PCAS`/UBX). Ben's "what commands will it accept" question lands here: if we want to *configure* the M100/L76K (not just read it), we need the active path + the per-family dialects (UBX vs `$PCAS`).
2. **Persistence** (already captured on #216): prior work re-detects on each enable; no cross-boot store.
3. **BLE-jam root cause** must be confirmed at the failing layer (serial capture isolating BLE_DEBUG_LOGGING from the GPS line) before keeping `setTxTimeoutMs(0)` or the status line.

## Recommendation

Treat `feat/gps-state-query`'s **autobaud SM + 0xC1 + toggle-decouple + integer formatter as the salvage base** to cherry-pick onto `feat/216-gps-autobaud` (after the design Task). Quarantine `setTxTimeoutMs(0)` + the unconditional status line pending an on-device BLE-jam root-cause capture. Plan net-new for nRF52/RAK. No code moved in this Task — assessment only.
