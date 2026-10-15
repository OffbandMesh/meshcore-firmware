#pragma once

// Replies to requests the GUI cares about (#1454, PLAN.md §3 B3): login,
// status, trace and path discovery. The phone gets them as before when it's
// connected; the GUI gets every one, connected phone or not, from a box that
// keeps the latest reply of each kind until the GUI takes it.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

enum class ReplyKind : uint8_t { Login, Status, Trace, Path, Count };

struct Reply {
  ReplyKind kind;
  uint8_t peer[4];     // first 4 bytes of the peer's public key (0 for a trace)
  uint32_t tag;        // request tag, where the request has one
  uint32_t at_ms;      // when it arrived
  bool ok;             // login: accepted; others: true
  uint8_t len;         // bytes in data
  // The body in the phone frame's layout after its code and reserved bytes
  // (PUSH_CODE_LOGIN_*, _STATUS_RESPONSE, _TRACE_DATA, _PATH_DISCOVERY_RESPONSE),
  // so the GUI and the phone parse one format. Sized to a whole phone frame
  // (MAX_FRAME_SIZE, 176) so no reply body is ever cut.
  uint8_t data[176];
};

class ReplyBox {
public:
  // Keeps `r` as the latest reply of its kind, replacing an untaken one. A
  // body longer than data[] (more than a phone frame) is cut to fit.
  void post(ReplyKind kind, const uint8_t peer[4], uint32_t tag, uint32_t at_ms, bool ok,
            const uint8_t* data, size_t len);

  // True if a reply of `kind` is waiting; copies it to *out and clears it.
  bool take(ReplyKind kind, Reply* out);

  // True if a reply of `kind` is waiting, without taking it.
  bool waiting(ReplyKind kind) const;

private:
  Reply _box[size_t(ReplyKind::Count)] = {};
  bool _full[size_t(ReplyKind::Count)] = {};
};

}  // namespace gui
}  // namespace offband
