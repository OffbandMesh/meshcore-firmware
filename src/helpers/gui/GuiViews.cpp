#include "GuiViews.h"

#include <stdarg.h>
#include <stdio.h>

#include "../TimeZones.h"
#include "SignalLevel.h"

// D4 and D5 replace the remaining stubs (D1, #1495).

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

// ------------------------------------------------------------------- Home

void formatFreq(float mhz, char* out, size_t n) { put(out, n, "%.3f", double(mhz)); }

void radioSub(uint8_t sf, float bw_khz, uint8_t cr, char* out, size_t n) {
  // 125 and 250 print whole; 62.5, 7.8 and 10.4 keep their tenth.
  const long t = long(bw_khz * 10.0f + 0.5f);
  if (t % 10 == 0) put(out, n, "SF%u · BW%ld · CR%u", unsigned(sf), t / 10, unsigned(cr));
  else put(out, n, "SF%u · BW%ld.%ld · CR%u", unsigned(sf), t / 10, t % 10, unsigned(cr));
}

void heardSub(uint16_t repeaters, uint16_t rooms, uint16_t chat, char* out, size_t n) {
  put(out, n, "%u rpt · %u room · %u chat", unsigned(repeaters), unsigned(rooms), unsigned(chat));
}

const char* gpsValue(bool present, FixType fix) {
  if (!present) return "Off";
  if (fix == FixType::Fix3D) return "3D";
  if (fix == FixType::Fix2D) return "2D";
  return "No fix";
}

void gpsSub(uint8_t sats, bool clock_synced, char* out, size_t n) {
  put(out, n, "%u sat%s%s", unsigned(sats), sats == 1 ? "" : "s", clock_synced ? " · clock synced" : "");
}

void formatUptime(uint32_t secs, char* out, size_t n) {
  const uint32_t m = secs / 60, h = m / 60, d = h / 24;
  if (h == 0) put(out, n, "%lum", (unsigned long)m);
  else if (d == 0) put(out, n, "%luh %lum", (unsigned long)h, (unsigned long)(m % 60));
  else put(out, n, "%lud %luh", (unsigned long)d, (unsigned long)(h % 24));
}

void batterySub(uint16_t millivolts, uint32_t uptime_secs, char* out, size_t n) {
  char up[16];
  formatUptime(uptime_secs, up, sizeof up);
  const unsigned cv = (unsigned(millivolts) + 5) / 10;   // hundredths of a volt
  put(out, n, "%u.%02u V · %s up", cv / 100, cv % 100, up);
}

void formatAirtime(uint16_t permille, char* out, size_t n) {
  put(out, n, "%u.%u%%", unsigned(permille / 10), unsigned(permille % 10));
}

// Threads with unread messages, newest activity first. Returns the count.
static size_t unreadThreads(const MsgStore& s, uint16_t* out, size_t max) {
  if (!s.ready()) return 0;
  size_t k = 0;
  for (int t = 0; t < s.maxThreads() && k < max; t++) {
    const Thread* th = s.threadAt(t);
    if (!th || th->unread == 0) continue;
    size_t i = k++;
    while (i > 0 && s.threadAt(out[i - 1])->last_seq < th->last_seq) {
      out[i] = out[i - 1];
      i--;
    }
    out[i] = uint16_t(t);
  }
  return k;
}

constexpr size_t kMaxListed = 256;

void unreadSub(const MsgStore& store, char* out, size_t n) {
  uint16_t ts[kMaxListed];
  const size_t k = unreadThreads(store, ts, kMaxListed);
  if (k == 0) return blank(out, n);
  const char* a = store.threadAt(ts[0])->name;
  if (k == 1) return put(out, n, "%s", a);
  const char* b = store.threadAt(ts[1])->name;
  if (k == 2) return put(out, n, "%s, %s", a, b);
  put(out, n, "%s, %s +%u", a, b, unsigned(k - 2));
}

bool AdvertButton::press(uint32_t now_ms) {
  if (_pending || state(now_ms) != State::Idle) return false;
  _pending = true;
  return true;
}

void AdvertButton::result(bool sent, uint32_t now_ms) {
  _pending = false;
  _state = sent ? State::Sent : State::NotSent;
  _at = now_ms;
}

AdvertButton::State AdvertButton::state(uint32_t now_ms) const {
  if (_state != State::Idle && now_ms - _at >= kAdvertFeedbackMs) return State::Idle;
  return _state;
}

void keySummary(const uint8_t* k, char* out, size_t n) {
  put(out, n, "%02x%02x %02x%02x … %02x%02x", k[0], k[1], k[2], k[3], k[30], k[31]);
}

// ------------------------------------------------------------------- Lock

void formatClock(int zone, uint32_t utc, char* out, size_t n) {
  if (!n) return;
  if (!tz::clockSet(utc)) return put(out, n, "--:--");
  tz::clock(zone, utc, out, n);
}

// Days since 1970-01-01 to a civil date (H. Hinnant's days_from_civil, inverted).
static void civil(int32_t z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = unsigned(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = int(yoe) + era * 400 + (*m <= 2);
}

void formatDate(int zone, uint32_t utc, char* out, size_t n) {
  if (!tz::clockSet(utc)) return blank(out, n);
  static const char* const kDay[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const int64_t local = tz::localSeconds(zone, utc);
  const int32_t days = int32_t(local >= 0 ? local / 86400 : (local - 86399) / 86400);
  int y;
  unsigned m, d;
  civil(days, &y, &m, &d);
  put(out, n, "%s %u %s", kDay[tz::weekday(days)], d, kMon[m - 1]);
}

// The newest incoming message of thread t, or 0.
static uint32_t newestIncoming(const MsgStore& s, int t) {
  uint32_t seqs[16];
  const size_t k = s.threadMessages(t, seqs, 16);
  for (size_t i = k; i > 0; i--) {
    const MsgRecord* m = s.msg(seqs[i - 1]);
    if (m && !m->own) return seqs[i - 1];
  }
  return 0;
}

size_t lockItems(const MsgStore& store, uint32_t* out, size_t max, uint32_t* more) {
  uint16_t ts[kMaxListed];
  const size_t k = unreadThreads(store, ts, kMaxListed);
  size_t shown = 0;
  uint32_t rest = 0;
  for (size_t i = 0; i < k; i++) {
    const uint32_t seq = shown < max ? newestIncoming(store, ts[i]) : 0;
    if (seq) out[shown++] = seq;
    else rest += store.threadAt(ts[i])->unread;
  }
  if (more) *more = rest;
  return shown;
}

void lockLabel(const Thread& t, char* out, size_t n) {
  if (ThreadKind(t.kind) == ThreadKind::Direct && t.unread == 1) put(out, n, "%s · direct", t.name);
  else put(out, n, "%s · %u new", t.name, unsigned(t.unread));
}

void lockText(const MsgRecord& m, ThreadKind kind, char* out, size_t n) {
  const int len = int(m.text_len);
  if (kind != ThreadKind::Direct && m.sender[0]) put(out, n, "%s: %.*s", m.sender, len, m.text);
  else put(out, n, "%.*s", len, m.text);
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
