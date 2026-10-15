#include "GuiPrefs.h"

namespace offband {
namespace gui {

// B1 stubs: the tests in test_gui_data pin the behavior; B6 implements it.
GuiPrefs::GuiPrefs() {}
NotifyMode GuiPrefs::channelMode(uint8_t) const { return NotifyMode(0xFF); }
void GuiPrefs::setChannelMode(uint8_t, NotifyMode) {}
void GuiPrefs::onChannelJoined(uint8_t) {}
void GuiPrefs::sanitize() {}
void GuiPrefs::structure() {}

}  // namespace gui
}  // namespace offband
