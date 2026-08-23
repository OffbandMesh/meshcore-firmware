# RC52 display bring-up — captured boot sessions (#951, #965)

Curated from a continuous Feather-sniffer capture, 2026-08-23 17:39Z → 18:37Z, reading the
MeshLog UART mirror off carrier header **pin 11** (P0.08, board TX), ground on **pin 20**.

The raw capture was 1,920,700 B and contains binary runs from the DFU cycles. What matters is
extracted here; the raw file was session-temporary and is not preserved.

**Why the wire and not USB:** on this board `Serial` is USB-CDC, which dies with the chip. Every
log ever captured that way necessarily comes from a boot that *succeeded*. The wire witnesses the
boots that fail — which is the entire reason the `_diag` envs set `OFFBAND_LOG_MIRROR_UART`.

---

## Every boot recorded

| Time (UTC) | Role | Reset reason | Display |
|---|---|---|---|
| 17:41:46 | repeater | `Soft Reset` (post-DFU) | **dark** — wrong pins (pre-`75ff23dc`) |
| 17:49:37 | repeater | `Reset Pin` | dark — same image |
| 18:01:02 | repeater | `Soft Reset` (post-DFU) | ✅ **renders** — first ever, corrected pins |
| 18:04:07 | repeater | `Reset Pin` | ✅ renders |
| 18:15:24 | room-server | `Soft Reset` (post-DFU) | ❌ **dark** — see #965 |
| 18:15:54 | room-server | **`Watchdog`** | ✅ renders |
| 18:34:35 | room-server | `Reset Pin` | ✅ renders |

Two independent conclusions fall out of that table:

1. **The repeater renders on its first post-DFU boot; the room server does not.** Same driver,
   same panel, same artifact lineage. So the dark first boot is role-specific, not a driver or
   a DFU property. (#965)
2. **Only `Soft Reset` on the room-server role fails.** `Watchdog` and `Reset Pin` both render.

---

## The first successful render (repeater, 18:01:02)

```
[BEACON] APP:CTOR -- earliest static ctor; bootloader + platform startup COMPLETED
[BEACON] setup:ENTRY -- before Serial.begin
[BEACON] setup:before SafeBoot::checkAndMaybeSleep
[BEACON] setup:post SafeBoot::checkAndMaybeSleep
[BEACON] setup:before board.begin
[1044] [boot] vbat=4112 mV (multiplier 4.90, nominal)
[BEACON] setup:post board.begin
[BEACON] setup:before crashLogStandardInit
[CrashLog] fresh boot; no previous-boot log to recover.
[1080] [boot] repeater up; reset=Soft Reset
[BEACON] setup:post crashLogStandardInit
[BEACON] setup:before board.beginBootSafety
[BEACON] setup:post board.beginBootSafety      <- 18:01:02.462
[1829] DEBUG: Set _preambleMillis=91           <- 18:01:03.201   (+739 ms)
[1939] DEBUG: RadioLibWrapper: noise_floor = -120
```

**The 739 ms gap is the measurement that matters.** `display.begin()` is not beacon-instrumented
in the role examples, but it sits between the last beacon and `radio_init()`. `RC52Display::begin()`
contains ~430 ms of mandated delays (reset pulse 10+20+120, SWRESET 120, SLPOUT 120, DISPON 10)
plus ~100 command writes and a full-screen clear and blit. A skipped call shows a near-zero gap.

Use this gap as the cheap proxy for "did the display driver actually run" on any future RC52 boot.

**No `back buffer alloc FAILED` line**, so the 56,320 B buffer allocated — the buffered path is
what rendered.

**`crashLogStandardInit` completed**, printing `fresh boot; no previous-boot log to recover`. The
RC32 #740/#741 CrashLog boot hang does **not** reproduce on RC52.

---

## The failing room-server boot (18:15:24)

```
[BEACON] setup:post crashLogStandardInit       <- 18:15:24.815
[1803] DEBUG: Set _preambleMillis=91           <- 18:15:25.551   (+736 ms)
[1835] DEBUG: loop - skipping busy (or evicted) client 4F
[1915] DEBUG: RadioLibWrapper: noise_floor = -119
...                                             (~29 s of normal operation)
[BEACON] APP:CTOR                              <- 18:15:53.520
[1071] [boot] room-server up; reset=Watchdog   <- 18:15:54.601
```

**736 ms — the same window as the boot that rendered.** So on the dark boot the driver ran, took
its normal init time, and produced nothing. It did not hang and it was not skipped. The board then
served a client normally for 29 s before the watchdog fired.

That is the heart of #965: a role that runs correctly, shows nothing, and resets itself once.

---

## Remote reset is NOT functional on this rig

```
18:35:54.163  <<< INJECT RST
18:35:54.165  >>> RST asserted  @up=56252s
18:35:54.265  >>> RST released -- RC32 booting
```

No boot banner followed; board `millis()` ran continuously through it ([77682] and climbing).
Owner confirmed the RST/USER wires are landed on the wrong pins.

**A failed inject is not evidence of a dead board.** SNIFFER-v3 reports `RST asserted` /
`RST released` whether or not anything is connected to A0.

Correct Feather landing, per the SNIFFER-v3 sketch header: board **pin 18 (RST) → A0**, fourth
wire → **A1**, board TX → Feather RX (or TX under `SNIFFER_RC52`), GND → GND. Note the hardware
log's 2026-08-19 entry describes a *different* rig (`wroom-sniffer`, GPIO17/GPIO16) which was not
attached during this session.

⚠ **RC52 has no BOOT pin.** RC32 pin 5 is `GPIO0/BOOT`, an ESP32 strap; RC52 pin 5 is `P1.10` =
USER. The sketch's `A1`/`BOOT` naming and its `BOOTRST` command are RC32 carry-overs and mean
nothing on this board.

---

## Incidental observations, not chased

- The room server emits `DEBUG: loop - skipping busy (or evicted) client 4F` about every 156 ms,
  continuously, whenever client `4F` is attached. Unexamined; may be normal, may be related to the
  watchdog in #965.
- Board clock reads `15/5/2024` on every boot — no RTC sync. Expected without GPS or a time source.
- `noise_floor` sits at −112 to −120 across all boots. The radio is unaffected by any of this.

Agent: RainyHeron (session 6071ff56)
