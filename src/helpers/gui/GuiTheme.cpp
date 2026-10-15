#include "GuiTheme.h"

namespace offband {
namespace gui {

// Token values are the design of record: ObScreen.dc.html THEMES/EXTRA
// (spec v0.3). A wash given there as rgba(r,g,b,a) is stored with alpha
// round(a * 255): .09 -> 0x17, .12 -> 0x1F, .10 -> 0x1A.

namespace {

constexpr uint32_t kNodeTypes[NodeTypeCount] = {0x8CC8FF, 0xC3A5FF, 0xF2D27A, 0xFF9EC4};

ThemeTokens offband() {
  ThemeTokens t = {};
  t.bg = 0x0F1412;
  t.bg2 = 0x161C19;
  t.bg3 = 0x1D2521;
  t.line = 0x34403A;
  t.line2 = 0x232C28;
  t.ink = 0xEFF3E8;
  t.ink2 = 0xBAC4B5;
  t.dim = 0x7C8B82;
  t.sig = 0x7BEFA8;
  t.sigInk = 0x0F1412;
  t.wash = 0x177BEFA8;
  t.warn = 0xFFA552;
  t.ok = 0x7BEFA8;
  t.bad = 0xFF7A6E;
  for (int i = 0; i < NodeTypeCount; i++) t.nodeType[i] = kNodeTypes[i];
  t.flat = false;
  return t;
}

ThemeTokens steel() {
  ThemeTokens t = {};
  t.bg = 0x1D2D3D;
  t.bg2 = 0x22364A;
  t.bg3 = 0x2C455D;
  t.line = 0x416180;
  t.line2 = 0x2C455D;
  t.ink = 0xF5F5F8;
  t.ink2 = 0xB5D9FD;
  t.dim = 0x94BCE3;
  t.sig = 0x94BCE3;
  t.sigInk = 0x1D2D3D;
  t.wash = 0x1F94BCE3;
  t.warn = 0xFFA552;
  t.ok = 0x7BEFA8;
  t.bad = 0xFF8A80;
  for (int i = 0; i < NodeTypeCount; i++) t.nodeType[i] = kNodeTypes[i];
  t.flat = false;
  return t;
}

// Sunlight theme: selection by fill inversion, so the wash is opaque.
ThemeTokens monoHc() {
  ThemeTokens t = {};
  t.bg = 0x000000;
  t.bg2 = 0x000000;
  t.bg3 = 0x1A1A1A;
  t.line = 0xFFFFFF;
  t.line2 = 0x6B6B6B;
  t.ink = 0xFFFFFF;
  t.ink2 = 0xFFFFFF;
  t.dim = 0xC8C8C8;
  t.sig = 0xFFFFFF;
  t.sigInk = 0x000000;
  t.wash = 0xFF1A1A1A;
  t.warn = 0xFFFFFF;
  t.ok = 0xFFFFFF;
  t.bad = 0xFFFFFF;
  for (int i = 0; i < NodeTypeCount; i++) t.nodeType[i] = 0xFFFFFF;
  t.flat = true;
  return t;
}

}  // namespace

// Compact-class values from the design (D1 #1323 §1.3; ObScreen.dc.html:338-339).
LookTokens lookTokens(Look look) {
  if (look == Look::Classic) return LookTokens{0, 0, 0, false, true};
  return LookTokens{6, 10, 3, true, false};
}

ThemeTokens themeTokens(ThemeId id, uint32_t accent_rgb) {
  switch (id) {
    case ThemeId::Steel:
      return steel();
    case ThemeId::MonoHC:
      return monoHc();
    case ThemeId::Accent: {
      // Offband neutrals with the picked signal color. The prototype keeps a
      // fixed yellow wash whatever is picked (D1 gap 5); this derives it.
      ThemeTokens t = offband();
      t.sig = accent_rgb & 0xFFFFFF;
      t.wash = 0x1A000000u | (accent_rgb & 0xFFFFFF);
      return t;
    }
    case ThemeId::Offband:
      break;
  }
  return offband();
}

}  // namespace gui
}  // namespace offband
