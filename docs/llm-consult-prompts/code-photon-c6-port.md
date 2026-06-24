# Gemini Adversarial CODE Review — Offband: MeshSmith Photon-1W ESP32-C6 port

Adversarial reviewer for **Offband**, an MIT fork of **MeshCore** (NOT Meshtastic). C++ firmware, PlatformIO / arduino-esp32. We are PORTING MeshSmith's Photon-1W ESP32-C6 board support into our tree by vendoring their variant dir + minimal base-class adaptations. Prove concerns with evidence (file:line/symbol); if you can't tell from the bundle, say so. No rubber-stamping, no flattery, no padding.

## What changed
1. **Vendored** `variants/meshsmith_photon_esp32c6/` from MeshSmith's MeshCore fork (their MIT board support) — mostly verbatim.
2. **Our base-class adaptations (the review focus):**
   - `src/helpers/ESP32Board.h`: added 3 default-no-op virtuals (`hasWirelessAntennaSwitch() const`, `getWirelessAntennaExternal(bool&) const`, `setWirelessAntennaExternal(bool)`) that the Photon board overrides (its antenna-select RF switch). Upstream MeshCore has no such API; we added them to `ESP32Board` (not the common `mesh::MainBoard`).
   - `src/helpers/sensors/MicroNMEALocationProvider.h`: changed `_gps_serial` from private to **protected** so the vendored `ATGM336HLocationProvider` subclass can reach it directly (we deliberately use the existing member instead of MeshSmith's fork-only `gpsSerial()` accessor, to stay upstream-consistent).
   - `variants/.../ATGM336HLocationProvider.h`: repointed `gpsSerial()` -> `_gps_serial->`.
   - `variants/.../platformio.ini` BLE env: added `h2zero/NimBLE-Arduino @ ^2.0.0` (env was missing it).
3. Build: all 3 roles (repeater, companion USB, companion BLE) compile clean on the host. Bench-verify on real hardware is deferred (no device yet).

Radio is an Ebyte **E22-900M30S** (SX1262 + integrated 30 dBm PA); DIO2-only T/R switch; `LORA_TX_POWER=20` -> ~30 dBm out.

## Questions
1. Are the 3 antenna virtuals safe added to `ESP32Board` rather than `mesh::MainBoard`? Any board broken by them? Signature/const-correctness vs the Photon overrides?
2. Is private->protected on `_gps_serial` sound (vs an accessor)? Any encapsulation/lifetime risk for the subclass usage?
3. Anything unsafe in the vendored board/target/GPS code on our base — the `begin()` antenna GPIO + NVS (Preferences) sequence, the MAX17048 fuel-gauge I2C reads, the GPS config-command timing?
4. NimBLE dep — correct version/placement for the BLE env only (USB env must NOT pull it)?
5. Anything a host-compile-green would NOT catch (init order, the antenna `begin()` delay before SX1262 power-up, runtime hazards)?

## Output
```
## Summary
## Issues
- **[BLOCKER/MAJOR/MINOR/QUESTION] <area>** — <problem + evidence>. <fix>.
## Direct answers
1..5
## Verdict
```

## Files
__FILES__
