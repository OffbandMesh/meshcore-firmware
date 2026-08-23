// RC32 UART0 sniffer + remote reset -- Adafruit Feather ESP32-S3.
// BUILD ID: SNIFFER-v3                                          (#740, #702)
//
// v2 was listen-only. v3 adds a command surface so a host can assert the RC32's
// RST and BOOT lines without a human pressing buttons, which is what unblocks
// unattended reset cycles.
//
// FLASH WITH ARDUINO IDE, NOT PLATFORMIO. Two prior PlatformIO attempts put
// `Serial` on the TinyUSB CDC peripheral while the enumerated port was
// USB-Serial-JTAG: the sketch flashed and verified but printed nothing. The
// Arduino IDE handles the Feather's USB config correctly out of the box.
// (#704 handoff.)
//
// ---------------------------------------------------------------------------
// WIRING
// ---------------------------------------------------------------------------
//                                  Feather        generic S3 WROOM
//   RC32 pin 12 (U0TXD / GPIO43) -> RX             GPIO18     [v2, existing]
//   RC32 pin 20 (GND)            -> GND            GND        [v2, existing]
//   RC32 pin 18 (RST)            -> A0             GPIO17     [v3, new]
//   RC32 pin  5 (GPIO0 / BOOT)   -> A1             GPIO16     [v3, new]
//   sniffer TX                   -> NOT CONNECTED (either board)
//
// GND is not optional and is not a formality: without a common reference the
// UART sees garbage or nothing at all, which reads exactly like a dead board.
//
// ---------------------------------------------------------------------------
// OPEN-DRAIN ONLY -- THIS IS NOT OPTIONAL
// ---------------------------------------------------------------------------
// Both RC32 lines already have 10K pull-ups to 3V3 (RST: R31 + C32 1uF to
// CHIP_PU; BOOT: 10K + 100nF). We therefore ONLY ever pull them LOW and
// otherwise sit high-impedance. We never drive either line HIGH -- doing so
// would fight the pull-up and put two drivers on one net.
//
// Consequences that matter:
//   * Pins are initialised to INPUT (high-Z) as the FIRST thing in setup(), so
//     a Feather reboot or reflash can never hold the RC32 in reset.
//   * If the Feather is unpowered its pins are high-Z, so an unpowered or
//     disconnected sniffer has no effect on the RC32 whatsoever.
//   * Releasing is `pinMode(pin, INPUT)`, never `digitalWrite(pin, HIGH)`.
//
// ---------------------------------------------------------------------------
// COMMANDS (newline-terminated, on the Feather's USB serial)
// ---------------------------------------------------------------------------
//   RST      pulse RST low -> normal reset
//   BOOT     pulse BOOT low on its own (rarely useful alone)
//   BOOTRST  BOOT low, pulse RST, hold BOOT past the strap sample -> ROM
//            download mode
//   PING     liveness check
//   HELP     list commands
//
// Every action stamps a ">>>" line into the same stream as the RC32 data, so a
// captured log self-documents the stimulus immediately above the resulting ROM
// banner. That removes the "what caused this boot?" ambiguity that has dogged
// every capture on this board so far.

#define SNIFF_BAUD   115200   // ESP32-S3 ROM bootloader default
#define HB_FAST_MS   1000     // heartbeat cadence for the first 30 s
#define HB_FAST_FOR  30000    // then slow down so it cannot drown real data
#define HB_SLOW_MS   10000

// ---------------------------------------------------------------------------
// PIN MAP -- Feather by default, generic ESP32-S3 WROOM via one #define
// ---------------------------------------------------------------------------
// On the Feather these are symbolic names resolved by the board variant header,
// and raw GPIO numbers must NOT be substituted -- they differ across Feather S3
// variants.
//
// On a generic ESP32-S3 WROOM DevKitC those same names are wrong in a way that
// looks like a wiring fault rather than a config error: `RX` resolves to
// GPIO44, which IS U0RXD and is wired to the onboard USB-UART bridge. Sniffing
// on it puts two drivers on the console UART. So the generic build uses explicit
// GPIOs chosen to be free on every S3 WROOM module variant.
//
// Avoided deliberately on the generic map:
//   GPIO0/3/45/46  strapping pins
//   GPIO19/20      native USB D-/D+
//   GPIO26..32     SPI flash
//   GPIO33..37     octal PSRAM -- unusable on N8R8 / N16R8 modules, which is
//                  most of what ships as "ESP32-S3 WROOM"
//   GPIO43/44      UART0 / USB-UART bridge
//   GPIO48         onboard RGB LED
//
// Build for a generic board by defining SNIFFER_GENERIC_S3 (uncomment below).
//#define SNIFFER_GENERIC_S3 1
#define SNIFFER_RC52  1

