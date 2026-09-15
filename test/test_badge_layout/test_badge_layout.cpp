// Native unit tests for the badge screens' layout (#1230): the owner's design on a
// 21 x 8 grid of 6 x 8 cells. Ages, word wrap, how a thread's messages become rows,
// what scrolls into view, and the selected row's marquee.

#include <gtest/gtest.h>
#include <climits>
#include <string>
#include <vector>
#include "helpers/ui/BadgeLayout.h"

using namespace badgeui;

namespace {
std::string age(uint32_t secs) {
  char buf[8];
  formatAge(secs, buf, sizeof(buf));
  return buf;
}

std::vector<std::string> lines(const char* text, int width) {
  Span spans[16];
  const int n = wrap(text, width, spans, 16);
  std::vector<std::string> out;
  for (int i = 0; i < n; i++) out.push_back(std::string(text + spans[i].start, spans[i].len));
  return out;
}

std::string rowText(const MsgView* msgs, const Row& r) {
  return std::string(msgs[r.msg].text + r.span.start, r.span.len);
}
}  // namespace

TEST(BadgeLayoutAge, NowThenMinutesHoursDays) {
  EXPECT_EQ("now", age(0));
  EXPECT_EQ("now", age(59));
  EXPECT_EQ("1m", age(60));
  EXPECT_EQ("59m", age(3599));
  EXPECT_EQ("1h", age(3600));
  EXPECT_EQ("23h", age(86399));
  EXPECT_EQ("1d", age(86400));
  EXPECT_EQ("99d", age(99u * 86400u));
  EXPECT_EQ("old", age(100u * 86400u));
}

namespace {
std::string when(int zone, uint32_t t, uint32_t now) {
  char buf[8];
  formatWhen(zone, t, now, buf, sizeof(buf));
  return buf;
}
constexpr uint32_t kNow = 1789492020;   // 2026-09-15 17:07 UTC, 13:07 EDT
}  // namespace

// #1233: with a zone set, today reads as the clock does; older, as an age.
TEST(BadgeLayoutWhen, TodayInTheZoneIsAClockTime) {
  EXPECT_EQ("12:04", when(offband::tz::kEastern, kNow - 3780, kNow));   // an hour and 3 minutes ago
}

TEST(BadgeLayoutWhen, YesterdayIsAnAge) {
  EXPECT_EQ("1d", when(offband::tz::kEastern, kNow - 86400, kNow));
}

TEST(BadgeLayoutWhen, NoZoneOrNoClockIsAnAge) {
  EXPECT_EQ("1h", when(offband::tz::kNotSet, kNow - 3780, kNow));
  EXPECT_EQ("1h", when(offband::tz::kEastern, 1000, 1000 + 3780));   // a clock never set (1970)
}

namespace {
std::string position(long lat_e6, long lon_e6) {
  char buf[24];
  formatPosition(lat_e6, lon_e6, buf, sizeof(buf));
  return buf;
}
std::string altitude(long mm) {
  char buf[16];
  formatAltitude(mm, buf, sizeof(buf));
  return buf;
}
}  // namespace

// #1235: the GPS screen's position, four decimals, hemispheres as letters.
TEST(BadgeLayoutGps, PositionInEachHemisphere) {
  EXPECT_EQ("39.1031N  84.5120W", position(39103100, -84512000));
  EXPECT_EQ("33.8688S  151.2093E", position(-33868800, 151209300));
}

TEST(BadgeLayoutGps, PositionRoundsAndCarries) {
  EXPECT_EQ("39.1032N  84.5121W", position(39103150, -84512050));   // halves round away from zero
  EXPECT_EQ("40.0000N  0.0000E", position(39999950, 0));             // the carry reaches the degrees
  EXPECT_EQ("0.0000N  0.0000E", position(-40, -49));                 // too small to have a side
}

TEST(BadgeLayoutGps, TheWidestPositionFitsTheRow) {
  EXPECT_EQ("90.0000S  180.0000W", position(-90000000, -180000000));
  EXPECT_LE(position(-90000000, -180000000).size() + 1, (size_t)kCols);   // after the row's space
}

