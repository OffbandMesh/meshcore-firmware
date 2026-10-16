#include "GuiViews.h"

#include <stdarg.h>
#include <stdio.h>

#include "SignalLevel.h"

// D3-D5 replace the remaining stubs (D1, #1495).

namespace offband {
namespace gui {

static void blank(char* out, size_t n) {
  if (n) out[0] = '\0';
}

// snprintf, then backs a cut off to a whole UTF-8 character.
static void put(char* out, size_t n, const char* f, ...) {
  if (!n) return;
  va_list ap;
  va_start(ap, f);
  const int w = vsnprintf(out, n, f, ap);
  va_end(ap);
  if (w < 0) {
    out[0] = '\0';
    return;
  }
  size_t len = size_t(w) < n ? size_t(w) : n - 1;
  if (size_t(w) >= n) {
    // Drop a trailing partial sequence: back up to its lead byte, then keep
    // it only if the whole sequence fit.
    size_t lead = len;
    while (lead > 0 && (uint8_t(out[lead - 1]) & 0xC0) == 0x80) lead--;
    if (lead > 0 && uint8_t(out[lead - 1]) >= 0xC0) {
      const uint8_t b = uint8_t(out[lead - 1]);
      const size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
      if (len - (lead - 1) < need) len = lead - 1;
    }
    out[len] = '\0';
  }
}

// ------------------------------------------------------------- status bar

Tone batteryTone(uint8_t percent) {
  if (percent > 30) return Tone::Ok;
  if (percent >= 15) return Tone::Warn;
  return Tone::Bad;
}

BleIcon bleIcon(bool enabled, bool connected) {
  if (!enabled) return BleIcon::Hidden;
  return connected ? BleIcon::Connected : BleIcon::Advertising;
}

GpsIcon gpsIcon(bool present, FixType fix, uint32_t secs_without_fix) {
  if (!present) return GpsIcon::Hidden;
  if (fix == FixType::Fix3D) return GpsIcon::Fix3D;
  if (fix == FixType::Fix2D) return GpsIcon::Fix2D;
  return secs_without_fix >= kGpsTimeoutSecs ? GpsIcon::TimedOut : GpsIcon::Seeking;
}

bool gpsBlinkLit(uint32_t now_ms) { return now_ms % 1000 < 500; }

// --------------------------------------------------------- LoRa popover

const char* signalWord(uint8_t bars) {
  switch (bars) {
    case 4: return "Strong";
    case 3: return "Good";
    case 2: return "Marginal";
    case 1: return "At the edge";
    default: return "Below floor";
  }
}

void loraTitle(size_t packets, char* out, size_t n) {
  if (packets == 0) put(out, n, "No packets yet");
  else if (packets == 1) put(out, n, "LoRa · last packet");
  else put(out, n, "LoRa · last %u packets", unsigned(packets));
}

// One decimal, rounded half away from zero, so -2.5 and +6.5 print as such
// on every libc.
static void tenths(float v, bool plus, const char* unit, char* out, size_t n) {
  const long t = long(v * 10.0f + (v < 0 ? -0.5f : 0.5f));
  const char* sign = t < 0 ? "-" : (plus ? "+" : "");
  const long a = t < 0 ? -t : t;
  put(out, n, "%s%ld.%ld %s", sign, a / 10, a % 10, unit);
}

void formatSnr(float snr_db, char* out, size_t n) { tenths(snr_db, true, "dB", out, n); }

void formatDbm(int16_t dbm, char* out, size_t n) { put(out, n, "%d dBm", int(dbm)); }

void formatMargin(float snr_db, uint8_t sf, char* out, size_t n) {
  tenths(snr_db - demodFloorDb(sf), false, "dB", out, n);
}

void formatFreq(float, char* out, size_t n) { blank(out, n); }
void radioSub(uint8_t, float, uint8_t, char* out, size_t n) { blank(out, n); }
void heardSub(uint16_t, uint16_t, uint16_t, char* out, size_t n) { blank(out, n); }
const char* gpsValue(bool, FixType) { return ""; }
void gpsSub(uint8_t, bool, char* out, size_t n) { blank(out, n); }
void formatUptime(uint32_t, char* out, size_t n) { blank(out, n); }
void batterySub(uint16_t, uint32_t, char* out, size_t n) { blank(out, n); }
void formatAirtime(uint16_t, char* out, size_t n) { blank(out, n); }
void unreadSub(const MsgStore&, char* out, size_t n) { blank(out, n); }

bool AdvertButton::press(uint32_t) { return false; }
void AdvertButton::result(bool, uint32_t) {}
AdvertButton::State AdvertButton::state(uint32_t) const { return State::Idle; }

void formatClock(int, uint32_t, char* out, size_t n) { blank(out, n); }
void formatDate(int, uint32_t, char* out, size_t n) { blank(out, n); }
size_t lockItems(const MsgStore&, uint32_t*, size_t, uint32_t* more) {
  if (more) *more = 0;
  return 0;
}

size_t chatOrder(const MsgStore&, uint16_t*, size_t) { return 0; }
void chatPreview(const MsgStore&, int, char* out, size_t n) { blank(out, n); }
void deliveryMeta(const MsgRecord&, bool, uint8_t, uint8_t, char* out, size_t n) { blank(out, n); }
void metaLine(const char*, const char*, const char*, char* out, size_t n) { blank(out, n); }

void ComposeBuffer::start(size_t) {}
bool ComposeBuffer::insert(const char*, size_t) { return false; }
bool ComposeBuffer::backspace() { return false; }
void ComposeBuffer::counter(char* out, size_t n) const { blank(out, n); }
Tone ComposeBuffer::counterTone() const { return Tone::Bad; }

void ShiftState::tap(uint32_t) {}
void ShiftState::typedLetter() {}
void ShiftState::autoCap(const char*, size_t) {}

void PageState::typed(char) {}

size_t lettersLayout(int16_t, int16_t, KeyRect*, size_t) { return 0; }
int nearestKey(const KeyRect*, size_t, int16_t, int16_t) { return -1; }

}  // namespace gui
}  // namespace offband