#if defined(SNIFFER_GENERIC_S3)
  #define PIN_SNIFF_RX   18
  #define PIN_RC32_RST   17
  #define PIN_RC32_BOOT  16
#else
  #ifdef SNIFFER_RC52
    #define PIN_SNIFF_RX  TX
  #else
    #define PIN_SNIFF_RX   RX
  #endif
  #define PIN_RC32_RST   A0
  #define PIN_RC32_BOOT  A1
#endif

// ---------------------------------------------------------------------------
// MAX17048 FUEL GAUGE ON Wire1  (#780 / Heltec beta Q01)
// ---------------------------------------------------------------------------
// WHY. The RC32 reports battery VOLTAGE only, and turning voltage into charge
// requires a discharge curve we do not have for this cell -- the estimator in
// scripts/battery_runtime.py has to guess with a nominal table, and its two
// methods disagreed by seven hours on the first run. A MAX17048 reports modelled
// charge directly, which is the quantity we were failing to infer.
//
// TOPOLOGY. The gauge sits between the battery and the RC32: the cell feeds the
// gauge, the gauge passes power through to the RC32, and the SNIFFER reads it
// over I2C. The device under test is not modified and does not know it is being
// measured.
//
// Do NOT use the Feather's ONBOARD MAX17048 for this. It measures whatever is on
// the Feather's own battery JST, and connecting the RC32's cell there would let
// the USB-powered Feather CHARGE it -- which does not merely perturb a discharge
// test, it prevents one from ever finishing.
//
// WHY Wire1, AND WHY IT MUST BE Wire1.
//   * House convention: Wire is onboard devices, Wire1 is anything plugged in.
//   * It is also forced here. The MAX1704x has NO address-select pin -- it is
//     hard-wired to 0x36 -- and the Feather already carries one onboard at 0x36
//     on Wire. Two devices, one address, one bus is unresolvable.
//
// CONSEQUENCE FOR WIRING: the STEMMA QT connector is physically on Wire. Plugging
// the external gauge into it lands it on the SAME bus as the onboard one and
// collides. The external gauge must be hand-wired to the Wire1 pins below.
//
// BRING-UP ORDER IS NOT OPTIONAL. On ESP32 the pins are arguments to begin();
// on nRF52 they must be set BEFORE begin(). Getting it the wrong way round does
// not warn, it simply fails to come up -- the same class of failure as
// "HSPI Does not have default pins on ESP32S3". Pattern copied from
// src/helpers/sensors/EnvironmentSensorManager.cpp:637, which is the canonical
// version in this project.
#define SNIFF_GAUGE 1

// Free on the sniffer: RX carries the sniff line, A0/A1 carry RST/BOOT.
// A2/A3 are unused. Change these to match however you actually wire it.
#ifndef PIN_GAUGE_SDA
  #define PIN_GAUGE_SDA A2
#endif
#ifndef PIN_GAUGE_SCL
  #define PIN_GAUGE_SCL A3
#endif

// INA219 REGISTERS -- #938. [verified: TI INA219 datasheet (sbos448), fetched
// 2026-08-22]
//
//   0x00 Config (reset 0x399F)   0x01 Shunt V   0x02 Bus V
//   0x03 Power                   0x04 Current   0x05 Calibration
//
// !! ONE DECODE DETAIL THE DATASHEET SUMMARY GOT WRONG, AND WHY WE DO NOT
// !! FOLLOW IT. A summary of the datasheet reported the SHUNT register as
// !! "bits 15-3", the same shift as the bus register. That cannot be right at
// !! the default configuration, and the config register proves it:
// !!
// !!     reset value 0x399F = 0011 1001 1001 1111
// !!                            ^^  PG = 0b11 -> PGA /8 -> +/-320 mV range
// !!
// !! At 10 uV per LSB, +/-320 mV needs +/-32000 counts. A 13-bit field (bits
// !! 15-3) holds only +/-4096 counts = +/-40.96 mV, which is the PGA /1 range,
// !! not the default. So the shunt register is a FULL signed 16-bit value and
// !! must NOT be shifted. Computed from the mechanism rather than read off the
// !! annotation -- shifting it would have quietly reported 1/8th of the real
// !! current, a plausible number that no sanity check would catch.
//
// The BUS register genuinely is bits 15-3, with CNVR at bit 1 and OVF at bit 0.
#define INA219_REG_SHUNT   0x01
#define INA219_REG_BUS     0x02

