// #2149 / #2150: the T-Deck Pro keyboard's pure layer. A wrong key-number
// formula shifts every key by one; a lost release with no reset leaves
// Backspace repeating on its own; a repeat that reaches Back walks the user
// out of the screen they were typing in.

#include <gtest/gtest.h>
#include <string.h>

#include <string>

#include "helpers/gui/KeyInput.h"
#include "helpers/gui/TdeckProKeys.h"

using namespace offband::gui;

namespace {

// The chip position of the key `phys_col` from the left in `row`.
uint8_t chipCol(uint8_t phys_col) { return uint8_t(kProKeyCols - 1 - phys_col); }

// Presses and releases the key, returning what it typed (0 for nothing).
uint8_t tap(ProKeyState& s, uint8_t row, uint8_t phys_col, uint32_t now = 0) {
  ProKeyOut out{0, false};
  const bool typed = s.event(row, chipCol(phys_col), true, now, &out);
  ProKeyOut ignored;
  EXPECT_FALSE(s.event(row, chipCol(phys_col), false, now + 1, &ignored)) << "a release types nothing";
  return typed ? out.byte : 0;
}

void press(ProKeyState& s, uint8_t row, uint8_t phys_col, uint32_t now = 0) {
  ProKeyOut out;
  s.event(row, chipCol(phys_col), true, now, &out);
}
void release(ProKeyState& s, uint8_t row, uint8_t phys_col, uint32_t now = 0) {
  ProKeyOut out;
  s.event(row, chipCol(phys_col), false, now, &out);
}

// Physical positions, left to right as the keycaps read.
constexpr uint8_t kAltRow = 2, kAltCol = 0;
constexpr uint8_t kLShiftRow = 3, kLShiftCol = 5;
constexpr uint8_t kMicCol = 6, kSpaceCol = 7, kSymCol = 8, kRShiftCol = 9;
constexpr uint8_t kBspRow = 1, kBspCol = 9;
constexpr uint8_t kEnterRow = 2, kEnterCol = 9;

const char* const kLetterRows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};

}  // namespace

// ---------------------------------------------------------------- decode --

TEST(Tca8418Decode, KeyNumberIsRowTimesTenPlusColumnPlusOne) {
  uint8_t r = 9, c = 9;
  bool p = false;
  ASSERT_TRUE(tca8418Decode(0x81, &r, &c, &p));   // key 1, pressed
  EXPECT_EQ(r, 0);
  EXPECT_EQ(c, 0);
  EXPECT_TRUE(p);
  ASSERT_TRUE(tca8418Decode(0x17, &r, &c, &p));   // key 23, released (SCPS215G Table 5)
  EXPECT_EQ(r, 2);
  EXPECT_EQ(c, 2);
  EXPECT_FALSE(p);
  ASSERT_TRUE(tca8418Decode(0x80 | 40, &r, &c, &p));   // the last key of a 4x10 matrix
  EXPECT_EQ(r, 3);
  EXPECT_EQ(c, 9);
}

TEST(Tca8418Decode, RejectsEmptyGpiAndOutsideTheMatrix) {
  uint8_t r = 7, c = 7;
  bool p = true;
  EXPECT_FALSE(tca8418Decode(0x00, &r, &c, &p)) << "an empty FIFO reads 0";
  EXPECT_FALSE(tca8418Decode(0x80, &r, &c, &p)) << "key 0 does not exist";
  EXPECT_FALSE(tca8418Decode(0x80 | 41, &r, &c, &p)) << "row 4 is not wired";
  EXPECT_FALSE(tca8418Decode(0x80 | 80, &r, &c, &p));
  EXPECT_FALSE(tca8418Decode(0x80 | 97, &r, &c, &p)) << "a GPI event is not a key";
  EXPECT_FALSE(tca8418Decode(0x7F, &r, &c, &p));
  EXPECT_EQ(r, 7) << "a rejected byte leaves the outputs alone";
  EXPECT_EQ(c, 7);
  EXPECT_TRUE(p);
}

TEST(Tca8418Matrix, FourByTenSelectsRowsZeroToThreeAndAllTenColumns) {
  const MatrixSelect m = tca8418MatrixSelect(4, 10);
  EXPECT_EQ(m.rows, 0x0F);
  EXPECT_EQ(m.cols_lo, 0xFF);
  EXPECT_EQ(m.cols_hi, 0x03);
}

