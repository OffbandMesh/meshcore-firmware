// #1439 (Epic A #1425): the GUI foundation's pure logic, pinned to the design
// of record (spec v0.3, ObScreen prototype) before any of it is implemented.
//
// WHY THESE ARE TESTED HERE
//   Every one of these decides what the user sees or what a button does, and
//   none of it needs hardware to check. A wrong bar threshold or a swapped
//   theme token is otherwise found by squinting at a panel, one board at a
//   time.

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

#include "helpers/gui/ClockPolicy.h"
#include "helpers/gui/GuiTheme.h"
#include "helpers/gui/InputMap.h"
#include "helpers/gui/SignalLevel.h"

using namespace offband::gui;

// ---------------------------------------------------------------- signal bars

TEST(SignalLevel, DemodFloorPerSpreadingFactor) {
  // SX1262 demodulation SNR limits, 2.5 dB per SF step.
  EXPECT_FLOAT_EQ(demodFloorDb(5), -2.5f);
  EXPECT_FLOAT_EQ(demodFloorDb(6), -5.0f);
  EXPECT_FLOAT_EQ(demodFloorDb(7), -7.5f);
  EXPECT_FLOAT_EQ(demodFloorDb(8), -10.0f);
  EXPECT_FLOAT_EQ(demodFloorDb(9), -12.5f);
  EXPECT_FLOAT_EQ(demodFloorDb(10), -15.0f);
  EXPECT_FLOAT_EQ(demodFloorDb(11), -17.5f);
  EXPECT_FLOAT_EQ(demodFloorDb(12), -20.0f);
}

TEST(SignalLevel, OutOfRangeSfUsesTheMostPermissiveFloor) {
  EXPECT_FLOAT_EQ(demodFloorDb(0), -20.0f);
  EXPECT_FLOAT_EQ(demodFloorDb(4), -20.0f);
  EXPECT_FLOAT_EQ(demodFloorDb(13), -20.0f);
}

TEST(SignalLevel, BarThresholdsAtSf7) {
  // Margin = SNR - (-7.5).
  EXPECT_EQ(signalBars(-7.6f, 7), 0);  // margin -0.1
  EXPECT_EQ(signalBars(-7.5f, 7), 1);  // margin 0: the spec counts 0-3 as one bar
  EXPECT_EQ(signalBars(-4.6f, 7), 1);  // margin 2.9
  EXPECT_EQ(signalBars(-4.5f, 7), 2);  // margin 3
  EXPECT_EQ(signalBars(-0.6f, 7), 2);  // margin 6.9
  EXPECT_EQ(signalBars(-0.5f, 7), 3);  // margin 7
  EXPECT_EQ(signalBars(4.4f, 7), 3);   // margin 11.9
  EXPECT_EQ(signalBars(4.5f, 7), 4);   // margin 12
  EXPECT_EQ(signalBars(30.0f, 7), 4);
}

TEST(SignalLevel, SameSnrReadsDifferentlyBySf) {
  EXPECT_EQ(signalBars(-8.0f, 7), 0);   // below the SF7 floor
  EXPECT_EQ(signalBars(-8.0f, 12), 4);  // 12 dB above the SF12 floor
}

TEST(SignalLevel, RolePerBarCount) {
  EXPECT_EQ(signalRole(0), SignalRole::Bad);
  EXPECT_EQ(signalRole(1), SignalRole::Bad);
  EXPECT_EQ(signalRole(2), SignalRole::Warn);
  EXPECT_EQ(signalRole(3), SignalRole::Ok);
  EXPECT_EQ(signalRole(4), SignalRole::Ok);
}

// --------------------------------------------------------------------- themes

