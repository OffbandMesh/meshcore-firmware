// Native tests for the Morse LED timeline (#1597).
//
// This drives three bright LEDs on a conference badge for ten seconds at a time, and the
// only thing a person can do with it is read it. So the thing worth testing is that the
// message is actually correct Morse with correct gaps -- a pattern that is nearly right is
// indistinguishable from noise to anyone trying to decode it, and the whole point is that
// it CAN be decoded.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include <helpers/ui/MorseBlink.h>

using namespace offband;

namespace {
// The timeline rendered back into dots, dashes and gaps -- so a test failure shows the
// message rather than a unit count.
std::string render(const char* text) {
  const uint32_t total = morseTotalUnits(text);
  std::string out;
  for (uint32_t u = 0; u < total; u++) {
    out += morseAt(text, u).lit ? '#' : '.';
  }
  return out;
}
}  // namespace

// ---- the alphabet ----------------------------------------------------------------------

TEST(Morse, SingleLettersAreTheStandardCode) {
  EXPECT_EQ("#", render("E"));            // dot
  EXPECT_EQ("###", render("T"));          // dash
  EXPECT_EQ("#.#", render("I"));          // dot gap dot
  EXPECT_EQ("#.###", render("A"));        // dot gap dash
  EXPECT_EQ("###.###", render("M"));      // dash gap dash
  EXPECT_EQ("###.#", render("N"));
  EXPECT_EQ("###.###.###", render("O"));
  EXPECT_EQ("#.#.#.#", render("H"));
}

TEST(Morse, UnknownCharactersContributeNothing) {
  EXPECT_EQ(morseTotalUnits("E"), morseTotalUnits("E!"));
  EXPECT_EQ(morseTotalUnits("E"), morseTotalUnits("E123"));
  EXPECT_EQ(0u, morseTotalUnits(""));
  EXPECT_EQ(0u, morseTotalUnits("!!!"));
  EXPECT_EQ(0u, morseTotalUnits(nullptr));
}

TEST(Morse, LowerCaseWorks) {
  EXPECT_EQ(render("SOS"), render("sos"));
}

// ---- the gap rules, which are the easy thing to get subtly wrong -----------------------

TEST(Morse, LetterGapIsThreeUnitsOfDark) {
  // "EE": dot, 3 dark, dot
  EXPECT_EQ("#...#", render("EE"));
}

TEST(Morse, WordGapIsSevenUnitsOfDark) {
  // "E E": dot, 7 dark, dot
  EXPECT_EQ("#.......#", render("E E"));
}

TEST(Morse, RepeatedAndLeadingSpacesDoNotAddGaps) {
  EXPECT_EQ(render("E E"), render("E  E"));
  EXPECT_EQ(render("E"), render(" E"));
  EXPECT_EQ(render("E"), render("E "));
}

// ---- the actual message ----------------------------------------------------------------

// The owner's message and unit, so the figures quoted on #1597 are pinned rather than
// remembered: 129 units at 80 ms is 10.3 s per repetition.
TEST(Morse, TheMessageIsTheLengthWeQuoted) {
  EXPECT_EQ(129u, morseTotalUnits("I AM NOT WORTHY"));
  EXPECT_EQ(80u, kMorseUnitMs);
  const double secs = 129.0 * kMorseUnitMs / 1000.0;
  EXPECT_NEAR(10.3, secs, 0.05);
}

TEST(Morse, TheMessageDecodesBackToItself) {
  // Decode the rendered timeline and check it spells the message. If this passes, somebody
  // at the conference with a Morse chart gets the joke; if it fails, they get noise.
  const std::string t = render("I AM NOT WORTHY");
  std::string decoded, code;
  size_t i = 0;
  auto flush_letter = [&]() {
    if (code.empty()) return;
    for (char c = 'A'; c <= 'Z'; c++) {
      if (code == morseFor(c)) { decoded += c; break; }
    }
    code.clear();
  };
  while (i < t.size()) {
    if (t[i] == '#') {
      size_t n = 0;
      while (i < t.size() && t[i] == '#') { n++; i++; }
      code += (n == 1) ? '.' : '-';
    } else {
      size_t n = 0;
      while (i < t.size() && t[i] == '.') { n++; i++; }
      if (n >= 7) { flush_letter(); decoded += ' '; }
      else if (n >= 3) { flush_letter(); }
    }
  }
  flush_letter();
  EXPECT_EQ("I AM NOT WORTHY", decoded);
}