TEST(Tca8418Matrix, MatchesTheDatasheetExampleAndClamps) {
  const MatrixSelect m = tca8418MatrixSelect(4, 3);   // SCPS215G 9.2.2.2: 0x0F, 0x07, 0x00
  EXPECT_EQ(m.rows, 0x0F);
  EXPECT_EQ(m.cols_lo, 0x07);
  EXPECT_EQ(m.cols_hi, 0x00);
  const MatrixSelect big = tca8418MatrixSelect(12, 14);
  EXPECT_EQ(big.rows, 0xFF);
  EXPECT_EQ(big.cols_lo, 0xFF);
  EXPECT_EQ(big.cols_hi, 0x03);
}

TEST(Tca8418Cfg, OverflowFlagNeedsBothOverflowBits) {
  // The erratum (SCPS215G 8.6.4): with only OVR_FLOW_IEN the flag never sets.
  EXPECT_TRUE(tca8418::kCfgValue & tca8418::kCfgOvrFlowIen);
  EXPECT_TRUE(tca8418::kCfgValue & tca8418::kCfgOvrFlowM);
  EXPECT_TRUE(tca8418::kCfgValue & tca8418::kCfgKeIen);
  EXPECT_FALSE(tca8418::kCfgValue & 0x80) << "no auto-increment: KEY_EVENT_A is read one byte at a time";
}

// ------------------------------------------------------------------- map --

TEST(ProKeyMap, LetterRowsReadLeftToRightAcrossTheKeycaps) {
  for (uint8_t row = 0; row < 3; row++) {
    const char* letters = kLetterRows[row];
    const uint8_t first = row == 2 ? 1 : 0;   // Alt is left of z
    for (uint8_t i = 0; letters[i]; i++) {
      const ProKeyCap cap = proKeyAt(row, chipCol(uint8_t(first + i)));
      EXPECT_EQ(cap.kind, ProKey::Char) << "row " << int(row) << " key " << int(i);
      EXPECT_EQ(cap.plain, letters[i]) << "row " << int(row) << " key " << int(i);
    }
  }
}

TEST(ProKeyMap, EveryLetterOnceAndNoOtherCharKeyTypesALetter) {
  int seen[26] = {};
  for (uint8_t r = 0; r < kProKeyRows; r++) {
    for (uint8_t c = 0; c < kProKeyCols; c++) {
      const ProKeyCap cap = proKeyAt(r, c);
      if (cap.kind == ProKey::Char && cap.plain >= 'a' && cap.plain <= 'z') seen[cap.plain - 'a']++;
    }
  }
  for (int i = 0; i < 26; i++) EXPECT_EQ(seen[i], 1) << char('a' + i);
}

TEST(ProKeyMap, TheNonLetterKeysSitWhereTheKeycapsAre) {
  EXPECT_EQ(proKeyAt(kBspRow, chipCol(kBspCol)).kind, ProKey::Backspace);
  EXPECT_EQ(proKeyAt(kEnterRow, chipCol(kEnterCol)).kind, ProKey::Enter);
  EXPECT_EQ(proKeyAt(kAltRow, chipCol(kAltCol)).kind, ProKey::Alt);
  EXPECT_EQ(proKeyAt(2, chipCol(8)).plain, '$');
  EXPECT_EQ(proKeyAt(3, chipCol(kLShiftCol)).kind, ProKey::Shift);
  EXPECT_EQ(proKeyAt(3, chipCol(kMicCol)).kind, ProKey::Mic);
  EXPECT_EQ(proKeyAt(3, chipCol(kSpaceCol)).kind, ProKey::Space);
  EXPECT_EQ(proKeyAt(3, chipCol(kSymCol)).kind, ProKey::Sym);
  EXPECT_EQ(proKeyAt(3, chipCol(kRShiftCol)).kind, ProKey::Shift);
}

TEST(ProKeyMap, NoSwitchLeftOfTheBottomRowOrOutsideTheMatrix) {
  for (uint8_t phys = 0; phys < 5; phys++) EXPECT_EQ(proKeyAt(3, chipCol(phys)).kind, ProKey::None) << int(phys);
  EXPECT_EQ(proKeyAt(4, 0).kind, ProKey::None);
  EXPECT_EQ(proKeyAt(0, 10).kind, ProKey::None);
  EXPECT_EQ(proKeyAt(255, 255).kind, ProKey::None);
}

TEST(ProKeyMap, PhysicalColumnReversesTheChipColumn) {
  EXPECT_EQ(proKeyPhysicalCol(0), 9);
  EXPECT_EQ(proKeyPhysicalCol(9), 0);
}

// ------------------------------------------------------------- modifiers --

