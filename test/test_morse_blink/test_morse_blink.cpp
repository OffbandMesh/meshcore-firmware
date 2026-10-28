// Native tests for the Morse LED timeline (#1597).
//
// This drives three bright LEDs on a conference badge for ten seconds at a time, and the
// only thing a person can do with it is read it. So the thing worth testing is that the
// message is actually correct Morse with correct gaps -- a pattern that is nearly right is
// indistinguishable from noise to anyone trying to decode it, and the whole point is that
// it CAN be decoded.

#include <gtest/gtest.h>

#include <cctype>
#include <cstdint>
#include <set>
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

TEST(Morse, UnencodableCharactersContributeNothing) {
  // Punctuation has no code, and morseAt() skips a nullptr without adding an element or a
  // gap, so it costs exactly nothing. That is the right behaviour for a character that
  // cannot be rendered -- the alternative is blinking garbage.
  EXPECT_EQ(morseTotalUnits("E"), morseTotalUnits("E!"));
  EXPECT_EQ(morseTotalUnits("E"), morseTotalUnits("E!?,."));
  EXPECT_EQ(0u, morseTotalUnits(""));
  EXPECT_EQ(0u, morseTotalUnits("!!!"));
  EXPECT_EQ(0u, morseTotalUnits(nullptr));
  EXPECT_EQ(nullptr, morseFor('!'));
  EXPECT_EQ(nullptr, morseFor(' '));     // handled as a word gap, not as a character
  EXPECT_EQ(nullptr, morseFor('\0'));
  EXPECT_EQ(nullptr, morseFor('-'));     // the rendering of a dash is not an input
}

// ---- digits (#1640) --------------------------------------------------------------------
//
// This block replaces a single line that used to read
//
//     EXPECT_EQ(morseTotalUnits("E"), morseTotalUnits("E123"));
//
// inside a test called UnknownCharactersContributeNothing. That assertion PINNED the
// defect: digits were silently dropped, and the one case covering them asserted that they
// were, grouped in with punctuation so it read as intentional. It was the only digit case
// in the file.

TEST(Digits, EachDigitIsItsItuCode) {
  EXPECT_STREQ(".----", morseFor('1'));
  EXPECT_STREQ("..---", morseFor('2'));
  EXPECT_STREQ("...--", morseFor('3'));
  EXPECT_STREQ("....-", morseFor('4'));
  EXPECT_STREQ(".....", morseFor('5'));
  EXPECT_STREQ("-....", morseFor('6'));
  EXPECT_STREQ("--...", morseFor('7'));
  EXPECT_STREQ("---..", morseFor('8'));
  EXPECT_STREQ("----.", morseFor('9'));
  EXPECT_STREQ("-----", morseFor('0'));
}

TEST(Digits, TheCodesFollowTheRuleRatherThanATable) {
  // Derived from the mechanism, not restated from the header. A second copy of a lookup
  // table only proves it was copied; this fails if any entry drifts off the sequence.
  //
  // THE RULE, which is not quite the obvious one. Every digit is five elements, and the
  // ten of them are one progression counted from 1 to 10, where '0' is the tenth:
  //
  //   1-5   n dots first, dashes filling the rest      1 .----   5 .....
  //   6-0   (n-5) dashes first, dots filling the rest  6 -....   0 -----
  //
  // So the dots lead in the first half and the DASHES lead in the second. The first draft
  // of this test and of the header comment both said "dots counting up, then dashes
  // counting up", which is right for 1-5 and wrong for 6-0 -- it makes '6' six dots and
  // minus-one dashes. That underflowed a size_t and took the whole suite down with a
  // CTRL_BREAK rather than a failed assertion, which is exactly why this is computed:
  // EachDigitIsItsItuCode passed happily the whole time, because the TABLE was correct.
  for (char d = '0'; d <= '9'; d++) {
    const char* code = morseFor(d);
    ASSERT_NE(nullptr, code) << "digit " << d << " has no code";
    const std::string s(code);
    ASSERT_EQ(5u, s.size()) << "every digit is five elements; " << d << " is " << s;

    const size_t n = (d == '0') ? 10u : (size_t)(d - '0');  // the 1..10 progression
    ASSERT_GE(n, 1u);
    ASSERT_LE(n, 10u);
    const std::string want = (n <= 5)
        ? std::string(n, '.') + std::string(5 - n, '-')
        : std::string(n - 5, '-') + std::string(10 - n, '.');
    EXPECT_EQ(want, s) << "digit " << d << " is step " << n << " of the progression";
  }

  // And the progression is a progression: each step moves exactly one element from the
  // trailing run to the leading run, so no two digits share a code.
  std::set<std::string> seen;
  for (char d = '0'; d <= '9'; d++) {
    EXPECT_TRUE(seen.insert(morseFor(d)).second) << "digit " << d << " duplicates another";
  }
  EXPECT_EQ(10u, seen.size());
}

