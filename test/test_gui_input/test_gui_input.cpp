// #1443: button timing and the screen's on/dim/off states. These decide
// whether a press is a click, hold or pocket lock, and whether the screen stays lit while
// someone is reading it.

#include <gtest/gtest.h>

#include "helpers/gui/InputTiming.h"
#include "helpers/gui/KeyInput.h"

using namespace offband::gui;

namespace {

// Holds the button down from `t0` for `held` ms, polling every 5 ms, and
// returns the kind reported on release (or fails if none or several).
PressKind press(ButtonTracker& b, uint32_t t0, uint32_t held) {
  PressKind kind = PressKind::Press;
  int reports = 0;
  for (uint32_t t = t0; t < t0 + held; t += 5) {
    if (b.update(true, t, &kind)) reports++;
  }
  for (uint32_t t = t0 + held; t < t0 + held + 60; t += 5) {
    if (b.update(false, t, &kind)) reports++;
  }
  EXPECT_EQ(reports, 1);
  return kind;
}

}  // namespace

TEST(ButtonTracker, ClassifiesByHowLongItWasHeld) {
  ButtonTracker b;
  EXPECT_EQ(press(b, 1000, 100), PressKind::Press);
  EXPECT_EQ(press(b, 2000, 1300), PressKind::Hold);
  EXPECT_EQ(press(b, 4000, 3100), PressKind::LongHold);
}

// Clicks are counted until kMultiClickMs passes with no further click, then
// reported once as 1, 2 or 3.
TEST(ClickCounter, SingleClickReportsAfterTheWindow) {
  ClickCounter c;
  c.click(1000);
  EXPECT_EQ(c.poll(1000 + kMultiClickMs - 1, false), 0);
  EXPECT_EQ(c.poll(1000 + kMultiClickMs, false), 1);
  EXPECT_EQ(c.poll(5000, false), 0);  // reported once
}

TEST(ClickCounter, ClicksInsideTheWindowAddUp) {
  ClickCounter c;
  c.click(1000);
  c.click(1200);
  EXPECT_EQ(c.poll(1200 + kMultiClickMs - 1, false), 0);
  EXPECT_EQ(c.poll(1200 + kMultiClickMs, false), 2);
  c.click(3000);
  c.click(3200);
  c.click(3400);
  EXPECT_EQ(c.poll(3400 + kMultiClickMs, false), 3);
}

// The bench found double-clicks needed to be very fast (#1445): the window
// must not close while the next click is being held down.
TEST(ClickCounter, WindowStaysOpenWhileTheNextClickIsHeld) {
  ClickCounter c;
  c.click(1000);
  // Pressed again at 1300, held 250 ms: past the window, but still down.
  EXPECT_EQ(c.poll(1300, true), 0);
  EXPECT_EQ(c.poll(1549, true), 0);
  c.click(1550);
  EXPECT_EQ(c.poll(1550 + kMultiClickMs, false), 2);
}

// A click then a hold inside the window: the click is taken first, so the
// two actions run in the order they were pressed (Gemini, #1445).
TEST(ClickCounter, TakeHandsOverPendingClicksAtOnce) {
  ClickCounter c;
  c.click(1000);
  EXPECT_EQ(c.take(), 1);
  EXPECT_EQ(c.poll(1000 + kMultiClickMs, false), 0);  // not reported again
  EXPECT_EQ(c.take(), 0);
}

TEST(ClickCounter, MoreThanThreeIsATripleClick) {
  ClickCounter c;
  for (uint32_t t = 1000; t < 1500; t += 100) c.click(t);
  EXPECT_EQ(c.poll(1400 + kMultiClickMs, false), 3);
}

TEST(ClickCounter, ClicksOutsideTheWindowAreSeparate) {
  ClickCounter c;
  c.click(1000);
  EXPECT_EQ(c.poll(1000 + kMultiClickMs, false), 1);
  c.click(1000 + kMultiClickMs + 10);
  EXPECT_EQ(c.poll(1000 + 2 * kMultiClickMs + 10, false), 1);
}

TEST(ButtonTracker, PressAndHoldReportOnlyOnRelease) {
  ButtonTracker b;
  PressKind kind;
  for (uint32_t t = 0; t < 2900; t += 5) EXPECT_FALSE(b.update(true, t, &kind));
}

// The pocket lock must fire at 3 s while still held, not when let go.
TEST(ButtonTracker, LongHoldFiresAtThreeSecondsWhileHeld) {
  ButtonTracker b;
  PressKind kind = PressKind::Press;
  uint32_t fired_at = 0;
  for (uint32_t t = 0; t < 5000; t += 5) {
    if (b.update(true, t, &kind)) {
      EXPECT_EQ(fired_at, 0u) << "reported twice";
      fired_at = t;
    }
  }
  EXPECT_EQ(kind, PressKind::LongHold);
  EXPECT_GE(fired_at, kLongHoldMs);
  EXPECT_LE(fired_at, kLongHoldMs + kDebounceMs + 5);
  for (uint32_t t = 5000; t < 5100; t += 5) EXPECT_FALSE(b.update(false, t, &kind));
}

