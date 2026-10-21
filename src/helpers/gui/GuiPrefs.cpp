#include "GuiPrefs.h"

#include <string.h>

// The board's natural orientation (Epic J #1434): 0 on the Heltecs' portrait
// panels; a landscape board's env sets 90 so a fresh radio starts in class B.
#ifndef GUI_DEFAULT_ROTATION
  #define GUI_DEFAULT_ROTATION 0
#endif

namespace offband {
namespace gui {

GuiPrefs::GuiPrefs() {
  notify_default = uint8_t(NotifyMode::All);
  memset(channel_notify, uint8_t(NotifyMode::All), sizeof(channel_notify));
  memset(canned, 0, sizeof(canned));
  theme = 0;
  accent = kDefaultAccentRgb;
  look = 0;
  brightness = 100;
  dim_secs = 30;
  rotation = GUI_DEFAULT_ROTATION;   // a landscape board (the T-Deck) starts in class B
  clock_12h = 0;
  notify_on = 1;
  notify_dm = 1;
  notify_channels = 1;
  notify_new_node = 0;
  clear_path_on_fail = 0;
  jump_to_unread = 0;
  wake_touch = 1;
  zone_asked = 0;
  temp_f = 1;
  sd_used = 0;
}

static bool validMode(uint8_t m) { return m <= uint8_t(NotifyMode::Off); }

NotifyMode GuiPrefs::channelMode(uint8_t slot) const {
  if (slot >= kGuiMaxChannels || !validMode(channel_notify[slot])) return NotifyMode::All;
  return NotifyMode(channel_notify[slot]);
}

void GuiPrefs::setChannelMode(uint8_t slot, NotifyMode m) {
  if (slot < kGuiMaxChannels && validMode(uint8_t(m))) channel_notify[slot] = uint8_t(m);
}

void GuiPrefs::onChannelJoined(uint8_t slot) {
  setChannelMode(slot, validMode(notify_default) ? NotifyMode(notify_default) : NotifyMode::All);
}

void GuiPrefs::sanitize() {
  if (!validMode(notify_default)) notify_default = uint8_t(NotifyMode::All);
  for (uint8_t& m : channel_notify) {
    if (!validMode(m)) m = uint8_t(NotifyMode::All);
  }
  for (auto& c : canned) c[kCannedLen - 1] = '\0';
  if (theme > 3) theme = 0;
  accent &= 0xFFFFFF;
  if (look > 1) look = 0;
  if (brightness < 10) brightness = 10;
  if (brightness > 100) brightness = 100;
  if (dim_secs > 3600) dim_secs = 3600;
  if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) rotation = 0;
  for (uint8_t* f : {&clock_12h, &notify_on, &notify_dm, &notify_channels, &notify_new_node, &clear_path_on_fail,
                     &jump_to_unread, &wake_touch, &zone_asked, &temp_f, &sd_used}) {
    *f = *f ? 1 : 0;
  }
}

// ConfigSerializer keys are letters and '_' only (no digits), at most
// CONFIG_MAX_KEYLEN - 1 characters. Never rename one: an old file's value
// would be dropped.
void GuiPrefs::structure() {
  def("ndef", notify_default);
  def("nchan", channel_notify, sizeof(channel_notify));
  static const char* const kCannedKeys[kCannedCount] = {"can_a", "can_b", "can_c", "can_d", "can_e",
                                                        "can_f", "can_g", "can_h", "can_i"};
  for (uint8_t i = 0; i < kCannedCount; i++) def(kCannedKeys[i], canned[i], kCannedLen);
  def("theme", theme);
  def("accent", accent);
  def("look", look);
  def("bright", brightness);
  def("dim", dim_secs);
  def("rot", rotation);
  def("hour", clock_12h);
  def("n_on", notify_on);
  def("n_dm", notify_dm);
  def("n_ch", notify_channels);
  def("n_node", notify_new_node);
  def("clrpath", clear_path_on_fail);
  def("jump", jump_to_unread);
  def("wtouch", wake_touch);
  def("tzask", zone_asked);
  def("tempf", temp_f);
  def("sdused", sd_used);
}

}  // namespace gui
}  // namespace offband
