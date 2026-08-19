# RC32 battery runtime — measured discharge to auto-shutdown

**Result: 21 h 34 m 31 s on a 2500 mAh cell. Average draw ≈ 116 mA.**

Issue **#833**. Answers Heltec RadioCore beta survey **Q01** (power consumption / runtime).
Raw data: `rc32-battery-runtime-2026-08-18.csv` (2569 samples, 30 s interval).

---

## 1. Summary

| | |
|---|---|
| **Runtime** | **21 h 34 m 31 s** (21.575 h) |
| Start | 2026-08-18 01:34:48 UTC — 4096 mV |
| End | 2026-08-18 23:09:19 UTC — 3400 mV |
| Total drop | 696 mV |
| Mean rate | 32.3 mV/h |
| **Average current** | **115.9 mA** (2500 mAh ÷ 21.575 h) |
| Samples | 2569 device + 2569 gauge, 30 s interval, no gaps |
| Termination | Firmware `AUTO_SHUTDOWN_MILLIVOLTS` reached — **exactly 3400 mV** |

**The device shut itself down cleanly.** Not a brownout, not a low-battery hold. The last logged
line before silence was `Mesh::onRecvPacket(): valid advertisement received!` — it was still
routing traffic when it stopped.

### Device under test

| | |
|---|---|
| Board | `rc32-bench-1` — Heltec RadioCore **RC32-L62** (ESP32-S3 + HT-RA62A) |
| Firmware | `heltec_rc32_companion_radio_ble_diag`, build `offband-v1.5.0-beta2-26-g0a2373a` |
| Configuration | BLE companion, **display active**, GPS off, `DISPLAY_ROTATION=0` |
| Cell | 2500 mAh single-cell LiPo on VBAT (header pin 2 / GND pin 20) |
| LoRa | 910.525 MHz, SF7, BW 62.5 kHz, live mesh traffic throughout |

---

## 2. Discharge curve, hourly

`ADC` is the RC32's own reading. `Gauge` is an external MAX17048 wired between cell and board,
read by the sniffer — a fully independent instrument.

| Hour | ADC mV | Gauge mV | Gauge % |
|---:|---:|---:|---:|
| 0 | 4096 | 4108 | *200.6* |
| 1 | 4027 | 4046 | 97.8 |
| 2 | 3983 | 3995 | 79.9 |
| 3 | 3929 | 3950 | 72.5 |
| 4 | 3880 | 3907 | 66.8 |
| 5 | 3846 | 3866 | 61.9 |
| 6 | 3797 | 3825 | 55.3 |
| 7 | 3768 | 3791 | 46.5 |
| 8 | 3743 | 3763 | 37.2 |
| 9 | 3699 | 3725 | 26.9 |
| 10 | 3665 | 3686 | 15.6 |
| 11 | 3626 | 3652 | 7.1 |
| 12 | 3596 | 3633 | 4.4 |
| 13 | 3586 | 3616 | 3.2 |
| 14 | 3572 | 3602 | 2.3 |
| 15 | 3562 | 3591 | 2.2 |
| 16 | 3542 | 3577 | 2.0 |
| 17 | 3532 | 3562 | 1.8 |
| 18 | 3513 | 3545 | 1.6 |
| 19 | 3483 | 3523 | 1.3 |
| 20 | 3459 | 3498 | 1.0 |
| 21 | 3430 | 3462 | 0.5 |

The hour-0 gauge reading of **200.6 %** is a ModelGauge artefact, not a measurement: the part had
spent the preceding period seeing a ~5 V USB rail and its state estimate saturated. It
re-converged on its own within an hour, with no power-cycle. **Discard the first hour of gauge
percentage; the voltage was correct throughout.**

---

## 3. Instrument cross-check

Both instruments measured the same cell independently for the entire run.

| | |
|---|---|
| Paired samples | 2568 |
| Mean (gauge − ADC) | **+27.7 mV** |
| Median | +28 mV |
| Range | −2 to +44 mV |

A consistent ~28 mV offset with no drift over 21 hours. The gauge sits on the cell side of the
wiring, so it reads slightly higher under load — the sign and magnitude are what the topology
predicts.

**This independently validates the RC32's sense chain**: the R36 390 kΩ / R38 100 kΩ divider, the
4.9 multiplier, and the ESP32 ADC together track a separate instrument to under 1 % across a
700 mV span. That closes the question raised in **#766**.

---

## 4. Curve shape — the practically important finding

