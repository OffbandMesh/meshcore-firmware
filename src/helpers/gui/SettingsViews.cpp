#include "SettingsViews.h"

#include <stdio.h>
#include <string.h>

#include "GuiViews.h"
#include "NodeViews.h"   // thousands

namespace offband {
namespace gui {

namespace {

// The MeshCore app's community presets (maintained by Liam Cottle,
// api.meshcore.nz, updated 2026-10-01), then Offband's published overlay
// (OffbandMesh/config-profiles radio-presets/offband.json), as the Offband
// client bundles them (meshcore-open assets/radio_presets, 3c7c4d9d).
// Generated; regenerate from those two files rather than editing by hand.
// {title, kHz, Hz, SF, CR, TX dBm (0: leave), path hash bytes (0: leave)}
const RadioPreset kPresets[] = {
    {"Australia", 915800, 250000, 10, 5, 0, 0},
    {"Australia (Narrow)", 916575, 62500, 7, 7, 0, 0},
    {"Australia (Mid)", 915075, 125000, 9, 5, 0, 0},
    {"Australia: SA, WA", 923125, 62500, 8, 8, 0, 0},
    {"Australia: QLD", 923125, 62500, 8, 5, 0, 0},
    {"Brazil", 923125, 62500, 8, 8, 0, 0},
    {"Canada", 910525, 62500, 7, 5, 0, 3},
    {"Costa Rica", 910525, 125000, 11, 5, 0, 0},
    {"EU/UK (Narrow)", 869618, 62500, 8, 8, 0, 0},
    {"EU/UK (Deprecated)", 869525, 250000, 11, 5, 0, 0},
    {"Czech Republic (Narrow)", 869432, 62500, 7, 5, 0, 2},
    {"EU 433MHz (Long Range)", 433650, 250000, 11, 5, 0, 0},
    {"EU 433MHz (Narrow)", 433650, 62500, 8, 8, 0, 0},
    {"Hungary", 869618, 62500, 7, 5, 0, 2},
    {"Netherlands", 869618, 62500, 7, 5, 0, 0},
    {"Netherlands (Limburg)", 869618, 62500, 8, 8, 0, 2},
    {"New Zealand (Narrow)", 917375, 62500, 7, 5, 0, 2},
    {"New Zealand (Gisborne)", 917375, 250000, 11, 5, 0, 1},
    {"Portugal 433", 433375, 62500, 9, 6, 0, 0},
    {"Portugal 868", 869618, 62500, 7, 6, 0, 0},
    {"Slovakia", 869618, 62500, 7, 5, 0, 2},
    {"Switzerland", 869618, 62500, 8, 8, 0, 0},
    {"USA", 910525, 62500, 7, 5, 0, 0},
    {"USA - Southern California", 927875, 62500, 7, 5, 0, 3},
    {"Vietnam (Narrow)", 920250, 62500, 8, 5, 0, 0},
    {"Vietnam (Deprecated)", 920250, 250000, 11, 5, 0, 0},
    {"USA - Arizona", 908205, 62500, 9, 8, 20, 0},
    {"USA - Philly Mesh", 919500, 500000, 10, 5, 20, 2},
    {"USA - OKI-Mesh 500kHz Test", 909750, 500000, 10, 5, 20, 0},
    {"Russia Artyom (VVO)", 864281, 62500, 8, 6, 20, 0},
    {"Russia Biysk (BSK)", 869000, 62500, 8, 5, 20, 0},
    {"Russia Chelyabinsk (CEK)", 868731, 62500, 8, 6, 20, 0},
    {"Russia Cherepovets (CEE)", 868570, 62500, 7, 8, 20, 0},
    {"Russia Irkutsk (IKT)", 868731, 62500, 7, 7, 20, 0},
    {"Russia Ivanovo (IWA)", 868731, 62500, 8, 8, 20, 0},
    {"Russia Izhevsk (IJK)", 868732, 62500, 8, 8, 20, 0},
    {"Russia Kaluga (KLF)", 868731, 62500, 7, 7, 20, 0},
    {"Russia Kazan (KZN)", 868731, 62500, 8, 6, 20, 0},
    {"Russia Khabarovsk (KHV)", 864281, 62500, 8, 6, 20, 0},
    {"Russia Kirov (KVX)", 868731, 62500, 8, 8, 20, 0},
    {"Russia Lipetsk (LPK)", 868950, 62500, 9, 7, 20, 0},
    {"Russia Moscow (MOW)", 868731, 62500, 7, 7, 20, 0},
    {"Russia Nizhny Novgorod (GOJ)", 868731, 62500, 8, 6, 20, 0},
    {"Russia Novosibirsk (OVB)", 869000, 62500, 9, 8, 20, 0},
    {"Russia Rostov-on-Don (ROV)", 868731, 62500, 9, 7, 20, 0},
    {"Russia Ryazan (RZN)", 868880, 62500, 9, 5, 20, 0},
    {"Russia Samara (KUF)", 864281, 62500, 8, 7, 20, 0},
    {"Russia Saratov (GSV)", 864281, 62500, 8, 7, 20, 0},
    {"Russia St. Petersburg (LED)", 868856, 62500, 7, 7, 20, 0},
    {"Russia Tambov (TBW)", 868950, 62500, 10, 5, 20, 0},
    {"Russia Tula (TYA)", 868731, 62500, 8, 7, 20, 0},
    {"Russia Tver (KLD)", 869169, 62500, 8, 8, 20, 0},
    {"Russia Ufa (UFA)", 868732, 62500, 8, 8, 20, 0},
    {"Russia Volgograd (VOG)", 869525, 62500, 7, 7, 20, 0},
    {"Russia Voronezh (VOZ)", 868731, 62500, 8, 6, 20, 0},
    {"Russia Yekaterinburg (SVX)", 869046, 62500, 7, 7, 20, 0},
    {"Off-Grid 433", 433000, 250000, 11, 8, 20, 0},
    {"Off-Grid 869", 869000, 250000, 11, 8, 14, 0},
    {"Off-Grid 918", 918000, 250000, 11, 8, 20, 0},
};
constexpr size_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

// A preset that sets the path hash size (Canada: 3-byte) only matches that
// size; one that leaves it alone matches any.
bool matches(const RadioPreset& p, const RadioValues& v) {
  return p.freq_khz == v.freq_khz && p.bw_hz == v.bw_hz && p.sf == v.sf && p.cr == v.cr &&
         (p.path_bytes == 0 || p.path_bytes - 1 == v.path_hash_mode);
}

}  // namespace

void freqText(uint32_t khz, char* out, size_t n) {
  textf(out, n, "%lu.%03lu", (unsigned long)(khz / 1000), (unsigned long)(khz % 1000));
}

void bwText(uint32_t hz, char* out, size_t n) {
  if (hz % 1000 == 0) {
    textf(out, n, "%lu kHz", (unsigned long)(hz / 1000));
    return;
  }
  char frac[4];
  textf(frac, sizeof frac, "%03lu", (unsigned long)(hz % 1000));
  for (int i = 2; i > 0 && frac[i] == '0'; i--) frac[i] = '\0';   // 62.500 -> 62.5
  textf(out, n, "%lu.%s kHz", (unsigned long)(hz / 1000), frac);
}

void sfText(uint8_t sf, char* out, size_t n) { textf(out, n, "SF%u", unsigned(sf)); }
void crText(uint8_t cr, char* out, size_t n) { textf(out, n, "4/%u", unsigned(cr)); }
void dbmText(int v, char* out, size_t n) { textf(out, n, "%d dBm", v); }
void pctText(int v, char* out, size_t n) { textf(out, n, "%d%%", v); }

void durationText(uint32_t secs, char* out, size_t n) {
  if (secs == 0) textf(out, n, "Never");
  else if (secs >= 3600 && secs % 3600 == 0) textf(out, n, "%lu h", (unsigned long)(secs / 3600));
  else if (secs >= 60 && secs % 60 == 0) textf(out, n, "%lu min", (unsigned long)(secs / 60));
  else textf(out, n, "%lu s", (unsigned long)secs);
}

void pathHashText(uint8_t mode, char* out, size_t n) {
  const unsigned bytes = unsigned(mode) + 1;
  const unsigned hops = 64 / bytes > 63 ? 63 : 64 / bytes;
  textf(out, n, "%u-byte · %u hops", bytes, hops);
}

void radioSummary(uint32_t khz, uint8_t sf, char* out, size_t n) {
  char f[16];
  freqText(khz, f, sizeof f);
  textf(out, n, "%s · SF%u", f, unsigned(sf));
}

void clockText(uint8_t hour, uint8_t minute, bool h12, char* out, size_t n) {
  const unsigned h24 = hour % 24;
  if (!h12) {
    textf(out, n, "%02u:%02u", h24, unsigned(minute));
    return;
  }
  const unsigned h = h24 % 12 == 0 ? 12 : h24 % 12;
  textf(out, n, "%u:%02u %s", h, unsigned(minute), h24 >= 12 ? "PM" : "AM");
}

const char* telemetryText(uint8_t mode) {
  return mode == 0 ? "Never" : mode == 1 ? "Contacts" : "Everyone";
}

bool freqOk(uint32_t khz) { return khz >= kFreqMinKhz && khz <= kFreqMaxKhz; }

void airtimeText(uint32_t tx_secs, uint32_t up_secs, char* out, size_t n) {
  const uint32_t permille = up_secs ? uint32_t((uint64_t(tx_secs) * 1000 + up_secs / 2) / up_secs) : 0;
  textf(out, n, "%lu.%lu%%", (unsigned long)(permille / 10), (unsigned long)(permille % 10));
}

void sentText(uint32_t flood, uint32_t direct, char* out, size_t n) {
  char a[16], b[16];
  thousands(flood, a, sizeof a);
  thousands(direct, b, sizeof b);
  textf(out, n, "%s · %s", a, b);
}

void lastPacketText(int snr_x4, int rssi, char* out, size_t n) {
  // Tenths, rounded half away from zero, as the Nodes rows show SNR.
  const int t = snr_x4 >= 0 ? (snr_x4 * 10 + 2) / 4 : -((-snr_x4 * 10 + 2) / 4);
  const int a = t < 0 ? -t : t;
  textf(out, n, "%c%d.%d · %d", t < 0 ? '-' : '+', a / 10, a % 10, rssi);
}

void hopLimitText(uint32_t v, char* out, size_t n) {
  if (v == 0) textf(out, n, "Any");
  else if (v == 1) textf(out, n, "Direct");
  else textf(out, n, "%lu hop%s", (unsigned long)(v - 1), v == 2 ? "" : "s");
}

void autoAddText(bool manual, uint8_t bits, char* out, size_t n) {
  const uint8_t types = bits & 0x1E;
  const char* what = !manual || types == 0x1E ? "all" : types ? "some" : "off";
  textf(out, n, "Auto-add: %s", what);
}

bool latOk(int32_t v) { return v >= -90000000 && v <= 90000000; }
bool lonOk(int32_t v) { return v >= -180000000 && v <= 180000000; }

void positionText(int32_t lat, int32_t lon, char* out, size_t n) {
  if (lat == 0 && lon == 0) {
    textf(out, n, "Not set");
    return;
  }
  auto four = [](int32_t e6, char* o, size_t m) {   // degrees to 4 places, rounded
    const int64_t t = (int64_t(e6 < 0 ? -e6 : e6) + 50) / 100;
    textf(o, m, "%s%lld.%04lld", e6 < 0 ? "-" : "", (long long)(t / 10000), (long long)(t % 10000));
  };
  char a[16], b[16];
  four(lat, a, sizeof a);
  four(lon, b, sizeof b);
  textf(out, n, "%s, %s", a, b);
}

void voltsText(uint32_t mv, char* out, size_t n) {
  if (mv == 0) {
    textf(out, n, "—");
    return;
  }
  const uint32_t c = (mv + 5) / 10;   // hundredths
  textf(out, n, "%lu.%02lu V", (unsigned long)(c / 100), (unsigned long)(c % 100));
}

void batteryNowText(uint32_t mv, int pct, char* out, size_t n) {
  char v[16];
  voltsText(mv, v, sizeof v);
  textf(out, n, "%s · %d%%", v, pct);
}

bool pinOk(uint32_t pin) { return pin >= 100000 && pin <= 999999; }

void pinText(uint32_t stored, uint32_t active, char* out, size_t n) {
  if (stored) textf(out, n, "******");   // the GUI fonts have no •
  else if (active) textf(out, n, "%06lu", (unsigned long)active);
  else textf(out, n, "None");
}

int logIndex(bool on, uint8_t level) {
  if (!on) return 0;
  return level < 1 ? 1 : level > 3 ? 3 : level;
}

const char* logName(int i) {
  static const char* const kNames[kLogCount] = {"Off", "Errors", "Debug", "Packets"};
  return i >= 0 && size_t(i) < kLogCount ? kNames[i] : kNames[0];
}

void ofText(uint32_t a, uint32_t b, char* out, size_t n) {
  char x[16], y[16];
  thousands(a, x, sizeof x);
  thousands(b, y, sizeof y);
  textf(out, n, "%s / %s", x, y);
}

void buildText(const char* sha, const char* ymd, char* out, size_t n) {
  static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int y = 0, m = 0, d = 0;
  if (ymd && sscanf(ymd, "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
    textf(out, n, "%s · %s %d", sha, kMonths[m - 1], d);
  } else {
    textf(out, n, "%s", sha);
  }
}

const char* resetName(uint32_t code) {
  // esp_reset_reason_t (ResetReason.h has the long forms), in a row's width.
  switch (code) {
    case 1: return "Power on";
    case 2: return "RST button";
    case 3: return "Software";
    case 4: return "Crash";
    case 5:
    case 6:
    case 7: return "Watchdog";
    case 8: return "Deep sleep";
    case 9: return "Brownout";
    case 10: return "SDIO";
    case 11: return "USB";
    case 12: return "JTAG";
    case 13: return "eFuse";
    case 14: return "Power glitch";
    case 15: return "CPU lockup";
    default: return "Unknown";
  }
}

const char* themeName(uint8_t theme) {
  static const char* const kNames[] = {"Offband", "Steel", "Mono HC", "Accent"};
  return theme < 4 ? kNames[theme] : kNames[0];
}

int choiceIndex(const uint32_t* choices, size_t n, uint32_t v) {
  for (size_t i = 0; i < n; i++) {
    if (choices[i] == v) return int(i);
  }
  return -1;
}

int stepBy(int v, int dir, const StepSpec& s) {
  if (s.step <= 0) return v;
  const int off = v - s.min;
  const bool on_grid = off % s.step == 0;
  const int k = off / s.step;
  int next;
  if (dir > 0) next = on_grid ? v + s.step : s.min + (k + 1) * s.step;
  else next = on_grid ? v - s.step : s.min + k * s.step;
  return next < s.min ? s.min : next > s.max ? s.max : next;
}

size_t radioPresetCount() { return kPresetCount; }
const RadioPreset& radioPreset(size_t i) { return kPresets[i < kPresetCount ? i : 0]; }

int presetFor(const RadioValues& v, int hint) {
  if (hint >= 0 && size_t(hint) < kPresetCount && matches(kPresets[hint], v)) return hint;
  for (size_t i = 0; i < kPresetCount; i++) {
    if (matches(kPresets[i], v)) return int(i);
  }
  return -1;
}

RadioValues applyPreset(const RadioValues& v, size_t preset, int8_t tx_max) {
  RadioValues r = v;
  if (preset >= kPresetCount) return r;
  const RadioPreset& p = kPresets[preset];
  r.freq_khz = p.freq_khz;
  r.bw_hz = p.bw_hz;
  r.sf = p.sf;
  r.cr = p.cr;
  if (p.tx_dbm > 0) r.tx_dbm = p.tx_dbm < tx_max ? p.tx_dbm : tx_max;
  if (p.path_bytes > 0) r.path_hash_mode = uint8_t(p.path_bytes - 1);
  return r;
}

size_t radioChanges(const RadioValues& was, const RadioValues& now, char (*lines)[48], size_t max) {
  size_t k = 0;
  auto add = [&](const char* name, const char* a, const char* b) {
    if (k < max) textf(lines[k], sizeof lines[k], "%s %s → %s", name, a, b);
    k++;
  };
  char a[24], b[24];
  if (was.freq_khz != now.freq_khz) {
    freqText(was.freq_khz, a, sizeof a);
    freqText(now.freq_khz, b, sizeof b);
    add("Frequency", a, b);
  }
  if (was.bw_hz != now.bw_hz) {
    bwText(was.bw_hz, a, sizeof a);
    bwText(now.bw_hz, b, sizeof b);
    add("Bandwidth", a, b);
  }
  if (was.sf != now.sf) {
    sfText(was.sf, a, sizeof a);
    sfText(now.sf, b, sizeof b);
    add("Spreading factor", a, b);
  }
  if (was.cr != now.cr) {
    crText(was.cr, a, sizeof a);
    crText(now.cr, b, sizeof b);
    add("Coding rate", a, b);
  }
  if (was.tx_dbm != now.tx_dbm) {
    textf(a, sizeof a, "%d", int(was.tx_dbm));
    dbmText(now.tx_dbm, b, sizeof b);
    add("TX power", a, b);
  }
  if (was.repeat != now.repeat) add("Off-grid repeat", was.repeat ? "on" : "off", now.repeat ? "on" : "off");
  if (was.path_hash_mode != now.path_hash_mode) {
    textf(a, sizeof a, "%u-byte", unsigned(was.path_hash_mode) + 1);
    textf(b, sizeof b, "%u-byte", unsigned(now.path_hash_mode) + 1);
    add("Path hash", a, b);
  }
  return k;
}

void NumberEntry::start(const char* text, uint8_t decimals, uint8_t max_len, bool signed_) {
  _decimals = decimals;
  _max = max_len < sizeof _buf ? max_len : uint8_t(sizeof _buf - 1);
  _signed = signed_;
  _len = 0;
  _buf[0] = '\0';
  for (const char* p = text ? text : ""; *p; p++) key(*p);
}

bool NumberEntry::key(char c) {
  if (c == '-') {   // the sign, at the front, toggled
    if (!_signed) return false;
    if (_buf[0] == '-') {
      memmove(_buf, _buf + 1, _len);
      _len--;
      return true;
    }
    if (_len >= _max) return false;
    memmove(_buf + 1, _buf, size_t(_len) + 1);
    _buf[0] = '-';
    _len++;
    return true;
  }
  if (_len >= _max) return false;
  const char* dot = strchr(_buf, '.');
  if (c == '.') {
    if (dot || _decimals == 0) return false;
  } else if (c >= '0' && c <= '9') {
    if (dot && size_t(_buf + _len - dot - 1) >= _decimals) return false;
  } else {
    return false;
  }
  _buf[_len++] = c;
  _buf[_len] = '\0';
  return true;
}

void NumberEntry::backspace() {
  if (_len) _buf[--_len] = '\0';
}

bool NumberEntry::scaled(uint8_t decimals, int32_t* out) const {
  const bool neg = _buf[0] == '-';
  int64_t v = 0;
  unsigned frac_digits = 0;
  bool any = false, after = false;
  for (const char* p = _buf + (neg ? 1 : 0); *p; p++) {
    if (*p == '.') {
      after = true;
      continue;
    }
    any = true;
    if (after && frac_digits >= decimals) continue;   // past the precision kept
    v = v * 10 + (*p - '0');
    if (after) frac_digits++;
    if (v > 0x7FFFFFFFLL) return false;
  }
  if (!any) return false;
  while (frac_digits < decimals) {
    v *= 10;
    frac_digits++;
    if (v > 0x7FFFFFFFLL) return false;
  }
  *out = int32_t(neg ? -v : v);
  return true;
}

bool NumberEntry::thousandths(uint32_t* out) const {
  if (_buf[0] == '-') return false;   // frequencies have no sign
  uint32_t whole = 0, frac = 0;
  unsigned frac_digits = 0;
  bool any = false, after = false;
  for (const char* p = _buf; *p; p++) {
    if (*p == '.') {
      after = true;
      continue;
    }
    any = true;
    if (after) {
      if (frac_digits < 3) {
        frac = frac * 10 + uint32_t(*p - '0');
        frac_digits++;
      }
    } else {
      whole = whole * 10 + uint32_t(*p - '0');
      if (whole > 0xFFFFFFFFu / 1000) return false;   // too big to hold in thousandths
    }
  }
  if (!any) return false;
  while (frac_digits < 3) {
    frac *= 10;
    frac_digits++;
  }
  if (whole * 1000 > 0xFFFFFFFFu - frac) return false;
  *out = whole * 1000 + frac;
  return true;
}

}  // namespace gui
}  // namespace offband