// Bench shunt, owner-confirmed from the silkscreen: R100 = 0.100 ohm.
// Shunt LSB 10 uV / 0.1 ohm  ->  exactly 100 uA per count. Range +/-3.2 A.
// Blind to single-digit-uA sleep current; that is a property of the shunt, not
// the chip, and no averaging setting recovers it.
#define INA219_SHUNT_MILLIOHM  100

#define MAX1704X_ADDR    0x36
#define MAX1704X_VCELL   0x02
#define MAX1704X_SOC     0x04
#define MAX1704X_VERSION 0x08

// Sampling cadence. Matches the RC32's own [pwr] line so the two series line up
// in the capture without interpolation.
#define GAUGE_PERIOD_MS 30000

// RST: R31 10K + C32 1uF gives a ~10 ms RC on CHIP_PU. 100 ms is comfortably
// past it. (Holding RST for 1 s was tested by the owner and changes nothing --
// the C32 short-press theory is refuted, #702.)
#define RST_ASSERT_MS      100
// Time to hold BOOT low after RST is released, so the ROM samples the strap
// with BOOT still asserted.
#define BOOT_HOLD_AFTER_MS 100

static uint32_t hb_next = 0, hb_n = 0;
static uint32_t rx_bytes = 0;
static char cmd_buf[32];
static uint8_t cmd_len = 0;

#if SNIFF_GAUGE
#include <Wire.h>
static bool     gauge_present = false;
static uint32_t gauge_next    = 0;

// Read one 16-bit big-endian register. Returns false on any bus error rather
// than handing back a plausible-looking zero -- a fuel gauge that silently
// reports 0% would be worse than one that reports nothing.
static bool gaugeRead16At(uint8_t addr, uint8_t reg, uint16_t* out) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(addr, (uint8_t)2) != 2) return false;
  uint8_t hi = Wire1.read(), lo = Wire1.read();
  *out = ((uint16_t)hi << 8) | lo;
  return true;
}

static bool gaugeRead16(uint8_t reg, uint16_t* out) {
  return gaugeRead16At(MAX1704X_ADDR, reg, out);
}

// Forward declaration: gaugeBegin() calls this, and it is defined below.
// Arduino's automatic prototype generation is unreliable for `static` functions,
// so it is declared explicitly rather than relying on the preprocessor.
// Which INA is actually on the bus (#938). Detection is printed today and the
// read path will switch on it tomorrow -- the parts do NOT share a register map,
// so "read the INA" is a different routine per family:
//
//   INA219      current/power read ZERO until the calibration register is
//               written; shunt (0x01) and bus (0x02) voltage read raw. With the
//               0.1 ohm shunt on the bench module, shunt LSB 10 uV -> 100 uA
//               per count, +/-3.2 A full scale. Simplest correct path is to
//               skip calibration entirely and compute I = Vshunt / R on the
//               host -- no magic constant to get wrong.
//   INA228      different map, 20-bit, and a hardware CHARGE accumulator, which
//               is the feature that makes a runtime figure honest when the load
//               is bursty (a LoRa node's draw is dominated by short TX bursts,
//               and host-side sampling misses them between reads).
//
// Register maps to be confirmed against the TI datasheets before any of that is
// written -- the notes above are working knowledge, not a citation.
enum InaKind {
  INA_NONE = 0,
  INA_219_CLASS,     // ACKs, but exposes no TI manufacturer ID
  INA_226, INA_260, INA_3221,
  INA_228, INA_237, INA_238,
  INA_TI_UNKNOWN     // TI mfg ID present, part ID not one we know
};
static InaKind ina_kind = INA_NONE;
static uint8_t ina_addr = 0;

static void inaProbe();

