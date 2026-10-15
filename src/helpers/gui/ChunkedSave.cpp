#include "ChunkedSave.h"

namespace offband {
namespace gui {

// B1 stubs: the tests in test_gui_data pin the behavior; B7 implements it.
void ChunkedSave::begin(size_t, size_t) {}
bool ChunkedSave::next(size_t*, size_t*) { return false; }
void ChunkedSave::restart(size_t) {}

}  // namespace gui
}  // namespace offband
