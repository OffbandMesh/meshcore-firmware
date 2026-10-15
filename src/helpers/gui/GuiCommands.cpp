#include "GuiCommands.h"

namespace offband {
namespace gui {

// B1 stubs: the tests in test_gui_data pin the behavior; B2 implements it.
CmdResult checkRadioParams(uint32_t, uint32_t, uint8_t, uint8_t) { return CmdResult::Failed; }
CmdResult checkTxPower(int8_t, int8_t) { return CmdResult::Failed; }
CmdResult checkMessageText(size_t, size_t) { return CmdResult::Failed; }

}  // namespace gui
}  // namespace offband
