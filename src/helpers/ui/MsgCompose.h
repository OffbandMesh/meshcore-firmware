#pragma once

// #1228: composing a message on the badge's keyboard: how much a message may carry, and
// a line that stops there. Header-only and free of the mesh, so it is unit-tested
// natively; the thread screen (#1230) puts it on the display.

#include <stddef.h>
#include <stdint.h>
#include "UIScreen.h"
#include "LineEdit.h"

namespace compose {

// How many characters a message may carry. MeshCore caps a text payload at max_text
// bytes (MAX_TEXT_LEN). A DM gets all of it; a channel message goes out as
// "<name>: <text>", so the name and ": " come off the top.
inline int budget(bool to_channel, size_t name_len, int max_text) {
  if (!to_channel) return max_text;
  const int left = max_text - (int)name_len - 2;
  return left > 0 ? left : 0;
}

// One line of text that stops at the budget. Backspace always works; printables and
// Tab (a space) stop once the budget is used up.
template <size_t N>
class Composer {
  LineEdit<N> _line;
  int _budget = (int)N;

public:
  void start(int budget_chars) {
    _line.clear();
    _budget = budget_chars < (int)N ? (budget_chars > 0 ? budget_chars : 0) : (int)N;
  }
  bool apply(uint8_t key) {
    if (key != KEY_BACKSPACE && (int)_line.length() >= _budget) return false;
    return _line.apply(key);
  }
  void clear() { _line.clear(); }
  int remaining() const { return _budget - (int)_line.length(); }
  const char* text() const { return _line.text(); }
  size_t length() const { return _line.length(); }
  const char* tail(size_t width) const { return _line.tail(width); }
};

}  // namespace compose
