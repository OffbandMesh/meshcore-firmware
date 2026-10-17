#include "NodeViews.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "GuiViews.h"

// E3 and E4 replace the remaining stubs (E1, #1530).

namespace offband {
namespace gui {

static void blank(char* out, size_t n) {
  if (n) out[0] = '\0';
}

// textf, returning the length written, for building a line in pieces.
static int textfLen(char* out, size_t n, const char* f, ...) {
  if (!n) return 0;
  va_list ap;
  va_start(ap, f);
  vtextf(out, n, f, ap);
  va_end(ap);
  return int(strlen(out));
}

// ---------------------------------------------------------------- the list

NodeKind nodeKind(uint8_t adv_type) {
  switch (adv_type) {   // ADV_TYPE_* (AdvertDataHelpers.h)
    case 1: return NodeKind::Chat;
    case 2: return NodeKind::Repeater;
    case 3: return NodeKind::Room;
    case 4: return NodeKind::Sensor;
    default: return NodeKind::Other;
  }
}

bool passesFilter(const NodeRow& r, TypeFilter f, bool unread_only) {
  if (unread_only && r.unread == 0) return false;
  switch (f) {
    case TypeFilter::Chat: return r.kind == NodeKind::Chat;
    case TypeFilter::Repeater: return r.kind == NodeKind::Repeater;
    case TypeFilter::Room: return r.kind == NodeKind::Room;
    case TypeFilter::Sensor: return r.kind == NodeKind::Sensor;
    case TypeFilter::All:
    default: return true;
  }
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

static int hexVal(char c) {
  c = lower(c);
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool searchMatches(const NodeRow& r, const char* query) {
  if (!query || !query[0]) return true;
  // Anywhere in the name, ASCII letters compared without case (owner,
  // 2026-10-17: names are searched by a part remembered, not the start).
  for (size_t at = 0; r.name[at]; at++) {
    size_t i = 0;
    while (query[i] && r.name[at + i] && lower(query[i]) == lower(r.name[at + i])) i++;
    if (!query[i]) return true;
  }
  size_t i = 0;
  // Key hex prefix, when every character is a hex digit.
  if (!r.key) return false;
  for (i = 0; query[i]; i++) {
    const int v = hexVal(query[i]);
    if (v < 0 || i >= 64) return false;
    const uint8_t byte = r.key[i / 2];
    if (v != ((i % 2) ? (byte & 0x0F) : (byte >> 4))) return false;
  }
  return true;
}

static int nameCmp(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    const char x = lower(*a), y = lower(*b);
    if (x != y) return x < y ? -1 : 1;
  }
  return *a ? 1 : *b ? -1 : 0;
}

// Most recently heard first; the never-heard after them, A-Z.
static bool before(const NodeRow& a, const NodeRow& b) {
  if (a.heard != b.heard) return a.heard;
  if (a.heard) return a.heard_age_s < b.heard_age_s;
  return nameCmp(a.name, b.name) < 0;
}

size_t nodeOrder(const NodeRow* rows, size_t n, TypeFilter f, bool unread_only, const char* query, uint16_t* out,
                 size_t max) {
  if (!rows || max == 0) return 0;
  size_t k = 0;
  for (size_t r = 0; r < n; r++) {
    if (!passesFilter(rows[r], f, unread_only) || !searchMatches(rows[r], query)) continue;
    // Insert in order, keeping only the first `max`.
    size_t i = k < max ? k++ : max;
    if (i == max) {
      if (!before(rows[r], rows[out[max - 1]])) continue;
      i = max - 1;
    }
    while (i > 0 && before(rows[r], rows[out[i - 1]])) {
      out[i] = out[i - 1];
      i--;
    }
    out[i] = uint16_t(r);
  }
  return k;
}

void nodeCount(size_t shown, size_t total, bool filtered, char* out, size_t n) {
  if (filtered) textf(out, n, "%u of %u", unsigned(shown), unsigned(total));
  else textf(out, n, "%u", unsigned(total));
}

void hopsText(uint8_t out_path_len, char* out, size_t n) {
  if (out_path_len == 0xFF) return textf(out, n, "flood");
  const unsigned hops = out_path_len & 63;   // the top bits carry the hash size
  if (hops == 0) textf(out, n, "direct");
  else textf(out, n, "%u hop%s", hops, hops == 1 ? "" : "s");
}

void agoText(bool heard, uint32_t age_s, char* out, size_t n) {
  if (!heard) textf(out, n, "—");
  else if (age_s < 60) textf(out, n, "now");
  else if (age_s < 3600) textf(out, n, "%lum", (unsigned long)(age_s / 60));
  else if (age_s < 86400) textf(out, n, "%luh", (unsigned long)(age_s / 3600));
  else textf(out, n, "%lud", (unsigned long)(age_s / 86400));
}

void rowSub(const NodeRow& r, char* out, size_t n) {
  char hops[16], ago[8];
  hopsText(r.out_path_len, hops, sizeof hops);
  agoText(r.heard, r.heard_age_s, ago, sizeof ago);
  textf(out, n, "%s · %s", hops, ago);
}

// One decimal, rounded half away from zero, with a sign (as the status bar).
static void signedTenths(float v, const char* unit, char* out, size_t n) {
  const long t = long(v * 10.0f + (v < 0 ? -0.5f : 0.5f));
  const long a = t < 0 ? -t : t;
  textf(out, n, "%s%ld.%ld%s", t < 0 ? "-" : "+", a / 10, a % 10, unit);
}

void rowSnr(float snr_db, char* out, size_t n) { signedTenths(snr_db, "", out, n); }

void badgeText(uint16_t unread, char* out, size_t n) {
  if (unread > 99) textf(out, n, "99+");
  else textf(out, n, "%u", unsigned(unread));
}

static const char* filterWords(TypeFilter f) {
  switch (f) {
    case TypeFilter::Chat: return "chat";
    case TypeFilter::Repeater: return "repeaters";
    case TypeFilter::Room: return "rooms";
    case TypeFilter::Sensor: return "sensors";
    case TypeFilter::All:
    default: return "all types";
  }
}

void matchLine(size_t matches, TypeFilter f, char* out, size_t n) {
  textf(out, n, "%u match%s · %s", unsigned(matches), matches == 1 ? "" : "es", filterWords(f));
}

const char* emptyTitle(TypeFilter f) {
  switch (f) {
    case TypeFilter::Chat: return "No companions heard yet";
    case TypeFilter::Repeater: return "No repeaters heard yet";
    case TypeFilter::Room: return "No rooms heard yet";
    case TypeFilter::Sensor: return "No sensors heard yet";
    case TypeFilter::All:
    default: return "No nodes heard yet";
  }
}

// ----------------------------------------------------------------- detail

static const char* kindName(NodeKind k) {
  switch (k) {
    case NodeKind::Chat: return "Companion";
    case NodeKind::Repeater: return "Repeater";
    case NodeKind::Room: return "Room server";
    case NodeKind::Sensor: return "Sensor";
    default: return "Node";
  }
}

void detailKind(NodeKind k, uint8_t out_path_len, bool heard, float snr_db, char* out, size_t n) {
  char hops[16];
  hopsText(out_path_len, hops, sizeof hops);
  if (!heard) return textf(out, n, "%s · %s", kindName(k), hops);
  char snr[16];
  signedTenths(snr_db, " dB", snr, sizeof snr);
  textf(out, n, "%s · %s · %s", kindName(k), hops, snr);
}

static const char* const kStatusKeys[6] = {"Battery", "Uptime", "Airtime", "Relayed", "Noise", "RTT"};

size_t emptyStatusCells(Cell* out, size_t max) {
  size_t k = 0;
  for (; k < 6 && k < max; k++) {
    textf(out[k].key, sizeof out[k].key, "%s", kStatusKeys[k]);
    textf(out[k].value, sizeof out[k].value, "—");
  }
  return k;
}

static uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

size_t statusCells(const uint8_t* body, size_t len, uint32_t rtt_ms, Cell* out, size_t max) {
  // The body: a 6-byte key prefix, then RepeaterStats (little-endian, no
  // padding); the cells read up to n_sent_direct, 32 bytes in.
  constexpr size_t kPrefix = 6, kNeeded = kPrefix + 32;
  if (!body || len < kNeeded) return 0;
  const uint8_t* s = body + kPrefix;
  const uint16_t mv = le16(s + 0);
  const int16_t noise = int16_t(le16(s + 4));
  const uint32_t air = le32(s + 16), up = le32(s + 20);
  const uint32_t relayed = le32(s + 24) + le32(s + 28);

  char v[6][16];
  const unsigned cv = (unsigned(mv) + 5) / 10;
  textf(v[0], sizeof v[0], "%u.%02u V", cv / 100, cv % 100);
  formatUptime(up, v[1], sizeof v[1]);
  const uint32_t permille = up ? uint32_t((uint64_t(air) * 1000 + up / 2) / up) : 0;
  textf(v[2], sizeof v[2], "%lu.%lu%%", (unsigned long)(permille / 10), (unsigned long)(permille % 10));
  thousands(relayed, v[3], sizeof v[3]);
  textf(v[4], sizeof v[4], "%d", int(noise));
  if (rtt_ms) {
    const unsigned t = (rtt_ms + 50) / 100;
    textf(v[5], sizeof v[5], "%u.%u s", t / 10, t % 10);
  } else {
    textf(v[5], sizeof v[5], "—");
  }
  size_t k = 0;
  for (; k < 6 && k < max; k++) {
    textf(out[k].key, sizeof out[k].key, "%s", kStatusKeys[k]);
    textf(out[k].value, sizeof out[k].value, "%s", v[k]);
  }
  return k;
}

size_t detailCells(const NodeRow& r, int16_t rssi, const uint8_t* status, size_t len, uint32_t rtt_ms, Cell* out,
                   size_t max) {
  Cell stat[6];
  if (statusCells(status, len, rtt_ms, stat, 6) == 0) emptyStatusCells(stat, 6);
  if (r.kind == NodeKind::Repeater) {
    size_t k = 0;
    for (; k < 6 && k < max; k++) out[k] = stat[k];
    return k;
  }

  Cell all[6];
  size_t n = 0;
  auto put = [&](const char* key, const char* value) {
    textf(all[n].key, sizeof all[n].key, "%s", key);
    textf(all[n].value, sizeof all[n].value, "%s", value);
    n++;
  };
  char v[16];
  agoText(r.heard, r.heard_age_s, v, sizeof v);
  put("Heard", v);
  if (r.snr_known) rowSnr(r.snr_db, v, sizeof v);
  else textf(v, sizeof v, "—");
  put("SNR", v);
  if (r.snr_known) textf(v, sizeof v, "%d", int(rssi));
  else textf(v, sizeof v, "—");
  put("RSSI", v);

  if (r.kind == NodeKind::Room) {
    // ServerStats: RepeaterStats, then n_posted at 48 (simple_room_server).
    constexpr size_t kPosted = 6 + 48;
    if (status && len >= kPosted + 2) textf(v, sizeof v, "%u", unsigned(status[kPosted] | (status[kPosted + 1] << 8)));
    else textf(v, sizeof v, "—");
    put("Posts", v);
    all[n++] = stat[0];   // Battery
    all[n++] = stat[1];   // Uptime
  } else if (r.kind == NodeKind::Sensor) {
    all[n++] = stat[0];   // Battery
    all[n++] = stat[1];   // Uptime
    all[n++] = stat[2];   // Airtime
  } else {
    textf(v, sizeof v, "%u", unsigned(r.unread));
    put("Unread", v);
    if (r.out_path_len == 0xFF) textf(v, sizeof v, "flood");
    else textf(v, sizeof v, "%u", unsigned(r.out_path_len & 63));
    put("Hops", v);
  }
  size_t k = 0;
  for (; k < n && k < max; k++) out[k] = all[k];
  return k;
}

void thousands(uint32_t v, char* out, size_t n) {
  char digits[12];
  const int len = snprintf(digits, sizeof digits, "%lu", (unsigned long)v);
  char buf[16];
  int o = 0;
  for (int i = 0; i < len; i++) {
    if (i && (len - i) % 3 == 0) buf[o++] = ',';
    buf[o++] = digits[i];
  }
  buf[o] = '\0';
  textf(out, n, "%s", buf);
}

void traceSnrLine(const uint8_t* body, size_t len, char* out, size_t n) {
  blank(out, n);
  if (!body || len < 10) return;
  const uint8_t path_len = body[0];
  const uint8_t sz = body[1] & 0x03;   // hash bytes per hop: 1 << sz
  const size_t hops = size_t(path_len) >> sz;
  const size_t snrs = 10 + path_len;   // after path_len, flags, tag, auth, hashes
  if (len < snrs + hops + 1) return;
  char line[96];
  int o = textfLen(line, sizeof line, "snr:");
  for (size_t i = 0; i <= hops; i++) {
    char one[16];
    signedTenths(int8_t(body[snrs + i]) / 4.0f, "", one, sizeof one);
    o += textfLen(line + o, sizeof line - size_t(o), i ? " › %s" : " %s", one);
  }
  textf(out, n, "%s", line);
}

// ------------------------------------------------------------ letter wheel

namespace {
// A-Z, 0-9, space, then the two controls.
constexpr int kWheelChars = 26 + 10 + 1;
constexpr int kWheelSize = kWheelChars + 2;
const char* const kWheelLabels[kWheelSize] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "space", "⌫", "Done"};
int wrap(int i) { return ((i % kWheelSize) + kWheelSize) % kWheelSize; }
}  // namespace

void LetterWheel::next() { _i = wrap(_i + 1); }
void LetterWheel::prev() { _i = wrap(_i - 1); }

LetterWheel::Pick LetterWheel::pick() const {
  if (_i < kWheelChars) return Pick::Char;
  return _i == kWheelChars ? Pick::Backspace : Pick::Done;
}

char LetterWheel::ch() const {
  if (_i < 26) return char('A' + _i);
  if (_i < 36) return char('0' + _i - 26);
  return _i == 36 ? ' ' : 0;
}

const char* LetterWheel::label() const { return kWheelLabels[_i]; }
const char* LetterWheel::labelAt(int offset) const { return kWheelLabels[wrap(_i + offset)]; }

// -------------------------------------------------------- password book

namespace {
constexpr char kPwMagic[4] = {'O', 'B', 'P', 'W'};
constexpr uint16_t kPwVersion = 1;
constexpr size_t kPwHeader = 16;   // magic, version, slot count, reserved
}  // namespace

void PasswordBook::set(const uint8_t* key, const char* password) {
  if (!key || !password) return;
  Entry* slot = nullptr;
  for (Entry& e : _e) {
    if (e.used && memcmp(e.key, key, sizeof e.key) == 0) slot = &e;
  }
  if (!slot) {
    for (Entry& e : _e) {
      if (!e.used) {
        slot = &e;
        break;
      }
    }
  }
  if (!slot) {   // full: the least recently set goes
    slot = &_e[0];
    for (Entry& e : _e) {
      if (e.stamp < slot->stamp) slot = &e;
    }
  }
  memset(slot, 0, sizeof *slot);
  memcpy(slot->key, key, sizeof slot->key);
  slot->used = 1;
  slot->stamp = ++_clock;
  textf(slot->pw, sizeof slot->pw, "%s", password);
}

const char* PasswordBook::get(const uint8_t* key) const {
  if (!key) return nullptr;
  for (const Entry& e : _e) {
    if (e.used && memcmp(e.key, key, sizeof e.key) == 0) return e.pw;
  }
  return nullptr;
}

void PasswordBook::forget(const uint8_t* key) {
  for (Entry& e : _e) {
    if (e.used && key && memcmp(e.key, key, sizeof e.key) == 0) memset(&e, 0, sizeof e);
  }
}

size_t PasswordBook::size() const {
  size_t n = 0;
  for (const Entry& e : _e) n += e.used;
  return n;
}

bool PasswordBook::save(ByteSink& out) const {
  uint8_t h[kPwHeader] = {};
  memcpy(h, kPwMagic, 4);
  h[4] = uint8_t(kPwVersion & 0xFF);
  h[5] = uint8_t(kPwVersion >> 8);
  h[6] = uint8_t(kPasswordSlots & 0xFF);
  h[7] = uint8_t(kPasswordSlots >> 8);
  if (!out.write(h, sizeof h)) return false;
  return out.write(_e, sizeof _e);
}

LoadResult PasswordBook::load(ByteSource& in) {
  uint8_t h[kPwHeader];
  const size_t got = in.read(h, sizeof h);
  if (got == 0) return LoadResult::Empty;
  if (got < sizeof h || memcmp(h, kPwMagic, 4) != 0) return LoadResult::BadHeader;
  if (uint16_t(h[4] | (h[5] << 8)) != kPwVersion) return LoadResult::UnknownVersion;
  if (uint16_t(h[6] | (h[7] << 8)) != kPasswordSlots) return LoadResult::BadHeader;
  Entry e[kPasswordSlots];
  if (in.read(e, sizeof e) != sizeof e) return LoadResult::BadHeader;
  memcpy(_e, e, sizeof e);
  _clock = 0;
  for (Entry& x : _e) {
    x.pw[kPasswordLen - 1] = '\0';   // never trust a terminator from the file
    if (x.stamp > _clock) _clock = x.stamp;
  }
  return LoadResult::Ok;
}

}  // namespace gui
}  // namespace offband
