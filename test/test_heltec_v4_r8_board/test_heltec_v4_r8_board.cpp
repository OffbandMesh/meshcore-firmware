// #1338: host tests for the Heltec V4-R8 board's runtime controls (Epic #1322).
//
// Builds the real variants/heltec_v4_r8/HeltecV4R8Board.cpp and LoRaFEMControl.cpp
// against test/mocks_esp32 (see [env:native_heltec_v4_r8]). Pin numbers come from
// the env's -D flags, which mirror variants/heltec_v4_r8/platformio.ini.
//
// The R8's FEM is a KCT8103L: CSD (GPIO2) enables the chip, CTX (GPIO5) selects
// LNA-in (LOW) or LNA-bypass (HIGH) in RX. TX drives CTX HIGH.

#include <gtest/gtest.h>
#include <cstring>
#include "HeltecV4R8Board.h"

namespace {

void resetMocks() {
  std::memset(g_mock_pin_level, 0, sizeof(g_mock_pin_level));
  std::memset(g_mock_pin_out, 0, sizeof(g_mock_pin_out));
  std::memset(g_mock_pin_mode, 0, sizeof(g_mock_pin_mode));
  std::memset(g_mock_rtc_hold, 0, sizeof(g_mock_rtc_hold));
  g_mock_ext1_wake_mask = 0;
  g_mock_timer_wake_us = 0;
  g_mock_deep_sleep_starts = 0;
  g_mock_esp32_poweroff_calls = 0;
  g_mock_reset_reason = ESP_RST_POWERON;
}

class R8Board : public ::testing::Test {
protected:
  HeltecV4R8Board board;
  void SetUp() override {
    resetMocks();
    board.begin();
  }
};

}  // namespace

// --- Vext: the R8 moved it to GPIO40, active LOW (dim-OLED report on #1322) ---

TEST_F(R8Board, VextIsDrivenActiveLowAtBoot) {
  EXPECT_EQ(OUTPUT, g_mock_pin_mode[PIN_VEXT_EN]);
  EXPECT_EQ(LOW, g_mock_pin_out[PIN_VEXT_EN]);
}

// --- FEM / LNA control (#1339) ---

TEST_F(R8Board, ReportsLnaControl) {
  EXPECT_TRUE(board.canControlLoRaFemLna());
}

TEST_F(R8Board, EnablingLnaDrivesCtxLowInRx) {
  EXPECT_TRUE(board.setLoRaFemLnaEnabled(true));
  EXPECT_TRUE(board.isLoRaFemLnaEnabled());
  EXPECT_EQ(HIGH, g_mock_pin_out[P_LORA_KCT8103L_PA_CSD]);
  EXPECT_EQ(LOW, g_mock_pin_out[P_LORA_KCT8103L_PA_CTX]);
}

TEST_F(R8Board, DisablingLnaBypassesIt) {
  board.setLoRaFemLnaEnabled(true);
  EXPECT_TRUE(board.setLoRaFemLnaEnabled(false));
  EXPECT_FALSE(board.isLoRaFemLnaEnabled());
  EXPECT_EQ(HIGH, g_mock_pin_out[P_LORA_KCT8103L_PA_CTX]);
}

TEST_F(R8Board, TransmitThenReceiveRestoresTheLnaSetting) {
  board.setLoRaFemLnaEnabled(true);
  board.onBeforeTransmit();
  EXPECT_EQ(HIGH, g_mock_pin_out[P_LORA_KCT8103L_PA_CTX]);
  board.onAfterTransmit();
  EXPECT_EQ(LOW, g_mock_pin_out[P_LORA_KCT8103L_PA_CTX]);
}

// TX is asynchronous, so a `fem` command can land mid-packet. The FEM must stay
// in its TX state; the new setting takes effect at the TX->RX turnaround.
TEST_F(R8Board, LnaChangeDuringTransmitWaitsForReceive) {
  board.setLoRaFemLnaEnabled(false);
  board.onBeforeTransmit();
  EXPECT_TRUE(board.setLoRaFemLnaEnabled(true));
  EXPECT_TRUE(board.isLoRaFemLnaEnabled());
  EXPECT_EQ(HIGH, g_mock_pin_out[P_LORA_KCT8103L_PA_CTX]);
  board.onAfterTransmit();
  EXPECT_EQ(LOW, g_mock_pin_out[P_LORA_KCT8103L_PA_CTX]);
}