TEST(BadgeLayoutGps, AltitudeInWholeMeters) {
  EXPECT_EQ("265m", altitude(265400));
  EXPECT_EQ("266m", altitude(265500));
  EXPECT_EQ("0m", altitude(-499));
  EXPECT_EQ("-86m", altitude(-86000));
  EXPECT_EQ("-87m", altitude(-86500));
  // The most negative value takes no signed overflow on the way.
  const std::string lowest = altitude(LONG_MIN);
  EXPECT_EQ('-', lowest[0]);
  EXPECT_EQ(std::to_string((0UL - (unsigned long)LONG_MIN + 500) / 1000) + "m", lowest.substr(1));
}

TEST(BadgeLayoutGps, UtcTimeOfDay) {
  char buf[12];
  formatUtcTime(kNow + 42, buf, sizeof(buf));
  EXPECT_STREQ("17:07:42", buf);
  formatUtcTime(0, buf, sizeof(buf));
  EXPECT_STREQ("00:00:00", buf);
}

TEST(BadgeLayoutWrap, BreaksAtSpaces) {
  EXPECT_EQ((std::vector<std::string>{"anyone at the", "CTF table?"}), lines("anyone at the CTF table?", 15));
}

TEST(BadgeLayoutWrap, SplitsAWordLongerThanTheLine) {
  EXPECT_EQ((std::vector<std::string>{"abcd", "efgh", "ij"}), lines("abcdefghij", 4));
}

TEST(BadgeLayoutWrap, DropsSpacesAtABreak) {
  EXPECT_EQ((std::vector<std::string>{"hello", "world"}), lines("hello   world", 7));
}

TEST(BadgeLayoutWrap, ShortTextIsOneLine) {
  EXPECT_EQ((std::vector<std::string>{"omw, 5 min"}), lines("omw, 5 min", 19));
  EXPECT_TRUE(lines("", 19).empty());
}

TEST(BadgeLayoutWrap, StopsAtMaxLines) {
  Span spans[2];
  EXPECT_EQ(2, wrap("a b c d e", 1, spans, 2));
}

// A channel message: the sender's tag in columns 0-4, text from column 6, and the
// rows after it indented to match (design 1a, "Thread settled").
TEST(BadgeLayoutThread, ChannelMessageTagsItsFirstRow) {
  const MsgView msgs[] = {{false, "Abend", "anyone at the CTF table?"}};
  Row rows[8];
  const int n = layoutThread(msgs, 1, true, -1, rows, 8);
  ASSERT_EQ(2, n);
  EXPECT_TRUE(rows[0].first);
  EXPECT_EQ(kIndent, rows[0].col);
  EXPECT_EQ("anyone at the", rowText(msgs, rows[0]));
  EXPECT_FALSE(rows[1].first);
  EXPECT_EQ(kIndent, rows[1].col);
  EXPECT_EQ("CTF table?", rowText(msgs, rows[1]));
  EXPECT_TRUE(rows[1].last);
}

// One sender in a DM, so no tag: the text gets the whole width.
TEST(BadgeLayoutThread, DmMessageUsesTheWholeWidth) {
  const MsgView msgs[] = {{false, "", "see you at the CTF table"}};
  Row rows[8];
  const int n = layoutThread(msgs, 1, false, -1, rows, 8);
  ASSERT_EQ(2, n);
  EXPECT_EQ(0, rows[0].col);
  EXPECT_EQ("see you at the CTF", rowText(msgs, rows[0]));
}

// Mine sit on the right, ending at column 18, with columns 19-20 for the mark.
TEST(BadgeLayoutThread, OwnMessagesAreRightAligned) {
  const MsgView msgs[] = {{true, "", "omw, 5 min"}};
  Row rows[8];
  ASSERT_EQ(1, layoutThread(msgs, 1, true, -1, rows, 8));
  EXPECT_EQ(kMineEnd - 10, rows[0].col);
  EXPECT_TRUE(rows[0].first);
  EXPECT_TRUE(rows[0].last);
}

