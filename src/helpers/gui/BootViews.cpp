#include "BootViews.h"

#include <ctype.h>
#include <string.h>

#include "GuiViews.h"        // textf
#include "SettingsViews.h"   // resetName

namespace offband {
namespace gui {

const char* checkTag(Check c) {
  switch (c) {
    case Check::Ok: return "[ok]";
    case Check::Pending: return "[..]";
    default: return "[--]";
  }
}

size_t bootRows(const BootFacts& f, BootRow* rows, size_t max) {
  size_t n = 0;
  char t[sizeof rows[0].text];
  auto add = [&](Check s, const char* text) {
    if (n >= max) return;
    rows[n].state = s;
    textf(rows[n].text, sizeof rows[n].text, "%s", text);
    n++;
  };
  switch (f.prefs) {
    case PrefsSource::File: add(Check::Ok, "Settings"); break;
    case PrefsSource::Migrated: add(Check::Ok, "Settings migrated"); break;
    default: add(Check::Off, "Settings: defaults"); break;
  }
  textf(t, sizeof t, "%s radio", f.radio ? f.radio : "LoRa");
  add(Check::Ok, t);
  if (f.gps_built) {
    if (!f.gps_on) add(Check::Off, "GPS off");
    else add(f.gps_seen ? Check::Ok : Check::Pending, "GPS");
  }
  textf(t, sizeof t, "Contacts %u / %u", unsigned(f.contacts), unsigned(f.contacts_max));
  add(Check::Ok, t);
  if (f.ble_built) {
    if (!f.ble_on) add(Check::Off, "Bluetooth off");
    else add(Check::Ok, f.phone ? "Phone connected" : "BLE advertising");
  }
  return n;
}

bool uncleanBoot(uint32_t code, uint8_t attempt) {
  if (attempt > 1) return true;
  switch (code) {
    case 0:    // unknown
    case 1:    // power on
    case 2:    // RST
    case 3:    // software restart
    case 8:    // deep-sleep wake
    case 11:   // USB host
      return false;
    default:
      return true;
  }
}

void bootLine(uint8_t attempt, uint8_t limit, uint32_t code, char* out, size_t n) {
  char why[24];
  textf(why, sizeof why, "%s", resetName(code));
  for (char* p = why; *p; p++) *p = char(tolower(static_cast<unsigned char>(*p)));
  if (limit) textf(out, n, "boot %u/%u · last reset: %s", unsigned(attempt), unsigned(limit), why);
  else textf(out, n, "last reset: %s", why);
}

void bootVersionLine(const char* meshcore, const char* offband, char* out, size_t n) {
  // Both in full, without the leading "v"; Offband as the splash shows it.
  const char* mm = meshcore && (meshcore[0] == 'v' || meshcore[0] == 'V') ? meshcore + 1 : (meshcore ? meshcore : "");
  const char* o = offband && (offband[0] == 'v' || offband[0] == 'V') ? offband + 1 : (offband ? offband : "");
  if (o[0]) textf(out, n, "Offband %s - MC %s", o, mm);
  else textf(out, n, "MC %s", mm);
}

int bootProgress(uint32_t elapsed_ms, uint32_t hold_ms) {
  if (hold_ms == 0 || elapsed_ms >= hold_ms) return 100;
  return int(uint64_t(elapsed_ms) * 100 / hold_ms);
}

size_t morseMarks(const char* code) {
  size_t n = 0;
  for (const char* p = code; p && *p; p++) n += (*p == '.' || *p == '-');
  return n;
}

size_t morseLit(int progress, size_t marks) {
  if (progress <= 0) return 0;
  if (progress >= 100) return marks;
  return marks * size_t(progress) / 100;
}

}  // namespace gui
}  // namespace offband
