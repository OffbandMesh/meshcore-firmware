#pragma once

// #1229: the badge's own copy of recent messages, so it can show an inbox and threads
// without a phone. MeshCore's companion keeps messages only in the queue the phone
// drains; this keeps a second, badge-side copy.
//
// A conversation is a channel or a contact, identified by its key: a contact by its
// public key, a channel by its secret. A key doesn't change when the phone renames,
// re-sorts or moves things, so a conversation can't turn into someone else. Each
// conversation keeps its unread count, when it was last active, and pinned and muted
// flags.
//
// Messages share one pool. When it's full, the conversation holding the most messages
// gives up its oldest, so a busy channel can't push a quiet DM out. Unread counts live
// with the conversation and outlive evicted messages.
//
// A conversation index stays good until the next convo() call that has to make room,
// which can evict one. The open conversation (setOpen) is never evicted, so a screen
// can keep that index; anything else should re-read ordered() or find() each time.
//
// RAM, for the badge's <24, 32, 160, 32>: 24 x 80 B of conversations plus
// 32 x 192 B of messages, about 8 KB.
//
// Pure: no Arduino and no mesh. The caller passes times in.

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BadgeSendTracker.h"   // BadgeSend

namespace offband {

// How much of s[0, len) fits in `max` bytes without ending inside a UTF-8 character,
// so a name or text cut to fit is still valid UTF-8.
inline size_t utf8Fit(const char* s, size_t len, size_t max) {
  if (len <= max) return len;
  size_t n = max;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;   // s[n] is the first byte cut off
  return n;
}

// MeshCore sends a channel message as "<name>: <text>". Copies the name into `sender`,
// cut to fit, and returns the text after ": ". Text with no ": " within the first 32
// characters (a MeshCore name is at most 31) has no name: `sender` comes back empty
// and the whole text is returned. The first ": " is the split, as for any reader of
// the wire format: a name that itself holds ": " can't be told apart.
inline const char* splitSender(const char* text, char* sender, size_t n) {
  if (n > 0) sender[0] = 0;
  const char* sep = strstr(text, ": ");
  if (sep == nullptr || sep == text || sep - text > 31) return text;
  if (n > 0) {
    const size_t len = utf8Fit(text, (size_t)(sep - text), n - 1);
    memcpy(sender, text, len);
    sender[len] = 0;
  }
  return sep + 2;
}

template <int CONVOS, int MSGS, int TEXT_CAP, int KEY_LEN>
class BadgeStore {
  static_assert(CONVOS > 0 && CONVOS < 255, "conversation indexes are 8-bit");
  static_assert(MSGS > 0, "the pool needs a message");
  static_assert(KEY_LEN > 0, "a conversation needs a key");

public:
  static constexpr int kNameLen = 32;     // ChannelDetails::name and ContactInfo::name
  static constexpr int kSenderLen = 16;   // a channel message's sender, for its tag
  enum Kind : uint8_t { Empty = 0, Channel, Contact };

  struct Convo {
    Kind      kind;
    bool      pinned;
    bool      muted;
    uint16_t  unread;
    uint32_t  last_seq;         // the newest message's sequence number; 0 before any
    uint32_t  last_time;        // when that message came or went, in RTC seconds
    uint8_t   key[KEY_LEN];     // a contact's public key, or a channel's secret
    char      name[kNameLen];   // without the # or @
  };

  struct Msg {
    uint32_t   seq;             // 0: a free slot. Later messages have larger numbers.
    uint32_t   time;            // when the badge got or sent it, in RTC seconds
    uint16_t   handle;          // an outgoing DM's tracker handle, else 0
    uint8_t    convo;
    bool       outgoing;
    BadgeSend  status;          // outgoing only: None for a channel send
    uint8_t    hops;            // 0xFF: direct, or not known
    int8_t     rssi;            // dBm, 0 when not known
    char       sender[kSenderLen];   // a channel message's sender, else empty
    char       text[TEXT_CAP + 1];
  };

  BadgeStore() {
    memset(_convos, 0, sizeof(_convos));
    memset(_msgs, 0, sizeof(_msgs));
  }

  int find(Kind kind, const uint8_t* key) const {
    for (int i = 0; i < CONVOS; i++) {
      if (_convos[i].kind == kind && memcmp(_convos[i].key, key, KEY_LEN) == 0) return i;
    }
    return -1;
  }

