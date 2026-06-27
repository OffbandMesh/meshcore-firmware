# Bench procedure — D1 (BLE-jam isolation) + D2 (M100 identity)

**Epic #216 / Task #219.** Run on **ST-P** (Heltec V4 #4, ESP32-S3, **COM10**, currently Observer-flashed).
Goal: convert the two design unknowns into on-device evidence **before** any implementation. Every flash is **Tier-2 — human-gated**.

## Prerequisites
- ST-P on USB (COM10). Offband MeshCore client on the phone (current version — older clients mis-behave with 1.1.1+, see [[client-is-a-first-class-suspect]]).
- Capture tool: `scripts/_cap_serial.py` (currently in the primary clone; read-only; asserts DTR for the S3 JTAG-CDC).
- For D2 only: the **M100 Mini on a USB-UART adapter** (separate COM port) — TX/RX/GND to the adapter, NOT the radio's USB. (`_cap_serial.py` can't transmit; D2 uses `scripts/_probe_ublox.py`.)

---

## D1 — BLE-jam isolation (the linchpin)

**Two images, identical code, only `BLE_DEBUG_LOGGING` differs** (built from `feat/gps-state-query`):
- `STP-D1-DBGLOG-ON.bin`  (BLE_DEBUG_LOGGING=1, the prior baseline)
- `STP-D1-DBGLOG-OFF.bin` (BLE_DEBUG_LOGGING=0)

GPS need not be attached for D1 (autobaud cycles + the 5 s `[GPS]` line print regardless).

**Steps (repeat for each image):**
1. **[Tier-2 — human]** Flash the image to ST-P (NVS-preserving ESP32 pio-flash preview→confirm).
2. Start capture in one terminal:
   `python scripts/_cap_serial.py COM10 90 1 > capture-<ON|OFF>.log`
   (90 s, DTR asserted so the S3 JTAG-CDC emits TX.)
3. While capturing: connect the phone app, then **force a reconnect + full sync** (disconnect/reconnect in the app) and exercise it for ~60 s.
4. Watch for the jam signature: app stalls, serial shows `send_queue is full!` / `recv_queue is full!`, device limps (user button no longer lights the display), while the 5 s `[GPS]` line may still print.

**Prediction (falsifiable):**
- `DBGLOG-ON`  → **BLE jams** (queue-full spam, app stalls).
- `DBGLOG-OFF` → **stable** (no queue-full, app syncs), with the GPS autobaud + `[GPS]` line still running.
- ⇒ confirms the wedge is the `BLE_DEBUG_LOGGING` flood, **not** the GPS work (prior misattribution).

**Falsifier:** if `DBGLOG-OFF` **also** jams (GPS `[GPS]` line still printing), the status line is genuinely implicated → the design's logging section re-opens and `setTxTimeoutMs(0)` stays a live question. **I'm wrong in that case — say so.**

**Decision it drives:** keep / drop / debug-gate `setTxTimeoutMs(0)` and the `[GPS]` line in the implementation (design §5.2).

---

## D2 — M100 identity (read-only `MON-VER`)

Settles genuine-u-blox vs clone → which command dialect the future *active* phase uses. Does **not** block passive detection.

**Steps:**
1. Wire the **M100 on a USB-UART adapter** (adapter TX→M100 RX, adapter RX→M100 TX, GND common; M100 VIN per HARDWARE.md — 3V3). Note the adapter's COM port.
2. `python scripts/_probe_ublox.py <adapter_COM> 115200 4`
   (sends `UBX-MON-VER` = `B5 62 0A 04 00 00 0E 34`, captures 4 s.)
3. If inconclusive, retry at `9600` and check TX/RX orientation.

**Verdict (printed by the tool):**
- UBX-MON-VER frame + version strings ⇒ **genuine u-blox** (UBX-configurable later).
- NMEA only, no UBX ⇒ **clone** (drive via NMEA/PUBX).
- Nothing ⇒ wrong baud/wiring; retry.

**Decision it drives:** the command-dialect assumption for the later active-probe phase (design §7). The M100 *already reads* at 115200 in firmware `[verified: prior gps-jam log]`; D2 only classifies *how to command it*.

---

## After D1 + D2
Record both captures under `docs/diagnostics/` (redact nothing sensitive — GPS coords from a real fix are PII-adjacent; keep raw logs local, summarize the verdict only). Update design §5.2 + §7 with the evidence, Gemini-review, then bring the finalized design to the human for AGREE before opening the implementation Task.
