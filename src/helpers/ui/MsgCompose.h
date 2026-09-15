#pragma once

// #1228: composing a message on the badge's keyboard. Header-only and free of the mesh,
// so it is unit-tested natively; the screens in UITask put it on the display.

#include <stddef.h>
#include <stdint.h>
#include <string.h>
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

// Where a message goes. The phone can add, delete or re-sort contacts and change
// channels while the picker is up, so a position could name someone else by the time
// Enter is pressed. A contact is therefore its public-key prefix, and `index` is only
// where it sat when the list was built; findContact() turns the prefix back into a
// contact, and the compose screen keeps that contact's whole key from then on. A channel
// is its slot; the compose screen keeps the channel it was picked as and finds it again
// when it sends.
struct Target {
  enum Kind : uint8_t { Channel, Contact };
  // Eight bytes: matching a given contact's first eight means making about 2^64 keys,
  // and the list, one entry per channel and contact, stays small.
  static constexpr size_t kKeyLen = 8;
  Kind kind;
  uint16_t index;
  uint8_t key[kKeyLen];   // contacts only; zero for a channel
};

// The picker's list. The screen reads names back from the mesh when it draws a row.
template <size_t CAP>
class TargetList {
  Target _items[CAP];
  size_t _count = 0;

public:
  void clear() { _count = 0; }
  bool add(Target::Kind kind, uint16_t index, const uint8_t* key = nullptr) {
    if (_count >= CAP) return false;
    Target& t = _items[_count];
    t.kind = kind;
    t.index = index;
    for (size_t k = 0; k < Target::kKeyLen; k++) t.key[k] = key != nullptr ? key[k] : 0;
    _count++;
    return true;
  }
  size_t count() const { return _count; }
  const Target& at(size_t i) const { return _items[i]; }
};

// Fills `list` with the named channels first, then the chat contacts, in the mesh's
// order. `channel_named(i)` and `contact_is_chat(i, key)` ask the mesh, which keeps
// this free of it; the contact callback also fills in the contact's key prefix. Stops
// quietly when the list is full.
template <size_t CAP, class ChannelNamed, class ContactIsChat>
void buildTargets(TargetList<CAP>& list, int channels, ChannelNamed channel_named,
                  int contacts, ContactIsChat contact_is_chat) {
  list.clear();
  for (int i = 0; i < channels; i++) {
    if (channel_named(i) && !list.add(Target::Channel, (uint16_t)i)) return;
  }
  uint8_t key[Target::kKeyLen];
  for (int i = 0; i < contacts; i++) {
    if (contact_is_chat(i, key) && !list.add(Target::Contact, (uint16_t)i, key)) return;
  }
}

// The one chat contact whose key starts with `prefix`, or -1 when there is none or more
// than one. A prefix isn't a whole key, so if two contacts ever share one, nobody is
// picked rather than a guess. `contact_is_chat` is the callback buildTargets() takes.
template <class ContactIsChat>
int findContact(const uint8_t* prefix, int contacts, ContactIsChat contact_is_chat) {
  uint8_t key[Target::kKeyLen];
  int found = -1;
  for (int i = 0; i < contacts; i++) {
    if (!contact_is_chat(i, key) || memcmp(key, prefix, Target::kKeyLen) != 0) continue;
    if (found >= 0) return -1;
    found = i;
  }
  return found;
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
