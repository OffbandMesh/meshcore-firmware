#include "ChunkedSave.h"

namespace offband {
namespace gui {

void ChunkedSave::begin(size_t total, size_t per_step) {
  _total = total;
  _per_step = per_step ? per_step : 1;
  _pos = 0;
  _active = true;
}

bool ChunkedSave::next(size_t* from, size_t* to) {
  if (!_active) return false;
  if (_pos >= _total) {
    _active = false;
    return false;
  }
  *from = _pos;
  *to = (_total - _pos > _per_step) ? _pos + _per_step : _total;
  _pos = *to;
  return true;
}

void ChunkedSave::restart(size_t total) {
  _total = total;
  _pos = 0;
  _active = true;
}

}  // namespace gui
}  // namespace offband
