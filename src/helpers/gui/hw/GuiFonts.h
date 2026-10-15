#pragma once

// The GUI's one font table (design spec v0.3 "Type", D1 #1323 §1.4): screens
// ask for a role, never a font, so a size or face change is one line here.
// The fonts are generated from OFL-licensed TTFs; see fonts/README.md.

#include <lvgl.h>

namespace offband {
namespace gui {

enum class FontRole : uint8_t {
  Title,      // Barlow Condensed SemiBold 18
  Body,       // Barlow Condensed 14
  BodySmall,  // Barlow Condensed 12
  Label,      // Barlow Condensed 9, set in caps
  Data,       // JetBrains Mono SemiBold 14
  Meta,       // JetBrains Mono 10
  Clock,      // JetBrains Mono SemiBold 46, digits and colon only
};

const lv_font_t* font(FontRole role);

}  // namespace gui
}  // namespace offband