  // The conversation for this kind and key, made if it's new. Its name is refreshed
  // either way. With the table full, the least recently active conversation that isn't
  // pinned or open goes, messages and all; -1 when there's none to give up.
  int convo(Kind kind, const uint8_t* key, const char* name) {
    int c = find(kind, key);
    if (c < 0) {
      c = freeConvo();
      if (c < 0) return -1;
      memset(&_convos[c], 0, sizeof(_convos[c]));
      _convos[c].kind = kind;
      memcpy(_convos[c].key, key, KEY_LEN);
    }
    copy(_convos[c].name, name, kNameLen);
    return c;
  }

  const Convo* convoAt(int c) const { return live(c) ? &_convos[c] : nullptr; }

  // A message that arrived. Counts as unread unless its conversation is open.
  uint32_t addIncoming(int c, uint32_t time, const char* sender, const char* text,
                       uint8_t hops, int8_t rssi) {
    Msg* m = add(c, time, text);
    if (m == nullptr) return 0;
    copy(m->sender, sender, kSenderLen);
    m->hops = hops;
    m->rssi = rssi;
    if (c != _open && _convos[c].unread < 0xFFFF) _convos[c].unread++;
    return m->seq;
  }

  // A message the badge sent. `handle` is its DM tracker handle, 0 for a channel.
  uint32_t addOutgoing(int c, uint32_t time, const char* text, uint16_t handle, BadgeSend status) {
    Msg* m = add(c, time, text);
    if (m == nullptr) return 0;
    m->outgoing = true;
    m->handle = handle;
    m->status = status;
    m->hops = 0xFF;
    return m->seq;
  }

  bool setStatus(uint32_t seq, BadgeSend status) {
    Msg* m = msgRef(seq);
    if (m == nullptr || !m->outgoing) return false;
    m->status = status;
    return true;
  }

  // The newest outgoing message with this tracker handle takes the status.
  bool setStatusForHandle(uint16_t handle, BadgeSend status) {
    if (handle == 0) return false;
    Msg* best = nullptr;
    for (Msg& m : _msgs) {
      if (m.seq != 0 && m.outgoing && m.handle == handle && (best == nullptr || m.seq > best->seq)) best = &m;
    }
    if (best == nullptr) return false;
    best->status = status;
    return true;
  }

  // Brings each outgoing DM still sending up to date: `status_of(handle)` returns the
  // tracker's word on it. The tracker never drops a DM that is still sending, so None
  // can't normally come back; if it does, the DM counts as failed, which offers a resend.
  template <class StatusOf>
  void refreshSending(StatusOf status_of) {
    for (Msg& m : _msgs) {
      if (m.seq == 0 || !m.outgoing || m.handle == 0 || m.status != BadgeSend::Sending) continue;
      const BadgeSend now = status_of(m.handle);
      if (now != BadgeSend::Sending) m.status = (now == BadgeSend::None) ? BadgeSend::Failed : now;
    }
  }

  // #1232: a channel send (no tracker handle) waits `after_secs` for a repeater to be
  // heard passing it on, then stops waiting: no mark rather than a failure, since nodes
  // in direct range got it without echoing. A repeat heard later still ticks it.
  void expireChannelSends(uint32_t now, uint32_t after_secs) {
    for (Msg& m : _msgs) {
      if (m.seq == 0 || !m.outgoing || m.handle != 0 || m.status != BadgeSend::Sending) continue;
      if (now >= m.time && now - m.time >= after_secs) m.status = BadgeSend::None;
    }
  }

  // Drops one message, as when a failed DM is sent again in its place.
  bool remove(uint32_t seq) {
    Msg* m = msgRef(seq);
    if (m == nullptr) return false;
    m->seq = 0;
    return true;
  }

  const Msg* msg(uint32_t seq) const { return const_cast<BadgeStore*>(this)->msgRef(seq); }

  void markRead(int c) {
    if (live(c)) _convos[c].unread = 0;
  }

  // The conversation on screen, or -1. Opening one reads it.
  void setOpen(int c) {
    _open = live(c) ? c : -1;
    markRead(_open);
  }
  int open() const { return _open; }

  void setPinned(int c, bool on) {
    if (live(c)) _convos[c].pinned = on;
  }
  void setMuted(int c, bool on) {
    if (live(c)) _convos[c].muted = on;
  }

