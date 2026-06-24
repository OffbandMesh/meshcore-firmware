# Photon‑1W (MeshSmith) firmware support — design of record

- **Status:** Research / design (no implementation) — deliverable of `Crosswire-jws` (#192)
- **Date:** 2026-06-24
- **Author:** CalmDune (firmware session)
- **Feature:** #190 · **Research epic:** #191 · **This task:** #192
- **Scope:** decide what it takes to add the **MeshSmith Photon‑1W** as an Offband build/flash option, per MCU variant, and define the follow‑on implementation work. EtherMesh‑1W is explicitly **out of scope** (see §7).

---

## 1. Decision summary

**Photon‑1W is a port, not a build‑from‑scratch.** It runs the same C++ MeshCore family Offband builds, on the **same MeshCore 1.16.0 base we are on**, and MeshSmith already maintains two self‑contained board variants for it (MIT, in their fork). The work is to **vendor those two variant directories** into our tree and wire their environments into our `platformio.ini`, then verify on hardware.

It is **not** a reuse of our stock `xiao_c6` / `xiao_nrf52` envs — Photon is a custom carrier board with a different LoRa pin map, a 1 W PA module, on‑board GPS, and battery charge sensing (§4). Each MCU therefore needs its **own** variant.

**Recommendation:** proceed per‑MCU, **ESP32‑C6 track first** (it's on our exact 1.16.0 base and is the simpler flash path), nRF52 second — but the **first gate is hardware/RF/power bench verification on a physical unit** (§6A), per the adversarial review (§11).

---

## 2. What Photon‑1W is

Pulled from MeshSmith's own site bundle (2026-06-24) and confirmed against the `MeshSmith/MeshCore` fork:

- Form factor: "Xiao‑based 1 W node", ~$65 DIY kit, solar‑capable (Waveshare solar add‑on).
- **Two MCU variants:** Seeed **XIAO ESP32‑C6** or Seeed **XIAO nRF52840**.
- **Radio / 1 W chain:** Ebyte **E22‑900M30S** module = SX1262 + integrated PA, **30 dBm (1 W)** output.
- On‑board **GPS** (ATGM336H on the C6 board; a Photon GPS provider on the nRF52 board) and **battery charge‑rate** sensing.
- Stock firmware: MeshSmith's own MeshCore fork (`github.com/MeshSmith/MeshCore`), a fork of `meshcore-dev/MeshCore` (our shared upstream).

### The 1 W RF chain (important)

The E22‑900M30S is **SX1262 + an integrated 30 dBm PA**. MeshSmith drives the SX1262 at **`LORA_TX_POWER=20`** and lets the module's PA bring it to 30 dBm — their comment: *"20dBm in → 30dBm out. Anything higher will cause distortion in the PA output."*

T/R switching is done **entirely via `SX126X_DIO2_AS_RF_SWITCH=1`** with `RXEN`/`TXEN` = `RADIOLIB_NC`. This differs from our existing 1 W board `lilygo_tbeam_1w`, which uses a **discrete** PA with DIO2 (TX) **plus** a separate `RXEN` GPIO (RX/LNA). So `tbeam_1w` is a useful precedent for "how Offband expresses a 1 W front end," but Photon's wiring is module‑internal and simpler.

> **Regulatory note:** 30 dBm / 1 W EIRP carries region‑specific power and duty‑cycle limits. The firmware exposes raw TX power (`LORA_TX_POWER` / `get tx`); the deployment‑side responsibility (legal power for the operator's region/band) is unchanged from any other high‑power board and should be called out in user docs, not enforced in firmware.

---

## 3. Source of truth in the fork

Photon lives on **branches**, not `main`, split by MCU. Both variant directories are present together on **`v1.16.0-meshsmith-photon`** (our exact base):

| Variant dir | MCU | Base branch to vendor from |
|---|---|---|
| `variants/meshsmith_photon_esp32c6` | XIAO ESP32‑C6 | `v1.16.0-meshsmith-photon` |
| `variants/meshsmith_photon_nrf52` | XIAO nRF52840 | `v1.16.0-meshsmith-photon` |

(`EtherMesh` is a separate branch; the nRF52 dir also exists on the older `*-nrf52` / v1.15 branches, but v1.16.0 is the right source for both.)

Each dir is self‑contained:

- **ESP32‑C6:** `platformio.ini`, `MeshsmithPhotonC6Board.cpp/.h`, `target.cpp/.h`, `ATGM336HLocationProvider.h`
- **nRF52:** `platformio.ini`, `MeshsmithPhotonNRFBoard.cpp/.h`, `target.cpp/.h`, `PhotonGPSLocationProvider.h`, `variant.cpp/.h` (custom XIAO‑nRF52 pin remap)

---

## 4. Gap analysis vs. our existing variants

Photon cannot reuse our stock XIAO envs — the LoRa control pins, I²C, and the extra peripherals differ:

| | Our `xiao_c6` | Photon **ESP32‑C6** | Our `xiao_nrf52` | Photon **nRF52** |
|---|---|---|---|---|
| Radio | SX1262 | SX1262 (E22 module) | SX1262 | SX1262 (E22 module) |
| NSS / DIO1 / BUSY / RESET | 22 / 1 / 21 / 2 | **21 / 0 / 2 / 1** | D‑pins (stock) | **D3 / D0 / D2 / D1** + custom `variant.cpp` |
| SCLK / MISO / MOSI | 19 / 20 / 18 | 19 / 20 / 18 | (stock) | (stock) |
| RXEN | 23 | **NC** (DIO2 only) | D5 | **NC** (DIO2 only) |
| I²C SDA / SCL | 16 / 17 | **22 / 23** | (stock) | **4 / 5** |
| TX power | 22 | **20** (→30 dBm via PA) | 22 | **20** (→30 dBm via PA) |
| On‑board GPS | none | **ATGM336H** (9600, Serial1) | none | **Photon GPS** (115200, Serial1) |
| Battery charge‑rate | no | **yes** | no | **yes** |
| Display | (per env) | NullDisplayDriver | (per env) | NullDisplayDriver |
| Custom board class | `XiaoC6Board.cpp` | `MeshsmithPhotonC6Board` + `target.*` | stock | `MeshsmithPhotonNRFBoard` + `target.*` + custom `variant.*` |

**Roles already defined by MeshSmith** (per variant): `companion_radio_ble`, `companion_radio_usb`, `repeater`, `room_server` — each combined with the `e22p_30dbm` TX profile into concrete envs (e.g. `meshsmith_photon_esp32c6_e22p_30dbm_companion_radio_usb`). This matches Offband's active roles (companion + repeater), with room_server as a bonus.

---

## 5. Recommended approach — vendor the variants

For each MCU track:

1. **Copy the variant directory** `variants/meshsmith_photon_<mcu>/` from `v1.16.0-meshsmith-photon` into our tree, preserving MeshSmith's MIT attribution.
2. **Wire the envs** into our `platformio.ini` `extra_configs` (the variant ships its own `platformio.ini` fragment, as ours do).
3. **Reconcile base dependencies** — confirm our 1.16.0 tree provides the same `esp32c6_base` / `nrf52_base` / `sensor_base` blocks the fragment extends (§6).
4. **Build** the role envs we care about (companion + repeater, both MCUs).
5. **Bench‑verify** on real Photon hardware: radio TX/RX at 30 dBm, GPS fix, battery sense, BLE/USB companion pairing.
6. **Docs:** add Photon to the build/flash reference; include the 30 dBm regulatory note.

This keeps us aligned with MeshSmith's upstream (easy to re‑sync) and avoids re‑deriving a pin map by hand.

**Architectural decision — RESOLVED by tree verification (2026-06-24):** the `target.*` board abstraction and the `sensor_base`/`esp32c6_base`/`nrf52_base` blocks MeshSmith's variants extend are **already standard in our tree** — those base blocks are defined in our `platformio.ini`, and **76 of our variants ship `target.cpp/.h`** (46 also carry a `*Board.cpp`; many have both — the same dual structure Photon uses). So vendoring as‑is uses our **native** conventions; there is no "fork within a fork." (Our specific `xiao_c6`/`xiao_nrf52` happen to use the older `*Board.cpp`‑direct style without `target.*`, but that's one variant's choice, not a standard Photon would violate.) **Decision: vendor as‑is — no refactor needed.** This refutes the adversarial review's "architectural pollution" concern (§11).

---

## 6. Gating verification (before estimating/committing implementation)

> **Framing:** we are vendoring **MeshSmith's own variant config for MeshSmith's own board** — not designing a 1 W front end from scratch. The electrical scheme (E22 driven via DIO2‑only switching at 20 dBm‑in; the carrier board's power/thermal budget) is *their* hardware design, shipped in a product. So 6A is **verify‑the‑vendor's‑config‑on‑our‑build**, not "design a 1 W board." We bench‑verify because we can't independently confirm how deeply they validated — not because the scheme is ours to invent. (Adversarial review pushed back hard here — see §11; the hardware‑first reordering below is the result.)

### 6A — Hardware / RF / power bench verification (PRIMARY go/no‑go) — needs a physical Photon‑1W
1. **Power draw:** measure peak/avg supply current during a 30 dBm TX burst (`LORA_TX_POWER=20`); confirm the carrier board's regulator handles the ~1 A PA draw without brownout. #1 go/no‑go.
2. **RF control:** confirm `DIO2_AS_RF_SWITCH` T/R behavior on the E22‑900M30S (RXEN/TXEN = NC) — no LNA/PA hazard; cross‑check the E22‑900M30S datasheet.
3. **Output / spectrum / thermal:** verify ~30 dBm out, acceptable harmonics, and sustained‑TX (repeater‑role) thermal behavior.

### 6B — Software base compatibility (SECONDARY) — mostly CONFIRMED (2026-06-24)
- ✓ `target.*` per‑variant entry pattern is **standard in our tree** (76 variants use it) — not a fork addition.
- ✓ `esp32c6_base` / `nrf52_base` / `sensor_base` are **defined in our `platformio.ini`** (the blocks the Photon variants extend).
- Remaining to confirm at port time: the GPS provider headers (`ATGM336HLocationProvider.h`, `PhotonGPSLocationProvider.h`) depend only on in‑tree helpers (`helpers/sensors`, `helpers/ui/NullDisplayDriver.cpp`); and TCXO `SX126X_DIO3_TCXO_VOLTAGE=1.8` matches the board.

6B is largely de‑risked: vendoring should be close to a clean copy + env wiring.

---

## 7. Out of scope — EtherMesh‑1W

EtherMesh‑1W (`"ESP32-P4 ethernet node"`) runs **`pymc_usb` = pyMC, a *Python* MeshCore stack** (Pi‑HAT / Ethernet‑bridge lineage), on an **ESP32‑P4** that has no native LoRa radio. Our tree has **no ESP32‑P4 board** (not in ~75 variants; none in code) and is C++, not Python. Supporting EtherMesh would be adopting/forking pyMC or a from‑scratch P4+Ethernet port — a separate strategic decision, **not** a board‑support task. Tracked separately only if pursued.

---

## 8. Risks & open questions

- **R1 — base/abstraction drift (§6):** `target.*` / `sensor_base` compatibility is the gating item. *Mitigation:* run §6 checks as the first implementation task.
- **R2 — hardware to verify on:** do we have a physical Photon‑1W (each MCU) on the bench? Without one, we can compile but cannot validate the 30 dBm chain, GPS, or battery sense. *Owner input needed.*
- **R3 — flash path:** ESP32‑C6 = serial/esptool via `scripts/pio-flash`. nRF52 = UF2/DFU (per the `rak-nrf52-flash-via-uf2` pattern; `pio upload` is hook‑blocked). Both are known paths.
- **R4 — license/attribution:** MeshSmith fork is MIT off our shared upstream; vendoring is clean **with** attribution preserved in the copied files + `LICENSE.txt` lineage intact.
- **R5 — upstream divergence:** if MeshSmith later moves Photon to a newer base, re‑sync cost depends on how far our trees drift. Low now (both on 1.16.0).
- **R6 — regulatory power policy (open owner decision):** the E22 hardware ceiling is 30 dBm (firmware can't exceed it). The firmware has **no region/RF‑band enum** (radio is raw freq/sf/cr/bw), so it cannot *know* the legal cap for an operator's region. Options: **(a)** default to full 30 dBm — *rejected* (ships an illegal‑in‑many‑regions default with no friction); **(b)** **conservative default + a deliberate, explicit override to raise toward 30 dBm + clear docs on operator legal responsibility** — a *conscious* choice, not whim; **(c)** a single hardcoded `MAX_ALLOWED_TX_POWER` — simplest, but wrong for some regions (too low for US915; possibly still high vs EU868 duty‑cycle); **(d)** build true region‑awareness — largest scope. *Recommend (b). Owner decision (§11).*

---

## 9. Proposed implementation breakdown (follow‑on)

Two implementation epics under feature #190, each blocked by the §6 verification:

- **Epic: §6 gating verification** — **6A hardware/RF/power bench verification** (primary go/no‑go; needs a physical unit) **+ 6B software base‑compat** (`target.*` / `sensor_base` / GPS‑provider deps). Produces the go/no‑go **and** the vendor‑as‑is‑vs‑refactor verdict (§5). *Gates the two below.*
- **Epic: Photon‑1W ESP32‑C6 support** — vendor `meshsmith_photon_esp32c6`, wire envs, build companion+repeater, bench‑verify, docs.
- **Epic: Photon‑1W nRF52 support** — vendor `meshsmith_photon_nrf52` (incl. custom `variant.*`), wire envs, build companion+repeater, bench‑verify (UF2 flash), docs.

Sizing per CLAUDE‑BASE (1–3 files / 15–30 min per task) applies when these epics are broken into tasks at planning time.

---

## 10. References

- MeshSmith site (specs): product pages are a client‑rendered SPA; specs extracted from `meshsmith.net/assets/index-*.js` and the `MeshSmith/MeshCore` fork.
- Fork: `github.com/MeshSmith/MeshCore` @ `v1.16.0-meshsmith-photon` → `variants/meshsmith_photon_esp32c6`, `variants/meshsmith_photon_nrf52`.
- Our precedents: `variants/xiao_c6`, `variants/xiao_nrf52`, `variants/lilygo_tbeam_1w` (1 W PA expression).
- Investigation session: 2026-06-24 (CalmDune).

---

## 11. Adversarial review adjudication (Gemini 2.5 Pro, 2026-06-24)

Full log: `docs/llm-consultations/2026-06-24-2026-06-24-photon-1w-design-gemini-gemini-2.5-pro.log`. Gemini verdict: **"Reconsider."** Adjudicated (not rubber‑stamped):

- **BLOCKER "RF front‑end control" + BLOCKER "power budget" → DOWNGRADED to bench‑verify (§6A), not design blockers.** Gemini reviewed this as a from‑scratch 1 W board bring‑up. It is not: we vendor **MeshSmith's own config for MeshSmith's own board** — they own the electrical design (E22 module, DIO2 switching, regulator/power). "DIO2‑only could destroy the LNA" / "regulator can't supply ~1 A" are answered by the board being a real product on this exact config. **Accepted kernel:** still bench‑verify on a real unit, and **reprioritize hardware/RF/power verification to gate before software** (§6 reordered, §9 gating epic now 6A‑first). Gemini's reprioritization was correct and improved the plan.
- **MAJOR "architectural pollution" → initially accepted, then REFUTED by tree verification (2026-06-24).** `target.*`/`sensor_base` are **already standard in our tree** (76 variants use `target.*`; the base blocks are defined in our `platformio.ini`), so vendoring as‑is uses native conventions — no "fork within a fork." Decision: **vendor as‑is, no refactor** (§5). *Lesson: verify the tree before accepting an architectural claim.*
- **MINOR "regulatory cap" → open owner decision (§8 R6).** Not doc‑only and not whim‑based: recommend a **conservative default + conscious override + docs**. Firmware has no region enum to auto‑enforce the correct legal cap, and the hardware tops at 30 dBm.
- **"Missed: TCXO" → REJECTED (already covered):** the variant sets `SX126X_DIO3_TCXO_VOLTAGE=1.8` (§3, §6B).
- **"Missed: thermal" → ACCEPTED** as a bench item (§6A.3) for sustained‑TX/repeater role.
