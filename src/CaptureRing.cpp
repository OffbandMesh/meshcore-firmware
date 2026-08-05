#include "CaptureRing.h"
#include <string.h>

static inline size_t ring_wrap(size_t i, size_t cap) {
  return i >= cap ? i - cap : i;
}

CaptureRing::CaptureRing(uint8_t* storage, size_t capacity)
    : _buf(storage), _cap(capacity), _tail(0), _count(0) {}

void CaptureRing::append(const uint8_t* data, size_t len) {
  if (len == 0 || _cap == 0) return;

  // A single chunk larger than the whole ring cannot be kept intact; retain
  // only its most-recent tail (linearized to the start of the buffer).
  if (len >= _cap) {
    memcpy(_buf, data + (len - _cap), _cap);
    _tail = 0;
    _count = _cap;
    return;
  }

  // Evict whole oldest lines until len bytes fit. Prefer dropping at a '\n'
  // boundary so a snapshot never begins mid-line; if the held bytes contain no
  // newline yet, fall back to dropping just enough oldest bytes.
  while (_count + len > _cap) {
    size_t drop = 0;
    bool found = false;
    for (size_t i = 0; i < _count; ++i) {
      if (_buf[ring_wrap(_tail + i, _cap)] == '\n') {
        drop = i + 1;
        found = true;
        break;
      }
    }
    if (!found) {
      drop = (_count + len) - _cap;  // no line boundary; drop the minimum
    }
    if (drop > _count) drop = _count;
    _tail = ring_wrap(_tail + drop, _cap);
    _count -= drop;
  }

  size_t head = ring_wrap(_tail + _count, _cap);
  for (size_t i = 0; i < len; ++i) {
    _buf[head] = data[i];
    head = ring_wrap(head + 1, _cap);
  }
  _count += len;
}

size_t CaptureRing::bytesUsed() const { return _count; }

size_t CaptureRing::capacity() const { return _cap; }

size_t CaptureRing::snapshot(uint8_t* out, size_t out_cap, size_t offset) const {
  if (offset >= _count) return 0;
  size_t avail = _count - offset;
  size_t n = avail < out_cap ? avail : out_cap;
  for (size_t i = 0; i < n; ++i) {
    out[i] = _buf[ring_wrap(_tail + offset + i, _cap)];
  }
  return n;
}

size_t CaptureRing::consume(uint8_t* out, size_t out_cap) {
  if (_count == 0 || out_cap == 0) return 0;
  size_t limit = _count < out_cap ? _count : out_cap;
  // Take through the last '\n' that fits in out_cap so we hand back only whole
  // lines. If no '\n' fits (a line longer than out_cap, or an unterminated
  // trailing line), take `limit` bytes anyway so the drain always progresses.
  size_t last_nl = 0;
  bool found = false;
  for (size_t i = 0; i < limit; ++i) {
    if (_buf[ring_wrap(_tail + i, _cap)] == '\n') { last_nl = i + 1; found = true; }
  }
  size_t take = found ? last_nl : limit;
  for (size_t i = 0; i < take; ++i) {
    out[i] = _buf[ring_wrap(_tail + i, _cap)];
  }
  _tail = ring_wrap(_tail + take, _cap);
  _count -= take;
  return take;
}

void CaptureRing::clear() {
  _tail = 0;
  _count = 0;
}
