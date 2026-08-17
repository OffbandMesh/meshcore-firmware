# RC32 beta binary — working checklist

> **Mirror of [#773](https://github.com/OffbandMesh/meshcore-firmware/issues/773).**
> The issue is the source of truth (board discipline); this file exists so the checklist is
> readable when GitHub is unavailable — it was unreachable during the 2026-08-17 incident,
> which is why this copy exists. If the two disagree, the issue wins. Update both together.

Snapshot: 2026-08-17, after the overnight #702 / #763 / #766 session.

---

## Path to a tester binary

Ordered — each item unblocks the next.

- [x] **1. Merge #772** — four rc32 envs added to `.github/release-envs.txt`.
      **Merged `36b6936a`, 2026-08-17T15:14Z**, confirmed on `firmware-base`.
      *Without this the release pipeline emitted no RC32 asset at all.*

- [ ] **2. Land `fix/704-rc32-no-gps`** — 4 commits, unmerged, **no PR yet**.
      **This is the real blocker on tester safety**, not #772:

      4c0d212b  disable GPS on heltec_rc32 -- its enable pin is the VDD_SPI strap
      bfcafb97  strip peripherals the RC32 does not have
      25c1ef80  align heltec_rc32 to Heltec's own board definition
      3467ef54  DISPLAY_ROTATION=0 -- landscape, owner-confirmed

      Both the GPS and rotation changes are already hardware-confirmed. They simply never
      landed. Until they do, the stock env ships with GPS **enabled** on a pin that is the
      VDD_SPI strap.

- [ ] **3. Add rc32 envs to `ci.yml`** — no rc32 env is in the CI matrix, so `ci-green` does
      not cover them and nothing catches a break on push. Mirror the four release envs.

- [ ] **4. Hardware-verify the STOCK env on `rc32-bench-1`** — one flash, then confirm boot,
      display, BLE pairing, radio.
      **No RC32 companion env has ever been booted in stock form.** Every bench result to
      date came from `..._ble_diag`, which locally sets `ENV_INCLUDE_GPS=0` and
      `DISPLAY_ROTATION=0` — masking exactly what #704 fixes. This is the step that would
      otherwise burn the tester.

- [ ] **5. Confirm the tester's sub-variant** — RC32-**L62** (HT-RA62A LoRa) vs RC32-**68**
      (HT-HC01_V2 Wi-Fi HaLow). Different pinouts; only L62 is supported. A HaLow unit needs
      a new variant — new work, not a build. Ask before promising anything.

- [ ] **6. Cut `offband-v1.5.0-beta3`** — requires the release preview and an explicit human
      "ship it" per `VERSIONING.md`. Must come **after** items 2 and 4.

- [ ] **7. Send the tester the release link** — not a CI artifact, not a file from the bench.

---

## Carried debt from the overnight session

- [ ] **#763 — level-gate the UART0 mirror.**
      It is a synchronous bounded busy-wait at 115200 baud and now runs regardless of capture
      state. With `BLE_DEBUG_LOGGING=1` it blocked inside `setup()`: boot 2 s → 118 s → never
      completing. Contained today (`OFFBAND_MESHLOG_UART0` is set on one env only) but must be
      fixed before the mirror spreads. Options recorded on the issue: give the mirror its own
      level ceiling, or make it drop rather than wait when the FIFO is full (matching #447).

- [ ] **#754 — CrashLog boot telemetry is a tautology.**
      `rtc_count` is compared against `nvs_count` but sourced from the same place, so the
      check always passes and reports health it never verified. Open, unresolved.

- [ ] **Commit the diagnostic tooling.** Uncommitted in the primary clone on `firmware-base`;
      needs its own branch + issue:
      - `tools/diag/rc32-boot-740/scripts/battery_runtime.py` (new)
      - `tools/diag/rc32-boot-740/rc32_uart_sniffer_v3/rc32_uart_sniffer_v3.ino` (modified —
        board-agnostic pin map via `SNIFFER_GENERIC_S3`)
      - this file

---

## Heltec beta deliverables

- [ ] **Submit the feedback form.** Drafted and saved, gitignored, at
      `docs/radiocore/vendor/RC32-beta-feedback-submitted-2026-08-17.md`.
      Name and Discord ID are the owner's to fill; nothing is submitted by an agent.

- [ ] **Finish the discharge run and attach the curve to Q01.**
      Capture runs on a detached process (COM16 → `tools/diag/rc32-boot-740/evidence/763-power-telemetry.log`).
      Analyse with `tools/diag/rc32-boot-740/scripts/battery_runtime.py`.
      Attach a **trimmed CSV**, not the raw multi-hour log.
      The average-mA figure is worth as much as the runtime — it is the input for the
      solar/car build, and the number Heltec cannot answer.

---

## Next board

- [ ] **RCC6 bring-up (#624).**
      No variant exists in-tree and upstream MeshCore has none, so we would be first.
      The starting point is real now: the carrier schematic reads reliably via PyMuPDF
      coordinate extraction — row-pair net labels against pin labels, then render the region
      to confirm visually. Text extraction alone repeatedly misled during #766; do not trust
      the flattened dumps.
      **Caveat:** the UART0 beacon/mirror is **unverified on ESP32-C6** — RISC-V, different
      register layout. It reads `UART_TXFIFO_CNT_S` from the target's own `soc/uart_reg.h` so
      it should adapt, but that is untested.

---

## Closed overnight, for context

- **#702** — RST no-boot. Root cause `Serial.setTxTimeoutMs(0)`: `tries--` underflows to
  4.29e9, making the escape hatch unreachable (~50 days). Offband-introduced — not upstream,
  not Heltec. Fixed.
- **#766** — RC32 battery read. **Not a firmware defect:** reversed battery leads. Every
  configured value matches the schematic (`ADC_IN`=GPIO7, `ADC_Ctrl`=GPIO15 active-high,
  R36 390K / R38 100K = 4.9).
- **#769 / PR #770** — UART0 mirror, capture decoupling, battery telemetry. Merged.
- **#771 / PR #772** — rc32 envs in the release matrix. Merged.
- **#762** — display work: colour splash, dark palette, antialiased font. Merged.
