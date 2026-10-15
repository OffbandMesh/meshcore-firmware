#include "GuiReplies.h"

#include <string.h>

namespace offband {
namespace gui {

void ReplyBox::post(ReplyKind kind, const uint8_t peer[4], uint32_t tag, uint32_t at_ms, bool ok,
                    const uint8_t* data, size_t len) {
  const size_t k = size_t(kind);
  if (k >= size_t(ReplyKind::Count)) return;
  Reply& r = _box[k];
  r.kind = kind;
  if (peer) memcpy(r.peer, peer, sizeof(r.peer));
  else memset(r.peer, 0, sizeof(r.peer));
  r.tag = tag;
  r.at_ms = at_ms;
  r.ok = ok;
  if (len > sizeof(r.data)) len = sizeof(r.data);
  r.len = uint8_t(len);
  if (data && len) memcpy(r.data, data, len);
  _full[k] = true;
}

bool ReplyBox::take(ReplyKind kind, Reply* out) {
  const size_t k = size_t(kind);
  if (k >= size_t(ReplyKind::Count) || !_full[k]) return false;
  if (out) *out = _box[k];
  _full[k] = false;
  return true;
}

bool ReplyBox::waiting(ReplyKind kind) const {
  const size_t k = size_t(kind);
  return k < size_t(ReplyKind::Count) && _full[k];
}

}  // namespace gui
}  // namespace offband
