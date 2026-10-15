#include "GuiMetrics.h"

#include <math.h>
#include <new>
#include <string.h>

namespace offband {
namespace gui {

// ------------------------------------------------------------------ SnrMedian

void SnrMedian::add(float snr_db) {
  if (!isfinite(snr_db)) return;
  _v[_next] = snr_db;
  _next = (_next + 1) % kSnrWindow;
  if (_n < kSnrWindow) _n++;
}

float SnrMedian::median() const {
  if (_n == 0) return 0.0f;
  float s[kSnrWindow];
  memcpy(s, _v, sizeof(float) * _n);  // the first _n slots are the live ones until the ring fills
  for (size_t i = 1; i < _n; i++) {   // insertion sort: at most 8 values
    float v = s[i];
    size_t j = i;
    while (j > 0 && s[j - 1] > v) {
      s[j] = s[j - 1];
      j--;
    }
    s[j] = v;
  }
  return (_n % 2) ? s[_n / 2] : (s[_n / 2 - 1] + s[_n / 2]) / 2.0f;
}

// -------------------------------------------------------------- AirtimeWindow

static constexpr uint32_t kMinuteMs = 60000;
static constexpr uint32_t kHourMs = 60 * kMinuteMs;
static constexpr size_t kBuckets = 60;

// _bucket_ms[0] is the current minute, which began at _minute (a millis()
// value). Time is advanced by unsigned difference, so it survives the
// millis() wrap.
void AirtimeWindow::advance(uint32_t now_ms) {
  if (!_started) {
    _started = true;
    _minute = now_ms;
    return;
  }
  uint32_t steps = (now_ms - _minute) / kMinuteMs;
  if (steps == 0) return;
  if (steps >= kBuckets) {
    memset(_bucket_ms, 0, sizeof(_bucket_ms));
  } else {
    memmove(&_bucket_ms[steps], &_bucket_ms[0], sizeof(uint32_t) * (kBuckets - steps));
    memset(&_bucket_ms[0], 0, sizeof(uint32_t) * steps);
  }
  _minute += steps * kMinuteMs;
}

void AirtimeWindow::addTx(uint32_t now_ms, uint32_t airtime_ms) {
  advance(now_ms);
  _bucket_ms[0] += airtime_ms;
}

uint32_t AirtimeWindow::lastHourMs(uint32_t now_ms) {
  advance(now_ms);
  uint32_t sum = 0;
  for (size_t i = 0; i < kBuckets; i++) sum += _bucket_ms[i];
  return sum;
}

uint16_t AirtimeWindow::dutyPermille(uint32_t now_ms) {
  uint64_t permille = uint64_t(lastHourMs(now_ms)) * 1000 / kHourMs;
  return permille > 1000 ? 1000 : uint16_t(permille);
}

// ---------------------------------------------------------------- HeardCounts

void HeardCounts::add(uint8_t payload_type) {
  if (payload_type < 16) _n[payload_type]++;
}

uint32_t HeardCounts::count(uint8_t payload_type) const {
  return payload_type < 16 ? _n[payload_type] : 0;
}

uint32_t HeardCounts::total() const {
  uint32_t t = 0;
  for (uint32_t n : _n) t += n;
  return t;
}

// --------------------------------------------------------------- ContactStats

bool ContactStats::begin(size_t capacity) {
  delete[] _t;
  _t = new (std::nothrow) ContactStat[capacity];
  _cap = _t ? capacity : 0;
  _n = 0;
  return _t != nullptr;
}

ContactStats::~ContactStats() { delete[] _t; }

ContactStat* ContactStats::slot(const uint8_t key[6], bool create) {
  for (size_t i = 0; i < _n; i++) {
    if (memcmp(_t[i].key, key, 6) == 0) return &_t[i];
  }
  if (!create || _n >= _cap) return nullptr;
  ContactStat& s = _t[_n++];
  memcpy(s.key, key, 6);
  s.heard = false;
  s.snr_db = 0.0f;
  s.rssi = 0;
  s.unread = 0;
  return &s;
}

void ContactStats::onHeard(const uint8_t key[6], float snr_db, int16_t rssi) {
  ContactStat* s = slot(key, true);
  if (!s) return;
  s->heard = true;
  s->snr_db = snr_db;
  s->rssi = rssi;
}

void ContactStats::onDirectMessage(const uint8_t key[6]) {
  ContactStat* s = slot(key, true);
  if (s && s->unread < 0xFFFF) s->unread++;
}

void ContactStats::markRead(const uint8_t key[6]) {
  ContactStat* s = slot(key, false);
  if (s) s->unread = 0;
}

void ContactStats::forget(const uint8_t key[6]) {
  ContactStat* s = slot(key, false);
  if (!s) return;
  *s = _t[--_n];  // order doesn't matter; the last entry fills the gap
}

const ContactStat* ContactStats::find(const uint8_t key[6]) const {
  for (size_t i = 0; i < _n; i++) {
    if (memcmp(_t[i].key, key, 6) == 0) return &_t[i];
  }
  return nullptr;
}

uint32_t ContactStats::totalUnread() const {
  uint32_t t = 0;
  for (size_t i = 0; i < _n; i++) t += _t[i].unread;
  return t;
}

}  // namespace gui
}  // namespace offband
