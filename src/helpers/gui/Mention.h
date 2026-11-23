#pragma once

// Mentions from the GUI's compose (#2234), as the Offband client makes them
// (mention_autocomplete.dart): "@" at the start of a word opens suggestions,
// each further letter narrows them, and choosing one puts "@[Name] " in place
// of what was typed. The receiver matches only "@[" ... "]"
// (MyMesh::textMentionsSelf), so a bare "@name" is plain text, and a
// keyboard with no "[" or "]" (the T-Deck Pro's) could never make one.
//
// The name goes in byte for byte, as it was heard on the air or advertised
// (client #497, firmware #510): no trimming and no case folding. That is why
// the store's sender (MsgRecord::sender, cut to 13 bytes) is never offered:
// a cut name can't be told from a whole one, since the cut stops at a
// character boundary, and a cut name never matches.
//
// Pure: no Arduino, no LVGL.

#include <stddef.h>
#include <stdint.h>

#include "GuiViews.h"
#include "NodeViews.h"

namespace offband {
namespace gui {

constexpr size_t kMentionNameLen = 32;   // ContactInfo::name, with its terminator
constexpr size_t kMentionShown = 4;      // suggestions on screen at once

// The channel senders heard since boot, by full name, newest first: what the
// store can't keep (above). A fixed table across all channels; the sender
// heard longest ago makes room.
class RecentSenders {
public:
  static constexpr size_t kCap = 32;
  // A message from `name` in thread `thread`. An empty name, or one too long
  // to keep whole, is not kept.
  void heard(int thread, const char* name);
  // Thread t's senders, newest first; returns how many went into out.
  size_t list(int thread, const char** out, size_t max) const;
  void clear();

private:
  struct Entry {
    uint32_t order;   // 0: free
    int16_t thread;
    char name[kMentionNameLen];
  };
  Entry _e[kCap] = {};
  uint32_t _next = 1;
};

// Whether the draft ends in an open mention: an "@" at the start or after a
// character that isn't a letter or digit (so "me@home" isn't one), then no
// space and no "]". *at is the "@"'s offset; the query is what follows it.
bool openMention(const char* text, size_t len, size_t* at);

// Whether `name` starts with the query, ignoring ASCII case, as the
// receiver's match does.
bool mentionPrefix(const char* name, const char* query, size_t qlen);

// Whether a name can make a mention: not empty, not all spaces, and no "]",
// which would end the token early.
bool mentionable(const char* name);

// The names offered for `query`, at most `max`, in order: this channel's
// recent senders (newest first), then chat contacts (most recently heard
// first, never heard last). Our own name, names already offered (ASCII case
// ignored, as the receiver ignores it) and names that can't make a mention
// are left out. The pointers are into `recent` and `rows`.
size_t mentionPicks(const char* query, size_t qlen, const char* const* recent, size_t nrecent, const NodeRow* rows,
                    size_t nrows, const char* self, const char** out, size_t max);

// Puts "@[name] " in place of the open mention at `at`. All or nothing: false,
// and the draft as it was, when the token doesn't fit the budget or the name
// isn't well-formed UTF-8. The trailing space is left off when only it
// doesn't fit.
bool applyMention(ComposeBuffer& buf, size_t at, const char* name);

}  // namespace gui
}  // namespace offband
