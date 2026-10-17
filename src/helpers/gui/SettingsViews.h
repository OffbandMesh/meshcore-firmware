#pragma once

// Setup (Epic G, #1431): how settings read, what they may be set to, and the
// radio presets. Free of LVGL and the mesh so it runs in the native tests.
// Design of record: ObScreen.dc.html (Settings, Radio, Messages & alerts,
// Display) and the owner-approved gap mockups (epic-g-gap-mockups.html,
// 2026-10-17).

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

// ---------------------------------------------------------------- values

// "910.525" from 910525 kHz.
void freqText(uint32_t khz, char* out, size_t n);
// "62.5 kHz", "125 kHz", "31.25 kHz", "7.8 kHz" from Hz.
void bwText(uint32_t hz, char* out, size_t n);
void sfText(uint8_t sf, char* out, size_t n);   // "SF7"
void crText(uint8_t cr, char* out, size_t n);   // "4/5" from 5
void dbmText(int v, char* out, size_t n);       // "22 dBm"
void pctText(int v, char* out, size_t n);       // "70%"
// "30 s", "2 min", "90 s", "1 h"; 0 reads "Never".
void durationText(uint32_t secs, char* out, size_t n);
// The path hash mode as the drawn row reads: "2-byte · 32 hops". Mode is
// the hash size in bytes, less one; hops are what a 64-byte path holds
// (63 at most: the hop count is six bits).
void pathHashText(uint8_t mode, char* out, size_t n);
// The Settings list's Radio value: "910.525 · SF7".
void radioSummary(uint32_t khz, uint8_t sf, char* out, size_t n);
// The clock, 24-hour or 12-hour: "21:07", "9:07 PM".
void clockText(uint8_t hour, uint8_t minute, bool h12, char* out, size_t n);
// Telemetry modes as the app names them: 0 Never, 1 Contacts, 2 Everyone.
const char* telemetryText(uint8_t mode);

// ------------------------------------------------------------ the choices

// The bandwidths a picker offers, in Hz.
constexpr uint32_t kBandwidthsHz[] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000};
constexpr size_t kBandwidthCount = sizeof(kBandwidthsHz) / sizeof(kBandwidthsHz[0]);
constexpr uint8_t kSfMin = 5, kSfMax = 12;
constexpr uint8_t kCrMin = 5, kCrMax = 8;   // 4/5 .. 4/8
// Frequency, the same range the phone path enforces (checkRadioParams).
constexpr uint32_t kFreqMinKhz = 150000, kFreqMaxKhz = 2500000;
bool freqOk(uint32_t khz);

// A stepper's range (mockup 4): the value moves by step and stays inside.
struct StepSpec {
  int min, max, step;
};
int stepBy(int v, int dir, const StepSpec& s);

// ----------------------------------------------------------- display

// "Offband", "Steel", "Mono HC", "Accent" (a ThemeId); Offband if unknown.
const char* themeName(uint8_t theme);
// The Accent theme's eight colors (owner choice 12B: derived from the
// Offband palette, for review): the default accent, then the type tints,
// signal, warn and bad.
constexpr uint32_t kAccents[] = {0xF2C14E, 0x7BEFA8, 0x8CC8FF, 0xC3A5FF, 0xFF9EC4, 0xF2D27A, 0xFFA552, 0xFF7A6E};
constexpr size_t kAccentCount = sizeof(kAccents) / sizeof(kAccents[0]);
// Dim after (0: never) and Screen off, in seconds.
constexpr uint32_t kDimChoices[] = {10, 30, 60, 120, 300, 0};
constexpr size_t kDimCount = sizeof(kDimChoices) / sizeof(kDimChoices[0]);
constexpr uint32_t kOffChoices[] = {30, 60, 120, 300, 600, 1800};
constexpr size_t kOffCount = sizeof(kOffChoices) / sizeof(kOffChoices[0]);
// Where v is in a choice list; -1 if it isn't one.
int choiceIndex(const uint32_t* choices, size_t n, uint32_t v);

// ----------------------------------------- Node, stats, Contacts (G4)

// Radio stats rows (mockup 10): transmit share of uptime "1.2%", sent
// "412 · 96" (flood · direct), last packet "+6.3 · -97" (SNR x4, RSSI).
void airtimeText(uint32_t tx_secs, uint32_t up_secs, char* out, size_t n);
void sentText(uint32_t flood, uint32_t direct, char* out, size_t n);
void lastPacketText(int snr_x4, int rssi, char* out, size_t n);

