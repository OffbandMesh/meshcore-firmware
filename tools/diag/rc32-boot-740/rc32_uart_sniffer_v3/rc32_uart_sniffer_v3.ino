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
//   RC32 pin 12 (U0TXD / GPIO43) -> Feather RX      [v2, existing]
//   RC32 pin 20 (GND)            -> Feather GND     [v2, existing]
//   RC32 pin 18 (RST)            -> Feather A0      [v3, new]
//   RC32 pin  5 (GPIO0 / BOOT)   -> Feather A1      [v3, new]
//   Feather TX                   -> NOT CONNECTED
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

// Symbolic Feather pin names -- resolved by the board variant header. Do not
// substitute raw GPIO numbers; they differ across Feather S3 variants.
#define PIN_RC32_RST   A0
#define PIN_RC32_BOOT  A1

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

  Serial1.begin(SNIFF_BAUD, SERIAL_8N1, RX, -1);   // RX only -- TX is -1

  Serial.println();
  Serial.println("================================================");
  Serial.println("=== RC32 UART0 SNIFFER  BUILD ID: SNIFFER-v3 ===");
  Serial.printf ("=== listening on Feather RX, %d 8N1\n", SNIFF_BAUD);
  Serial.println("=== heartbeat 1/s for 30s, then 1/10s");
  Serial.println("=== any line without [hb] or >>> is RC32 data");
  Serial.println("=== RST->A0  BOOT->A1  (open-drain, pull-low only)");
  Serial.println("=== cmds: RST BOOTRST BOOT PING HELP");
  Serial.println("================================================");
}

void loop() {
  // Relay first so real data is never delayed behind a heartbeat.
  while (Serial1.available()) {
    Serial.write(Serial1.read());
    rx_bytes++;
  }

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
