# Heltec V4 — FEM / LNA verified ground truth

**Issue:** [#321](https://github.com/OffbandMesh/meshcore-firmware/issues/321) · **Epic:** #320 · **Feature:** #319
**Citadel:** `Crosswire-sw2` · **Agent:** BrownHawk (session `8a9de97d`)
**Date:** 2026-07-19

## Purpose

Adversarial re-verification of every Heltec V4 FEM/LNA claim carried in this repo. Prior in-repo
material (firmware comments, `HARDWARE.md`, issue #298, the 2026-07-18 LLM-consult logs) was
treated as **unverified assertion**, not as input. Claims are tagged `[verified: <source>]` or
`[hypothesis: untested]` per SAFELANE §11. Where a source could not be obtained, that is recorded
as the finding rather than inferred around.

**Source hierarchy used:** Heltec schematics and Heltec official documentation > component vendor
datasheets > upstream MeshCore PR discussion > repo code (evidence of firmware behavior only,
never of hardware fact).

---

## 1. Executive summary

Five things matter:

1. **The two-FEM story is real and confirmed** — GC1109 on the older board, KCT8103L on the newer.
   Verified directly from Heltec's own schematics, not from code comments.
2. **The revision *labels* used in this repo are not Heltec's documented ones.** Heltec's hardware
   update log knows only **V4.0** and **V4.3.1**. "V4.2" and "V4.3" are *schematic filenames*, and a
   separate *datasheet document* is versioned `V4.2.0`. The repo treats these as board revisions.
   (This says the labels are undocumented — not that no such board shipped.)
3. **GC1109 has no LNA bypass at all.** Its receive path is always through the LNA. So "LNA is only
   controllable on KCT8103L" is correct — but not for the reason the code implies.
4. **Therefore the upstream default is backwards.** MeshCore 1.16 defaults the LNA *off* on the
   newer KCT8103L boards. The older GC1109 board physically cannot turn its LNA off. Net effect:
   **the newer, better board ships with worse receive than the older one.** This is the substance
   behind #298.
5. **The FEM auto-detect's stated mechanism is wrong**, even though its outcome may be right. The
   code attributes detection to chip-internal pull resistors; the GC1109 datasheet documents none,
   and the schematics show external ones.
6. **The two boards hand a different FEM pin to the radio.** On the GC1109 board the SX1262's DIO2
   drives **CTX**; on the KCT8103L board the MCU takes CTX on GPIO5 and DIO2 most likely drives
   **CPS** instead. This explains the firmware's asymmetric handling, which had looked like an
   inconsistency.

**Load-bearing gap:** no KCT8103L datasheet could be obtained anywhere. The polarity of the
user-facing `fem on/off` control is therefore **unverified**, and several conclusions below are
explicitly conditional on it. One bench test closes it (§8).

### Review history

This document was revised after an adversarial review (Gemini 2.5 Pro,
`docs/llm-consultations/2026-07-19-f321-fem-lna-research-gemini-gemini-2.5-pro.log`) returned a
BLOCKER: the DIO2→CTX claim had been tagged `[verified:]` against a GitHub PR *discussion* rather
than a schematic — the same evidentiary failure this document was chartered to correct. The review
was upheld in full. Rather than downgrade the tag, the claim was independently re-verified from the
V4.2 schematic (R5), which in turn surfaced R8. Two MAJOR findings (R1 overreach, #298 punted as
"out of scope") and one MINOR (R7 under-prioritised) were also upheld and corrected.

---

## 2. Revision map — what Heltec actually publishes

`[verified: Heltec Hardware Update Log]` — the official log documents exactly two entries:

| Revision | Date | Relevant changes |
|---|---|---|
| **V4.0** | 2025-09-24 | ESP32-S3N8 → S3R2; 8 MB internal → 16 MB external flash + 2 MB PSRAM; LoRa output "21 ± 1 dBm to 28 ± 1 dBm"; removed CP2102; added SH1.25-8Pin GNSS interface; 36 → 40 pin |
| **V4.3.1** | 2026-02-25 | **FEM upgraded to KCT8103L (enables software control of RX LNA)**; **GPIO5 reassigned as FEM control pin**; **GPIO46 freed for user applications**; reverse-polarity MOSFET AO3400 → **SI2302**; SAW filter pads **U10 and U11 reserved, unpopulated by default**; max output remains 28 dBm (explicitly *not* 30 dBm) |

`[verified: resource.heltec.cn schematic directory listing]` — separately, two schematics are published:

- `WiFi_LoRa_32_V4.2.pdf` (listed 2025-12-01)
- `HTIT-WB32LAF_V4.3.pdf` (listed 2025-12-26)

`[verified: Heltec resource server]` — and the product datasheet is `WiFi_LoRa_32_V4.2.0.pdf`, a
**document** version number.

### Finding R1 — "V4.2" and "V4.3" are not *documented* board revisions

Heltec's revision log jumps V4.0 → V4.3.1 with nothing between. The strings "V4.2" and "V4.3" in
this repo trace to *schematic filenames*, and there is an additional collision with the
`V4.2.0`-versioned datasheet document.

**Scoped deliberately.** This says the labels are undocumented in Heltec's customer-facing log — it
does **not** establish that no such board shipped. A changelog is a marketing summary, not an
engineering manifest, and Heltec plainly produced schematics under both names. Absence from the log
is not absence of hardware; an earlier draft of this finding overreached on exactly that point.

**Consequence:** the firmware's board-name strings `"Heltec V4.3 OLED"` / `"Heltec V4.3 TFT"`
(`variants/heltec_v4/HeltecV4Board.cpp:81-83`) report a revision Heltec does not publish, and
derive it purely from FEM part. A KCT8103L board is most plausibly a **V4.3.1**.
`[hypothesis: untested]` — whether a distinct physical "V4.3" shipped, or whether that schematic
was a pre-release of V4.3.1, is unresolved; Heltec's community thread on revision updates was
unreachable (`community.heltec.cn` refused connection) and is the outstanding source.

---

## 3. FEM parts — confirmed from schematics

`[verified: WiFi_LoRa_32_V4.2.pdf]` — reference designator **U10 = GC1109**, 16-pin:
`TX1 RX2 TX_ALT3 CSD4 CPS5 CTX6 LNA_IN7 RX_FLT8 ANT9 GND10 TX_IN11 GND12 PA_OUT13 N/C14 VCC0-15 VCC1-16 GND17`.
Control nets named `PA_CSD`, `PA_CPS`, `PA_CTX`.

`[verified: HTIT-WB32LAF_V4.3.pdf]` — reference designator **U8 = KCT8103L**, 16-pin:
`TX1 RX2 NC3 CSD4 CPS5 CTX6 LNA_IN7 RX_FLT8 ANT9 GND10 TX_IN11 GND12 PA_OUT13 N/C14 VCC0-15 VCC1-16 GND17`.
Same control nets `PA_CSD`, `PA_CPS`, `PA_CTX`.

### Finding R2 — the two FEMs are pin-compatible

The only pinout difference is **pin 3**: `TX_ALT` (GC1109) vs `NC` (KCT8103L). Both expose the same
three control pins in the same positions. This is a drop-in substitution at the footprint level.

**Consequence:** the firmware's *asymmetric* treatment of the two parts — driving CPS on the GC1109
branch and CTX on the KCT8103L branch — is **not** explained by pinout. It is explained by which
FEM pin the SX1262's DIO2 takes over on each board. See **R8**.

### Finding R3 — the V4.3 board carries the reserved SAW filter footprints

`[verified: HTIT-WB32LAF_V4.3.pdf]` — `U10 NC` and `U11 NC`, both 5-pin `IN1 GND2 GND3 OUT4 GND5`
footprints, unpopulated. This corroborates Heltec's V4.3.1 log entry verbatim. The V4.2 schematic
has no equivalent.

---

## 4. GC1109 — full control logic, from the vendor datasheet

`[verified: Geo-chip GC1109 Preliminary Data Sheet Rev0.9.2]`

**Table 4, Control Logic (VBAT & VCC on):**

| Mode | CSD | CTX | CPS |
|---|---|---|---|
| Shutdown | 0 | X | X |
| **Receive LNA mode** | 1 | **0** | X |
| Transmit bypass | 1 | 1 | 0 |
| Transmit | 1 | 1 | 1 |

`"1"` > 1.2 V; `"0"` < 0.3 V; `X` = don't care.

**Electrical specifications:**

| Parameter | Value |
|---|---|
| Frequency range | 860–930 MHz |
| Saturated output power | 30 dBm |
| TX small-signal gain | 30 dB |
| **Receive gain** | **17 dB** |
| **Receive noise figure** | **2 dB typ / 2.5 dB max** |
| Insertion loss, TX bypass | 1 dB |
| Supply current, TX @ +30 dBm | 600 mA |
| Supply current, RX | 6.5 mA |
| Sleep current | < 1 µA |
| Package | 16-pin MCM 3.0 × 3.0 × 0.75 mm |

Note from the datasheet: *"VBAT must be prior to CSD/CPS/CTX for the power on sequence."*

### Finding R4 — GC1109 has no LNA bypass

There is exactly one receive mode. `CSD=1, CTX=0` selects "Receive LNA mode" and CPS is explicitly
don't-care. There is **no state in which the part receives with the LNA bypassed.** The "bypass" in
this datasheet is *transmit* bypass — it bypasses the PA, not the LNA.

**Consequence:** `setLnaCanControl(true)` being set only on the KCT8103L branch
(`LoRaFEMControl.cpp:33`) is **correct**, and `canControlLoRaFemLna()` returning false for GC1109 is
correct. But the firmware carries no statement of *why*, and the reason is not "the older part is
worse" — it is that the older part is permanently in its best receive state.

### Finding R5 — the GC1109 code comments are datasheet-accurate

`variants/heltec_v4/platformio.ini:22-23` and `LoRaFEMControl.cpp:63` describe
`CPS: 1 = full PA, 0 = bypass` and `for RX, CPS is don't care`. Both match Table 4 exactly.
`[verified: GC1109 datasheet Table 4]`

I initially read this as a bug — that RX requires `CTX=0` and the firmware never drives CTX. It is
not a bug: **CTX is driven by the SX1262's DIO2 RF-switch output**, not by the MCU. The MCU drives
only CSD (GPIO2) and CPS (GPIO46). Recording the refuted hypothesis deliberately — the code was
right and the first reading was wrong.

**Sourcing note (this claim was re-worked after adversarial review).** It was first tagged
`[verified:]` against the *discussion* in upstream PR #1249 — a GitHub comment standing in for a
schematic-level fact. That is precisely the failure mode this document exists to eliminate, and the
review was correct to call it a blocker. It has been re-verified independently:

`[verified: WiFi_LoRa_32_V4.2.pdf, positioned-text extraction via PyMuPDF]` On the SX1262 symbol
(U9), the left-edge pins are 7–12, terminating at **pin 12 = DIO2**. Pin-name glyphs sit at a
consistent **+2.6 pt** offset from their pin-number glyphs across that edge (pin 9→`DCC_SW`,
10→`VBAT`, 11→`VBAT_IO`, 12→`DIO2`). The net label **`PA_CTX` sits at y = 523.9**, against
`DIO2` at y = 521.3 — Δ2.6, matching the established offset exactly, and positioned to the left of
the symbol body where an outgoing wire label belongs. No other left-edge pin is within a pin pitch
(5.3 pt). **DIO2 → FEM CTX on the GC1109 board is confirmed from the schematic.**

*Method limits, stated plainly:* this is geometric inference over extracted text, not a rendered
net trace (the PDF render path is unavailable on this host — missing native canvas binding). It is
strong where an offset pattern is consistent and a single candidate falls inside one pin pitch, and
it is **not** conclusive where labels are ambiguous — see R8, where it deliberately stops short.

---

### Finding R8 — the two boards hand a *different* FEM pin to the radio

Applying the same positioned-text method to the V4.3 sheet does **not** reproduce the V4.2 result,
and that difference is itself the finding.

`[verified: HTIT-WB32LAF_V4.3.pdf, positioned-text extraction]` On the V4.3 SX1262 symbol (U9),
**pin 12 = DIO2 sits at y = 529.2**. No net label aligns with it at the V4.2-style Δ2.6. The nearest
left-side net label is **`PA_CPS` at y = 539.1** (Δ9.9 ≈ two pin pitches), and **`PA_CTX` does not
appear anywhere near the radio** — it is at (130.8, 286.8), up in the FEM section.

`[hypothesis: geometrically suggested, not conclusive]` **On the KCT8103L board, DIO2 most likely
drives CPS rather than CTX.** Δ9.9 is too loose to call verified — stated as hypothesis, not fact.

**Why this is coherent rather than anomalous:** `[verified: Heltec Hardware Update Log]` Heltec
states that at V4.3.1 *"GPIO5 reassigned as FEM control pin"* and *"GPIO46 now available for user
applications."* If the MCU took over **CTX** on GPIO5 and gave up GPIO46 (which was **CPS** on the
older board), then DIO2 must land on the remaining control pin — CPS. That is exactly the split the
firmware implements:

| Board | FEM | MCU drives | SX1262 DIO2 drives |
|---|---|---|---|
| V4.2-era | GC1109 | CSD (GPIO2), CPS (GPIO46) | **CTX** `[verified]` |
| V4.3.1 | KCT8103L | CSD (GPIO2), CTX (GPIO5) | **CPS** `[hypothesis]` |

**Consequence:** the asymmetry flagged in R2 is not a firmware inconsistency — it tracks a real
board-level rewiring. It also means the LNA control on the newer board works by the MCU holding CTX
while the radio toggles CPS, which is a materially different arrangement from the older board and
should be documented as such rather than described as "the same FEM logic with an extra pin."

---

## 5. KCT8103L — the gap

**No public datasheet was obtained.** Attempts:

- Web search returns no vendor datasheet; the part is from Kxcomtech / 康希通信 (Kexi Communications).
- The one distributor page carrying documentation (`sekorm.com/product/517646801.html`) returned
  **HTTP 457** and could not be read.
- The part does not appear in Mouser/Digi-Key indexed results.

**Therefore the following remain `[hypothesis: untested]` and must not be stated as fact:**

- That `CTX` LOW = LNA engaged and HIGH = LNA bypassed (`LoRaFEMControl.cpp:77-82`, `:96-100`).
  This is the *entire* semantic basis of the user-facing `fem on|off` control.
- That KCT8103L's control truth table matches GC1109's despite the shared pinout.
- KCT8103L LNA gain, noise figure, and TX gain. `HARDWARE.md` states its advantage over GC1109 is
  RX noise figure rather than TX gain — **unsourced**; no datasheet supports or refutes it.

**What *is* verified about it:** `[verified: Heltec Hardware Update Log]` Heltec states the KCT8103L
upgrade "enables software control of RX LNA" and that GPIO5 became the FEM control pin. That
corroborates the *existence* and *direction* of the control, and corroborates the pin, but not the
polarity or the electrical behavior.

### Finding R6 — the user-facing LNA control rests on an unsourced polarity

`fem on` → `radio_fem_rxgain = 1` → `setLNAEnable(true)` → `CTX = LOW`. If that polarity is
inverted relative to the real part, the control does the opposite of what it says, and no in-repo
evidence would catch it. **This is the highest-value bench test available** (§8).

---

## 6. Board identification

### 6.1 By firmware probing — mechanism is misstated

Current probe (`LoRaFEMControl.cpp:19-41`): release RTC hold on GPIO2, set INPUT, delay 1 ms, read.
HIGH → KCT8103L; LOW → GC1109. The comment attributes this to:

> `GC1109 CSD: internal pull-down → reads LOW` / `KCT8103L CSD: internal pull-up → reads HIGH`

**Finding R7 — the stated mechanism is not supported.** `[verified: GC1109 datasheet §6-7]` The
GC1109 datasheet's pin description and recommended operating conditions document **no internal
pull-up or pull-down on CSD, CPS, or CTX.** Meanwhile `[verified: both schematics]` the FEM control
nets sit alongside external resistors (10 kΩ parts `R40`/`R42`/`R33`/`R39` on the V4.3 sheet;
`R44 10K` on the V4.2 sheet) — i.e. any defined idle level is most likely a **board-level** pull,
not a chip-internal one.

The probe's *outcome* may still be reliable — a board-level pull is arguably a more dependable
discriminator than a chip-internal one. But the documented rationale is wrong, and anyone reasoning
from it (e.g. porting the probe to another board) will get it wrong.

`[hypothesis: untested]` — the exact net-to-resistor topology per revision (which resistor, to which
rail, on which net) is **not** established. The positioned-text method that settled R5 does not
resolve it: resistor designators and value glyphs (`R40 10K`, `R42`, `R33`, `R39`) extract without
the connectivity that gives them meaning.

**This is a required action, not an optional one.** Ground truth for a detection mechanism is not
complete while the mechanism is unknown — the probe is currently trusted on outcome alone. Closing
it needs eyes on the two schematic sheets (a human, or a host where PDF rendering works). Until
then the auto-detect should be treated as empirically-working-for-unknown-reasons.

### 6.2 By visual inspection — one solid discriminator, one weak

| Method | Reliability | Basis |
|---|---|---|
| **FEM package marking** (read the part on the RF section) | Strong, but needs magnification | `[verified: schematics]` GC1109 at **U10** vs KCT8103L at **U8** — note the *designator itself differs between revisions*, so locate by position in the RF chain, not by silkscreen number |
| **Unpopulated 5-pin filter footprints (U10/U11)** near the RX chain | Strong | `[verified: V4.3 schematic + Heltec log]` present only on the KCT8103L board |
| **Reverse-polarity MOSFET marking** | Moderate | `[verified: Heltec log + schematics]` AO3400/AO3401A → **SI2302** at V4.3.1. Si2302 present ⇒ V4.3.1+ |
| Silkscreen revision string | Unverified | `[hypothesis: untested]` no source confirms what revision string, if any, is printed on the board |

---

## 7. Adjudication of prior in-repo claims

| # | Claim | Verdict |
|---|---|---|
| 1 | V4.2 carries GC1109; V4.3 carries KCT8103L | **Confirmed as to parts** `[verified: both schematics]`; **revision labels are not Heltec's** (R1) |
| 2 | FEM auto-detected via GPIO2 default pull level | **Behavior confirmed in code; stated mechanism refuted** (R7) |
| 3 | LNA controllable only on KCT8103L | **Confirmed** — and the reason is that GC1109 has no LNA bypass at all (R4), not that it is deficient |
| 4 | KCT8103L: CTX LOW = LNA on, HIGH = bypass | **Unverified — no datasheet obtainable** (R6). Load-bearing for the user-facing control |
| 5 | GC1109 CPS: 1 = full PA, 0 = bypass; don't-care in RX | **Confirmed** `[verified: GC1109 Table 4]` (R5) |
| 6 | `HARDWARE.md`: high-power V4.3 = +28 ±1 dBm at antenna | **Consistent with Heltec** `[verified: update log]` — "maximum output remains 28 dBm (not 30 dBm)", despite the GC1109's own 30 dBm saturated rating |
| 7 | `HARDWARE.md`: KCT8103L's advantage is RX noise figure, not TX gain | **Unsourced.** Plausible and directionally consistent with Heltec's "software control of RX LNA" framing, but no datasheet backs it |
| 8 | `HARDWARE.md`: KCT8103L small-signal gain is undocumented publicly | **Confirmed** — independently reproduced here (§5) |
| 9 | Board name string derived from FEM type | **Confirmed in code; semantically wrong** (R1) — it reports an unpublished revision |
| 10 | #298: companion role never applies `radio_fem_rxgain` | **Code claim not re-verified here, but its implications are now settled.** R4 makes any LNA control on GC1109 boards **moot** — there is nothing to apply. On KCT8103L boards the miss is real and matters: combined with the upstream LNA-off default (§8), an unfixed companion role runs permanently LNA-bypassed on the better front end. **Priority is high *if* R6 polarity confirms**; the fix should be gated on `canControlLoRaFemLna()` so it is a no-op on GC1109 rather than a wasted write |

---

## 8. What this means for a user-facing setting

**Which boards can expose an LNA preference:**

- **KCT8103L boards (V4.3.1)** — yes. Heltec states software LNA control is the point of the part.
  Exposed today as `fem on|off|status` + persisted `radio_fem_rxgain`.
- **GC1109 boards (V4.0-era)** — **no, and none is needed.** The part has no LNA bypass; it is
  always in its best receive state. The correct UX is to report the control as unavailable, which
  `canControlLoRaFemLna()` already does.

**The default is the real problem** — *conditional on R6*. Upstream MeshCore 1.16 defaults the LNA
off on exactly the boards that have the better front end. A user upgrading V4.0 → V4.3.1 gets
*worse* receive unless they know to issue `fem on`. Offband already defaults it ON for repeaters;
#298 covers the roles that were missed.

This conclusion follows strictly from three premises — (1) GC1109 is permanently LNA-on `[verified]`,
(2) KCT8103L boards default LNA-off `[verified: code]`, and (3) the `fem on/off` polarity is not
inverted `[hypothesis — R6]`. Premises 1 and 2 are solid; **premise 3 is not yet evidence.** If the
polarity turns out inverted, the conclusion flips entirely and the current default is accidentally
correct. Do not repeat this framing to users until the §8 bench test has run.

**Before any of this is asserted to users — one bench test settles R6:**

> On a KCT8103L board, hold the radio in RX and read the noise floor with `fem on` vs `fem off`.
> LNA engaged should show a **higher** noise floor (LNA gain raises the noise floor along with the
> signal) and better sensitivity on a weak, distant node. If `fem off` shows the higher noise
> floor, **the polarity is inverted** and the control is backwards.

That test needs no new tooling and no probe — the CLI and `noise_floor` reporting already exist.

---

## 9. Outstanding sources

| Source | Status | Why it matters |
|---|---|---|
| KCT8103L datasheet (Kxcomtech) | **Not obtained** — `sekorm.com` returns HTTP 457 | Settles R6 polarity, LNA gain, NF — the highest-value gap |
| `community.heltec.cn` revision thread | **Not obtained** — connection refused | Would settle whether a distinct "V4.3" shipped (R1) |
| Visual reading of both schematic sheets | **Required, not done** | Pull-resistor topology behind the FEM auto-detect (R7); confirmation of DIO2→CPS on V4.3 (R8) |

**Tooling note:** PDF page rendering is unavailable on this host — the PDF toolkit's native canvas
binding (`@napi-rs/canvas-win32-x64-msvc`) fails to load, and `pdftoppm`/poppler is not installed.
Positioned-text extraction via PyMuPDF was used instead and was sufficient for R5 but not for R7/R8.
Installing poppler or repairing the canvas binding would close both gaps without needing a human.

---

## 10. Recommended follow-ups (do not action from this doc — file under #320)

1. Correct the FEM auto-detect comment to describe a board-level pull, not a chip-internal one.
2. Reconcile board-name strings with Heltec's published revisions, or stop reporting a revision the
   vendor does not publish.
3. Run the §8 bench test; only then treat the `fem on/off` polarity as verified.
4. Re-adjudicate #298 against R4 (GC1109 needs no control) and the default-is-backwards framing.
5. Correct the unsourced KCT8103L claims in `HARDWARE.md` to `[hypothesis]` pending a datasheet.
6. Check whether current code still holds GPIO46 (an ESP32-S3 **strapping pin**) as OUTPUT from
   boot — the concern raised in upstream PR #1249, which was closed and superseded by #1600 with
   only the deep-sleep fix retained.

## Sources

- [Heltec — WiFi LoRa 32 V4 Hardware Update Log](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/hardware-update-log)
- [Heltec — WiFi LoRa 32 V4 product overview](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/)
- [Heltec — WiFi LoRa 32(V4.3.1) product page](https://heltec.org/project/wifi-lora-32-v4/)
- [Heltec — schematic directory](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/Schematic) (`WiFi_LoRa_32_V4.2.pdf`, `HTIT-WB32LAF_V4.3.pdf`)
- [Geo-chip — GC1109 860-930 MHz RF Front-End Module datasheet, Rev0.9.2](https://www.geochipinc.com/uploads/23940615_1756826539.pdf)
- [meshcore-dev/MeshCore PR #1249 — GC1109 FEM pin handling](https://github.com/meshcore-dev/MeshCore/pull/1249) (closed, superseded by #1600)
- [Kxcomtech KCT8103L distributor page](https://www.sekorm.com/product/517646801.html) — **inaccessible, HTTP 457**