// Contacts › Up to: autoadd_max_hops, 0 any, 1 direct only, N up to N-1.
constexpr uint32_t kHopChoices[] = {0, 1, 2, 3, 4, 6};
constexpr size_t kHopCount = sizeof(kHopChoices) / sizeof(kHopChoices[0]);
void hopLimitText(uint32_t v, char* out, size_t n);
// The list's Contacts value: "Auto-add: all / some / off". Manual off
// means every type; on, the type bits (chat, repeater, room, sensor) decide.
void autoAddText(bool manual, uint8_t bits, char* out, size_t n);

// Node › GPS interval, seconds.
constexpr uint32_t kGpsChoices[] = {10, 30, 60, 300, 600};
constexpr size_t kGpsCount = sizeof(kGpsChoices) / sizeof(kGpsChoices[0]);
// Position, degrees x 1e6: "39.5000, -84.4000", or "Not set" at 0, 0.
bool latOk(int32_t lat_e6);
bool lonOk(int32_t lon_e6);
void positionText(int32_t lat_e6, int32_t lon_e6, char* out, size_t n);

// What Node, Privacy and Contacts show and set (the companion's prefs).
struct NodeSettings {
  char name[32];
  bool gps_on;
  uint32_t gps_secs;
  int32_t lat_e6, lon_e6;
  bool share_location;
  bool extra_acks;
  uint8_t tel_base, tel_loc, tel_env;   // 0 Never, 1 Contacts, 2 Everyone
  bool manual_add;                      // false: every type is added
  uint8_t auto_bits;                    // chat 0x02, repeater 0x04, room 0x08, sensor 0x10
  uint8_t auto_hops;                    // kHopChoices
  uint16_t contacts, contacts_max;
};

// Radio stats, as the phone's stats read them.
struct RadioStats {
  uint32_t uptime_secs, tx_air_secs;
  int16_t noise_floor;
  int snr_x4, rssi;
  uint32_t recv, sent_flood, sent_direct;
};

// ------------------------------------------------------------- radio

struct RadioValues {
  uint32_t freq_khz;
  uint32_t bw_hz;
  uint8_t sf, cr;
  int8_t tx_dbm;
  uint8_t path_hash_mode;
  bool repeat;
};

// A named preset (the MeshCore app's community list, then Offband's
// published overlay). tx 0 and path_bytes 0 leave those settings alone.
struct RadioPreset {
  const char* title;
  uint32_t freq_khz, bw_hz;
  uint8_t sf, cr;
  int8_t tx_dbm;
  uint8_t path_bytes;
};
size_t radioPresetCount();
const RadioPreset& radioPreset(size_t i);
// The preset these values are: `hint` (the one last picked) if it still
// matches, else the first that does; -1 for Custom. Frequency, bandwidth,
// SF and CR decide, and the path hash size where the preset sets one
// (USA and Canada differ only in that); TX power doesn't.
int presetFor(const RadioValues& v, int hint);
// The values with a preset applied; TX power is held to the board's maximum.
RadioValues applyPreset(const RadioValues& v, size_t preset, int8_t tx_max);

// The discard prompt's lines (mockup 6): "TX power 22 → 20 dBm", one per
// changed value, at most `max`; returns how many changed in all.
size_t radioChanges(const RadioValues& was, const RadioValues& now, char (*lines)[48], size_t max);

// -------------------------------------------------------------- number pad

// The frequency (and later position) entry of mockup 5: digits, one point,
// at most `decimals` after it, `max_len` characters in all.
class NumberEntry {
public:
  // signed_: '-' toggles a leading minus (a position).
  void start(const char* text, uint8_t decimals, uint8_t max_len, bool signed_ = false);
  bool key(char c);   // a digit, '.' or (signed) '-'; false if it doesn't fit
  void backspace();
  const char* text() const { return _buf; }
  // The value in thousandths ("910.525" -> 910525); false if empty or a bare point.
  bool thousandths(uint32_t* out) const;
  // The value times 10^decimals, signed ("-84.4", 6 -> -84400000); false if
  // empty, a bare point or sign, or too big for 32 bits.
  bool scaled(uint8_t decimals, int32_t* out) const;

private:
  char _buf[16] = {};
  uint8_t _len = 0, _decimals = 3, _max = 10;
  bool _signed = false;
};

}  // namespace gui
}  // namespace offband
