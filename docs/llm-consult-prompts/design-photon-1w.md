# Gemini Adversarial DESIGN Review — Offband (MeshCore fork): Photon‑1W support

You are an adversarial **design** reviewer for **Offband**, an MIT fork of **MeshCore** (NOT Meshtastic —
do not apply Meshtastic conventions). C++ firmware, PlatformIO / arduino-esp32. There is NO new code yet —
review the DESIGN OF RECORD for adding support for a third‑party board. Prove concerns with reasoning;
if you can't tell from what's given, say so. Do NOT rubber‑stamp, flatter, or pad. Be blunt about real hazards.

## Context

The **MeshSmith Photon‑1W** is a XIAO‑based 1 W LoRa node sold by MeshSmith, who maintain their own MIT
fork of MeshCore (`MeshSmith/MeshCore`) off the same `meshcore-dev/MeshCore` upstream we use. Both their
Photon branches and our tree are on the **MeshCore 1.16.0 base**. They ship two self‑contained board
variant directories (`meshsmith_photon_esp32c6`, `meshsmith_photon_nrf52`). The design proposes to
**vendor those two variant dirs** into Offband and wire their envs, rather than author variants from scratch.

Key hardware facts (from their variant `platformio.ini`):
- Radio is an **Ebyte E22‑900M30S** = SX1262 + integrated 30 dBm (1 W) PA. They drive SX1262 at
  `LORA_TX_POWER=20` ("20 dBm in → 30 dBm out"), and do T/R switching **purely via
  `SX126X_DIO2_AS_RF_SWITCH=1`** with `SX126X_RXEN`/`TXEN` = `RADIOLIB_NC`.
- Photon pin maps differ from our stock `xiao_c6` / `xiao_nrf52` (different NSS/DIO1/BUSY/RESET, I²C,
  RXEN=NC). Photon adds on‑board GPS (ATGM336H on C6; a Photon GPS provider on nRF52) and battery
  charge‑rate sensing. nRF52 ships a custom `variant.cpp/.h`.
- Their variant fragments `extend` `esp32c6_base` / `nrf52_base` / `sensor_base` and include a per‑variant
  `target.cpp/.h` board abstraction that our stock `xiao_*` variants (e.g. `XiaoC6Board.cpp`) do NOT use.

## Claims / decisions to stress‑test
1. **"Port, not rewrite" via vendoring** the two variant dirs + wiring envs is the right approach (vs.
   authoring fresh variants, or maintaining an out‑of‑tree overlay). Sound? Hidden coupling?
2. **The gating risk is `target.*` / `sensor_base` compatibility with our 1.16.0 base** (design §6). Is
   that the right gating item, or is there a larger risk being under‑weighted?
3. **E22‑900M30S with DIO2‑only T/R switching** (RXEN/TXEN = NC): is that electrically sane for that
   module, or does the E22‑900M30S require explicit TXEN/RXEN PA control that DIO2 alone cannot provide?
   Any PA‑bias / RX‑path hazard if a stock SX1262 driver assumes a different switch topology?
4. **`LORA_TX_POWER=20` → 30 dBm out**: any firmware‑side hazard (current limit 140 mA, TCXO, ramp)?
5. **EtherMesh‑1W excluded** (ESP32‑P4 + Python "pyMC", no native radio) as out‑of‑scope for the C++ tree.
   Agree, or is there a cheaper bridge than a full port?

## Direct questions
1. Anything in the vendor‑the‑dir plan that breaks when our base and theirs drift even slightly (build_src
   filters, `sensor_base` contract, GPS provider deps on `helpers/sensors` / `NullDisplayDriver`)?
2. Is splitting into two MCU implementation epics + one gating verification epic the right decomposition?
3. Any regulatory/firmware boundary issue with shipping a 1 W (30 dBm) default we should encode vs. doc?
4. What would you verify on bench hardware first to de‑risk fastest?
5. Anything missed entirely?

## Output format
```
## Summary
<1-3 sentences: is the design sound?>
## Issues
- **[BLOCKER/MAJOR/MINOR/QUESTION] <area>** — <problem + reasoning>. <fix>.
## Direct answers
1..5
## Verdict
<proceed as-is / proceed with changes / reconsider — what MUST change.>
```

## Reference files
__FILES__
