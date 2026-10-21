// Native tests for the UI screen-label resolver (#973).
//
// The defect this guards against is not "the wrong name is printed". It is that the
// trace printed a CONFIDENT, PLAUSIBLE, WRONG name: with `home` null, a transition to
// nothing resolved as "HOME". A reader of that log sees a healthy screen handoff and
// goes looking at the display driver, which is exactly what happened on RC52 and cost
// a flash cycle, a purpose-built driver instrument and a UART sniffer.
//
// So the important tests here are the NEGATIVE ones: NullWithNullMembersIsNotAMember
// and NullTableEntryNeverMatches. A resolver tested only with well-formed tables of
// non-null pointers passes just as happily with the original broken ordering.

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include <helpers/ui/ScreenName.h>

using namespace offband;

namespace {

// Stand-ins for the screen objects. Only their addresses matter.
int splash_obj, home_obj, msg_preview_obj, stranger_obj;

const void* kSplash = &splash_obj;
const void* kHome = &home_obj;
const void* kMsgPreview = &msg_preview_obj;
const void* kStranger = &stranger_obj;

}  // namespace

// ---------------------------------------------------------------- positive cases

TEST(ScreenName, ResolvesEachKnownScreen) {
  const ScreenLabel tbl[] = {
      {kSplash, "SPLASH"}, {kHome, "HOME"}, {kMsgPreview, "MSG_PREVIEW"}};
  EXPECT_STREQ("SPLASH", screenName(kSplash, tbl, 3));
  EXPECT_STREQ("HOME", screenName(kHome, tbl, 3));
  EXPECT_STREQ("MSG_PREVIEW", screenName(kMsgPreview, tbl, 3));
}

TEST(ScreenName, NullResolvesToNullWhenEveryMemberIsLive) {
  const ScreenLabel tbl[] = {
      {kSplash, "SPLASH"}, {kHome, "HOME"}, {kMsgPreview, "MSG_PREVIEW"}};
  EXPECT_STREQ("NULL", screenName(nullptr, tbl, 3));
}

// ---------------------------------------------------------------- negative cases
//
// These are the regression tests for the actual #973 defect.

// THE one that matters. `home` failed to allocate, so it is null; the UI then
// transitions to nothing. The old chain tested `c == home` first and printed "HOME"
// for a transition to NOTHING. It must say NULL.
TEST(ScreenName, NullWithNullMembersIsNotAMember) {
  const ScreenLabel tbl[] = {
      {kSplash, "SPLASH"}, {nullptr, "HOME"}, {kMsgPreview, "MSG_PREVIEW"}};
  EXPECT_STREQ("NULL", screenName(nullptr, tbl, 3));
}

// The CardKB badge sets `msg_preview = NULL` deliberately, so this fires on a healthy
// build, not only on an allocation failure.
TEST(ScreenName, NullWithADeliberatelyNullMemberIsStillNull) {
  const ScreenLabel tbl[] = {
      {kSplash, "SPLASH"}, {kHome, "HOME"}, {nullptr, "MSG_PREVIEW"}};
  EXPECT_STREQ("NULL", screenName(nullptr, tbl, 3));
}

// Every member null -- the worst case, where the old chain would name the first entry.
TEST(ScreenName, NullWithAllMembersNullIsStillNull) {
  const ScreenLabel tbl[] = {
      {nullptr, "SPLASH"}, {nullptr, "HOME"}, {nullptr, "MSG_PREVIEW"}};
  EXPECT_STREQ("NULL", screenName(nullptr, tbl, 3));
}

// The mirror image: a null table entry must not swallow a live pointer.
TEST(ScreenName, NullTableEntryNeverMatches) {
  const ScreenLabel tbl[] = {{nullptr, "HOME"}, {kSplash, "SPLASH"}};
  EXPECT_STREQ("SPLASH", screenName(kSplash, tbl, 2));
  EXPECT_STREQ("?", screenName(kStranger, tbl, 2));
}

// ---------------------------------------------------------------- edges

TEST(ScreenName, UnknownPointerIsQuestionMark) {
  const ScreenLabel tbl[] = {{kSplash, "SPLASH"}, {kHome, "HOME"}};
  EXPECT_STREQ("?", screenName(kStranger, tbl, 2));
}

TEST(ScreenName, EmptyTable) {
  const ScreenLabel tbl[] = {{kSplash, "SPLASH"}};
  EXPECT_STREQ("NULL", screenName(nullptr, tbl, 0));
  EXPECT_STREQ("?", screenName(kSplash, tbl, 0));
}

TEST(ScreenName, NullTablePointer) {
  EXPECT_STREQ("NULL", screenName(nullptr, nullptr, 0));
  EXPECT_STREQ("?", screenName(kSplash, nullptr, 0));
}

TEST(ScreenName, FirstMatchWinsForADuplicatedPointer) {
  const ScreenLabel tbl[] = {{kHome, "HOME"}, {kHome, "TOOLS"}};
  EXPECT_STREQ("HOME", screenName(kHome, tbl, 2));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
