#include "GuiReplies.h"

namespace offband {
namespace gui {

// B1 stubs: the tests in test_gui_data pin the behavior; B3 implements it.
void ReplyBox::post(ReplyKind, const uint8_t*, uint32_t, uint32_t, bool, const uint8_t*, size_t) {}
bool ReplyBox::take(ReplyKind, Reply*) { return false; }
bool ReplyBox::waiting(ReplyKind) const { return false; }

}  // namespace gui
}  // namespace offband
