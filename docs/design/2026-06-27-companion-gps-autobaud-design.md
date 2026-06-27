# Design-of-record — Companion GPS autobaud + 0xC1 reporting (#216)

**Feature:** #220 (GPS enablement for Companion and Repeater) · **Epic:** #216 · **Task:** #219 (Crosswire-7hb)
**Status:** DRAFT — pending the gating diagnostic (§4) + human AGREE before any implementation Task opens.
**Synthesizes:** #217 (Meshtastic pattern) + #218 (prior-effort assessment). Claims tagged `[verified: ref]` / `[hypothesis: untested]`.
**Test target:** **ST-P** — Heltec V4 #4 (ESP32-S3), COM10, currently Observer-flashed `[verified: HARDWARE.md:156-158]`.

---

## 1. Scope & decisions (locked with the human)

- **This Epic is Companion-only, ESP32-S3 first.** nRF52/RAK + Repeater is a separate Epic (#221) under the same Feature. `[human-decided]`
- **Passive-first.** Salvage the prior passive listen-for-valid-NMEA autobaud; add **active probing later** (own Epic/phase) once passive is stable. `[human-decided]`
- **Identity probe allowed now.** A read-only `UBX-MON-VER` identity probe of the M100 is in-scope for this Epic (informs design; not active *configuration*). `[human-decided]`
- **Persistence: yes.** Store last-good `{baud, model}`; boot tries stored baud first; defined re-detect triggers (§5.3). `[human-decided]`
- **Anti-wedge is structural, and serial logging must never flood USB CDC** (rules #1/#8/#10). `[human-constraint]`

## 2. Success criteria (from the Epic — the design must satisfy all)

Firmware: (1) device stable, never wedges; (2) detects + reads GPS whether onboard L76K or an externally-wired modem (M100); (3) reports fix/time/lat/lon/alt/sats/baud via the 0xC1 contract. Client: (1) reads GPS state via 0xC1.

## 3. Architecture (target)

Built on the salvage base from #218 (`feat/gps-state-query`, sound + working), reorganized onto `feat/216-gps-autobaud`:

- **Detection = non-blocking passive state machine** (`autoBaudStep`/`nmeaScanByte`): one bounded step per `loop()` while `gps_active && !locked`; candidate baud window `{stored?, GPS_BAUD_RATE?, 115200, 9600}`; validate a candidate by a **checksum-valid `$…*HH` NMEA sentence** (≥5 body chars); 1.5 s/candidate; on exhaustion fall back to candidate 0 and **lock** (no spin/wedge). `[verified: prior 14b1c570]` Mirrors the Meshtastic anti-wedge principle (cooperative SM + hard timeouts + bounded give-up) `[verified: #217]`.
- **Dedicated UART.** All GPS I/O on `Serial1`; the USB CDC console is never the GPS port `[verified: prior code + meshtastic GPS.cpp:42]`.
- **0xC1 reporting** (`CMD_OFFBAND_GPS`): ASCII `enabled/detected/active/fix/baud/lat/lon/alt/sats/time`, NUL-terminated, fork-only code in the 0xC0+ space; integer units (1e-6 deg, cm) to avoid the newlib-nano `%f` trap `[verified: prior 14b1c570]`. Client mirror: meshcore-client #135.
- **Toggle decouple.** Expose the `gps` setting regardless of one-shot detection (presence is reported via `gps_detected`/0xC1), fixing the chicken-egg that blocked enable `[verified: prior 14b1c570]`.
- **Persistence layer (new):** a small stored record of last-good `{baud, model}` consulted at boot (§5.3).
- **Serial-logging discipline (new):** the `[GPS]` status line gated behind an explicit debug flag, OFF in shipping builds; no unconditional `Serial` writes in the hot path (§5.2).

## 4. GATING DIAGNOSTIC — run FIRST, before any implementation

The prior session's instability was a serial-logging ↔ BLE-jam on the S3 HWCDC, **likely misattributed to GPS and never confirmed at the failing layer** `[#218]`. No implementation decision (esp. the fate of `setTxTimeoutMs(0)` and the `[GPS]` line) is locked until this is captured on **ST-P**.

### D1 — BLE-jam isolation (the linchpin)
- **Method:** build `feat/gps-state-query` for `heltec_v4_companion_radio_ble` **twice, identical code, only `BLE_DEBUG_LOGGING` toggled** (`=1` baseline vs `=0` override). Flash each to ST-P, connect the phone, force a reconnect/full-sync, capture serial (`_cap_serial.py COM10 <secs> 1`). GPS need not be attached for this test (autobaud cycles + the `[GPS]` line still run).
- **Prediction:** jams with `BLE_DEBUG_LOGGING=1`, **stable with `=0`** → the wedge is the debug flood, not GPS; prior misattribution confirmed.
- **Falsifier:** if it jams with `=0` (GPS line still printing), the status line is genuinely implicated → I'm wrong; re-open the logging design.
- **Decision it drives:** keep/drop/gate `setTxTimeoutMs(0)`; keep/gate the `[GPS]` line.

### D2 — M100 identity (read-only, passive-compatible)
- **Method:** with the M100 on ST-P (or on a USB-UART adapter), send `UBX-MON-VER` (`B5 62 0A 04 00 00` + checksum) at 115200 and capture the reply. **Note:** `_cap_serial.py` is capture-only — sending MON-VER needs a TX path (a one-off send script on a direct USB-UART, or a tiny temporary firmware echo). To be specified in the bench procedure.
- **Prediction:** a `UBX-MON-VER` response with version strings ⇒ genuine u-blox (UBX-configurable in the later active phase); silence / NMEA-only ⇒ u-blox-compatible clone (drive via NMEA/PUBX).
- **Decision it drives:** the command dialect assumption for the future active-probe phase; does not block passive detection.

## 5. Implementation plan (post-diagnostic, post-AGREE)

### 5.1 Salvage integration (cherry-pick order onto feat/216)
1. Autobaud SM + `nmeaScanByte` + header members.
2. 0xC1 query + `getGpsStatusText` + the toggle-decouple.
3. heltec_v4 ini autobaud comment.
Each step builds green before the next (commit per successful compile — version `+N` cadence).

### 5.2 Serial-logging discipline (contingent on D1)
- Default: gate the `[GPS]` line behind a dedicated debug flag (e.g. `GPS_STATUS_LOG`), **OFF** in shipping envs.
- `setTxTimeoutMs(0)`: keep only if D1 shows it's needed AND it does not reproduce the "stuck on Loading" sync failure `[verified: prior gps-jam log Build B]`; otherwise drop. Decided by D1 evidence, not assumption.

### 5.3 Persistence (new)
- Store last-good `{baud, model}` (NVS/prefs).
- Boot: try the **stored baud first** (fast path); full passive sweep only on failure.
- **Re-detect triggers:** (a) stored baud no longer validates — defined as **no checksum-valid sentence for T seconds** at the stored baud (T to be set, e.g. 5–10 s); (b) GPS setting toggled OFF; (c) OFF→ON (re-runs the sweep, per `start_gps()` already). `[human-decided]`

### 5.4 Quarantine / discard (from #218)
- `test-builds/*.bin` → gitignore, never commit. `scripts/_cap_serial.py` → keep as a tool. `docs/diagnostics` pwrsave logs + `pio-flash.py`/`rak3401` dirt → not this Epic (#208/#211), leave untouched.

## 6. Verify plan (on ST-P)
- Per-step build green (all relevant envs).
- On-device: D1 + D2 captures; then detection of the M100 @115200 (fix + coords via the `[GPS]` line and 0xC1); 0xC1 round-trip from the client; stability soak (no wedge, no BLE jam) with shipping log settings.
- Gemini review of the diff before any PR.

## 7. Deferred / out of scope (tracked elsewhere)
- nRF52/RAK detection (`Serial1.end()/begin(baud)` re-init) + repeater duty-cycle → Epic #221.
- Active modem probing/configuration (UBX `CFG`, `$PCAS`) → later phase once passive is stable `[human-decided]`.

## 8. Open questions to close during execution
- Exact `T` for the "no-longer-validates" re-detect window (§5.3).
- D2 TX mechanism for MON-VER (§4 D2 note).
- Whether the L76K's presumed-9600 baud is ever proven on hardware (needs an L76K on a test board; M100 is the part in hand).