TEST(ButtonTracker, ContactBounceShorterThanDebounceIsIgnored) {
  ButtonTracker b;
  PressKind kind;
  int reports = 0;
  // 10 ms blips: never stable for 20 ms, so never a press.
  for (uint32_t t = 0; t < 200; t += 10) {
    if (b.update((t / 10) % 2 == 0, t, &kind)) reports++;
  }
  for (uint32_t t = 200; t < 300; t += 5) {
    if (b.update(false, t, &kind)) reports++;
  }
  EXPECT_EQ(reports, 0);
}

TEST(ScreenTimer, OnThenDimThenOff) {
  EXPECT_EQ(screenStateFor(0, 30000, 120000, false), ScreenState::On);
  EXPECT_EQ(screenStateFor(29999, 30000, 120000, false), ScreenState::On);
  EXPECT_EQ(screenStateFor(30000, 30000, 120000, false), ScreenState::Dim);
  EXPECT_EQ(screenStateFor(120000, 30000, 120000, false), ScreenState::Off);
}

TEST(ScreenTimer, ZeroDisablesThatStep) {
  EXPECT_EQ(screenStateFor(500000, 0, 120000, false), ScreenState::Off);
  EXPECT_EQ(screenStateFor(500000, 30000, 0, false), ScreenState::Dim);
  EXPECT_EQ(screenStateFor(500000, 0, 0, false), ScreenState::On);
}

TEST(ScreenTimer, AlwaysOnWins) {
  EXPECT_EQ(screenStateFor(10000000, 30000, 120000, true), ScreenState::On);
}

// ------------------------------------------- T-Deck keyboard (J4 #1727)

TEST(TDeckKeyboard, PrintableAsciiIsACharacter) {
  for (uint8_t b : {uint8_t(' '), uint8_t('a'), uint8_t('Z'), uint8_t('1'), uint8_t('~'), uint8_t('$')}) {
    EXPECT_EQ(KeyKind::Char, classifyKey(b)) << int(b);
  }
}

TEST(TDeckKeyboard, EnterAndBackspace) {
  EXPECT_EQ(KeyKind::Enter, classifyKey(0x0D));
  EXPECT_EQ(KeyKind::Enter, classifyKey(0x0A));
  EXPECT_EQ(KeyKind::Backspace, classifyKey(0x08));
  EXPECT_EQ(KeyKind::Backspace, classifyKey(0x7F));
}

// 0 is "no key"; Alt+C sends 0x0C and Shift on a symbol a control code; a
// byte above ASCII is a glitch. None of them may reach a draft.
TEST(TDeckKeyboard, NoKeyControlCodesAndHighBytesAreIgnored) {
  for (uint8_t b : {uint8_t(0x00), uint8_t(0x0C), uint8_t(0x03), uint8_t(0x1B), uint8_t(0x1F),
                    uint8_t(0x80), uint8_t(0xFF)}) {
    EXPECT_EQ(KeyKind::None, classifyKey(b)) << int(b);
  }
}

TEST(TDeckKeyboard, DigitsOneToFourPickTheCannedReplies) {
  EXPECT_EQ(0, cannedSlotForKey('1'));
  EXPECT_EQ(3, cannedSlotForKey('4'));
}

TEST(TDeckKeyboard, OtherKeysPickNoCannedReply) {
  for (uint8_t b : {uint8_t('0'), uint8_t('5'), uint8_t('9'), uint8_t('a'), uint8_t(' '), uint8_t(0x0D),
                    uint8_t(0)}) {
    EXPECT_EQ(-1, cannedSlotForKey(b)) << int(b);
  }
}

// ------------------------------------------- T-Deck trackball (J4 #1727)

TEST(Trackball, OnePulseAStepByDefault) {
  uint8_t carry = 0;
  EXPECT_EQ(0, trackballSteps(0, 1, &carry));
  EXPECT_EQ(1, trackballSteps(1, 1, &carry));
  EXPECT_EQ(2, trackballSteps(2, 1, &carry));
  EXPECT_EQ(0, carry);
}

TEST(Trackball, APartialStepCarriesOver) {
  uint8_t carry = 0;
  EXPECT_EQ(0, trackballSteps(1, 2, &carry));
  EXPECT_EQ(1, carry);
  EXPECT_EQ(1, trackballSteps(1, 2, &carry));
  EXPECT_EQ(0, carry);
  EXPECT_EQ(1, trackballSteps(3, 2, &carry));
  EXPECT_EQ(1, carry);
}

TEST(Trackball, AHardSpinIsCappedAndLeavesNothingBehind) {
  uint8_t carry = 0;
  EXPECT_EQ(kTrackballMaxSteps, trackballSteps(40, 1, &carry));
  EXPECT_EQ(0, carry);
  EXPECT_EQ(0, trackballSteps(0, 1, &carry));
  EXPECT_EQ(kTrackballMaxSteps, trackballSteps(1000000, 2, &carry));
}

TEST(Trackball, BadArgumentsMoveNothingOrActAsOne) {
  EXPECT_EQ(0, trackballSteps(5, 1, nullptr));
  uint8_t carry = 0;
  EXPECT_EQ(2, trackballSteps(2, 0, &carry));   // 0 pulses a step reads as 1
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