// --- TX LED control, #542 parity (#1340) ---
// [env:native_heltec_v4_r8] builds these with the LED; [env:native_heltec_v4_r8_gui]
// builds the R8 TFT GUI's configuration, where GPIO46 is the Expansion Kit V2's KEY
// button and P_LORA_TX_LED is undefined (#1676).

// The GUI env names itself, so a drifted copy of its flags fails to build here
// rather than quietly running the LED tests instead.
#ifdef OFFBAND_TEST_R8_GUI_CONFIG
  #if defined(P_LORA_TX_LED)
    #error "R8 GUI config: GPIO46 is the KEY button, P_LORA_TX_LED must be undefined"
  #endif
  #if !defined(PIN_TOUCH_RST) || PIN_TOUCH_RST >= 0
    #error "R8 GUI config: the panel owns the GPIO21 reset, PIN_TOUCH_RST must be -1"
  #endif
#endif

#ifdef P_LORA_TX_LED
TEST_F(R8Board, ReportsLedControl) {
  EXPECT_TRUE(board.canControlLed());
}

TEST_F(R8Board, LedOffKeepsTheTxLedDark) {
  EXPECT_TRUE(board.setLedEnabled(false));
  EXPECT_FALSE(board.isLedEnabled());
  board.onBeforeTransmit();
  EXPECT_EQ(LOW, g_mock_pin_out[P_LORA_TX_LED]);
}

TEST_F(R8Board, LedOnFlashesOnTransmit) {
  board.setLedEnabled(false);
  EXPECT_TRUE(board.setLedEnabled(true));
  board.onBeforeTransmit();
  EXPECT_EQ(HIGH, g_mock_pin_out[P_LORA_TX_LED]);
  board.onAfterTransmit();
  EXPECT_EQ(LOW, g_mock_pin_out[P_LORA_TX_LED]);
}
#else
// GPIO46 belongs to the KEY button here, read by the GUI as an input.
constexpr int kKeyPin = 46;

TEST_F(R8Board, ReportsNoLedControlWithoutTheLed) {
  EXPECT_FALSE(board.canControlLed());
  EXPECT_FALSE(board.setLedEnabled(true));
}

TEST_F(R8Board, TransmitNeverDrivesTheKeyPin) {
  board.onBeforeTransmit();
  board.onAfterTransmit();
  board.setLedEnabled(false);
  EXPECT_NE(OUTPUT, g_mock_pin_mode[kKeyPin]);
  EXPECT_EQ(0, g_mock_pin_out[kKeyPin]);
}
#endif

// --- the shared panel/touch reset, GPIO21 (Expansion Board V2.03, #1675) ---

#if defined(PIN_TOUCH_RST) && PIN_TOUCH_RST < 0
// The GUI env gives the pulse to the panel driver; the board must not drive 21,
// or a later touch start would reset the panel after it's up.
TEST_F(R8Board, BoardLeavesTheSharedResetToThePanel) {
  EXPECT_NE(OUTPUT, g_mock_pin_mode[21]);
}
#endif

// --- powerOff cuts FEM power, matching the R2 (#1341) ---

TEST_F(R8Board, PowerOffCutsAndHoldsFemPower) {
  board.powerOff();
  EXPECT_EQ(LOW, g_mock_pin_out[P_LORA_PA_POWER]);
  EXPECT_TRUE(g_mock_rtc_hold[P_LORA_PA_POWER]);
}

// begin() claims Vext for the board; powerOff() must give that claim back so the
// OLED and antenna boost rail are switched off (GPIO40 HIGH = inactive).
TEST_F(R8Board, PowerOffReleasesVext) {
  board.powerOff();
  EXPECT_EQ(HIGH, g_mock_pin_out[PIN_VEXT_EN]);
}

TEST_F(R8Board, PowerOffDoesNotArmLoRaWake) {
  board.powerOff();
  EXPECT_EQ(0u, g_mock_ext1_wake_mask);
  EXPECT_EQ(1, g_mock_esp32_poweroff_calls);
}

// --- Duty-cycle sleep keeps listening (unchanged behavior) ---

TEST_F(R8Board, TimedSleepKeepsLoRaWakeArmed) {
  board.enterDeepSleep(60);
  EXPECT_NE(0u, g_mock_ext1_wake_mask & (1ULL << P_LORA_DIO_1));
  EXPECT_EQ(60000000ULL, g_mock_timer_wake_us);
  EXPECT_EQ(1, g_mock_deep_sleep_starts);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