  uint16_t totalUnread() const {
    uint32_t n = 0;
    for (const Convo& v : _convos) n += v.unread;
    return n > 0xFFFF ? 0xFFFF : (uint16_t)n;
  }

  // The inbox's order: pinned first, then unread, then the most recent. Conversations
  // with nothing in them yet come last. Returns how many indexes went into `out`.
  int ordered(uint8_t* out, int max) const {
    uint8_t all[CONVOS];
    int n = 0;
    for (int i = 0; i < CONVOS; i++) {
      if (_convos[i].kind == Empty) continue;
      int j = n++;
      while (j > 0 && before(i, all[j - 1])) {
        all[j] = all[j - 1];
        j--;
      }
      all[j] = (uint8_t)i;
    }
    const int count = n < max ? n : max;
    memcpy(out, all, (size_t)count);
    return count;
  }

  // Conversation c's messages, oldest first. When there are more than `max`, the
  // newest `max`.
  int thread(int c, uint32_t* seqs, int max) const {
    uint32_t all[MSGS];
    int n = 0;
    for (const Msg& m : _msgs) {
      if (m.seq == 0 || m.convo != c) continue;
      int j = n++;
      while (j > 0 && all[j - 1] > m.seq) {
        all[j] = all[j - 1];
        j--;
      }
      all[j] = m.seq;
    }
    const int first = n > max ? n - max : 0;
    for (int i = first; i < n; i++) seqs[i - first] = all[i];
    return n - first;
  }

private:
  Convo _convos[CONVOS];
  Msg _msgs[MSGS];
  uint32_t _seq = 0;
  int _open = -1;

  bool live(int c) const { return c >= 0 && c < CONVOS && _convos[c].kind != Empty; }

  static void copy(char* dst, const char* src, size_t n) {
    if (src == nullptr) src = "";
    const size_t len = utf8Fit(src, strlen(src), n - 1);
    memcpy(dst, src, len);
    dst[len] = 0;
  }

  bool before(int a, int b) const {
    const Convo& x = _convos[a];
    const Convo& y = _convos[b];
    if (x.pinned != y.pinned) return x.pinned;
    const bool xu = x.unread > 0, yu = y.unread > 0;
    if (xu != yu) return xu;
    if (x.last_seq != y.last_seq) return x.last_seq > y.last_seq;
    return a < b;
  }

  Msg* msgRef(uint32_t seq) {
    if (seq == 0) return nullptr;
    for (Msg& m : _msgs) {
      if (m.seq == seq) return &m;
    }
    return nullptr;
  }

  Msg* add(int c, uint32_t time, const char* text) {
    if (!live(c) || text == nullptr) return nullptr;
    Msg* m = freeMsg();
    memset(m, 0, sizeof(*m));
    if (++_seq == 0) _seq = 1;
    m->seq = _seq;
    m->time = time;
    m->convo = (uint8_t)c;
    copy(m->text, text, TEXT_CAP + 1);
    _convos[c].last_seq = m->seq;
    _convos[c].last_time = time;
    return m;
  }

  // A free slot, or the oldest message of the conversation holding the most.
  Msg* freeMsg() {
    int counts[CONVOS] = {0};
    for (Msg& m : _msgs) {
      if (m.seq == 0) return &m;
      counts[m.convo]++;
    }
    int most = 0;
    for (int n : counts) {
      if (n > most) most = n;
    }
    Msg* oldest = nullptr;
    for (Msg& m : _msgs) {
      if (counts[m.convo] == most && (oldest == nullptr || m.seq < oldest->seq)) oldest = &m;
    }
    return oldest;
  }

  // An empty slot, or the least recently active conversation that isn't pinned or
  // open, cleared along with its messages.
  int freeConvo() {
    int best = -1;
    for (int i = 0; i < CONVOS; i++) {
      if (_convos[i].kind == Empty) return i;
      if (_convos[i].pinned || i == _open) continue;
      if (best < 0 || _convos[i].last_seq < _convos[best].last_seq) best = i;
    }
    if (best >= 0) {
      for (Msg& m : _msgs) {
        if (m.seq != 0 && m.convo == best) m.seq = 0;
      }
    }
    return best;
  }
};

}  // namespace offband
