#pragma once

// The GUI's settings (#1457, PLAN.md §3 B6), stored as the "gui" group of
// /prefs.json. Prefs are keyed JSON: a key missing from an older file keeps
// its default here, so the layout needs no version number to grow.

#include <stdint.h>

#include <helpers/ConfigSerializer.h>

namespace offband {
namespace gui {

enum class NotifyMode : uint8_t { All = 0, Mentions = 1, Off = 2 };

constexpr uint8_t kGuiMaxChannels = 40;   // MAX_GROUP_CHANNELS in the GUI envs
constexpr uint8_t kCannedCount = 9;
constexpr uint8_t kCannedLen = 48;        // bytes including the terminator
constexpr uint32_t kDefaultAccentRgb = 0xF2C14E;

class GuiPrefs : public ConfigSerializer {
public:
  GuiPrefs();

  // Per-channel notification mode, and the mode a newly joined channel gets.
  uint8_t notify_default;                    // NotifyMode
  uint8_t channel_notify[kGuiMaxChannels];   // NotifyMode per channel slot
  char canned[kCannedCount][kCannedLen];
  uint8_t theme;        // ThemeId: 0 Offband, 1 Steel, 2 Mono HC, 3 Accent
  uint32_t accent;      // 0xRRGGBB
  uint8_t look;         // 0 Modern, 1 Classic
  uint8_t brightness;   // percent, 10..100
  uint16_t dim_secs;    // 0 = never dim; at most 3600
  uint16_t rotation;    // 0, 90, 180 or 270
  // Display › Clock and Messages & alerts (Epic G, G3); each 0 or 1.
  uint8_t clock_12h;            // 12-hour clock with AM/PM
  uint8_t notify_on;            // Enable notifications: off silences every alert
  uint8_t notify_dm;            // New DM from a contact
  uint8_t notify_channels;      // channel messages (each channel's mode still applies)
  uint8_t notify_new_node;      // a node heard for the first time
  uint8_t clear_path_on_fail;   // a DM that fails every try drops its route
  uint8_t jump_to_unread;       // conversations open at the first unread
  uint8_t wake_touch;           // a touch wakes the screen (else USR only)
  // Epic L (#1610): the one-time "Use <zone> time?" prompt has been shown.
  uint8_t zone_asked;
  // Epic M (#1661): Sensors › Units, temperature in °F (1) or °C (0). °F by
  // default (owner, 2026-10-19).
  uint8_t temp_f;
  // Epic I (#1703): a card has worked in this radio. Set the first time one
  // mounts; after a reboot with the slot empty, the header disk is red
  // rather than hidden (owner pick A, 2026-10-21).
  uint8_t sd_used;

  NotifyMode channelMode(uint8_t slot) const;
  void setChannelMode(uint8_t slot, NotifyMode m);
  // A channel was joined in `slot`: it takes the new-channel default.
  void onChannelJoined(uint8_t slot);

  // Pulls every value back into range after a load (a hand-edited or
  // corrupt file must not reach the GUI as-is).
  void sanitize();

protected:
  void structure() override;
};

}  // namespace gui
}  // namespace offband
