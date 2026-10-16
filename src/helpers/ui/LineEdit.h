#pragma once

#include <stddef.h>
#include <stdint.h>
#include "UIScreen.h"

// A one-line text editor over a fixed buffer of N characters, for anything that takes
// typed text on a small screen. Header-only, so it is unit-tested natively.
template <size_t N>
class LineEdit {
  char _buf[N + 1] = {0};
  size_t _len = 0;

public:
  const char* text() const { return _buf; }
  size_t length() const { return _len; }

  void clear() {
    _len = 0;
    _buf[0] = '\0';
  }

  // Apply one UI key and return whether it changed the text: printables append until
  // the line is full, Backspace deletes the last character, Tab inserts a space.
  // Navigation and control keys are left to the caller.
  bool apply(uint8_t key) {
    if (key == KEY_BACKSPACE) {
      if (_len == 0) return false;
      _buf[--_len] = '\0';
      return true;
    }
    if (key == KEY_TAB) key = ' ';
    if (key < 0x20 || key > 0x7E || _len >= N) return false;
    _buf[_len++] = (char)key;
    _buf[_len] = '\0';
    return true;
  }

  // The last `width` characters, for a display line narrower than the buffer.
  const char* tail(size_t width) const { return _len > width ? _buf + (_len - width) : _buf; }

  // #1473: a slice of the middle, for laying the buffer out across SEVERAL rows.
  //
  // `tail()` is enough for a message, where only the end being typed matters. It is not
  // enough for something long that has to be read back before it is committed -- a
  // 218-character RTTTL string on a 21-column screen shows its last 21 characters and
  // hides everything typed before them, so it cannot be proofread.
  //
  // Unlike `tail()` this COPIES, and that is not an oversight: a slice of the middle has
  // no terminator of its own, and `tail()` may return a bare pointer only because it
  // always runs to the end of the buffer. Writing one in place would mean mutating the
  // text to display it.
  //
  // Returns the number of characters written, and always terminates `out` when there is
  // room for a terminator at all. A `start` at or past the end is not an error -- it
  // yields an empty row, which is what a display wants for the tail of a part-full
  // layout rather than a refusal it has to special-case.
  size_t window(size_t start, size_t width, char* out, size_t out_cap) const {
    if (out == nullptr || out_cap == 0) return 0;
    out[0] = '\0';
    if (start >= _len || width == 0) return 0;
    size_t n = _len - start;
    if (n > width) n = width;
    if (n > out_cap - 1) n = out_cap - 1;   // room kept for the terminator
    for (size_t i = 0; i < n; i++) out[i] = _buf[start + i];
    out[n] = '\0';
    return n;
  }

  // How many rows of `width` the text needs. An empty buffer still needs one, so the
  // cursor has a row to sit on rather than the screen drawing nothing.
  //
  // Divides rather than using the usual `(_len + width - 1) / width`: that idiom
  // overflows when `width` is near SIZE_MAX, and on a 32-bit target it then returns 0 --
  // a row count that is never valid, and one a caller would use to draw nothing or to
  // size a loop that never runs. No display width goes anywhere near that, so this is
  // closing a latent trap rather than fixing a live bug, but it costs one extra
  // instruction and the function has no business having an arithmetic cliff in it.
  size_t rows(size_t width) const {
    if (width == 0 || _len == 0) return 1;
    const size_t whole = _len / width;
    return _len % width ? whole + 1 : whole;
  }
};