// ---- duty, because this is three bright LEDs on a battery ------------------------------

TEST(Morse, DutyIsWhatWeClaimed) {
  const char* msg = "I AM NOT WORTHY";
  const uint32_t total = morseTotalUnits(msg);
  uint32_t lit_units = 0;
  for (uint32_t u = 0; u < total; u++) {
    if (morseAt(msg, u).lit) lit_units++;
  }
  EXPECT_EQ(66u, lit_units);
  // 51%, within a point of the core's own toggle -- so this costs no more power than the
  // blink it replaces. Quoted on #1597; pinned here.
  EXPECT_NEAR(0.51, (double)lit_units / total, 0.01);
}

// ---- the repeating clock ---------------------------------------------------------------

TEST(Morse, ItLoopsForeverAndTheFirstUnitIsLit) {
  const char* msg = "I AM NOT WORTHY";
  const uint32_t period = morseTotalUnits(msg) * kMorseUnitMs;
  EXPECT_TRUE(morseLitAt(msg, 0));
  // One full period later, the same instant in the message.
  for (uint32_t ms = 0; ms < period; ms += 37) {
    EXPECT_EQ(morseLitAt(msg, ms), morseLitAt(msg, ms + period))
        << "the message did not repeat identically at ms=" << ms;
  }
}

TEST(Morse, ADegenerateUnitLengthDoesNotDivideByZero) {
  EXPECT_FALSE(morseLitAt("E", 0, 0));
  EXPECT_FALSE(morseLitAt("", 1234));
  EXPECT_FALSE(morseLitAt(nullptr, 1234));
}

// ---- the caller contract the main loop depends on -------------------------------------

// The loop caches the total and only walks when the unit advances, so morseUnitAt() has to
// be the single source of "which unit is it" -- and it has to agree with morseLitAt(),
// which is what these tests use. If the two ever diverge, the badge and the tests would be
// showing different messages.
TEST(Morse, UnitIndexAgreesWithTheConvenienceWrapper) {
  const char* msg = "I AM NOT WORTHY";
  const uint32_t total = morseTotalUnits(msg);
  for (uint32_t ms = 0; ms < total * kMorseUnitMs * 2; ms += 13) {
    const uint32_t unit = morseUnitAt(ms, total);
    EXPECT_EQ(morseAt(msg, unit).lit, morseLitAt(msg, ms))
        << "the loop's path and the test's path disagree at ms=" << ms;
  }
}

TEST(Morse, UnitIndexWrapsAndSurvivesDegenerateInput) {
  EXPECT_EQ(0u, morseUnitAt(0, 129));
  EXPECT_EQ(1u, morseUnitAt(kMorseUnitMs, 129));
  EXPECT_EQ(0u, morseUnitAt(129 * kMorseUnitMs, 129));   // wraps to the start
  EXPECT_EQ(0u, morseUnitAt(1234, 0));                   // no message: no divide by zero
  EXPECT_EQ(0u, morseUnitAt(1234, 129, 0));              // no unit length: likewise
}

// morseAt returns BOTH facts, so the length is never computed by a second code path that
// could drift from the gap rules. Pinned because an earlier version split them.
TEST(Morse, OneWalkReportsBothLengthAndLitState) {
  const MorseAt a = morseAt("I AM NOT WORTHY", 0);
  EXPECT_EQ(129u, a.total_units);
  EXPECT_TRUE(a.lit);                       // unit 0 is the first dot of "I"
  EXPECT_EQ(a.total_units, morseTotalUnits("I AM NOT WORTHY"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
