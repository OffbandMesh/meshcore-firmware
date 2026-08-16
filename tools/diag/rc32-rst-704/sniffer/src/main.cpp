#include <Arduino.h>
// RC32 UART0 sniffer -- Adafruit Feather ESP32-S3.   BUILD ID: SNIFFER-v2
//
// Wiring (two wires, listen-only):
//   RC32 pin 12 (U0TXD / GPIO43) -> Feather "RX"
//   RC32 pin 20 (GND)            -> Feather "GND"
//   Feather "TX" -> NOT CONNECTED (TX is passed as -1 below, so we can never
//   drive the RC32's line even by accident).
//
// v2 adds a HEARTBEAT because v1 produced silence and we could not tell whether
// that meant "RC32 said nothing" or "sniffer is not running". Silence is only
// evidence if the instrument is provably alive.

#define SNIFF_BAUD   115200   // ESP32-S3 ROM bootloader default
#define HB_FAST_MS   1000     // heartbeat cadence for the first 30 s
#define HB_FAST_FOR  30000    // then slow down so it cannot drown real data
#define HB_SLOW_MS   10000

static uint32_t hb_next = 0, hb_n = 0;
static uint32_t rx_bytes = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  Serial1.begin(SNIFF_BAUD, SERIAL_8N1, RX, -1);   // RX only

  Serial.println();
  Serial.println("================================================");
  Serial.println("=== RC32 UART0 SNIFFER  BUILD ID: SNIFFER-v2 ===");
  Serial.printf ("=== listening on Feather RX, %d 8N1\n", SNIFF_BAUD);
  Serial.println("=== heartbeat 1/s for 30s, then 1/10s");
  Serial.println("=== any line WITHOUT the [hb] prefix is RC32 data");
  Serial.println("================================================");
}

void loop() {
  // Relay first so real data is never delayed behind a heartbeat.
  while (Serial1.available()) {
    Serial.write(Serial1.read());
    rx_bytes++;
  }

  uint32_t now = millis();
  if ((int32_t)(now - hb_next) >= 0) {
    hb_next = now + (now < HB_FAST_FOR ? HB_FAST_MS : HB_SLOW_MS);
    // rx_bytes is the payoff: a rising count proves the WIRE carries data,
    // independently of whether that data is decodable at this baud.
    Serial.printf("[hb] SNIFFER-v2 alive  n=%lu  up=%lus  rx_bytes=%lu\n",
                  (unsigned long)++hb_n,
                  (unsigned long)(now / 1000),
                  (unsigned long)rx_bytes);
  }
}
