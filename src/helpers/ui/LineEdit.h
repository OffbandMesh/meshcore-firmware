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
};
