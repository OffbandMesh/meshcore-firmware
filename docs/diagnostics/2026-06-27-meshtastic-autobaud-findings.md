# Task 1 findings — How Meshtastic does GPS autobaud / modem detection

**Issue:** OffbandMesh/meshcore-firmware#217 (Epic #216) · **Citadel:** Crosswire-i3g
**Date:** 2026-06-27 · **Source (local, no web fetch):** `C:\Dev\LoRa\meshtastic-firmware\src\gps\GPS.cpp` + `GPS.h` + `configuration.h`
**Status of every claim below:** `[verified: <file:line>]` from reading the source. No on-device claims — that's later, on serial.

---

## TL;DR (plain grounding)

Meshtastic doesn't "autobaud" in the UART-line-rate-sensing sense. It **brute-forces a small list of baud rates, and at each baud rate runs a 7-step state machine that pings each known chipset family with its own command and looks for that family's signature reply.** The whole thing is built as a **cooperative state machine that does one bounded step per scheduler tick and yields** — that is the specific reason it can't wedge the device. It talks only to a **dedicated UART (`Serial1`), never the USB console.**

The L76K — our standard part — is handled as the **explicit last-ditch case**: it is *not* detected by boot messages, only by an active query at step 4.

---

## Q1 — Which baud rates, in what order?

`[verified: GPS.cpp:487-488]`
```c
static const int serialSpeeds[3]     = {9600, 115200, 38400};
static const int rareSerialSpeeds[3] = {4800, 57600, GPS_BAUDRATE};
```
- **Common list first** (9600 → 115200 → 38400), looped `GPS_PROBETRIES = 2` times `[verified: GPS.cpp:492, 507-515]`.
- **Then the rare list** (4800 → 57600 → GPS_BAUDRATE) `[verified: GPS.cpp:518-526]`. (Skipped on ESP32-C6 `[verified: GPS.cpp:517]`.)
- Default `GPS_BAUDRATE = 9600` `[verified: configuration.h:351]`.
- A variant may set `GPS_BAUDRATE_FIXED 1`, which collapses both lists to the single known baud — **no probing** `[verified: GPS.cpp:482-485, configuration.h:352-354]`.

**Relevance to us:** both of our parts are covered by the common list — L76K at 9600 (slot 0), M100 Mini at 115200 (slot 1). We would not even need the rare list for the two parts in hand.

## Q2 — What handshake confirms a modem at a given baud?

`probe(serialSpeed)` is a `switch(currentStep)` state machine, 7 cases `[verified: GPS.cpp:1271-1480]`. Per baud it walks:

| Step | Writes | Looks for | Detects |
|---|---|---|---|
| 0 | sets baud; optional HW-reset + passive boot-msg sniff; then silences NMEA | `$PAIR021,AG3335` / `UC6580` etc. | AG3335/AG3352/UC6580 `[GPS.cpp:1300-1310]` |
| 1 | `$PDTINFO` | `UC6580`/`UM600`/`CM121` | Unicore family `[GPS.cpp:1331-1333]` |
| 2 | `$PCAS06,1*1A` | `$GPTXT,01,01,02,HW=ATGM33..` | CASIC ATGM33x `[GPS.cpp:1339-1343]` |
| 3 | `$PAIR021*39` | `$PAIR021,AG33..` | Airoha `[GPS.cpp:1350-1356]` |
| **4** | `$PQTMVERNO*58`; **`$PCAS06,0*1B`** | LC86; **`$GPTXT,01,01,02,SW=`** | LC86; **L76K → `GNSS_MODEL_MTK`** `[GPS.cpp:1362-1363]` |
| 5 | `$PMTK605*31` | `Quectel-L76B` / `1010D` … | MTK family `[GPS.cpp:1371-1378]` |
| 6 | binary UBX `CFG` poll (0x06,0x08) + `MON-VER` (0x0A,0x04) w/ checksum | UBX ACK frame | u-blox `[GPS.cpp:1384-1409]` |

Matching is **substring search on the reply**, three flavors:
- NMEA text exact-ish: `getACK(msg, timeout)` → `strnstr(buffer, message, …)` `[verified: GPS.cpp:234-267, esp. 256]`
- NMEA text against a map: `getProbeResponse(timeout, responseMap, …)` → `strstr` per `ChipInfo` `[verified: GPS.cpp:1485-1535, esp. 1512]`
- u-blox **binary** framing: `getACK(class_id, msg_id, …)` `[verified: GPS.cpp:331+, called 1389]`

## Q3 — How are modem families distinguished?

