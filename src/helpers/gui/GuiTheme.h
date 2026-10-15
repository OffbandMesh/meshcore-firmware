#pragma once

// GUI themes as token tables (design spec v0.3 §"Themes", D1 #1323 §1.2).
// A theme changes colors only; layouts never change with the theme.
// Colors are 0xRRGGBB. `wash` is the translucent highlight behind selected
// rows and own messages, stored as 0xAARRGGBB.

#include <stdint.h>

namespace offband {
namespace gui {

enum class ThemeId : uint8_t { Offband, Steel, MonoHC, Accent };

enum NodeType : uint8_t { NodeCompanion, NodeRepeater, NodeRoom, NodeSensor, NodeTypeCount };

struct ThemeTokens {
  uint32_t bg, bg2, bg3;
  uint32_t line, line2;
  uint32_t ink, ink2, dim;
  uint32_t sig, sigInk;
  uint32_t wash;  // 0xAARRGGBB
  uint32_t warn, ok, bad;
  uint32_t nodeType[NodeTypeCount];
  // Flat themes drop fills: type tiles become outlines and the icon carries
  // the type, so nothing depends on color alone.
  bool flat;
};

// The default theme on first boot.
constexpr ThemeId kDefaultTheme = ThemeId::Offband;

// Look (spec v0.3 "Look", D1 §1.3), independent of the theme: Modern has
// rounded corners, tailed bubbles and pill chips; Classic is square
// hairline boxes, with your own message as a signal-colored outline.
enum class Look : uint8_t { Modern, Classic };
constexpr Look kDefaultLook = Look::Modern;

struct LookTokens {
  uint8_t radius;         // cards, rows, buttons
  uint8_t bubble_radius;  // message bubbles
  uint8_t bubble_tail;    // bubble tail size
  bool pill_chips;        // fully rounded chips and toggles
  bool own_msg_outline;   // own message drawn as an outline over the wash
};

LookTokens lookTokens(Look look);

// The user-picked signal color applies to the Accent theme only; the other
// themes ignore it. Accent's wash is derived from that color, so the
// highlight always matches the pick.
ThemeTokens themeTokens(ThemeId id, uint32_t accent_rgb);

}  // namespace gui
}  // namespace offband