static void gaugeBegin() {
  // ESP32 takes the pins as arguments to begin(). nRF52 needs setPins() FIRST.
  // See the header comment -- this order is the whole trap.
#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52)
  Wire1.setPins(PIN_GAUGE_SDA, PIN_GAUGE_SCL);
  Wire1.setClock(100000);
  Wire1.begin();
#else
  Wire1.begin(PIN_GAUGE_SDA, PIN_GAUGE_SCL, 100000);
#endif

  // Targeted probe, NOT a bus scan. A blind scan is what wedges the I2C
  // peripheral on the C6 (#294 -- hangs at 0x0d and never returns, so setup()
  // never reaches loop()). We know the address; there is no reason to sweep.
  uint16_t ver = 0;
  gauge_present = gaugeRead16(MAX1704X_VERSION, &ver);

  Serial.print("=== fuel gauge on Wire1 (SDA=");
  Serial.print((int)PIN_GAUGE_SDA);
  Serial.print(" SCL=");
  Serial.print((int)PIN_GAUGE_SCL);
  Serial.print("): ");
  if (gauge_present) {
    Serial.print("FOUND at 0x36, VERSION=0x");
    Serial.println(ver, HEX);
  } else {
    Serial.println("NOT FOUND -- check wiring, and that it is NOT in the STEMMA");
    Serial.println("    port (that is Wire, where the onboard gauge already sits at 0x36)");
  }

  inaProbe();
}

// ---------------------------------------------------------------------------
// INA CURRENT MONITOR -- IDENTIFICATION ONLY (for now)
// ---------------------------------------------------------------------------
// The MAX17048 answers "how full is the cell". An INA answers "how much is it
// drawing right now". Together they close the loop: measured charge, measured
// current, measured time -- no nominal tables and no bracketed range.
//
// This probes the four parts the Offband firmware already supports, because
// which one is present changes how current is decoded and guessing wrong
// produces confident nonsense rather than an error:
//
//   INA219  0x40  12-bit, external shunt, current needs a calibration write
//   INA260  0x41  16-bit + INTEGRATED 2 mOhm shunt, current readable directly
//   INA3221 0x42  3-channel, lower precision
//   INA226  0x44  16-bit, external shunt, current needs calibration
//
// Identification is positive, not inferred from address alone: TI parts carry a
// Manufacturer ID at 0xFE (0x5449, "TI") and a Die ID at 0xFF. Address tells you
// where something answered; the die ID tells you WHAT answered, and on a bus
// where addresses are configurable by strap those are different questions.
//
// Targeted probe of four known addresses -- NOT a bus scan. Blind scanning is
// what wedges the C6 I2C peripheral (#294).
//
// Deliberately identification-only: the current decode is written once the part
// is known, rather than shipping three decoders that never run and cannot be
// tested.
static void inaProbe() {
  // Address sweep is deliberately narrow: 0x40..0x4F is the INA family's
  // strap-selectable range. Still a bounded, known range, not a blind bus scan
  // (#294 -- a blind scan wedges the C6 I2C peripheral and never returns).
  bool any = false;
  for (uint8_t addr = 0x40; addr <= 0x4F; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() != 0) continue;
    if (addr == MAX1704X_ADDR) continue;   // not an INA
    any = true;
    if (ina_addr == 0) ina_addr = addr;

    // TWO ID register locations, because the families disagree:
    //   INA226 / INA260 / INA3221 -> MFG 0xFE, DIE 0xFF
    //   INA228 / INA237 / INA238  -> MFG 0x3E, DEVICE 0x3F
    // Probing only 0xFE/0xFF would find an INA228 at 0x40, get nothing back,
    // and misreport it as an INA219 (which genuinely has no ID registers).
    // Same address, wrong chip, wrong decode, plausible-looking output.
    uint16_t mfg_hi = 0, die_hi = 0, mfg_lo = 0, dev_lo = 0;
    bool have_hi = gaugeRead16At(addr, 0xFE, &mfg_hi) && gaugeRead16At(addr, 0xFF, &die_hi);
    bool have_lo = gaugeRead16At(addr, 0x3E, &mfg_lo) && gaugeRead16At(addr, 0x3F, &dev_lo);

    Serial.print("=== INA candidate at 0x");
    Serial.print(addr, HEX);

    const char* id = "";
    if (have_lo && mfg_lo == 0x5449) {
      // DEVICE_ID is [15:4] part, [3:0] revision -- mask the revision off.
      uint16_t part = (uint16_t)(dev_lo >> 4);
      ina_kind = (part == 0x228) ? INA_228
               : (part == 0x237) ? INA_237
               : (part == 0x238) ? INA_238
               : INA_TI_UNKNOWN;
      id = (part == 0x228) ? " -> INA228"
         : (part == 0x237) ? " -> INA237"
         : (part == 0x238) ? " -> INA238"
         : " -> TI part, unrecognised DEVICE_ID";
      Serial.print("  mfg=0x"); Serial.print(mfg_lo, HEX);
      Serial.print(" dev=0x");  Serial.print(dev_lo, HEX);
    } else if (have_hi && mfg_hi == 0x5449) {
      ina_kind = (die_hi == 0x2260) ? INA_226
               : (die_hi == 0x2270) ? INA_260
               : (die_hi == 0x3220) ? INA_3221
               : INA_TI_UNKNOWN;
      id = (die_hi == 0x2260) ? " -> INA226"
         : (die_hi == 0x2270) ? " -> INA260"
         : (die_hi == 0x3220) ? " -> INA3221"
         : " -> TI part, unrecognised DIE_ID";
      Serial.print("  mfg=0x"); Serial.print(mfg_hi, HEX);
      Serial.print(" die=0x");  Serial.print(die_hi, HEX);
    } else {
      // NO TI MANUFACTURER ID ANYWHERE -> INA219-class.
      //
      // Testing have_hi alone was WRONG and this is the bug #938 fixes. The
      // INA219 has no ID registers at all, so the expectation was that reads of
      // 0xFE/0xFF would FAIL and fall through to here. They do not: the part
      // ALIASES invalid register pointers instead of NAKing, so the transaction
      // succeeds and hands back plausible-looking garbage.
      //
      // Observed on the bench 2026-08-22 with a real INA219 at 0x40:
      //     mfg=0x5959  die=0x2719   -> was reported "unrecognised DIE_ID"
      //
      // 0x5449 is "TI" in ASCII and is the ONLY positive evidence that a
      // manufacturer-ID register exists. A successful read proves nothing.
      // Same failure shape the note above warns about, in the other direction:
      // a wrong decode that looks like an answer.
      ina_kind = INA_219_CLASS;
      id = " -> no TI mfg ID (INA219-class)";
      // Print what we did get, so a genuinely unknown part stays diagnosable
      // instead of being silently bucketed as an INA219.
      Serial.print("  raw 0xFE=0x"); Serial.print(mfg_hi, HEX);
      Serial.print(" 0xFF=0x");      Serial.print(die_hi, HEX);
      Serial.print(" 0x3E=0x");      Serial.print(mfg_lo, HEX);
      Serial.print(" 0x3F=0x");      Serial.print(dev_lo, HEX);
    }
    Serial.println(id);
  }
  if (!any) Serial.println("=== no INA found on Wire1 (swept 0x40..0x4F)");
}

