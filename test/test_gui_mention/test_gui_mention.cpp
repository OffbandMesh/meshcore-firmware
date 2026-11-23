// #2234: mentions from the GUI's compose, the pure rules (helpers/gui/Mention).
// The screen around them is test_gui_shell_flows.

#include <gtest/gtest.h>

#include <string.h>

#include <string>

#include "helpers/gui/Mention.h"

using namespace offband::gui;

namespace {

NodeRow row(const char* name, NodeKind kind, bool heard, uint32_t age) {
  NodeRow r{};
  snprintf(r.name, sizeof r.name, "%s", name);
  r.kind = kind;
  r.heard = heard;
  r.heard_age_s = age;
  return r;
}

std::string joined(const char* const* names, size_t n) {
  std::string s;
  for (size_t i = 0; i < n; i++) {
    if (i) s += "|";
    s += names[i];
  }
  return s;
}

std::string picks(const char* query, const char* const* recent, size_t nrecent, const NodeRow* rows, size_t nrows,
                  const char* self = "Bench-7", size_t max = kMentionShown) {
  const char* out[8] = {};
  const size_t n = mentionPicks(query, strlen(query), recent, nrecent, rows, nrows, self, out, max);
  return joined(out, n);
}

void draft(ComposeBuffer& b, const char* text, size_t budget = 140) {
  b.start(budget);
  b.insertText(text);
}

}  // namespace

// ------------------------------------------------------------- openMention

TEST(OpenMention, AnAtAtTheStartOpensOne) {
  size_t at = 99;
  EXPECT_TRUE(openMention("@", 1, &at));
  EXPECT_EQ(at, 0u);
  EXPECT_TRUE(openMention("@Wr", 3, &at));
  EXPECT_EQ(at, 0u);
}

TEST(OpenMention, AnAtAfterASpaceOrPunctuationOpensOne) {
  size_t at = 0;
  EXPECT_TRUE(openMention("hi @Wr", 6, &at));
  EXPECT_EQ(at, 3u);
  EXPECT_TRUE(openMention("(@Wr", 4, &at));
  EXPECT_EQ(at, 1u);
}

TEST(OpenMention, AnAtInsideAWordIsNotOne) {
  EXPECT_FALSE(openMention("me@home", 7, nullptr));
  EXPECT_FALSE(openMention("x9@", 3, nullptr));
}

TEST(OpenMention, ASpaceOrAClosedTokenEndsIt) {
  EXPECT_FALSE(openMention("@Wren ", 6, nullptr));
  EXPECT_FALSE(openMention("@[Wren]", 7, nullptr));
  EXPECT_FALSE(openMention("@[Wren] ", 8, nullptr));
  EXPECT_FALSE(openMention("", 0, nullptr));
  EXPECT_FALSE(openMention("hello", 5, nullptr));
}

// ------------------------------------------------------------ mentionPicks

TEST(MentionPicks, RecentSendersComeFirstNewestFirstThenContacts) {
  const char* recent[] = {"Wren", "River"};
  const NodeRow rows[] = {row("Ash", NodeKind::Chat, true, 50), row("Birch", NodeKind::Chat, true, 10)};
  EXPECT_EQ(picks("", recent, 2, rows, 2), "Wren|River|Birch|Ash");
}

TEST(MentionPicks, EachLetterNarrowsByPrefixIgnoringCase) {
  const char* recent[] = {"Wren", "wrangler", "River"};
  const NodeRow rows[] = {row("WRIGHT", NodeKind::Chat, true, 5), row("Awry", NodeKind::Chat, true, 1)};
  EXPECT_EQ(picks("wr", recent, 3, rows, 2), "Wren|wrangler|WRIGHT");
  EXPECT_EQ(picks("WRE", recent, 3, rows, 2), "Wren");
  EXPECT_EQ(picks("x", recent, 3, rows, 2), "");
}

TEST(MentionPicks, OwnNameIsNeverOffered) {
  const char* recent[] = {"bench-7", "Wren"};
  const NodeRow rows[] = {row("Bench-7", NodeKind::Chat, true, 1)};
  EXPECT_EQ(picks("", recent, 2, rows, 1), "Wren");
}

TEST(MentionPicks, ANameIsOfferedOnceIgnoringCase) {
  const char* recent[] = {"Wren", "WREN"};
  const NodeRow rows[] = {row("wren", NodeKind::Chat, true, 1), row("Ash", NodeKind::Chat, true, 2)};
  EXPECT_EQ(picks("", recent, 2, rows, 2), "Wren|Ash");
}

TEST(MentionPicks, OnlyChatContactsAndNeverHeardLast) {
  const NodeRow rows[] = {row("Hilltop RPT", NodeKind::Repeater, true, 1), row("Quiet", NodeKind::Chat, false, 0),
                          row("Room 1", NodeKind::Room, true, 2), row("Ash", NodeKind::Chat, true, 900)};
  EXPECT_EQ(picks("", nullptr, 0, rows, 4), "Ash|Quiet");
}

TEST(MentionPicks, ContactsOfTheSameAgeAllCome) {
  const NodeRow rows[] = {row("Ash", NodeKind::Chat, true, 30), row("Birch", NodeKind::Chat, true, 30),
                          row("Cedar", NodeKind::Chat, true, 5)};
  EXPECT_EQ(picks("", nullptr, 0, rows, 3), "Cedar|Ash|Birch");
}

