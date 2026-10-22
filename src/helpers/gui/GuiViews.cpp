#include "GuiViews.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../TimeZones.h"
#include "InputMap.h"
#include "SignalLevel.h"

// D4 and D5 replace the remaining stubs (D1, #1495).

namespace offband {
namespace gui {

static void blank(char* out, size_t n) {
  if (n) out[0] = '\0';
}

// snprintf, then backs a cut off to a whole UTF-8 character.
void vtextf(char* out, size_t n, const char* f, va_list ap) {
  if (!n) return;
  const int w = vsnprintf(out, n, f, ap);
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

void textf(char* out, size_t n, const char* f, ...) {
  va_list ap;
  va_start(ap, f);
  vtextf(out, n, f, ap);
  va_end(ap);
}

static void put(char* out, size_t n, const char* f, ...) {
  va_list ap;
  va_start(ap, f);
  vtextf(out, n, f, ap);
  va_end(ap);
}

// --------------------------------------------------------- display text

static bool steersOnly(uint32_t cp) {
  return cp == 0xFE0E || cp == 0xFE0F || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF) ||
         (cp >= 0xE0020 && cp <= 0xE007F);
}

void displayText(const char* in, size_t len, char* out, size_t n) {
  if (!n) return;
  size_t o = 0, i = 0;
  while (i < len && in[i]) {
    const uint8_t b = uint8_t(in[i]);
    const size_t k = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
    if (i + k > len) break;
    uint32_t cp = k == 1 ? b : b & (0xFF >> (k + 1));
    for (size_t j = 1; j < k; j++) cp = (cp << 6) | (uint8_t(in[i + j]) & 0x3F);
    if (!steersOnly(cp)) {
      if (o + k >= n) break;   // only whole characters
      memcpy(out + o, in + i, k);
      o += k;
    }
    i += k;
  }
  out[o] = '\0';
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

void formatClock(int zone, uint32_t utc, char* out, size_t n, bool h12) {
  if (!n) return;
  if (!tz::clockSet(utc)) return put(out, n, "--:--");
  if (!h12) return tz::clock(zone, utc, out, n);
  const int64_t local = tz::localSeconds(zone, utc);
  const int64_t secs = ((local % 86400) + 86400) % 86400;
  const unsigned h = unsigned(secs / 3600), m = unsigned(secs / 60 % 60);
  put(out, n, "%u:%02u %s", h % 12 == 0 ? 12u : h % 12, m, h >= 12 ? "PM" : "AM");
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

// ------------------------------------------------- Chats and Conversation

size_t chatOrder(const MsgStore& s, uint16_t* out, size_t max) {
  if (!s.ready() || max == 0) return 0;
  // Unread threads first, then by latest arrival; a thread with no messages
  // (last_seq 0) sinks to the end, in table order.
  auto before = [&](const Thread* a, const Thread* b) {
    const bool ua = a->unread > 0, ub = b->unread > 0;
    if (ua != ub) return ua;
    return a->last_seq > b->last_seq;
  };
  size_t k = 0;
  for (int t = 0; t < s.maxThreads(); t++) {
    const Thread* th = s.threadAt(t);
    if (!th) continue;
    size_t i = k < max ? k++ : max;   // when full, a thread only displaces the last
    if (i == max) {
      if (!before(th, s.threadAt(out[max - 1]))) continue;
      i = max - 1;
    }
    while (i > 0 && before(th, s.threadAt(out[i - 1]))) {
      out[i] = out[i - 1];
      i--;
    }
    out[i] = uint16_t(t);
  }
  return k;
}

void chatPreview(const MsgStore& s, int thread, char* out, size_t n) {
  const Thread* th = s.ready() ? s.threadAt(thread) : nullptr;
  if (!th) return blank(out, n);
  uint32_t seq = 0;
  if (s.threadMessages(thread, &seq, 1) == 0) return put(out, n, "No messages yet");
  const MsgRecord* m = s.msg(seq);
  if (!m) return put(out, n, "No messages yet");
  const int len = int(m->text_len);
  if (m->own) return put(out, n, "You: %.*s", len, m->text);
  lockText(*m, ThreadKind(th->kind), out, n);
}

void deliveryMeta(const MsgRecord& m, bool channel, uint8_t attempt, uint8_t max_attempts, char* out,
                  size_t n) {
  if (channel) {
    if (m.echoes) put(out, n, "heard ×%u", unsigned(m.echoes));
    else blank(out, n);
    return;
  }
  switch (Delivery(m.delivery)) {
    case Delivery::Sending:
      if (attempt > 1 && max_attempts > 1) put(out, n, "sending… try %u/%u", unsigned(attempt), unsigned(max_attempts));
      else put(out, n, "sending…");
      break;
    case Delivery::Delivered: put(out, n, "✓ delivered"); break;
    case Delivery::Failed: put(out, n, "✕ not delivered · hold to resend"); break;
    default: blank(out, n); break;
  }
}

void incomingMeta(uint8_t hops, char* out, size_t n) {
  if (hops == 0 || hops == 0xFF) put(out, n, "direct");
  else put(out, n, "%u hop%s", unsigned(hops), hops == 1 ? "" : "s");
}

void metaLine(const char* from, const char* time, const char* meta, char* out, size_t n) {
  if (!n) return;
  out[0] = '\0';
  const char* parts[3] = {from, time, meta};
  size_t len = 0;
  for (const char* p : parts) {
    if (!p || !*p) continue;
    if (len) put(out + len, n - len, " · %s", p);
    else put(out, n, "%s", p);
    len = strlen(out);
    if (len + 1 >= n) break;
  }
}

bool channelAlerts(NotifyMode mode, bool mentions_self) {
  switch (mode) {
    case NotifyMode::Off: return false;
    case NotifyMode::Mentions: return mentions_self;
    case NotifyMode::All:
    default: return true;
  }
}

// ---------------------------------------------------------------- Compose

void ComposeBuffer::start(size_t budget) {
  _budget = budget < kMsgTextMax ? budget : kMsgTextMax;
  _len = 0;
  _buf[0] = '\0';
}

// The byte length of the UTF-8 sequence a lead byte starts; 0 if it can't
// start one.
static size_t seqLen(uint8_t b) {
  if (b < 0x80) return 1;
  if (b >= 0xC2 && b <= 0xDF) return 2;
  if (b >= 0xE0 && b <= 0xEF) return 3;
  if (b >= 0xF0 && b <= 0xF4) return 4;
  return 0;
}

bool ComposeBuffer::insert(const char* utf8, size_t len) {
  if (!utf8 || len == 0 || seqLen(uint8_t(utf8[0])) != len || uint8_t(utf8[0]) == 0) return false;
  for (size_t i = 1; i < len; i++) {
    if ((uint8_t(utf8[i]) & 0xC0) != 0x80) return false;
  }
  if (_len + len > _budget) return false;
  memcpy(_buf + _len, utf8, len);
  _len += len;
  _buf[_len] = '\0';
  return true;
}

bool ComposeBuffer::backspace() {
  if (_len == 0) return false;
  do {
    _len--;
  } while (_len > 0 && (uint8_t(_buf[_len]) & 0xC0) == 0x80);
  _buf[_len] = '\0';
  return true;
}

void ComposeBuffer::counter(char* out, size_t n) const {
  put(out, n, "%u/%u", unsigned(_len), unsigned(_budget));
}

Tone ComposeBuffer::counterTone() const {
  return _len + 10 >= _budget ? Tone::Warn : Tone::Dim;
}

void ShiftState::tap(uint32_t now_ms) {
  const bool quick = _tapped && now_ms - _last_tap <= kMultiClickMs;
  if (_s == Shift::Lock) _s = Shift::Off;
  else if (_s == Shift::Once) _s = quick ? Shift::Lock : Shift::Off;
  else _s = Shift::Once;
  _last_tap = now_ms;
  _tapped = true;
}

void ShiftState::typedLetter() {
  if (_s == Shift::Once) _s = Shift::Off;
}

void ShiftState::autoCap(const char* text, size_t len) {
  if (_s == Shift::Lock) return;
  const bool start = len == 0 || (len >= 2 && text[len - 1] == ' ' &&
                                  (text[len - 2] == '.' || text[len - 2] == '!' || text[len - 2] == '?'));
  _s = start ? Shift::Once : Shift::Off;
}

void PageState::typed(char c) {
  if (_p == KeyPage::Letters) return;
  if (c != ' ') _typed_symbol = true;
  else if (_typed_symbol) select(KeyPage::Letters);
}

// ------------------------------------------------------- key geometry

namespace {

// One row: single-character keys from a UTF-8 string, between optional wide
// keys at either end.
struct RowSpec {
  const char* left;    // a wide key before the characters, or null
  const char* chars;   // UTF-8, one key per character
  const char* right;   // a wide key after, or null
};

// Labels for single characters live here so KeyRect can point at them.
constexpr size_t kMaxKeys = 40;
char g_labels[kMaxKeys][5];

constexpr int16_t kKeyW = 21;
constexpr int16_t kGap = 2;
constexpr int16_t kRowW = 10 * kKeyW + 9 * kGap;   // 228: the widest row

}  // namespace

bool typesLabel(const char* label) {
  if (!label || !label[0]) return false;
  const size_t k = seqLen(uint8_t(label[0]));
  return k && label[k] == '\0' && strcmp(label, "⇧") != 0 && strcmp(label, "⌫") != 0;
}

size_t pageLayout(KeyPage page, int16_t width, int16_t y0, KeyRect* out, size_t max) {
  return pageLayout(page, width, y0, out, max, kKeyW, kKeyH);
}

size_t pageLayout(KeyPage page, int16_t width, int16_t y0, KeyRect* out, size_t max, int16_t key_w,
                  int16_t key_h) {
  // The key size is the layout class's (Epic H): the names below stand for
  // it through the rest of the layout.
  const int16_t kKeyW = key_w;
  const int16_t kKeyH = key_h;
  const int16_t kRowW = int16_t(10 * kKeyW + 9 * kGap);
  static const RowSpec kLetters[3] = {{nullptr, "qwertyuiop", nullptr},
                                      {nullptr, "asdfghjkl", nullptr},
                                      {"⇧", "zxcvbnm", "⌫"}};
  static const RowSpec kSymbols[3] = {{nullptr, "1234567890", nullptr},
                                      {nullptr, "@#$_&-+()/", nullptr},
                                      {"=\\<", "*\"':;!?", "⌫"}};
  static const RowSpec kSymbols2[3] = {{nullptr, "[]{}<>~|\\^", nullptr},
                                       {nullptr, "%=°£¥§¿¡«»", nullptr},
                                       {"?123", "`´¨¬±×÷", "⌫"}};
  const RowSpec* rows = page == KeyPage::Letters ? kLetters : page == KeyPage::Symbols ? kSymbols : kSymbols2;
  const int16_t x0 = (width - kRowW) / 2;
  size_t n = 0;
  auto add = [&](int16_t x, int16_t y, int16_t w, const char* label) {
    if (n < max) out[n] = {x, y, w, kKeyH, label};
    n++;
  };

  // The hex keypad (mockup 6): 1-0, then a-f with Backspace beside them,
  // and the confirm key across the bottom. The third row is left empty so
  // the keypad keeps the keyboard's height and nothing above it moves.
  if (page == KeyPage::Hex) {
    static const char* const kHex = "1234567890abcdef";
    for (int i = 0; i < 16; i++) {
      const int r = i < 10 ? 0 : 1;
      const int col = i < 10 ? i : i - 10;
      const size_t slot = n < kMaxKeys ? n : kMaxKeys - 1;
      g_labels[slot][0] = kHex[i];
      g_labels[slot][1] = '\0';
      add(int16_t(x0 + col * (kKeyW + kGap)), int16_t(y0 + r * (kKeyH + kKeyRowGap)), kKeyW, g_labels[slot]);
    }
    const int16_t bk_x = int16_t(x0 + 6 * (kKeyW + kGap));
    add(bk_x, int16_t(y0 + kKeyH + kKeyRowGap), int16_t(x0 + kRowW - bk_x), "⌫");
    add(x0, int16_t(y0 + 3 * (kKeyH + kKeyRowGap)), kRowW, "Send");
    return n;
  }

  for (int r = 0; r < 3; r++) {
    const int16_t y = y0 + r * (kKeyH + kKeyRowGap);
    size_t chars = 0;
    for (const char* p = rows[r].chars; *p; p += seqLen(uint8_t(*p))) chars++;
    const bool wide = rows[r].left != nullptr;
    // Wide keys share what the characters leave of the row.
    const int16_t used = int16_t(chars * kKeyW + (chars - 1) * kGap);
    const int16_t wide_w = wide ? int16_t((kRowW - used - 2 * kGap) / 2) : 0;
    int16_t x = wide ? x0 : int16_t(x0 + (kRowW - used) / 2);
    if (wide) {
      add(x, y, wide_w, rows[r].left);
      x += wide_w + kGap;
    }
    for (const char* p = rows[r].chars; *p;) {
      const size_t k = seqLen(uint8_t(*p));
      const size_t slot = n < kMaxKeys ? n : kMaxKeys - 1;
      memcpy(g_labels[slot], p, k);
      g_labels[slot][k] = '\0';
      add(x, y, kKeyW, g_labels[slot]);
      x += kKeyW + kGap;
      p += k;
    }
    if (wide) add(x, y, wide_w, rows[r].right);
  }

  // The bottom row: page switch, space, Send; the symbol pages put "," and
  // "." beside the space bar.
  const int16_t y = y0 + 3 * (kKeyH + kKeyRowGap);
  const int16_t sw_w = 46, send_w = 50;
  int16_t x = x0;
  add(x, y, sw_w, page == KeyPage::Letters ? "?123" : "ABC");
  x += sw_w + kGap;
  int16_t space_w = kRowW - sw_w - send_w - 2 * kGap;
  if (page != KeyPage::Letters) {
    space_w -= 2 * (kKeyW + kGap);
    add(x, y, kKeyW, ",");
    x += kKeyW + kGap;
  }
  add(x, y, space_w, "space");
  x += space_w + kGap;
  if (page != KeyPage::Letters) {
    add(x, y, kKeyW, ".");
    x += kKeyW + kGap;
  }
  add(x, y, send_w, "Send");
  return n < max ? n : max;
}

int nearestKey(const KeyRect* keys, size_t count, int16_t x, int16_t y) {
  int best = -1;
  int32_t best_d = 0;
  for (size_t i = 0; i < count; i++) {
    const int32_t dx = int32_t(x) * 2 - (keys[i].x * 2 + keys[i].w);
    const int32_t dy = int32_t(y) * 2 - (keys[i].y * 2 + keys[i].h);
    // Distance to the key's box, not its centre, so a wide key (space)
    // still wins a touch anywhere on it.
    const int32_t ox = dx < 0 ? -dx : dx, oy = dy < 0 ? -dy : dy;
    const int32_t ex = ox > keys[i].w ? ox - keys[i].w : 0;
    const int32_t ey = oy > keys[i].h ? oy - keys[i].h : 0;
    const int32_t d = ex * ex + ey * ey;
    if (best < 0 || d < best_d) {
      best = int(i);
      best_d = d;
    }
  }
  return best;
}

// ------------------------------------------------------------- DM retry

void DmRetry::start(uint32_t seq, uint32_t now_ms, uint32_t timeout_ms) {
  _seq = seq;
  _attempt = 1;
  _deadline = now_ms + timeout_ms;
}

DmRetry::Step DmRetry::poll(uint32_t now_ms, bool delivered) {
  if (!_seq) return Step::Done;
  if (delivered) {
    _seq = 0;
    return Step::Done;
  }
  if (int32_t(now_ms - _deadline) < 0) return Step::Wait;
  if (_attempt < kDmAttempts) return Step::Resend;
  _seq = 0;
  return Step::Fail;
}

void DmRetry::resent(uint32_t now_ms, uint32_t timeout_ms) {
  _attempt++;
  _deadline = now_ms + timeout_ms;
}

}  // namespace gui
}  // namespace offband