// Read the INA219 and print current + bus voltage. Returns the bus millivolts
// (0 if unread) so the caller can cross-check it against the fuel gauge.
//
// NO CALIBRATION REGISTER IS WRITTEN, deliberately. The calibration register
// exists so the chip can do the I = V/R division for you, and getting its value
// wrong is the classic INA219 failure -- current and power silently read ZERO
// until it is right. Reading the raw shunt voltage and dividing on the host
// needs no magic constant and cannot be half-configured.
static uint32_t inaTick() {
  if (ina_kind != INA_219_CLASS || ina_addr == 0) return 0;

  uint16_t rs = 0, rb = 0;
  if (!gaugeRead16At(ina_addr, INA219_REG_SHUNT, &rs) ||
      !gaugeRead16At(ina_addr, INA219_REG_BUS,   &rb)) {
    Serial.println("[ina] read FAILED");
    return 0;
  }

  // Shunt: full signed 16-bit, 10 uV/LSB (see the register block above).
  int32_t shunt_uv = (int32_t)(int16_t)rs * 10;
  // I(uA) = V(uV) / R(ohm) = shunt_uv * 1000 / milliohm
  int32_t ua = (shunt_uv * 1000L) / (int32_t)INA219_SHUNT_MILLIOHM;

  // Bus: bits 15-3, 4 mV/LSB. Bit 1 CNVR, bit 0 OVF.
  uint32_t bus_mv = (uint32_t)(rb >> 3) * 4UL;
  bool ovf = (rb & 0x0001) != 0;

  int32_t ma_x10 = ua / 100;          // tenths of a mA
  Serial.print("[ina] bus_mv=");
  Serial.print(bus_mv);
  Serial.print(" ma=");
  if (ma_x10 < 0) { Serial.print('-'); ma_x10 = -ma_x10; }
  Serial.print(ma_x10 / 10); Serial.print('.'); Serial.print(ma_x10 % 10);
  Serial.print("  shunt_uv=");
  Serial.print(shunt_uv);
  // Raw values printed alongside the decode for the same reason the gauge does
  // it: if the decode is ever wrong, the raw number is what lets a captured log
  // be corrected after the fact instead of being silently wrong for hours.
  Serial.print("  raw_shunt=0x"); Serial.print(rs, HEX);
  Serial.print(" raw_bus=0x");    Serial.print(rb, HEX);
  if (ovf) Serial.print("  !OVF");
  Serial.println();
  return bus_mv;
}

