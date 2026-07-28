#pragma once
#include <stddef.h>
#include <stdint.h>

// CaptureRing — line-oriented byte ring buffer behind the serial-capture sink
// (#393). Stores a bounded window of the most-recent console bytes for
// on-demand download.
//
// Design notes:
//  - Backing storage is caller-owned (static allocation at the call site) so
//    the ring adds nothing to the heap — see design-of-record 4.2.
//  - Eviction is LINE-ORIENTED: on overflow, whole oldest lines are dropped
//    (advance past the next '\n') so a snapshot never begins mid-line.
//  - Pure C++ (no Arduino) so the logic is unit-testable on the native env.
//
// Not thread-safe by itself; the sink guards it with an ISR-safe critical
// section (see MeshLog).
class CaptureRing {
public:
  // storage/capacity: caller-owned buffer and its size in bytes.
  CaptureRing(uint8_t* storage, size_t capacity);

  // Append raw bytes (typically one formatted line ending in '\n'). Evicts
  // whole oldest lines as needed to make room. A single chunk larger than the
  // whole ring keeps only its most-recent tail.
  void append(const uint8_t* data, size_t len);

  // Bytes currently held.
  size_t bytesUsed() const;

  // Total capacity in bytes.
  size_t capacity() const;

  // Copy up to out_cap bytes into out, oldest-first, starting `offset` bytes
  // from the oldest byte. Returns bytes copied (0 if offset >= bytesUsed()).
  // The offset lets the download path stream the buffer in chunks.
  size_t snapshot(uint8_t* out, size_t out_cap, size_t offset = 0) const;

  // Drop all contents.
  void clear();

private:
  uint8_t* _buf;
  size_t   _cap;
  size_t   _tail;   // index of oldest byte
  size_t   _count;  // bytes currently held
};