By **protocol shape**, not by line speed:
- **NMEA/ASCII** parts (CASIC/L76K, MTK, Unicore, Airoha) answer ASCII `$P…` queries with `$…TXT` / `$PAIR021` / `$PDTINFO` strings `[verified: GPS.cpp:1333,1343,1356,1363]`.
- **u-blox** speaks **binary UBX** (`0xB5 0x62` sync + class/id + checksum), detected last `[verified: GPS.cpp:1384-1409]`.
- **L76K is deliberately the last-ditch NMEA case** — the passive boot-message detector explicitly *skips* it (`"as L76K is sort of a last ditch effort, we won't attempt to detect it by startup messages"`) `[verified: GPS.cpp:1305-1306]`; it's only found by the active `$PCAS06,0` query at step 4 `[verified: GPS.cpp:1363]`.

## Q4 — Timeouts / retries / give-up (the anti-wedge design) — most important for us

This is the part Ben's rules #1/#8/#10 care about. Three layers:

1. **Every serial read loop is bounded by a `millis()` deadline** and returns `NONE`/`UNKNOWN` on expiry — no unbounded waits anywhere:
   - `getACK(str)`: `while (millis() < startTimeout)` … `return GNSS_RESPONSE_NONE` `[verified: GPS.cpp:243, 266-267]`
   - `getProbeResponse`: `while (millis() - start < timeout)` … `return GNSS_MODEL_UNKNOWN` `[verified: GPS.cpp:1499, 1534]`
2. **One step per scheduler tick, then yield.** `runOnce()` calls `setup()`; if not done it `return currentDelay` and gives the CPU back `[verified: GPS.cpp:1099-1100]`. `probe()` executes a *single* `currentStep`, sets the next step + `currentDelay`, and returns `[verified: GPS.cpp:1324-1326, 1334-1336, …]`. So the worst-case *blocking* burst is one step (≤1 s: Airoha is 1000 ms, step 4 is 2×500 ms) — never a long spin.
3. **Bounded give-up.** Total UBX miss → `currentDelay=2000; currentStep=0`, retry the whole cycle in 2 s `[verified: GPS.cpp:1390-1394]`. After `GPS_PROBETRIES` full passes over all bauds with nothing found → `return true`, give up, fall back to `GPS_BAUDRATE` `[verified: GPS.cpp:518-524]`.

**Takeaway:** the non-wedge guarantee is *structural* — cooperative state machine + hard per-read timeouts — not a `catch()`. That's the pattern we should mirror.

## Q5 — Persisted? Reboot / hot-swap behavior?

`[verified: GPS.h:164-180]` `currentStep/currentDelay/speedSelect/probeTries` are plain RAM members; `[verified: grep]` no `save/store/prefs/nvs` write of the detected baud or model anywhere in the probe path.
- → **Detected baud is NOT persisted. It re-probes from scratch on every boot.**
- Once detected in a session, `gnssModel != UNKNOWN` short-circuits re-probing until reboot `[verified: GPS.cpp:506]` — so a **hot-swap mid-run is not re-detected** (cached model wins until restart).

**Design choice for us:** persisting the last-good baud could shorten boot, but Meshtastic deliberately doesn't — simpler, and robust to swapping parts between boots. Worth a deliberate decision in the design Task, not a default.

## Q6 — GPS UART vs USB console separation

`[verified: GPS.cpp:41-46]` `#define GPS_SERIAL_PORT Serial1` and `_serial_gps = &GPS_SERIAL_PORT`. All probe I/O is on `_serial_gps` (`Serial1`, a dedicated HW UART); the USB CDC `Serial` is never written in the probe path. Pins come from config (`rx_gpio/tx_gpio/gps_en_gpio`) `[verified: GPS.cpp:1539-1541]`.
- → Directly satisfies Ben's rule #11 (GPS UART ≠ USB serial). Our implementation must keep the same hard separation.

---

## What is / isn't applicable to Offband

**Applicable / worth adopting:**
- The **cooperative state-machine + hard-timeout** structure as the anti-wedge backbone (Q4). This is the single most important lesson.
- The **brute-force baud list** approach (9600 + 115200 cover both our parts).
- The **active `$PCAS06,0*1B` → `$GPTXT…SW=` L76K probe** at step 4 — our standard part, a ready-made detection we can reuse to finally *prove* the L76K baud (currently unproven).
- The **dedicated-UART** discipline (Q6).

**Probably not needed (scope):**
- The full 6-family fan-out (Unicore/Airoha/ATGM/u-blox binary). For Companion we have two known parts; we can carry a **minimal** probe set (L76K NMEA + a generic "valid NMEA at this baud" acceptance for the externally-wired M100/raw-pins case) and skip the u-blox binary machinery unless a target part needs it.
- Passive boot-message sniffing + HW-reset (gated on `PIN_GPS_RESET`) — depends on whether our variants wire a reset pin.

**Open questions to carry into the design Task (NOT answered here):**
- Is the **M100 Mini** NMEA-compatible with the L76K `$PCAS` command set, or does the externally-wired case need a "just accept any valid NMEA at a matched baud" path rather than chipset ID?
- Do we **persist** the detected baud or always re-probe (Meshtastic re-probes)?
- Which Offband file owns the GPS UART today, and does it already separate from USB? (→ Task 2 inventory + a fresh read of our `src/**` GPS path.)
