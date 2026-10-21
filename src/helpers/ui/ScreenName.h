#pragma once

// #973: resolve a screen pointer to a stable label for UI tracing.
//
// This is a pure function, in its own header, because the inline if/else chain it
// replaces carried a defect that actively misdirected diagnosis. The chain compared
// the candidate against each screen member BEFORE testing for null:
//
//     if      (c == splash)      to = "SPLASH";
//     else if (c == home)        to = "HOME";    // matches when BOTH are null
//     else if (c == nullptr)     to = "NULL";    // unreachable in exactly that case
//
// So whenever a member was itself null -- because its `new` failed (#973), or because
// it is deliberately null on a build (`msg_preview` on the CardKB badge) -- a
// transition to NOTHING matched that member and printed its name. The trace read as a
// clean, healthy handoff while the UI went dark, and that is what sent the first round
// of #973 diagnosis at the display driver instead of at the allocator.
//
// NULL IS TESTED FIRST, and the null test is deliberately NOT folded into the table
// loop: a table entry whose pointer is null must never match anything. Both properties
// are covered by test/test_screen_name, positively and negatively. Do not reorder them.

#include <stddef.h>

namespace offband {

// One screen pointer and the name to print for it. `ptr` is void* so this header
// stays free of the UI class hierarchy and can be unit-tested natively.
struct ScreenLabel {
  const void* ptr;
  const char* name;
};

// Returns "NULL" for a null screen, the matching table name for a known one, and
// "?" for a pointer the table does not describe.
inline const char* screenName(const void* c, const ScreenLabel* table, size_t count) {
  if (c == nullptr) return "NULL";          // FIRST -- see the header comment.
  if (table == nullptr) return "?";
  for (size_t i = 0; i < count; i++) {
    // The `!= nullptr` guard is not redundant with the test above: it keeps a null
    // TABLE ENTRY from ever being reported as a match, which is the same class of
    // confusion in the opposite direction.
    if (table[i].ptr != nullptr && table[i].ptr == c) return table[i].name;
  }
  return "?";
}

}  // namespace offband
