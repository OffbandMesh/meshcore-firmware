#include <gtest/gtest.h>
#include <helpers/ui/LineEdit.h>

#include <cstdint>
#include <string>

TEST(LineEdit, StartsEmpty) {
  LineEdit<8> line;
  EXPECT_STREQ("", line.text());
  EXPECT_EQ(0u, line.length());
}

TEST(LineEdit, PrintablesAppend) {
  LineEdit<8> line;
  EXPECT_TRUE(line.apply('h'));
  EXPECT_TRUE(line.apply('i'));
  EXPECT_TRUE(line.apply('!'));
  EXPECT_STREQ("hi!", line.text());
}

TEST(LineEdit, BackspaceDeletesTheLastCharacter) {
  LineEdit<8> line;
  line.apply('a');
  line.apply('b');
  EXPECT_TRUE(line.apply(KEY_BACKSPACE));
  EXPECT_STREQ("a", line.text());
  EXPECT_TRUE(line.apply(KEY_BACKSPACE));
  EXPECT_FALSE(line.apply(KEY_BACKSPACE));   // nothing left to delete
  EXPECT_STREQ("", line.text());
}

TEST(LineEdit, TabInsertsASpace) {
  LineEdit<8> line;
  line.apply('a');
  EXPECT_TRUE(line.apply(KEY_TAB));
  line.apply('b');
  EXPECT_STREQ("a b", line.text());
}

TEST(LineEdit, AFullLineRefusesMoreAndStaysTerminated) {
  LineEdit<3> line;
  EXPECT_TRUE(line.apply('x'));
  EXPECT_TRUE(line.apply('y'));
  EXPECT_TRUE(line.apply('z'));
  EXPECT_FALSE(line.apply('w'));
  EXPECT_STREQ("xyz", line.text());
  EXPECT_EQ(3u, line.length());
}

TEST(LineEdit, NavigationAndControlKeysAreLeftToTheCaller) {
  LineEdit<8> line;
  for (uint8_t key : {(uint8_t)KEY_LEFT, (uint8_t)KEY_UP, (uint8_t)KEY_DOWN, (uint8_t)KEY_RIGHT,
                      (uint8_t)KEY_ENTER, (uint8_t)KEY_CANCEL, (uint8_t)KEY_NEXT, (uint8_t)0}) {
    EXPECT_FALSE(line.apply(key)) << int(key);
  }
  EXPECT_STREQ("", line.text());
}

TEST(LineEdit, ClearEmptiesTheLine) {
  LineEdit<8> line;
  line.apply('a');
  line.clear();
  EXPECT_STREQ("", line.text());
  EXPECT_EQ(0u, line.length());
}

TEST(LineEdit, TailShowsTheEndOfALongLine) {
  LineEdit<16> line;
  for (char ch : {'a', 'b', 'c', 'd', 'e', 'f'}) line.apply(ch);
  EXPECT_STREQ("def", line.tail(3));
  EXPECT_STREQ("abcdef", line.tail(10));   // shorter than the width: the whole line
}

// #1473: the wrapped view. These exist because the thing being proofread is a
// 218-character RTTTL string on a 21-column screen -- `tail()` shows its last 21
// characters and hides the rest, so the string cannot be checked before it is saved.

TEST(LineEditWindow, SlicesTheMiddleAndTerminatesIt) {
  LineEdit<32> line;
  for (const char* p = "abcdefghij"; *p; p++) line.apply(*p);
  char out[8];
  EXPECT_EQ(3u, line.window(2, 3, out, sizeof(out)));
  EXPECT_STREQ("cde", out) << "a middle slice must carry its own terminator";
}

TEST(LineEditWindow, LaysALongLineOutAcrossRows) {
  LineEdit<64> line;
  for (int i = 0; i < 25; i++) line.apply((char)('a' + i % 26));
  const size_t width = 10;
  EXPECT_EQ(3u, line.rows(width));
  char out[16];
  std::string rebuilt;
  for (size_t r = 0; r < line.rows(width); r++) {
    line.window(r * width, width, out, sizeof(out));
    rebuilt += out;
  }
  EXPECT_EQ(std::string(line.text()), rebuilt) << "the rows do not reassemble the text";
}

// A part-full layout asks for rows past the end. That has to answer with an empty row
// rather than a refusal every caller would special-case.
TEST(LineEditWindow, PastTheEndIsAnEmptyRowNotAnError) {
  LineEdit<32> line;
  line.apply('x');
  char out[8];
  EXPECT_EQ(0u, line.window(1, 4, out, sizeof(out)));
  EXPECT_STREQ("", out);
  EXPECT_EQ(0u, line.window(99, 4, out, sizeof(out)));
  EXPECT_STREQ("", out);
}

