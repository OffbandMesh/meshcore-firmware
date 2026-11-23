#include "Mention.h"

#include <string.h>

namespace offband {
namespace gui {

namespace {

char fold(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

bool wordChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Equal, ignoring ASCII case: the receiver's strncasecmp.
bool sameName(const char* a, const char* b) {
  for (; *a && *b; a++, b++)
    if (fold(*a) != fold(*b)) return false;
  return *a == *b;
}

}  // namespace

// ------------------------------------------------------------ RecentSenders

void RecentSenders::heard(int thread, const char* name) {
  if (thread < 0 || !name || !name[0]) return;
  const size_t n = strlen(name);
  if (n >= kMentionNameLen) return;   // can't be kept whole
  Entry* slot = nullptr;
  for (Entry& e : _e) {
    if (e.order && e.thread == thread && strcmp(e.name, name) == 0) {
      slot = &e;   // heard before: it moves to the newest
      break;
    }
  }
  if (!slot) {
    slot = &_e[0];
    for (Entry& e : _e) {
      if (!e.order) {
        slot = &e;
        break;
      }
      if (e.order < slot->order) slot = &e;   // the one heard longest ago
    }
    slot->thread = int16_t(thread);
    memcpy(slot->name, name, n + 1);
  }
  slot->order = _next++;
}

size_t RecentSenders::list(int thread, const char** out, size_t max) const {
  size_t k = 0;
  uint32_t below = UINT32_MAX;   // newest first: each pass takes the next older one
  while (k < max) {
    const Entry* best = nullptr;
    for (const Entry& e : _e) {
      if (e.order && e.thread == thread && e.order < below && (!best || e.order > best->order)) best = &e;
    }
    if (!best) break;
    out[k++] = best->name;
    below = best->order;
  }
  return k;
}

void RecentSenders::clear() {
  for (Entry& e : _e) e.order = 0;
  _next = 1;
}

// ------------------------------------------------------------------ parsing

bool openMention(const char* text, size_t len, size_t* at) {
  if (!text) return false;
  for (size_t i = len; i > 0; i--) {
    const char c = text[i - 1];
    if (c == '@') {
      if (i >= 2 && wordChar(text[i - 2])) return false;   // "me@home"
      if (at) *at = i - 1;
      return true;
    }
    if (c == ' ' || c == '\n' || c == '\t' || c == ']') return false;
  }
  return false;
}

bool mentionPrefix(const char* name, const char* query, size_t qlen) {
  for (size_t i = 0; i < qlen; i++) {
    if (!name[i] || fold(name[i]) != fold(query[i])) return false;
  }
  return true;
}

bool mentionable(const char* name) {
  if (!name || !name[0] || strchr(name, ']')) return false;
  for (const char* p = name; *p; p++)
    if (*p != ' ' && *p != '\t') return true;
  return false;
}

size_t mentionPicks(const char* query, size_t qlen, const char* const* recent, size_t nrecent, const NodeRow* rows,
                    size_t nrows, const char* self, const char** out, size_t max) {
  size_t k = 0;
  auto offer = [&](const char* name) {
    if (k >= max || !mentionable(name) || !mentionPrefix(name, query, qlen)) return;
    if (self && sameName(name, self)) return;
    for (size_t i = 0; i < k; i++)
      if (sameName(out[i], name)) return;
    out[k++] = name;
  };
  for (size_t i = 0; i < nrecent && k < max; i++) offer(recent[i]);
  // Chat contacts, most recently heard first: one pass per age, youngest
  // first, so nothing is sorted or copied (a few hundred rows, four slots).
  // Rows of the same age go in table order.
  bool any_taken = false;
  uint32_t taken_age = 0;   // every age up to this one is done
  while (k < max) {
    const NodeRow* best = nullptr;
    for (size_t i = 0; i < nrows; i++) {
      const NodeRow& r = rows[i];
      if (r.kind != NodeKind::Chat || !r.heard || (any_taken && r.heard_age_s <= taken_age)) continue;
      if (!best || r.heard_age_s < best->heard_age_s) best = &r;
    }
    if (!best) break;
    taken_age = best->heard_age_s;
    any_taken = true;
    for (size_t i = 0; i < nrows && k < max; i++) {
      const NodeRow& r = rows[i];
      if (r.kind == NodeKind::Chat && r.heard && r.heard_age_s == taken_age) offer(r.name);
    }
  }
  for (size_t i = 0; i < nrows && k < max; i++) {   // never heard: last
    if (rows[i].kind == NodeKind::Chat && !rows[i].heard) offer(rows[i].name);
  }
  return k;
}

// ------------------------------------------------------------------ editing

namespace {

// Whether every byte of `s` belongs to a well-formed UTF-8 character, by the
// rule ComposeBuffer::insert() keeps (no overlongs, nothing past U+10FFFF).
bool wellFormed(const char* s) {
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(s); *p;) {
    size_t k = *p < 0x80 ? 1 : (*p >= 0xC2 && *p <= 0xDF) ? 2 : (*p >= 0xE0 && *p <= 0xEF) ? 3
                                                              : (*p >= 0xF0 && *p <= 0xF4) ? 4 : 0;
    if (k == 0) return false;
    for (size_t i = 1; i < k; i++)
      if ((p[i] & 0xC0) != 0x80) return false;   // a NUL ends it here too
    p += k;
  }
  return true;
}

}  // namespace

// Checked before anything changes, so a refusal leaves the draft untouched.
bool applyMention(ComposeBuffer& buf, size_t at, const char* name) {
  if (at >= buf.length() || buf.text()[at] != '@' || !mentionable(name) || !wellFormed(name)) return false;
  const size_t token = 2 + strlen(name) + 1;   // "@[" name "]"
  if (at + token > buf.budget()) return false;
  buf.truncate(at);
  buf.insertText("@[");
  buf.insertText(name);
  buf.insertText("]");
  buf.insertText(" ");   // when it fits
  return true;
}

}  // namespace gui
}  // namespace offband