TEST(MentionPicks, NamesThatCantMakeAMentionAreLeftOut) {
  const char* recent[] = {"", "   ", "odd]name", "Wren"};
  EXPECT_EQ(picks("", recent, 4, nullptr, 0), "Wren");
}

TEST(MentionPicks, StopsAtMax) {
  const char* recent[] = {"A1", "A2", "A3", "A4", "A5"};
  EXPECT_EQ(picks("a", recent, 5, nullptr, 0, "Bench-7", 3), "A1|A2|A3");
}

// ----------------------------------------------------------- RecentSenders

TEST(RecentSenders, NewestFirstPerChannel) {
  RecentSenders r;
  r.heard(1, "Wren");
  r.heard(2, "Other");
  r.heard(1, "River");
  const char* out[8];
  EXPECT_EQ(joined(out, r.list(1, out, 8)), "River|Wren");
  EXPECT_EQ(joined(out, r.list(2, out, 8)), "Other");
  EXPECT_EQ(r.list(3, out, 8), 0u);
}

TEST(RecentSenders, HearingAgainMovesToNewestWithoutADuplicate) {
  RecentSenders r;
  r.heard(1, "Wren");
  r.heard(1, "River");
  r.heard(1, "Wren");
  const char* out[8];
  EXPECT_EQ(joined(out, r.list(1, out, 8)), "Wren|River");
}

TEST(RecentSenders, KeepsTheFullNameTheStoreCuts) {
  RecentSenders r;
  const char* longName = "offband.org-GUI1\xF0\x9F\x9B\xB0\xEF\xB8\x8F";   // 23 bytes, satellite + VS16
  r.heard(1, longName);
  const char* out[2];
  ASSERT_EQ(r.list(1, out, 2), 1u);
  EXPECT_STREQ(out[0], longName);
}

TEST(RecentSenders, TheOneHeardLongestAgoMakesRoom) {
  RecentSenders r;
  char name[8];
  for (size_t i = 0; i < RecentSenders::kCap + 1; i++) {
    snprintf(name, sizeof name, "N%u", unsigned(i));
    r.heard(1, name);
  }
  const char* out[RecentSenders::kCap + 1];
  const size_t n = r.list(1, out, RecentSenders::kCap + 1);
  EXPECT_EQ(n, RecentSenders::kCap);
  EXPECT_STREQ(out[0], "N32");
  for (size_t i = 0; i < n; i++) EXPECT_STRNE(out[i], "N0");
}

TEST(RecentSenders, EmptyOrTooLongIsNotKept) {
  RecentSenders r;
  r.heard(1, "");
  r.heard(1, nullptr);
  r.heard(-1, "Wren");
  r.heard(1, "0123456789012345678901234567890123");   // 34 bytes
  const char* out[4];
  EXPECT_EQ(r.list(1, out, 4), 0u);
  EXPECT_EQ(r.list(-1, out, 4), 0u);
}

// ------------------------------------------------------------- applyMention

TEST(ApplyMention, ReplacesTheQueryWithTheTokenAndASpace) {
  ComposeBuffer b;
  draft(b, "hi @wr");
  ASSERT_TRUE(applyMention(b, 3, "Wren"));
  EXPECT_STREQ(b.text(), "hi @[Wren] ");
}

TEST(ApplyMention, TheNameGoesInByteForByte) {
  ComposeBuffer b;
  draft(b, "@off");
  const char* name = " offband.org-GUI1\xF0\x9F\x9B\xB0\xEF\xB8\x8F";   // a leading space is part of it
  ASSERT_TRUE(applyMention(b, 0, name));
  EXPECT_EQ(std::string(b.text()), std::string("@[") + name + "] ");
}

TEST(ApplyMention, ATokenThatDoesntFitChangesNothing) {
  ComposeBuffer b;
  draft(b, "0123456789@W", 16);   // "@[Wren]" needs 7 bytes from offset 10: 17 > 16
  EXPECT_FALSE(applyMention(b, 10, "Wren"));
  EXPECT_STREQ(b.text(), "0123456789@W");
}

TEST(ApplyMention, OnlyTheSpaceIsLeftOffWhenOnlyItDoesntFit) {
  ComposeBuffer b;
  draft(b, "012345678@W", 16);   // "@[Wren]" ends at 16 exactly
  ASSERT_TRUE(applyMention(b, 9, "Wren"));
  EXPECT_STREQ(b.text(), "012345678@[Wren]");
}

TEST(ApplyMention, AMalformedNameChangesNothing) {
  ComposeBuffer b;
  draft(b, "@W");
  EXPECT_FALSE(applyMention(b, 0, "W\xFFren"));
  EXPECT_STREQ(b.text(), "@W");
  EXPECT_FALSE(applyMention(b, 0, "odd]name"));
  EXPECT_STREQ(b.text(), "@W");
}

TEST(ApplyMention, OnlyAtAnAt) {
  ComposeBuffer b;
  draft(b, "hi @W");
  EXPECT_FALSE(applyMention(b, 1, "Wren"));
  EXPECT_FALSE(applyMention(b, 9, "Wren"));
  EXPECT_STREQ(b.text(), "hi @W");
}

TEST(ComposeTruncate, BacksUpToACharacterStart) {
  ComposeBuffer b;
  draft(b, "a\xC3\xA9z");   // a é z
  b.truncate(2);            // inside é
  EXPECT_STREQ(b.text(), "a");
  b.truncate(5);            // never lengthens
  EXPECT_STREQ(b.text(), "a");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