static void gaugeTick(uint32_t now) {
  if (!gauge_present || (int32_t)(now - gauge_next) < 0) return;
  gauge_next = now + GAUGE_PERIOD_MS;

  uint16_t vcell = 0, soc = 0;
  if (!gaugeRead16(MAX1704X_VCELL, &vcell) || !gaugeRead16(MAX1704X_SOC, &soc)) {
    Serial.println("[gauge] read FAILED");
    return;
  }

  // MAX17048: VCELL LSB = 78.125 uV, charge = SOC / 256 %.
  // Raw values are printed alongside the decode deliberately. If this turns out
  // to be a MAX17043 the scaling differs (12-bit VCELL, 1.25 mV/LSB) and the raw
  // number is what lets that be corrected after the fact instead of silently
  // logging a wrong voltage for hours.
  uint32_t mv = ((uint32_t)vcell * 78125UL) / 1000000UL;
  uint32_t pct_x10 = ((uint32_t)soc * 10UL) / 256UL;

  Serial.print("[gauge] mv=");
  Serial.print(mv);
  Serial.print(" charge=");
  Serial.print(pct_x10 / 10);
  Serial.print('.');
  Serial.print(pct_x10 % 10);
  Serial.print("%  raw_vcell=0x");
  Serial.print(vcell, HEX);
  Serial.print(" raw_soc=0x");
  Serial.println(soc, HEX);

  // TWO INSTRUMENTS, ONE TRUTH. The gauge senses UPSTREAM of the shunt and the
  // INA senses at VIN- (load side), so the difference between them IS the shunt
  // drop and must agree with the current we just computed:
  //
  //     gauge_mv - bus_mv  ~=  I * R
  //
  // Printed rather than asserted. If it stops agreeing, one of the gauge, the
  // shunt value or the decode is wrong, and this line says so before a wrong
  // number gets baked into a runtime figure (#833's "both instruments must
  // agree" rule, now with a second independent quantity behind it).
  uint32_t bus_mv = inaTick();
  if (bus_mv > 0 && mv > 0) {
    Serial.print("[xchk] gauge_mv-bus_mv=");
    Serial.print((int32_t)mv - (int32_t)bus_mv);
    Serial.println(" mV  (expect ~ I*0.1ohm; 10 mV per 100 mA)");
  }
}
#endif  // SNIFF_GAUGE

// Dead-man release. If this sketch ever hangs or crashes between od_assert()
// and od_release(), the RC32 would be held in reset (or download mode)
// indefinitely and would need the FEATHER power-cycled to recover -- a failure
// mode that looks exactly like the dead board we are investigating.
// loop() force-releases both lines if an assertion has been outstanding too
// long. (Gemini review, #740.)
#define OD_DEADMAN_MS 2000
static uint32_t od_deadline = 0;   // 0 = nothing asserted

// --- open-drain primitives -------------------------------------------------
static inline void od_assert(uint8_t pin) {   // pull LOW
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  od_deadline = millis() + OD_DEADMAN_MS;
}
static inline void od_release(uint8_t pin) {  // back to high-Z; pull-up restores HIGH
  pinMode(pin, INPUT);
}

static void stamp(const char* s) {
  Serial.print(">>> ");
  Serial.print(s);
  Serial.print("  @up=");
  Serial.print(millis() / 1000);
  Serial.println("s");
}