TEST(LineEditWindow, ClampsToWhatIsLeftAndToTheCallersBuffer) {
  LineEdit<32> line;
  for (const char* p = "abcdefghij"; *p; p++) line.apply(*p);
  char out[8];
  // Asking for more than remains gives what remains.
  EXPECT_EQ(3u, line.window(7, 99, out, sizeof(out)));
  EXPECT_STREQ("hij", out);
  // A caller buffer smaller than the width wins, and still terminates.
  char small[4];
  EXPECT_EQ(3u, line.window(0, 10, small, sizeof(small)));
  EXPECT_STREQ("abc", small);
}

// Degenerate arguments must not write anywhere or read out of bounds.
TEST(LineEditWindow, RefusesZeroWidthNullAndAZeroCapacityBuffer) {
  LineEdit<32> line;
  for (const char* p = "abc"; *p; p++) line.apply(*p);
  char out[8] = {'Z', 0};
  EXPECT_EQ(0u, line.window(0, 0, out, sizeof(out)));
  EXPECT_STREQ("", out) << "zero width should clear, not leave stale text";
  EXPECT_EQ(0u, line.window(0, 4, nullptr, 8));
  char none[1] = {'Z'};
  EXPECT_EQ(0u, line.window(0, 4, none, 0));
  EXPECT_EQ('Z', none[0]) << "a zero-capacity buffer must not be written at all";
}

TEST(LineEditWindow, AnEmptyBufferStillNeedsOneRow) {
  LineEdit<32> line;
  EXPECT_EQ(1u, line.rows(21)) << "the cursor needs a row to sit on";
  EXPECT_EQ(1u, line.rows(0)) << "a zero width must not divide by zero";
}

// Concrete expected values, NOT the implementation's own formula. Review caught that
// the first version of this test recomputed `(n + width - 1) / width` and compared it to
// a function that computes the same thing -- which verifies that the line was typed
// twice, not that it is right. It would have passed just as happily against
// `(n + width) / width`. It would also have had to be rewritten the moment the
// implementation changed to avoid an overflow, which is exactly what then happened.
TEST(LineEditWindow, RowCountsAreTheExpectedOnes) {
  struct Case { size_t len, width, rows; };
  const Case cases[] = {
      {0, 3, 1},    // empty still occupies a row
      {1, 3, 1}, {2, 3, 1}, {3, 3, 1},
      {4, 3, 2}, {6, 3, 2},
      {7, 3, 3},
      {21, 21, 1},  // exactly one screen row
      {22, 21, 2},
      {218, 21, 11},   // the RTTTL string this was built for
  };
  for (const Case& c : cases) {
    LineEdit<256> line;
    for (size_t i = 0; i < c.len; i++) line.apply('a');
    ASSERT_EQ(c.len, line.length());
    EXPECT_EQ(c.rows, line.rows(c.width))
        << "length " << c.len << " at width " << c.width;
  }
}

// A width near SIZE_MAX made the old ceiling-division idiom overflow and answer 0 -- a
// row count that is never valid. Theoretical for a display, but a function with an
// arithmetic cliff in it is a trap for whoever reuses it next.
TEST(LineEditWindow, AnAbsurdWidthStillAnswersOneRow) {
  LineEdit<32> line;
  for (const char* p = "abcdefghij"; *p; p++) line.apply(*p);
  EXPECT_EQ(1u, line.rows(SIZE_MAX));
  EXPECT_EQ(1u, line.rows(SIZE_MAX - 1));
  EXPECT_EQ(1u, line.rows(line.length()));
}

// The tightest buffer there is: room for a terminator and nothing else. Review flagged
// that this was untested, and it is the case where a reordered clamp would write out[1]
// and overflow a one-byte buffer.
TEST(LineEditWindow, ACapacityOfOneWritesOnlyTheTerminator) {
  LineEdit<8> line;
  line.apply('a');
  line.apply('b');
  char out[2] = {'Z', 'Z'};   // a guard byte after the single usable slot
  EXPECT_EQ(0u, line.window(0, 5, out, 1));
  EXPECT_STREQ("", out);
  EXPECT_EQ('Z', out[1]) << "wrote past a one-byte buffer";
}

// The composer shares this class, so the existing single-line view must be untouched.
TEST(LineEditWindow, TailIsUnchangedByTheWrappedView) {
  LineEdit<32> line;
  for (const char* p = "abcdefghij"; *p; p++) line.apply(*p);
  EXPECT_STREQ("hij", line.tail(3));
  char out[8];
  line.window(0, 3, out, sizeof(out));
  EXPECT_STREQ("hij", line.tail(3)) << "window() mutated the buffer";
  EXPECT_STREQ("abcdefghij", line.text());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