TEST(ProKeyState, PlainKeysTypeTheirLetterSpaceEnterAndBackspace) {
  ProKeyState s;
  EXPECT_EQ(tap(s, 0, 0), 'q');
  EXPECT_EQ(tap(s, 1, 8), 'l');
  EXPECT_EQ(tap(s, 2, 8), '$');
  EXPECT_EQ(tap(s, 3, kSpaceCol), ' ');
  EXPECT_EQ(tap(s, kEnterRow, kEnterCol), 0x0D);
  EXPECT_EQ(tap(s, kBspRow, kBspCol), 0x08);
}

TEST(ProKeyState, EveryKeyUnderEveryModifierTypesSomethingClassifyKeyTakes) {
  for (int mod = 0; mod < 3; mod++) {   // none, Shift held, Alt held
    for (uint8_t r = 0; r < kProKeyRows; r++) {
      for (uint8_t c = 0; c < kProKeyCols; c++) {
        const ProKey k = proKeyAt(r, c).kind;
        if (k == ProKey::None || k == ProKey::Shift || k == ProKey::Alt || k == ProKey::Sym) continue;
        ProKeyState s;
        if (mod == 1) press(s, 3, kLShiftCol);
        if (mod == 2) press(s, kAltRow, kAltCol);
        ProKeyOut out{0, false};
        const bool typed = s.event(r, c, true, 0, &out);
        if (k == ProKey::Mic && mod != 2) {
          EXPECT_FALSE(typed) << "the microphone key types only under Alt";
          continue;
        }
        ASSERT_TRUE(typed) << "row " << int(r) << " col " << int(c) << " mod " << mod;
        EXPECT_NE(classifyKey(out.byte), KeyKind::None) << "row " << int(r) << " col " << int(c) << " mod " << mod;
        EXPECT_FALSE(out.repeat);
      }
    }
  }
}

TEST(ProKeyState, HeldShiftCapitalizesAndDoesNotLatch) {
  ProKeyState s;
  press(s, 3, kLShiftCol);
  EXPECT_EQ(tap(s, 0, 0), 'Q');
  EXPECT_EQ(tap(s, 0, 1), 'W');
  release(s, 3, kLShiftCol);
  EXPECT_EQ(tap(s, 0, 2), 'e') << "a Shift used while held leaves nothing behind";
}

TEST(ProKeyState, TappedShiftAppliesToTheNextKeyOnly) {
  ProKeyState s;
  EXPECT_EQ(tap(s, 3, kRShiftCol), 0) << "Shift types nothing";
  EXPECT_TRUE(s.shiftActive());
  EXPECT_EQ(tap(s, 1, 0), 'A');
  EXPECT_FALSE(s.shiftActive());
  EXPECT_EQ(tap(s, 1, 1), 's');
}

TEST(ProKeyState, SecondShiftTapCancels) {
  ProKeyState s;
  tap(s, 3, kLShiftCol);
  tap(s, 3, kLShiftCol);
  EXPECT_FALSE(s.shiftActive());
  EXPECT_EQ(tap(s, 1, 0), 'a');
}

TEST(ProKeyState, ShiftLeavesSymbolsAlone) {
  ProKeyState s;
  press(s, 3, kLShiftCol);
  EXPECT_EQ(tap(s, 2, 8), '$');
  EXPECT_EQ(tap(s, 3, kSpaceCol), ' ');
}

TEST(ProKeyState, AltGivesThePrintedSymbol) {
  ProKeyState s;
  press(s, kAltRow, kAltCol);
  EXPECT_EQ(tap(s, 0, 0), '#');
  EXPECT_EQ(tap(s, 0, 1), '1');
  EXPECT_EQ(tap(s, 0, 9), '@');
  EXPECT_EQ(tap(s, 2, 7), '.');
  EXPECT_EQ(tap(s, 2, 6), ',');
  EXPECT_EQ(tap(s, 3, kMicCol), '0');
  release(s, kAltRow, kAltCol);
  EXPECT_EQ(tap(s, 0, 0), 'q');
}

TEST(ProKeyState, SymReachesTheSameLayerAsAlt) {
  ProKeyState s;
  tap(s, 3, kSymCol);
  EXPECT_TRUE(s.altActive());
  EXPECT_EQ(tap(s, 1, 4), '/');
  EXPECT_FALSE(s.altActive()) << "a tapped Sym applies to one key";
}

TEST(ProKeyState, KeyWithoutASymbolTypesItsPlainValueUnderAlt) {
  ProKeyState s;
  press(s, kAltRow, kAltCol);
  EXPECT_EQ(tap(s, 2, 8), '$');
  EXPECT_EQ(tap(s, 3, kSpaceCol), ' ');
}