TEST(Digits, ADigitNowCostsTimeWhereItUsedToCostNothing) {
  // The behaviour change, stated as an assertion rather than left in a commit message.
  // Each digit is five elements: 1+3+1+3+1+3+1+3+1 worst case, so the length depends on
  // the dot/dash mix. What is certain is that it is no longer free.
  EXPECT_GT(morseTotalUnits("E5"), morseTotalUnits("E"));
  EXPECT_GT(morseTotalUnits("E0"), morseTotalUnits("E"));
  // '5' is five dots: 5 x 1 unit + 4 x 1 gap = 9. Plus "E" (1) and a letter gap (3) = 13.
  EXPECT_EQ(13u, morseTotalUnits("E5"));
  // '0' is five dashes: 5 x 3 + 4 x 1 = 19. Plus 1 + 3 = 23.
  EXPECT_EQ(23u, morseTotalUnits("E0"));
}

TEST(Digits, ADigitBearingMessageDecodesBackToItself) {
  // The same round-trip the letters get, over a string with digits in it -- which is the
  // thing that silently produced a shorter pattern before.
  const std::string msg = "OFFBAND 0X4";
  const std::string t = render(msg.c_str());
  std::string decoded, code;
  size_t i = 0;
  auto flush_letter = [&]() {
    if (code.empty()) return;
    for (int c = 0; c < 128; c++) {
      const char* m = morseFor((char)c);
      if (m != nullptr && code == m && (isalpha(c) ? isupper(c) : true)) {
        decoded += (char)c;
        break;
      }
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
  EXPECT_EQ(msg, decoded);
}

// The matching scope note -- that adding digits here does NOT widen the #1609 trigger --
// is pinned in test_tune_trigger, where TuneTrigger.h is already in scope. Keeping this
// file to MorseBlink.h alone is deliberate: the trigger depends on the Morse table, not
// the other way round, and a test include that reversed that would hide it.

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

// #1887: the text the badge now blinks, pinned the same way: 251 units is 20.1 s per
// repetition.
TEST(Morse, TheBadgeTextIsTheLengthWeQuoted) {
  EXPECT_EQ(251u, morseTotalUnits("BE SURE TO DRINK YOUR OVALTINE"));
  EXPECT_NEAR(20.1, 251.0 * kMorseUnitMs / 1000.0, 0.05);
}

// Read a rendered timeline back the way a person with a Morse chart would.
static std::string decodeTimeline(const std::string& t) {
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
  return decoded;
}

TEST(Morse, TheMessageDecodesBackToItself) {
  // Decode the rendered timeline and check it spells the message. If this passes, somebody
  // at the conference with a Morse chart gets the joke; if it fails, they get noise.
  EXPECT_EQ("I AM NOT WORTHY", decodeTimeline(render("I AM NOT WORTHY")));
}

// #1887: the text the badge now blinks.
TEST(Morse, TheBadgeTextDecodesBackToItself) {
  EXPECT_EQ("BE SURE TO DRINK YOUR OVALTINE",
            decodeTimeline(render("BE SURE TO DRINK YOUR OVALTINE")));
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