TEST(BadgeLayoutThread, MessagesStayInOrder) {
  const MsgView msgs[] = {{false, "Abend", "one"}, {true, "", "two"}, {false, "Stryc", "three"}};
  Row rows[8];
  ASSERT_EQ(3, layoutThread(msgs, 3, true, -1, rows, 8));
  EXPECT_EQ(0, rows[0].msg);
  EXPECT_EQ(1, rows[1].msg);
  EXPECT_EQ(2, rows[2].msg);
}

// The selected message: a caret in column 0 on each of its rows, everything else one
// column right, and one meta row after it (design 1a, "Message selected").
TEST(BadgeLayoutThread, SelectedMessageGetsACaretAndAMetaRow) {
  const MsgView msgs[] = {{false, "Abend", "anyone?"}, {false, "Stryc", "flag drop at the CTF table"}};
  Row rows[8];
  const int n = layoutThread(msgs, 2, true, 1, rows, 8);
  ASSERT_EQ(4, n);   // "anyone?", two rows of Stryc's, and the meta row
  EXPECT_FALSE(rows[0].caret);
  EXPECT_TRUE(rows[1].caret);
  EXPECT_EQ(kIndent + 1, rows[1].col);
  EXPECT_EQ("flag drop at", rowText(msgs, rows[1]));
  EXPECT_TRUE(rows[2].caret);
  EXPECT_EQ(RowKind::Meta, rows[3].kind);
  EXPECT_EQ(1, rows[3].msg);
}

TEST(BadgeLayoutThread, StopsAtMaxRows) {
  const MsgView msgs[] = {{false, "", "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn"}};
  Row rows[3];
  EXPECT_EQ(3, layoutThread(msgs, 1, false, -1, rows, 3));
}

// With nothing selected, the newest rows sit at the bottom.
TEST(BadgeLayoutThreadTop, NewestAtTheBottom) {
  Row rows[10] = {};
  EXPECT_EQ(3, threadTop(rows, 10, 7, -1));
  EXPECT_EQ(0, threadTop(rows, 4, 7, -1));
}

// A selected message scrolls into view, as low on screen as it can sit.
TEST(BadgeLayoutThreadTop, SelectionScrollsIntoView) {
  Row rows[10] = {};
  for (int i = 0; i < 10; i++) rows[i].msg = (uint8_t)(i / 2);   // five messages of two rows
  EXPECT_EQ(0, threadTop(rows, 10, 7, 0));   // message 0 is rows 0-1: scroll to the top
  EXPECT_EQ(3, threadTop(rows, 10, 7, 4));   // the newest is already in view
  EXPECT_EQ(2, threadTop(rows, 10, 7, 1));   // rows 2-3: top goes to 2
}

// The inbox keeps one row of context above the selection.
TEST(BadgeLayoutListTop, KeepsContextAboveTheSelection) {
  EXPECT_EQ(0, listTop(0, 10, 7));
  EXPECT_EQ(0, listTop(1, 10, 7));
  EXPECT_EQ(3, listTop(4, 10, 7));
  EXPECT_EQ(3, listTop(9, 10, 7));   // never past the end
  EXPECT_EQ(0, listTop(2, 3, 7));    // everything fits
}

// A selected row too long for its box holds, slides to show its end, holds, and
// starts over (design 2a, "marquee-on-select").
TEST(BadgeLayoutMarquee, HoldsSlidesHolds) {
  EXPECT_EQ(0, marqueeOffset(100, 128, 5000));        // fits: never moves
  EXPECT_EQ(0, marqueeOffset(228, 128, 0));
  EXPECT_EQ(0, marqueeOffset(228, 128, kMarqueeHoldMs - 1));
  EXPECT_EQ(1, marqueeOffset(228, 128, kMarqueeHoldMs + kMarqueeMsPerPx));
  const uint32_t slide_end = kMarqueeHoldMs + 100 * kMarqueeMsPerPx;
  EXPECT_EQ(100, marqueeOffset(228, 128, slide_end));
  EXPECT_EQ(100, marqueeOffset(228, 128, slide_end + kMarqueeHoldMs - 1));
  EXPECT_EQ(0, marqueeOffset(228, 128, slide_end + kMarqueeHoldMs));   // round again
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
