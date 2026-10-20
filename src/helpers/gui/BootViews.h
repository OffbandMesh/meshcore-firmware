#pragma once

// The boot screen (Epic G G6, #1571): its check rows and its lines. The GUI
// starts once setup is done, so the rows are what setup left behind, read
// when the screen is built. Free of LVGL and the mesh so it runs in the
// native tests. Design of record: ObScreen.dc.html (boot) and mockup 18 of
// epic-g-gap-mockups.html (owner-approved 2026-10-17).

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

enum class Check : uint8_t { Ok, Pending, Off };
const char* checkTag(Check c);   // "[ok]", "[..]", "[--]"

// Where the settings came from at boot.
enum class PrefsSource : uint8_t { File, Migrated, Defaults };

// What setup left behind.
struct BootFacts {
  PrefsSource prefs;
  const char* radio;              // the chip, "SX1262"
  bool gps_built, gps_on, gps_seen;
  uint16_t contacts, contacts_max;
  bool ble_built, ble_on, phone;
};

constexpr size_t kBootRowsMax = 5;
struct BootRow {
  Check state;
  char text[28];
};
// The rows in setup's order. A radio that fails halts boot, so it is always
// ok here; what this build lacks (GPS, Bluetooth) has no row.
size_t bootRows(const BootFacts& f, BootRow* rows, size_t max);

// Whether the reset reason (esp_reset_reason) or a repeated boot attempt is
// worth a line. Power on, a restart, RST, deep sleep and a USB host's reset
// are clean; 0 (unknown) is what IDF 4.4 reports for the USB host's.
bool uncleanBoot(uint32_t reset_code, uint8_t attempt);
// "boot 1/3 · last reset: brownout"; with no SafeBoot count, the reset alone.
void bootLine(uint8_t attempt, uint8_t limit, uint32_t reset_code, char* out, size_t n);
// "Offband 1.5.0 - MC 1.17.0" from the short Offband version and "v1.17.0".
void bootVersionLine(const char* meshcore, const char* offband, char* out, size_t n);
// The bar: how far through the boot screen's hold, 0-100.
int bootProgress(uint32_t elapsed_ms, uint32_t hold_ms);

// The bar is "offband" in Morse (mockup 18b): its marks light left to right
// as the hold runs, so the word is spelled out when it ends. Letters are
// separated by spaces.
constexpr const char* kOffbandMorse = "--- ..-. ..-. -... .- -. -..";
size_t morseMarks(const char* code);              // dots and dashes
size_t morseLit(int progress, size_t marks);      // lit at 0-100

}  // namespace gui
}  // namespace offband
