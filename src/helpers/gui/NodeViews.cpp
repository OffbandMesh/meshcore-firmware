#include "NodeViews.h"

#include <string.h>

#include "GuiViews.h"

// E3 and E4 replace the remaining stubs (E1, #1530).

namespace offband {
namespace gui {

static void blank(char* out, size_t n) {
  if (n) out[0] = '\0';
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
  // Name prefix, ASCII letters compared without case.
  size_t i = 0;
  while (query[i] && r.name[i] && lower(query[i]) == lower(r.name[i])) i++;
  if (!query[i]) return true;
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

void detailKind(NodeKind, uint8_t, bool, float, char* out, size_t n) { blank(out, n); }
size_t statusCells(const uint8_t*, size_t, uint32_t, Cell*, size_t) { return 0; }
size_t emptyStatusCells(Cell*, size_t) { return 0; }
void thousands(uint32_t, char* out, size_t n) { blank(out, n); }
void traceSnrLine(const uint8_t*, size_t, char* out, size_t n) { blank(out, n); }

void LetterWheel::next() {}
void LetterWheel::prev() {}
LetterWheel::Pick LetterWheel::pick() const { return Pick::Done; }
char LetterWheel::ch() const { return 0; }
const char* LetterWheel::label() const { return ""; }
const char* LetterWheel::labelAt(int) const { return ""; }

void PasswordBook::set(const uint8_t*, const char*) {}
const char* PasswordBook::get(const uint8_t*) const { return nullptr; }
void PasswordBook::forget(const uint8_t*) {}
size_t PasswordBook::size() const { return 0; }
bool PasswordBook::save(ByteSink&) const { return false; }
LoadResult PasswordBook::load(ByteSource&) { return LoadResult::BadHeader; }

}  // namespace gui
}  // namespace offband
