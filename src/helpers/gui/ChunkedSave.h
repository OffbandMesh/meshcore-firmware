#pragma once

// Writes the contact list a slice at a time (#1458, PLAN.md §3 B7), so a
// 350-contact save (~1.8 s in one pass) never holds the loop long enough to
// stall a GUI frame. Each loop tick writes the next slice; the file is only
// swapped in when every record has been written.

#include <stddef.h>

namespace offband {
namespace gui {

class ChunkedSave {
public:
  // Starts a save of `total` records, at most `per_step` per tick.
  void begin(size_t total, size_t per_step);
  bool active() const { return _active; }
  // The next slice to write, [*from, *to). False once every record has been
  // handed out; the save is then finished and inactive.
  bool next(size_t* from, size_t* to);
  // The list changed under a save in progress (an add or delete shifts the
  // records): the save starts over from record 0 with the new total.
  void restart(size_t total);
  // Abandons the save (a full one-pass save is about to replace it).
  void cancel() { _active = false; }

private:
  size_t _total = 0;
  size_t _per_step = 1;
  size_t _pos = 0;
  bool _active = false;
};

}  // namespace gui
}  // namespace offband