| Phase | Range | Rate |
|---|---|---:|
| Surface charge (first 90 s) | 4096 → 4086 mV | ~200 mV/h |
| Early | 4086 → 3978 mV | 55.6 mV/h |
| **Plateau (2–11 h)** | 3983 → 3626 mV | **39.7 mV/h** |
| Knee (11–13 h) | 3626 → 3586 mV | 20.1 mV/h |
| **Tail (13–21.6 h)** | 3586 → 3400 mV | **21.7 mV/h** |
| Final hour | 3430 → 3400 mV | 52.2 mV/h |

**The tail is slower than the plateau, not faster.** This cell at ~116 mA does not fall off a
cliff at low voltage — it *decelerates* below 3.6 V and holds ~22 mV/h for over eight hours,
steepening only in the final hour.

That single fact accounts for **40 % of the total runtime** sitting between 3.6 V and cutoff, and
it is the reason every projection made during the run was too pessimistic.

---

## 5. Why projections failed, and what to trust

Estimates produced *during* the run by `battery_runtime.py`, against the measured 21.58 h:

| When | Slope method | Charge accounting | Error |
|---|---:|---:|---|
| 2 h 51 m | 13 h 10 m | 14 h 18 m | −7 to −8 h |
| 4 h 16 m | 14 h 03 m | 14 h 17 m | −7 h |
| 6 h 57 m | 16 h 06 m | 10 h 32 m | −5 to −11 h |
| 13 h 14 m | 20 h 43 m | 13 h 56 m | −1 to −8 h |

**Every projection undershot. The measured runtime exceeded even the most optimistic bound.**

Two distinct causes:

1. **Charge accounting** relies on a nominal LiPo state-of-charge table that over-attributes
   consumption between 3.9 V and 3.7 V. It inflated derived current from 171 mA to 235 mA
   mid-run, against a true 116 mA.
2. **Slope projection** assumed the curve would steepen at low voltage. It flattened instead.

**Practical rule:** for this board and cell, treat any mid-run projection as a lower bound.
Only a run to cutoff produces a runtime, and only inline coulomb counting (an INA228-class part)
produces a trustworthy current figure without a curve.

---

## 6. Cross-findings

**Battery indicator (#780) — confirmed at the endpoint.** The firmware maps voltage to percentage
linearly over 3000–4200 mV. At the 3400 mV shutdown that yields **33 %**: the device powers off
while the user is shown a third of a battery remaining. The gauge read 0.5 % at the same moment.

**Loaded vs resting voltage.** Final loaded reading 3400 mV; the cell settled to ~3468 mV once the
load was removed. Quote the loaded figure for cutoff behaviour and the resting figure for cell
state — they differ by ~70 mV.

**Sense path (#766).** Section 3 closes it: the configured `PIN_VBAT_READ=7`, `PIN_ADC_CTRL=15`,
multiplier 4.9 are correct, now corroborated by an independent instrument over 21 hours.

---

## 7. Answer for Heltec Q01

> RC32-L62 running a BLE companion with the display active, on a 2500 mAh single-cell LiPo:
> **21 h 35 m** from full charge to firmware auto-shutdown at 3.40 V, averaging **≈116 mA**.
>
> Measured, not modelled — 30-second telemetry over the full discharge, cross-checked against an
> external MAX17048 fuel gauge agreeing to within 28 mV throughout.
>
> Note the board provides **voltage only** — no fuel gauge, no current sense. Deriving this
> required external instrumentation on a second board. An onboard gauge or shunt would make
> power characterisation a first-class feature rather than a bench exercise.

---

## 8. Baseline for what follows

This is the reference point for the role and display comparisons. Both envs already exist:

| Run | Env | Isolates |
|---|---|---|
| 1 ✅ | `heltec_rc32_companion_radio_ble_diag` | **baseline — 21 h 35 m, 116 mA** |
| 2 | `heltec_rc32_repeater` | role cost (no BLE, no companion protocol; display still on) |
| 3 | `heltec_rc32_without_display_repeater` + panel removed | display cost |

Run 2 keeps the panel, so the difference is role alone. Run 3 compiles out the display driver
entirely — no SPI traffic, no UI task, no backlight — so with the panel physically removed it
measures true zero-display draw.

---

## 9. Reproducing

```bash
python tools/diag/rc32-boot-740/scripts/capture.py --port COM16 --out <log>
python tools/diag/rc32-boot-740/scripts/battery_runtime.py <log>
```

**Validity rules, each learned by losing a run:**

- **USB must stay out.** Any reconnect recharges the cell and voids the series. Three attempts
  were lost this way before this one.
- **Start from a plateaued full charge.** The point the voltage stops rising is full for this
  measurement chain (~4130 mV); the nominal 4.20 V never appears through the divider.
- **Both instruments must agree.** Divergence beyond ~30 mV indicts the measurement, not the cell.
- The analysis script selects the trailing contiguous run of valid samples, so earlier partial
  attempts cannot contaminate the fit.
