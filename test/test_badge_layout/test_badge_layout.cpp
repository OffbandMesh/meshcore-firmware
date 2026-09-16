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

// #1237: the layout measures pixels. These wrap tests use the fixed 6 px face, so a
// width in characters is still readable as one; the face tests below use the real
// proportional widths.
const Face& kFixed = fixedFace();
constexpr int kCellPx = 6;

std::vector<std::string> lines(const char* text, int chars, const Face& f = kFixed) {
  Span spans[16];
  const int n = wrap(f, text, chars * kCellPx, spans, 16);
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
std::string when(int zone, uint32_t t, uint32_t now, bool trusted = true) {
  char buf[8];
  formatWhen(zone, t, now, trusted, buf, sizeof(buf));
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

// The owner's bench (2026-09-15): after a reboot the badge's clock ran from the newest
// contact's last-heard time, hours behind. Until the phone or the GPS sets it, a row
// shows an age, which is true, rather than a clock time, which isn't.
TEST(BadgeLayoutWhen, AClockNobodyHasSetShowsAnAge) {
  EXPECT_EQ("3m", when(offband::tz::kEastern, kNow - 180, kNow, false));
  EXPECT_EQ("3m", when(offband::tz::kEastern, 1715770351 + 60, 1715770351 + 240, false));   // MeshCore's fallback start
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
  // It fits a row after the leading space, in either face.
  EXPECT_LE(textPx(kFixed, (" " + position(-90000000, -180000000)).c_str()), kScreenPx);
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

namespace {
std::string gpsState(bool on, bool module, bool fix, long sats) {
  char buf[16];
  formatGpsState(on, module, fix, sats, buf, sizeof(buf));
  return buf;
}
}  // namespace

TEST(BadgeLayoutGps, StateInAFewWords) {
  EXPECT_EQ("off", gpsState(false, true, false, 0));
  EXPECT_EQ("off", gpsState(false, false, true, 9));   // off says off, module or not
  EXPECT_EQ("No GPS Module", gpsState(true, false, false, 0));
  EXPECT_EQ("no fix", gpsState(true, true, false, 0));
  EXPECT_EQ("fix 9", gpsState(true, true, true, 9));
  EXPECT_EQ("fix", gpsState(true, true, true, 0));     // RMC's fix before GGA counts satellites
}

// A module that missed the check at boot still shows its fix once it has one.
TEST(BadgeLayoutGps, AFixOutranksTheModuleCheck) {
  EXPECT_EQ("fix 7", gpsState(true, false, true, 7));
}

// The longest word fits Settings' GPS row: " GPS", a space, then the value.
TEST(BadgeLayoutGps, NoGpsModuleFitsTheSettingsRow) {
  EXPECT_LE(textPx(kFixed, " GPS ") + textPx(kFixed, gpsState(true, false, false, 0).c_str()), kScreenPx);
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
  EXPECT_EQ(2, wrap(kFixed, "a b c d e", kCellPx, spans, 2));
}

// #1237: with a proportional face, what fits is measured, not counted. "iiiiiiii" is
// narrow and "MMMM" is wide, so the same box takes more of one than the other.
TEST(BadgeLayoutWrap, TheBodyFaceMeasuresEachCharacter) {
  const Face& body = bodyFace();
  EXPECT_LT(textPx(body, "iiiiiiii"), textPx(kFixed, "iiiiiiii"));
  EXPECT_EQ((std::vector<std::string>{"iiiiiiiiiiii"}), lines("iiiiiiiiiiii", 5, body));
  EXPECT_EQ((std::vector<std::string>{"MMMM", "MMMM"}), lines("MMMMMMMM", 4, body));
}

// However a line breaks, it never draws wider than its box -- except a box too narrow
// for even one character, where a line takes that character so the wrap still advances.
TEST(BadgeLayoutWrap, NoLineIsWiderThanItsBox) {
  const Face& body = bodyFace();
  const char* text = "flag drop at the CTF table, bring the badge and a laptop";
  for (int px = 2; px <= 128; px += 3) {
    Span spans[24];
    const int n = wrap(body, text, px, spans, 24);
    for (int i = 0; i < n; i++) {
      const bool fits = spanPx(body, text, spans[i]) <= px;
      EXPECT_TRUE(fits || spans[i].len == 1) << "at " << px << " px, line " << i;
    }
  }
}

// #1237: the faces stop at 0x7E. Anything else -- what translateUTF8ToBlocks leaves for
// an emoji or an accent -- measures as one block, which is what the screen draws.
TEST(BadgeLayoutFace, OutOfRangeCharactersMeasureAsBlocks) {
  const Face& body = bodyFace();
  const Face& meta = metaFace();
  EXPECT_EQ(body.missing, charPx(body, '\xDB'));
  EXPECT_EQ(meta.missing, charPx(meta, '\x07'));
  EXPECT_EQ(textPx(body, "ab") + body.missing, textPx(body, "a\xDB" "b"));
}

// #1238: the owner's three steps. Only the body text changes; the detail lines stay in
// the smallest face, as the mixed layout does.
TEST(BadgeLayoutTextSize, EachStepPicksItsBodyFace) {
  EXPECT_EQ(fixedFace().id, bodyFaceFor(kTextLarge).id);
  EXPECT_EQ(bodyFace().id, bodyFaceFor(kTextMedium).id);
  EXPECT_EQ(metaFace().id, bodyFaceFor(kTextSmall).id);
  EXPECT_EQ(metaFace().id, detailFace().id);
}

// #1244: the owner asked for the badge to start in the large size. NodePrefs ships the
// byte, not the enumerator, so this is what makes that byte mean large. The byte itself
// is held to this by a static_assert in BadgeScreens.cpp, which a native test cannot
// reach -- change one without the other and the badge stops building.
TEST(BadgeLayoutTextSize, ABadgeBootsIntoTheLargeFace) {
  EXPECT_EQ(0, (int)kDefaultTextSize);            // the value NodePrefs::ui_text_size holds
  EXPECT_EQ((int)kTextLarge, (int)kDefaultTextSize);
  EXPECT_EQ(fixedFace().id, bodyFaceFor(kDefaultTextSize).id);
}

// #1244: a size nothing ever wrote -- a corrupt preference, or one saved before #1238
// existed -- reads as the shipped default. It used to come back Org_01 while a fresh
// badge booted large, which is two answers to one question.
TEST(BadgeLayoutTextSize, AnUnknownSizeReadsAsTheShippedDefault) {
  for (int size : {99, 3, 255, -1}) {
    SCOPED_TRACE(size);
    EXPECT_EQ(bodyFaceFor(kDefaultTextSize).id, bodyFaceFor(size).id);
  }
}

// Each step down fits at least as many rows and characters as the one above.

TEST(BadgeLayoutTextSize, StepsGetSmaller) {
  int rows = 0, width = 0;
  for (int size = kTextLarge; size < kTextSteps; size++) {
    const Face& f = bodyFaceFor(size);
    EXPECT_GE(rowsFor(f), rows) << "step " << size;
    EXPECT_GE(kScreenPx / charPx(f, 'M'), width) << "step " << size;
    rows = rowsFor(f);
    width = kScreenPx / charPx(f, 'M');
  }
  EXPECT_EQ(8, rowsFor(bodyFaceFor(kTextLarge)));
  EXPECT_EQ(9, rowsFor(bodyFaceFor(kTextMedium)));
  EXPECT_EQ(10, rowsFor(bodyFaceFor(kTextSmall)));
}

// Every screen draws a title and then rows under it, and the GPS screen puts its last
// action on the final row. At each step that row has to be on the screen.
TEST(BadgeLayoutTextSize, TheLastRowFitsAtEveryStep) {
  for (int size = kTextLarge; size < kTextSteps; size++) {
    const Face& f = bodyFaceFor(size);
    const int list_rows = rowsFor(f) - 1;   // what the screens call kListRows
    EXPECT_GE(list_rows, 7) << "step " << size;
    EXPECT_LE((1 + list_rows - 1) * f.row_px + f.row_px, kScreenRowsPx) << "step " << size;
  }
}

// The Status screen's title is the body face and its lines are the detail face, so the
// lines have to fit in what the title leaves.
TEST(BadgeLayoutTextSize, StatusLinesFitUnderEveryTitle) {
  for (int size = kTextLarge; size < kTextSteps; size++) {
    const int title = bodyFaceFor(size).row_px;
    const int lines = (kScreenRowsPx - title - detailFace().row_px) / detailFace().row_px;
    EXPECT_GE(lines, 7) << "step " << size;   // six status lines and the footer
  }
}

TEST(BadgeLayoutTextSize, StepsAreNamed) {
  EXPECT_STREQ("large", textSizeName(kTextLarge));
  EXPECT_STREQ("medium", textSizeName(kTextMedium));
  EXPECT_STREQ("small", textSizeName(kTextSmall));
}

TEST(BadgeLayoutFace, FitPxCutsToWhatFits) {
  const Face& body = bodyFace();
  EXPECT_EQ(0, fitPx(body, "Abend", 2));
  EXPECT_EQ(5, fitPx(body, "Abend", 128));
  const int n = fitPx(body, "Abend", textPx(body, "Abe"));
  EXPECT_EQ(3, n);
}

// A channel message: the sender's tag in the left kTagPx, text after it, and the rows
// after it indented to match (design 1a, "Thread settled").
TEST(BadgeLayoutThread, ChannelMessageTagsItsFirstRow) {
  const MsgView msgs[] = {{false, "Abend", "anyone at the CTF table?"}};
  Row rows[8];
  const int n = layoutThread(kFixed, msgs, 1, true, -1, rows, 8);
  ASSERT_EQ(2, n);
  EXPECT_TRUE(rows[0].first);
  EXPECT_EQ(kTagPx + kGapPx, rows[0].x_px);
  EXPECT_EQ("anyone at the", rowText(msgs, rows[0]));
  EXPECT_FALSE(rows[1].first);
  EXPECT_EQ(kTagPx + kGapPx, rows[1].x_px);
  EXPECT_EQ("CTF table?", rowText(msgs, rows[1]));
  EXPECT_TRUE(rows[1].last);
}

// One sender in a DM, so no tag: the text gets the whole width.
TEST(BadgeLayoutThread, DmMessageUsesTheWholeWidth) {
  const MsgView msgs[] = {{false, "", "see you at the CTF table"}};
  Row rows[8];
  const int n = layoutThread(kFixed, msgs, 1, false, -1, rows, 8);
  ASSERT_EQ(2, n);
  EXPECT_EQ(0, rows[0].x_px);
  EXPECT_EQ("see you at the CTF", rowText(msgs, rows[0]));
}

// Mine sit flush right, clear of the status mark at the edge.
TEST(BadgeLayoutThread, OwnMessagesAreRightAligned) {
  const MsgView msgs[] = {{true, "", "omw, 5 min"}};
  Row rows[8];
  ASSERT_EQ(1, layoutThread(kFixed, msgs, 1, true, -1, rows, 8));
  EXPECT_EQ(kScreenPx - kMarkPx - kEdgePx - 10 * kCellPx, rows[0].x_px);
  EXPECT_TRUE(rows[0].first);
  EXPECT_TRUE(rows[0].last);
}

// The body face measures each character, so my message still ends at the same edge.
TEST(BadgeLayoutThread, OwnMessagesAreRightAlignedInTheBodyFace) {
  const Face& body = bodyFace();
  const MsgView msgs[] = {{true, "", "omw, 5 min"}};
  Row rows[8];
  ASSERT_EQ(1, layoutThread(body, msgs, 1, true, -1, rows, 8));
  EXPECT_EQ(kScreenPx - kMarkPx - kEdgePx - textPx(body, "omw, 5 min"), rows[0].x_px);
}

TEST(BadgeLayoutThread, MessagesStayInOrder) {
  const MsgView msgs[] = {{false, "Abend", "one"}, {true, "", "two"}, {false, "Stryc", "three"}};
  Row rows[8];
  ASSERT_EQ(3, layoutThread(kFixed, msgs, 3, true, -1, rows, 8));
  EXPECT_EQ(0, rows[0].msg);
  EXPECT_EQ(1, rows[1].msg);
  EXPECT_EQ(2, rows[2].msg);
}

// The selected message: a caret at the left on each of its rows, everything else clear
// of it, and one meta row after it (design 1a, "Message selected").
TEST(BadgeLayoutThread, SelectedMessageGetsACaretAndAMetaRow) {
  const MsgView msgs[] = {{false, "Abend", "anyone?"}, {false, "Stryc", "flag drop at the CTF table"}};
  Row rows[8];
  const int n = layoutThread(kFixed, msgs, 2, true, 1, rows, 8);
  ASSERT_EQ(4, n);   // "anyone?", two rows of Stryc's, and the meta row
  EXPECT_FALSE(rows[0].caret);
  EXPECT_TRUE(rows[1].caret);
  EXPECT_EQ(kTagPx + kGapPx + charPx(kFixed, '>') + 1, rows[1].x_px);   // clear of the caret
  EXPECT_EQ("flag drop at", rowText(msgs, rows[1]));
  EXPECT_TRUE(rows[2].caret);
  EXPECT_EQ(RowKind::Meta, rows[3].kind);
  EXPECT_EQ(1, rows[3].msg);
}

// The caret takes the body face's own '>' width, not a cell.
TEST(BadgeLayoutThread, TheBodyFaceShiftsForItsOwnCaret) {
  const Face& body = bodyFace();
  const MsgView msgs[] = {{false, "Abend", "anyone at the CTF table?"}};
  Row rows[8];
  ASSERT_LE(1, layoutThread(body, msgs, 1, true, 0, rows, 8));
  EXPECT_TRUE(rows[0].caret);
  EXPECT_EQ(kTagPx + kGapPx + charPx(body, '>') + 1, rows[0].x_px);
}

TEST(BadgeLayoutThread, StopsAtMaxRows) {
  const MsgView msgs[] = {{false, "", "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn"}};
  Row rows[3];
  EXPECT_EQ(3, layoutThread(kFixed, msgs, 1, false, -1, rows, 3));
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