TEST(ProKeyState, AltWinsOverShift) {
  ProKeyState s;
  press(s, 3, kLShiftCol);
  press(s, kAltRow, kAltCol);
  EXPECT_EQ(tap(s, 0, 0), '#');
}

TEST(ProKeyState, AltAndSymHeldTogetherReleaseCleanly) {
  ProKeyState s;
  press(s, kAltRow, kAltCol);
  press(s, 3, kSymCol);
  release(s, kAltRow, kAltCol);
  EXPECT_TRUE(s.altActive()) << "Sym is still down";
  release(s, 3, kSymCol);
  EXPECT_TRUE(s.altActive()) << "neither was used: the pair counts as a tap";
  EXPECT_EQ(tap(s, 0, 0), '#');
  EXPECT_FALSE(s.altActive());
}

TEST(ProKeyState, BothShiftsHeldAndReleasedWithoutTypingLatchOnce) {
  // Gemini review (#2149): with one flag for both keys, letting go of the
  // first ended the hold and latched while the second was still down.
  ProKeyState s;
  press(s, 3, kLShiftCol);
  press(s, 3, kRShiftCol);
  release(s, 3, kLShiftCol);
  EXPECT_EQ(tap(s, 0, 0), 'Q') << "the right Shift is still down";
  release(s, 3, kRShiftCol);
  EXPECT_FALSE(s.shiftActive()) << "Shift was used while held: nothing latches";
  EXPECT_EQ(tap(s, 0, 0), 'q');
}

TEST(ProKeyState, BothShiftsTappedTogetherCountAsOneTap) {
  ProKeyState s;
  press(s, 3, kLShiftCol);
  press(s, 3, kRShiftCol);
  release(s, 3, kLShiftCol);
  EXPECT_FALSE(s.event(0, chipCol(9), false, 0, nullptr)) << "an unrelated release";
  release(s, 3, kRShiftCol);
  EXPECT_TRUE(s.shiftActive());
  EXPECT_EQ(tap(s, 0, 0), 'Q');
  EXPECT_EQ(tap(s, 0, 0), 'q');
}

TEST(ProKeyState, MicWithoutAltLeavesALatchForTheNextKey) {
  ProKeyState s;
  tap(s, 3, kRShiftCol);
  EXPECT_EQ(tap(s, 3, kMicCol), 0);
  EXPECT_TRUE(s.shiftActive());
  EXPECT_EQ(tap(s, 0, 0), 'Q');
}

TEST(ProKeyState, ATappedModifierIsSpentByEnter) {
  ProKeyState s;
  tap(s, 3, kLShiftCol);
  EXPECT_EQ(tap(s, kEnterRow, kEnterCol), 0x0D);
  EXPECT_EQ(tap(s, 0, 0), 'q');
}

TEST(ProKeyState, OutOfMatrixAndReleaseTypeNothing) {
  ProKeyState s;
  ProKeyOut out{0x55, false};
  EXPECT_FALSE(s.event(3, 9, true, 0, &out)) << "no switch there";
  EXPECT_FALSE(s.event(9, 0, true, 0, &out));
  EXPECT_FALSE(s.event(0, 0, false, 0, &out)) << "a release without a press";
  EXPECT_EQ(out.byte, 0x55) << "nothing typed: out left alone";
}

TEST(ProKeyState, StrayModifierReleaseDoesNotLatch) {
  ProKeyState s;
  release(s, 3, kLShiftCol);
  release(s, kAltRow, kAltCol);
  EXPECT_FALSE(s.shiftActive());
  EXPECT_FALSE(s.altActive());
  EXPECT_EQ(tap(s, 0, 0), 'q');
}

TEST(ProKeyState, ResetForgetsHeldAndLatched) {
  ProKeyState s;
  press(s, 3, kLShiftCol);
  tap(s, kAltRow, kAltCol);
  s.reset();
  EXPECT_FALSE(s.shiftActive());
  EXPECT_FALSE(s.altActive());
  EXPECT_EQ(tap(s, 0, 0), 'q');
}

// ---------------------------------------------------------------- repeat --

