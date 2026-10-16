#pragma once

// #1478: assembling a custom tune pushed from the app, which cannot arrive in one frame.
//
// WHY CHUNKING IS NOT OPTIONAL. MAX_FRAME_SIZE is 176 bytes, and BLE is smaller still:
// SerialBLEInterface::maxFrameSize() returns the negotiated ATT MTU minus 3, capped at
// MAX_FRAME_SIZE - 3, and falls back to the BLE minimum of 20 before negotiation. A tune
// is up to 240 characters and the owner's reference string is 218, so neither fits a
// frame on any transport, let alone the worst case. The client sizes each chunk against
// maxFramePayload(); this side accepts whatever a frame carried.
//
// Pure and free of the filesystem and the frame layer, so it is unit-tested natively.
// The ui-new side owns an instance and hands a completed string to the slot store, which
// applies the SAME validator a typed string gets -- divergent validation between the two
// routes is how one of them becomes the trusted path and the other a trap.

#include <stddef.h>
#include <stdint.h>

namespace offband {

// One tune being assembled. N is the longest tune a slot holds.
template <size_t N>
class SlotAssembly {
public:
  enum class Result : uint8_t {
    Accepted,     // chunk taken, more expected
    Complete,     // the last chunk landed; text() is ready to validate and store
    BadOffset,    // not the next byte -- a lost, repeated or reordered frame
    BadSlot,      // a different slot mid-assembly, without restarting at offset 0
    TooLong,      // past N; refused rather than truncated into nonsense
    BadByte,      // outside printable ASCII, which a slot may not hold
  };

  // `offset` must be the number of bytes already accepted, and 0 starts or restarts.
  // Requiring the offset rather than trusting arrival order is what makes a dropped or
  // duplicated frame a reported error instead of a silently corrupted tune.
  Result feed(uint8_t slot, uint8_t offset, bool more, const uint8_t* bytes, size_t n) {
    if (offset == 0) {
      _slot = slot;
      _len = 0;
      _active = true;
    } else if (!_active || slot != _slot) {
      return Result::BadSlot;
    } else if (offset != _len) {
      return Result::BadOffset;
    }

    if (bytes == nullptr) n = 0;
    if (_len + n > N) {
      reset();
      return Result::TooLong;
    }
    for (size_t i = 0; i < n; i++) {
      // Matches what saveSlotRtttl stores and what LineEdit::apply can type, so a pushed
      // tune and a typed one are the same kind of thing (#1477).
      if (bytes[i] < 0x20 || bytes[i] > 0x7E) {
        reset();
        return Result::BadByte;
      }
      _buf[_len + i] = (char)bytes[i];
    }
    _len += n;
    _buf[_len] = '\0';

    if (more) return Result::Accepted;
    _active = false;   // the caller takes text() from here; a new push starts at 0 again
    return Result::Complete;
  }

  void reset() {
    _len = 0;
    _active = false;
    _buf[0] = '\0';
  }

  const char* text() const { return _buf; }
  size_t length() const { return _len; }
  uint8_t slot() const { return _slot; }
  bool active() const { return _active; }

private:
  char _buf[N + 1] = {0};
  size_t _len = 0;
  uint8_t _slot = 0;
  bool _active = false;
};

}  // namespace offband