static void do_reset(bool with_boot) {
  if (with_boot) {
    stamp("BOOT asserted (download-mode entry)");
    od_assert(PIN_RC32_BOOT);
    delay(10);
  }
  stamp(with_boot ? "RST asserted (BOOT+RST)" : "RST asserted");
  od_assert(PIN_RC32_RST);
  delay(RST_ASSERT_MS);
  od_release(PIN_RC32_RST);
  stamp("RST released -- RC32 booting");

  if (with_boot) {
    delay(BOOT_HOLD_AFTER_MS);
    od_release(PIN_RC32_BOOT);
    stamp("BOOT released -- expect ROM download mode");
  }
  od_deadline = 0;   // sequence completed cleanly; dead-man stands down
}

static void handle_cmd(const char* c) {
  if      (!strcasecmp(c, "RST"))     do_reset(false);
  else if (!strcasecmp(c, "BOOTRST")) do_reset(true);
  else if (!strcasecmp(c, "BOOT"))  {
    stamp("BOOT pulsed alone");
    od_assert(PIN_RC32_BOOT); delay(RST_ASSERT_MS); od_release(PIN_RC32_BOOT);
  }
  else if (!strcasecmp(c, "PING"))    stamp("PONG SNIFFER-v3");
  else if (!strcasecmp(c, "HELP"))    stamp("cmds: RST BOOTRST BOOT PING HELP");
  else if (c[0])                      stamp("unknown cmd (try HELP)");
}

void setup() {
  // FIRST: park both control lines high-Z before anything else can run, so a
  // Feather reboot cannot hold the RC32 in reset.
  od_release(PIN_RC32_RST);
  od_release(PIN_RC32_BOOT);

  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  Serial1.begin(SNIFF_BAUD, SERIAL_8N1, PIN_SNIFF_RX, -1);   // RX only -- TX is -1

  Serial.println();
  Serial.println("================================================");
  Serial.println("=== RC32 UART0 SNIFFER  BUILD ID: SNIFFER-v3 ===");
  Serial.printf ("=== listening on GPIO%d, %d 8N1\n", (int)PIN_SNIFF_RX, SNIFF_BAUD);
  Serial.println("=== heartbeat 1/s for 30s, then 1/10s");
  Serial.println("=== any line without [hb] or >>> is RC32 data");
  Serial.println("=== RST->A0  BOOT->A1  (open-drain, pull-low only)");
  Serial.println("=== cmds: RST BOOTRST BOOT PING HELP");
  Serial.println("================================================");

#if SNIFF_GAUGE
  gaugeBegin();
#endif
}

void loop() {
  // Relay first so real data is never delayed behind a heartbeat.
  while (Serial1.available()) {
    Serial.write(Serial1.read());
    rx_bytes++;
  }

#if SNIFF_GAUGE
  // After the relay, never before it. An I2C transaction takes a few hundred
  // microseconds and the RC32's UART has no flow control -- servicing the gauge
  // ahead of the relay would risk dropping the bytes this whole instrument
  // exists to capture.
  gaugeTick(millis());
#endif

  // Host commands.
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      cmd_buf[cmd_len] = '\0';
      if (cmd_len) handle_cmd(cmd_buf);
      cmd_len = 0;
    } else if (cmd_len < sizeof(cmd_buf) - 1) {
      cmd_buf[cmd_len++] = ch;
    }
  }

  uint32_t now = millis();

  // Dead-man: never leave the target held. Loud, never silent (SAFELANE 6).
  if (od_deadline && (int32_t)(now - od_deadline) >= 0) {
    od_release(PIN_RC32_RST);
    od_release(PIN_RC32_BOOT);
    od_deadline = 0;
    stamp("DEAD-MAN RELEASE -- lines were held too long; both released");
  }

  if ((int32_t)(now - hb_next) >= 0) {
    hb_next = now + (now < HB_FAST_FOR ? HB_FAST_MS : HB_SLOW_MS);
    // rx_bytes is the payoff: a rising count proves the WIRE carries data,
    // independently of whether that data is decodable at this baud.
    Serial.printf("[hb] SNIFFER-v3 alive  n=%lu  up=%lus  rx_bytes=%lu\n",
                  (unsigned long)++hb_n,
                  (unsigned long)(now / 1000),
                  (unsigned long)rx_bytes);
  }
}