TEST(ProKeyRepeat, BackspaceRepeatsAfterHalfASecondThenTenASecond) {
  ProKeyState s;
  ProKeyOut out;
  ASSERT_TRUE(s.event(kBspRow, chipCol(kBspCol), true, 1000, &out));
  EXPECT_EQ(out.byte, 0x08);
  EXPECT_FALSE(out.repeat) << "the press itself is not a repeat";
  EXPECT_FALSE(s.repeat(1499, &out));
  ASSERT_TRUE(s.repeat(1500, &out));
  EXPECT_EQ(out.byte, 0x08);
  EXPECT_TRUE(out.repeat);
  EXPECT_FALSE(s.repeat(1599, &out));
  EXPECT_TRUE(s.repeat(1600, &out));
  EXPECT_TRUE(s.repeat(1700, &out));
}

TEST(ProKeyRepeat, ALateLoopGetsOneRepeatNotABurst) {
  ProKeyState s;
  ProKeyOut out;
  s.event(kBspRow, chipCol(kBspCol), true, 0, &out);
  EXPECT_TRUE(s.repeat(3000, &out)) << "a 2.5 s refresh held the loop";
  EXPECT_FALSE(s.repeat(3000, &out));
  EXPECT_FALSE(s.repeat(3099, &out));
  EXPECT_TRUE(s.repeat(3100, &out));
}

TEST(ProKeyRepeat, ReleaseStopsIt) {
  ProKeyState s;
  ProKeyOut out;
  s.event(kBspRow, chipCol(kBspCol), true, 0, &out);
  s.event(kBspRow, chipCol(kBspCol), false, 200, &out);
  EXPECT_FALSE(s.repeat(600, &out));
  EXPECT_FALSE(s.repeat(5000, &out));
}

TEST(ProKeyRepeat, NoOtherKeyRepeats) {
  for (uint8_t r = 0; r < kProKeyRows; r++) {
    for (uint8_t c = 0; c < kProKeyCols; c++) {
      if (proKeyAt(r, c).kind == ProKey::Backspace) continue;
      ProKeyState s;
      ProKeyOut out;
      s.event(r, c, true, 0, &out);
      EXPECT_FALSE(s.repeat(600, &out)) << "row " << int(r) << " col " << int(c);
      EXPECT_FALSE(s.repeat(5000, &out)) << "row " << int(r) << " col " << int(c);
    }
  }
}

TEST(ProKeyRepeat, ResetStopsARepeatWhoseReleaseWasLost) {
  ProKeyState s;
  ProKeyOut out;
  s.event(kBspRow, chipCol(kBspCol), true, 0, &out);
  s.reset();   // an overflow: the release may never come
  EXPECT_FALSE(s.repeat(600, &out));
}

TEST(ProKeyRepeat, SurvivesTheMillisWrap) {
  ProKeyState s;
  ProKeyOut out;
  const uint32_t t = 0xFFFFFF00u;
  s.event(kBspRow, chipCol(kBspCol), true, t, &out);
  EXPECT_FALSE(s.repeat(t + 499, &out));
  EXPECT_TRUE(s.repeat(t + 500, &out)) << "0x" << std::hex << (t + 500);
}

TEST(ProKeyRepeat, AnotherKeyWhileHeldKeepsTheRepeatGoing) {
  ProKeyState s;
  ProKeyOut out;
  s.event(kBspRow, chipCol(kBspCol), true, 0, &out);
  s.event(0, chipCol(0), true, 100, &out);
  s.event(0, chipCol(0), false, 150, &out);
  EXPECT_TRUE(s.repeat(500, &out)) << "Backspace is still down";
}

// ----------------------------------------------------------------- queue --

TEST(ProKeyQueue, KeepsOrderAndWraps) {
  ProKeyQueue q;
  uint8_t v;
  for (int round = 0; round < 3; round++) {
    for (uint8_t i = 0; i < 20; i++) ASSERT_TRUE(q.push(uint8_t(i + 1)));
    for (uint8_t i = 0; i < 20; i++) {
      ASSERT_TRUE(q.pop(&v));
      EXPECT_EQ(v, i + 1);
    }
  }
  EXPECT_FALSE(q.pop(&v));
  EXPECT_EQ(q.dropped(), 0u);
}

TEST(ProKeyQueue, FullDropsTheNewestAndCountsIt) {
  ProKeyQueue q;
  for (uint8_t i = 0; i < ProKeyQueue::kCap; i++) ASSERT_TRUE(q.push(uint8_t(i + 1)));
  EXPECT_FALSE(q.push(0xEE));
  EXPECT_FALSE(q.push(0xEF));
  EXPECT_EQ(q.dropped(), 2u);
  EXPECT_EQ(q.size(), ProKeyQueue::kCap);
  uint8_t v;
  ASSERT_TRUE(q.pop(&v));
  EXPECT_EQ(v, 1) << "the oldest is kept";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
