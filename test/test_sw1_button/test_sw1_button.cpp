#include <gtest/gtest.h>
#include <vector>
#include <Arduino.h>
#include <helpers/ui/MomentaryButton.h>

// SW1, the QCC badge's user button: P1.00, pulled up to VCC by 10 k and switched to GND
// (badge schematic), so it reads LOW while pressed. variants/qcc_badge/target.cpp builds
// it as MomentaryButton(PIN_USER_BTN, 1000, true, true): a 1 s long press, active-low,
// internal pull-up on. These tests run that exact configuration. The UI then maps click
// to next page, double-click to previous page, long press to the page's action and
// triple-click to the buzzer toggle.
namespace {

constexpr uint8_t kSw1 = 6;   // PIN_USER_BTN

struct Sw1 {
  MomentaryButton btn{kSw1, 1000, true, true};
  std::vector<int> events;

  Sw1() {
    g_mock_millis = 10000;
    g_mock_pin_level[kSw1] = HIGH;   // released: the pull-up holds the line high
    btn.begin();
  }
  // Advance time in 10 ms loop passes, collecting whatever check() reports.
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
      g_mock_millis += 10;
      const int e = btn.check();
      if (e != BUTTON_EVENT_NONE) events.push_back(e);
    }
  }
  void hold(uint32_t ms) {
    g_mock_pin_level[kSw1] = LOW;
    run(ms);
    g_mock_pin_level[kSw1] = HIGH;
  }
};

using Events = std::vector<int>;

}  // namespace

TEST(Sw1Button, BeginTurnsOnThePullUp) {
  Sw1 sw;
  EXPECT_EQ(INPUT_PULLUP, g_mock_pin_mode[kSw1]);
}

TEST(Sw1Button, ReadsPressedWhenTheLineIsLow) {
  Sw1 sw;
  g_mock_pin_level[kSw1] = HIGH;
  EXPECT_FALSE(sw.btn.isPressed());
  g_mock_pin_level[kSw1] = LOW;
  EXPECT_TRUE(sw.btn.isPressed());
}

TEST(Sw1Button, IdleIsSilent) {
  Sw1 sw;
  sw.run(5000);
  EXPECT_EQ(Events{}, sw.events);
}

TEST(Sw1Button, AShortPressIsAClickOnceTheMultiClickWindowCloses) {
  Sw1 sw;
  sw.hold(100);
  sw.run(200);                                  // still inside the 280 ms window
  EXPECT_EQ(Events{}, sw.events);
  sw.run(200);
  EXPECT_EQ(Events{BUTTON_EVENT_CLICK}, sw.events);
}

TEST(Sw1Button, TwoQuickPressesAreADoubleClick) {
  Sw1 sw;
  sw.hold(80);
  sw.run(100);
  sw.hold(80);
  sw.run(500);
  EXPECT_EQ(Events{BUTTON_EVENT_DOUBLE_CLICK}, sw.events);
}

TEST(Sw1Button, ThreeQuickPressesAreATripleClick) {
  Sw1 sw;
  sw.hold(80);
  sw.run(100);
  sw.hold(80);
  sw.run(100);
  sw.hold(80);
  sw.run(500);
  EXPECT_EQ(Events{BUTTON_EVENT_TRIPLE_CLICK}, sw.events);
}

TEST(Sw1Button, HoldingOneSecondIsALongPressWhileStillHeld) {
  Sw1 sw;
  g_mock_pin_level[kSw1] = LOW;
  sw.run(990);
  EXPECT_EQ(Events{}, sw.events);               // not yet
  sw.run(30);
  EXPECT_EQ(Events{BUTTON_EVENT_LONG_PRESS}, sw.events);
  g_mock_pin_level[kSw1] = HIGH;                // releasing adds no click
  sw.run(500);
  EXPECT_EQ(Events{BUTTON_EVENT_LONG_PRESS}, sw.events);
}

TEST(Sw1Button, AlmostOneSecondIsStillAClick) {
  Sw1 sw;
  sw.hold(900);
  sw.run(500);
  EXPECT_EQ(Events{BUTTON_EVENT_CLICK}, sw.events);
}

TEST(Sw1Button, EachEventHasAShortLabel) {
  // Shown on the diag key-test screen (#1207).
  EXPECT_STREQ("-", buttonEventName(BUTTON_EVENT_NONE));
  EXPECT_STREQ("click", buttonEventName(BUTTON_EVENT_CLICK));
  EXPECT_STREQ("double", buttonEventName(BUTTON_EVENT_DOUBLE_CLICK));
  EXPECT_STREQ("triple", buttonEventName(BUTTON_EVENT_TRIPLE_CLICK));
  EXPECT_STREQ("long", buttonEventName(BUTTON_EVENT_LONG_PRESS));
  EXPECT_STREQ("-", buttonEventName(99));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