namespace {

double channel(uint32_t rgb, int shift) {
  double c = ((rgb >> shift) & 0xFF) / 255.0;
  return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(uint32_t rgb) {
  return 0.2126 * channel(rgb, 16) + 0.7152 * channel(rgb, 8) + 0.0722 * channel(rgb, 0);
}

double contrast(uint32_t a, uint32_t b) {
  double la = luminance(a), lb = luminance(b);
  if (la < lb) std::swap(la, lb);
  return (la + 0.05) / (lb + 0.05);
}

constexpr uint32_t kSomeAccent = 0x4EA8F2;

}  // namespace

TEST(GuiTheme, DefaultIsOffband) {
  EXPECT_EQ(kDefaultTheme, ThemeId::Offband);
}

TEST(GuiTheme, OffbandMatchesTheDesign) {
  ThemeTokens t = themeTokens(ThemeId::Offband, kSomeAccent);
  EXPECT_EQ(t.bg, 0x0F1412u);
  EXPECT_EQ(t.bg2, 0x161C19u);
  EXPECT_EQ(t.bg3, 0x1D2521u);
  EXPECT_EQ(t.line, 0x34403Au);
  EXPECT_EQ(t.line2, 0x232C28u);
  EXPECT_EQ(t.ink, 0xEFF3E8u);
  EXPECT_EQ(t.ink2, 0xBAC4B5u);
  EXPECT_EQ(t.dim, 0x7C8B82u);
  EXPECT_EQ(t.sig, 0x7BEFA8u);
  EXPECT_EQ(t.sigInk, 0x0F1412u);
  EXPECT_EQ(t.wash, 0x177BEFA8u);  // rgba(123,239,168,.09)
  EXPECT_EQ(t.warn, 0xFFA552u);
  EXPECT_EQ(t.ok, 0x7BEFA8u);
  EXPECT_EQ(t.bad, 0xFF7A6Eu);
  EXPECT_EQ(t.nodeType[NodeCompanion], 0x8CC8FFu);
  EXPECT_EQ(t.nodeType[NodeRepeater], 0xC3A5FFu);
  EXPECT_EQ(t.nodeType[NodeRoom], 0xF2D27Au);
  EXPECT_EQ(t.nodeType[NodeSensor], 0xFF9EC4u);
  EXPECT_FALSE(t.flat);
}

TEST(GuiTheme, SteelMatchesTheDesign) {
  ThemeTokens t = themeTokens(ThemeId::Steel, kSomeAccent);
  EXPECT_EQ(t.bg, 0x1D2D3Du);
  EXPECT_EQ(t.bg2, 0x22364Au);
  EXPECT_EQ(t.bg3, 0x2C455Du);
  EXPECT_EQ(t.line, 0x416180u);
  EXPECT_EQ(t.line2, 0x2C455Du);
  EXPECT_EQ(t.ink, 0xF5F5F8u);
  EXPECT_EQ(t.ink2, 0xB5D9FDu);
  EXPECT_EQ(t.dim, 0x94BCE3u);
  EXPECT_EQ(t.sig, 0x94BCE3u);
  EXPECT_EQ(t.sigInk, 0x1D2D3Du);
  EXPECT_EQ(t.wash, 0x1F94BCE3u);  // rgba(148,188,227,.12)
  EXPECT_EQ(t.warn, 0xFFA552u);
  EXPECT_EQ(t.ok, 0x7BEFA8u);
  EXPECT_EQ(t.bad, 0xFF8A80u);
  EXPECT_EQ(t.nodeType[NodeCompanion], 0x8CC8FFu);
  EXPECT_EQ(t.nodeType[NodeRepeater], 0xC3A5FFu);
  EXPECT_EQ(t.nodeType[NodeRoom], 0xF2D27Au);
  EXPECT_EQ(t.nodeType[NodeSensor], 0xFF9EC4u);
  EXPECT_FALSE(t.flat);
}

TEST(GuiTheme, MonoHcIsFlatBlackAndWhite) {
  ThemeTokens t = themeTokens(ThemeId::MonoHC, kSomeAccent);
  EXPECT_EQ(t.bg, 0x000000u);
  EXPECT_EQ(t.bg2, 0x000000u);
  EXPECT_EQ(t.bg3, 0x1A1A1Au);
  EXPECT_EQ(t.line, 0xFFFFFFu);
  EXPECT_EQ(t.line2, 0x6B6B6Bu);
  EXPECT_EQ(t.ink, 0xFFFFFFu);
  EXPECT_EQ(t.ink2, 0xFFFFFFu);
  EXPECT_EQ(t.dim, 0xC8C8C8u);
  EXPECT_EQ(t.warn, 0xFFFFFFu);
  EXPECT_EQ(t.ok, 0xFFFFFFu);
  EXPECT_EQ(t.bad, 0xFFFFFFu);
  EXPECT_EQ(t.sig, 0xFFFFFFu);
  EXPECT_EQ(t.sigInk, 0x000000u);
  EXPECT_EQ(t.wash, 0xFF1A1A1Au);  // opaque: selection by fill inversion
  for (int i = 0; i < NodeTypeCount; i++) EXPECT_EQ(t.nodeType[i], 0xFFFFFFu) << "type " << i;
  EXPECT_TRUE(t.flat);
}

TEST(GuiTheme, AccentIsOffbandWithThePickedSignalColor) {
  ThemeTokens a = themeTokens(ThemeId::Accent, kSomeAccent);
  ThemeTokens o = themeTokens(ThemeId::Offband, kSomeAccent);
  EXPECT_EQ(a.sig, kSomeAccent);
  // The prototype keeps a fixed #F2C14E wash whatever color is picked (D1
  // gap 5). The wash is derived from the pick instead, at 10% opacity.
  EXPECT_EQ(a.wash, 0x1A000000u | kSomeAccent);
  EXPECT_EQ(a.bg, o.bg);
  EXPECT_EQ(a.ink, o.ink);
  EXPECT_EQ(a.line, o.line);
  EXPECT_FALSE(a.flat);
}

TEST(GuiTheme, OnlyAccentUsesTheAccentArgument) {
  for (ThemeId id : {ThemeId::Offband, ThemeId::Steel, ThemeId::MonoHC}) {
    ThemeTokens x = themeTokens(id, 0x112233);
    ThemeTokens y = themeTokens(id, 0xAABBCC);
    EXPECT_EQ(x.sig, y.sig) << "theme " << int(id);
    EXPECT_EQ(x.wash, y.wash) << "theme " << int(id);
  }
}

// 7:1 (WCAG AAA) is our engineering bar, not a number from the design: the
// panels are read outdoors, and Mono HC exists for sunlight. Every design
// theme clears it, so a token edit that drops below it is the regression.
TEST(GuiTheme, InkIsReadableOnEveryBackground) {
  for (ThemeId id : {ThemeId::Offband, ThemeId::Steel, ThemeId::MonoHC, ThemeId::Accent}) {
    ThemeTokens t = themeTokens(id, kSomeAccent);
    EXPECT_GE(contrast(t.ink, t.bg), 7.0) << "theme " << int(id);
    EXPECT_GE(contrast(t.ink, t.bg3), 7.0) << "theme " << int(id);
  }
}

TEST(GuiLook, ModernIsRoundedWithTailedBubbles) {
  EXPECT_EQ(kDefaultLook, Look::Modern);
  LookTokens m = lookTokens(Look::Modern);
  EXPECT_EQ(m.radius, 6);
  EXPECT_EQ(m.bubble_radius, 10);
  EXPECT_EQ(m.bubble_tail, 3);
  EXPECT_TRUE(m.pill_chips);
  EXPECT_FALSE(m.own_msg_outline);
}

TEST(GuiLook, ClassicIsSquareWithOutlinedOwnMessages) {
  LookTokens c = lookTokens(Look::Classic);
  EXPECT_EQ(c.radius, 0);
  EXPECT_EQ(c.bubble_radius, 0);
  EXPECT_EQ(c.bubble_tail, 0);
  EXPECT_FALSE(c.pill_chips);
  EXPECT_TRUE(c.own_msg_outline);
}

// ------------------------------------------------------------------ input map

// The USR button keeps stock MeshCore's 1.2 s long press (ui-new
// LONG_PRESS_MILLIS). The multi-click gap is 400 ms: stock's 280 ms was too
// tight on the bench (#1445).
TEST(InputMap, PressHoldAndLongHoldTiming) {
  EXPECT_EQ(classifyPress(0), PressKind::Press);
  EXPECT_EQ(classifyPress(1199), PressKind::Press);
  EXPECT_EQ(classifyPress(1200), PressKind::Hold);
  EXPECT_EQ(classifyPress(2999), PressKind::Hold);
  EXPECT_EQ(classifyPress(3000), PressKind::LongHold);
  EXPECT_EQ(kMultiClickMs, 400u);
}

namespace {
const InputEvent kAllEvents[] = {
    InputEvent::UsrClick,  InputEvent::UsrDoubleClick, InputEvent::UsrTripleClick,
    InputEvent::UsrHold,   InputEvent::UsrLongHold,    InputEvent::AuxPress,
    InputEvent::AuxHold,   InputEvent::AuxLongHold,    InputEvent::TouchTap,
    InputEvent::TouchSwipeRight, InputEvent::KeyTab,   InputEvent::KeyEnter,
    InputEvent::KeyEsc,    InputEvent::KeyAltDigit,    InputEvent::KeyOther,
};
bool isUsr(InputEvent ev) {
  return ev == InputEvent::UsrClick || ev == InputEvent::UsrDoubleClick ||
         ev == InputEvent::UsrTripleClick || ev == InputEvent::UsrHold ||
         ev == InputEvent::UsrLongHold;
}
}  // namespace

TEST(InputMap, UsrWorksLikeStockMeshCore) {
  EXPECT_EQ(mapInput(InputEvent::UsrClick, true, false), Action::Next);
  EXPECT_EQ(mapInput(InputEvent::UsrDoubleClick, true, false), Action::Prev);
  EXPECT_EQ(mapInput(InputEvent::UsrHold, true, false), Action::Open);
  EXPECT_EQ(mapInput(InputEvent::UsrTripleClick, true, false), Action::ToggleBuzzer);
  EXPECT_EQ(mapInput(InputEvent::UsrLongHold, true, false), Action::PocketLock);
}

TEST(InputMap, AuxScrollsUpAndGoesBack) {
  EXPECT_EQ(mapInput(InputEvent::AuxPress, true, false), Action::Prev);
  EXPECT_EQ(mapInput(InputEvent::AuxHold, true, false), Action::Back);
}

// On an AUX board USR doesn't group clicks, so AUX held 3 s takes the
// buzzer from USR's triple-click (owner, 2026-10-17, #1534).
TEST(InputMap, AuxLongHoldTogglesTheBuzzer) {
  EXPECT_EQ(mapInput(InputEvent::AuxLongHold, true, false), Action::ToggleBuzzer);
}

TEST(InputMap, TouchAndKeyboard) {
  EXPECT_EQ(mapInput(InputEvent::TouchTap, true, false), Action::Open);
  EXPECT_EQ(mapInput(InputEvent::TouchSwipeRight, true, false), Action::Back);
  EXPECT_EQ(mapInput(InputEvent::KeyTab, true, false), Action::Next);
  EXPECT_EQ(mapInput(InputEvent::KeyEnter, true, false), Action::Open);
  EXPECT_EQ(mapInput(InputEvent::KeyEsc, true, false), Action::Back);
  EXPECT_EQ(mapInput(InputEvent::KeyAltDigit, true, false), Action::Canned);
  EXPECT_EQ(mapInput(InputEvent::KeyOther, true, false), Action::None);
}

TEST(InputMap, ScreenOffAnyInputOnlyWakes) {
  for (InputEvent ev : kAllEvents) {
    EXPECT_EQ(mapInput(ev, false, false), Action::Wake) << "event " << int(ev);
  }
}

// Pocket lock: in a pocket, touch and the second button get pressed by
// accident, so only USR wakes the screen.
TEST(InputMap, PocketLockedOnlyUsrWakes) {
  for (InputEvent ev : kAllEvents) {
    EXPECT_EQ(mapInput(ev, false, true), isUsr(ev) ? Action::Wake : Action::None)
        << "event " << int(ev);
  }
}

// --------------------------------------------------------------- clock policy

TEST(ClockPolicy, FullSpeedOnlyWhileTheScreenIsOn) {
  EXPECT_EQ(cpuMhzFor(true), 240u);
  EXPECT_EQ(cpuMhzFor(false), 80u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
