// Native tests for the trigger-matching rule.
//
// Every rule gets a POSITIVE and a NEGATIVE case, and for the composite gate every one of
// its conditions is removed in turn. That shape is deliberate: a gate tested only with
// messages that should fire will pass just as happily if it fires on everything, which is
// the failure that matters here -- a badge that sings at every message is worse than one
// that never does.
//
// The single most important test in the file is TheTrapActuallyTraps: the derived marker
// has to FAIL when it is copied from another sender. If that ever passes for the wrong
// sender, the whole mechanism has collapsed into a fixed marker and nobody would notice
// from the outside, because the message still looks right.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include <helpers/ui/TuneTrigger.h>

using namespace offband;

namespace {

// The mark, in its three accepted spellings, plus a near-miss.
const std::string kSat = "\xF0\x9F\x9B\xB0";                  // U+1F6F0
const std::string kSatVs16 = kSat + "\xEF\xB8\x8F";           // + U+FE0F
const std::string kSatVs15 = kSat + "\xEF\xB8\x8E";           // + U+FE0E
const std::string kOtherEmoji = "\xF0\x9F\x95\xBA";           // U+1F57A, a different glyph

const std::string kEnDash = "\xE2\x80\x93";                   // U+2013
const std::string kEmDash = "\xE2\x80\x94";                   // U+2014

// The owner's own badge name and the pattern that follows from it: 4th letter of
// "Strycher-QCC" is 'y', and Morse for Y is -.--
const std::string kOwner = "Strycher-QCC" + kSatVs16;

bool fires(const std::string& name, const std::string& body) {
  return messageFiresTrigger(name.c_str(), body.c_str());
}

// So a test can build a body by concatenation without a .c_str() at every call.
bool endsMorse(const std::string& body, char letter) {
  return bodyEndsWithMorseFor(body.c_str(), letter);
}

}  // namespace

// ---- the mark on the sender's name ------------------------------------------------------

TEST(Mark, AnyOfTheThreeSpellingsCounts) {
  for (const std::string& m : {kSat, kSatVs16, kSatVs15}) {
    const std::string n = "Strycher" + m;
    EXPECT_TRUE(nameHasMark(n.c_str(), n.size())) << "spelling of length " << m.size();
  }
}

TEST(Mark, ANameWithoutItIsRefused) {
  EXPECT_FALSE(nameHasMark("Strycher", 8));
  EXPECT_FALSE(nameHasMark("", 0));
  EXPECT_FALSE(nameHasMark(nullptr, 5));
}

TEST(Mark, ADifferentEmojiIsNotTheMark) {
  const std::string n = "Strycher" + kOtherEmoji;
  EXPECT_FALSE(nameHasMark(n.c_str(), n.size()));
}

TEST(Mark, ItHasToBeAtTheEnd) {
  // Containing the character is not carrying the mark -- the rule is checkable by looking
  // at the last glyph, which it would not be if a mark anywhere counted.
  const std::string n = kSat + "Strycher";
  EXPECT_FALSE(nameHasMark(n.c_str(), n.size()));
  const std::string mid = "Stry" + kSat + "cher";
  EXPECT_FALSE(nameHasMark(mid.c_str(), mid.size()));
}

TEST(Mark, StrippingItLeavesTheNameAlone) {
  EXPECT_EQ(8u, nameLenWithoutMark(kOwner.c_str(), 8));          // no mark in range
  const std::string n = "Strycher" + kSatVs16;
  EXPECT_EQ(8u, nameLenWithoutMark(n.c_str(), n.size()));
  const std::string bare = "Strycher" + kSat;
  EXPECT_EQ(8u, nameLenWithoutMark(bare.c_str(), bare.size()));
  EXPECT_EQ(8u, nameLenWithoutMark("Strycher", 8));              // unchanged when absent
}

// ---- which letter the Morse comes from --------------------------------------------------

TEST(Letter, TheOwnersNameGivesY) {
  EXPECT_EQ('Y', triggerLetterFor(kOwner.c_str()));
}

TEST(Letter, TheThreeSpellingsAgree) {
  // The loose match must not change WHICH letter is chosen.
  for (const std::string& m : {kSat, kSatVs16, kSatVs15}) {
    const std::string n = "Strycher-QCC" + m;
    EXPECT_EQ('Y', triggerLetterFor(n.c_str())) << "spelling of length " << m.size();
  }
}

TEST(Letter, NonLettersAreSkippedNotCounted) {
  // 4th LETTER, not 4th character. "QCC6030E" has letters Q,C,C,E -- the digits do not
  // count, so the answer is E and not '6' (which has no Morse in the table at all).
  const std::string n = "QCC6030E" + kSatVs16;
  EXPECT_EQ('E', triggerLetterFor(n.c_str()));
  const std::string punct = "S-t.r_y" + kSatVs16;
  EXPECT_EQ('Y', triggerLetterFor(punct.c_str()));
}

TEST(Letter, CaseDoesNotMatter) {
  const std::string lower = "strycher" + kSatVs16;
  EXPECT_EQ('Y', triggerLetterFor(lower.c_str()));
}

TEST(Letter, FewerThanFourLettersCannotPlay) {
  for (const char* base : {"", "A", "AB", "ABC", "A1B2C3"}) {
    const std::string n = std::string(base) + kSatVs16;
    EXPECT_EQ(0, triggerLetterFor(n.c_str())) << "name=" << base;
  }
}

TEST(Letter, TheMarkIsNeverCountedAsNameContent) {
  // A four-letter name plus the mark still yields its own fourth letter. If the mark's
  // bytes leaked into the count this would shift or fail.
  const std::string n = "Stry" + kSatVs16;
  EXPECT_EQ('Y', triggerLetterFor(n.c_str()));
}

TEST(Letter, NthLetterIsOneBased) {
  EXPECT_EQ('S', nthLetter("Strycher", 8, 1));
  EXPECT_EQ('Y', nthLetter("Strycher", 8, 4));
  EXPECT_EQ(0, nthLetter("Strycher", 8, 9));
  EXPECT_EQ(0, nthLetter("Strycher", 8, 0));
  EXPECT_EQ(0, nthLetter(nullptr, 8, 1));
}

// ---- the conspicuous marker -------------------------------------------------------------

TEST(Decoy, ItIsFoundAnywhereInTheBody) {
  EXPECT_TRUE(bodyHasDecoy("0x4"));
  EXPECT_TRUE(bodyHasDecoy("the 0x4 badge"));
  EXPECT_TRUE(bodyHasDecoy("ends with 0x4"));
}

TEST(Decoy, TheXIsCaseInsensitive) {
  EXPECT_TRUE(bodyHasDecoy("0X4"));
  EXPECT_TRUE(bodyHasDecoy("see 0X4 here"));
}

TEST(Decoy, NearMissesAreRefused) {
  EXPECT_FALSE(bodyHasDecoy("0x5"));
  EXPECT_FALSE(bodyHasDecoy("x4"));
  EXPECT_FALSE(bodyHasDecoy("0x"));
  EXPECT_FALSE(bodyHasDecoy("04"));
  EXPECT_FALSE(bodyHasDecoy(""));
  EXPECT_FALSE(bodyHasDecoy(nullptr));
}

// ---- the derived marker on the end of the body ------------------------------------------

TEST(MorseTail, TheExactPatternMatches) {
  EXPECT_TRUE(bodyEndsWithMorseFor("hello -.--", 'Y'));
  EXPECT_TRUE(bodyEndsWithMorseFor("-.--", 'Y'));
}

TEST(MorseTail, AnEmDashStandsForTwoHyphens) {
  // What a phone keyboard produces from "-.--". The arithmetic is the trap: one dash
  // character replaces TWO hyphens, so a naive map to a single '-' would match 'K' (-.-).
  EXPECT_TRUE(endsMorse("hello -." + kEmDash, 'Y'));
  EXPECT_FALSE(endsMorse("hello -." + kEmDash, 'K'))
      << "an em dash collapsed to one hyphen would match K and fire for the wrong letter";
}

TEST(MorseTail, AnEnDashDoesTheSame) {
  EXPECT_TRUE(endsMorse("hello -." + kEnDash, 'Y'));
}

TEST(MorseTail, TrailingWhitespaceIsTolerated) {
  EXPECT_TRUE(bodyEndsWithMorseFor("hello -.--  ", 'Y'));
  EXPECT_TRUE(bodyEndsWithMorseFor("hello -.--\t", 'Y'));
  EXPECT_TRUE(bodyEndsWithMorseFor("hello -.--\r\n", 'Y'));
}

TEST(MorseTail, AnotherLettersPatternIsRefused) {
  EXPECT_FALSE(bodyEndsWithMorseFor("hello -.--", 'J'));   // J is .---
  EXPECT_FALSE(bodyEndsWithMorseFor("hello .---", 'Y'));
}

TEST(MorseTail, ThePatternHasToBeAtTheEnd) {
  EXPECT_FALSE(bodyEndsWithMorseFor("-.-- hello", 'Y'));
  EXPECT_FALSE(bodyEndsWithMorseFor("say -.-- again", 'Y'));
}

TEST(MorseTail, AnythingAfterItThatIsNotWhitespaceIsARefusal) {
  EXPECT_FALSE(bodyEndsWithMorseFor("hello -.--!", 'Y'));
  EXPECT_FALSE(bodyEndsWithMorseFor("hello -.--.", 'Y'))
      << "an extra dot makes it a different pattern, not a tolerated suffix";
}

TEST(MorseTail, ATruncatedPatternIsRefused) {
  EXPECT_FALSE(bodyEndsWithMorseFor("hello -.-", 'Y'));
  EXPECT_FALSE(bodyEndsWithMorseFor("hello -.", 'Y'));
  EXPECT_FALSE(bodyEndsWithMorseFor("", 'Y'));
}

TEST(MorseTail, ALetterWithNoMorseIsRefused) {
  // morseFor() covers A-Z only. A digit or punctuation must refuse rather than match
  // anything, which is what makes the letters-only rule safe.
  EXPECT_FALSE(bodyEndsWithMorseFor("hello -.--", '6'));
  EXPECT_FALSE(bodyEndsWithMorseFor("hello -.--", 0));
  EXPECT_FALSE(bodyEndsWithMorseFor(nullptr, 'Y'));
}

TEST(MorseTail, ALongBodyStillMatchesOnItsTail) {
  // Only the tail can decide, so a body far longer than the scan window must still work.
  EXPECT_TRUE(bodyEndsWithMorseFor((std::string(500, 'x') + " -.--").c_str(), 'Y'));
  EXPECT_FALSE(bodyEndsWithMorseFor((std::string(500, 'x') + " .---").c_str(), 'Y'));
}

TEST(MorseTail, ADashStraddlingTheScanWindowIsStillSeen) {
  // The window is aligned to a UTF-8 lead byte precisely so this cannot half-read a dash.
  for (size_t pad = 18; pad <= 30; pad++) {
    const std::string body = std::string(pad, 'x') + " -." + kEmDash;
    EXPECT_TRUE(bodyEndsWithMorseFor(body.c_str(), 'Y')) << "pad=" << pad;
  }
}

// ---- the composite gate -----------------------------------------------------------------

TEST(Gate, AllFourConditionsPresentFires) {
  EXPECT_TRUE(fires(kOwner, "the 0x4 badge is great -.--"));
}

TEST(Gate, RemovingTheMarkStopsIt) {
  EXPECT_FALSE(fires("Strycher-QCC", "the 0x4 badge is great -.--"));
}

TEST(Gate, RemovingTheDecoyStopsIt) {
  EXPECT_FALSE(fires(kOwner, "the badge is great -.--"));
}

TEST(Gate, RemovingTheMorseStopsIt) {
  EXPECT_FALSE(fires(kOwner, "the 0x4 badge is great"));
}

TEST(Gate, AnUnplayableNameStopsIt) {
  const std::string shortName = "Stu" + kSatVs16;   // only three letters
  EXPECT_FALSE(fires(shortName, "the 0x4 badge is great -.--"));
}

TEST(Gate, TheTrapActuallyTraps) {
  // THE TEST THIS FILE EXISTS FOR. Somebody watches the hall sing, reads the message on
  // their phone, copies it verbatim -- mark, decoy, dots and dashes -- and sends it from
  // their own badge. It must do nothing, because the pattern follows from the SENDER's
  // name and theirs is different.
  const std::string copycat = "Benjamin" + kSatVs16;      // 4th letter J, so .---
  EXPECT_EQ('J', triggerLetterFor(copycat.c_str()));
  EXPECT_FALSE(fires(copycat, "the 0x4 badge is great -.--"))
      << "a pattern copied from another sender fired -- the derived key has collapsed "
         "into a fixed one";

  // And the same sender, using THEIR own pattern, does fire.
  EXPECT_TRUE(fires(copycat, "the 0x4 badge is great .---"));
}

TEST(Gate, DegenerateInputsAreRefused) {
  EXPECT_FALSE(messageFiresTrigger(nullptr, "the 0x4 badge -.--"));
  EXPECT_FALSE(messageFiresTrigger(kOwner.c_str(), nullptr));
  EXPECT_FALSE(messageFiresTrigger(nullptr, nullptr));
  EXPECT_FALSE(fires("", ""));
  EXPECT_FALSE(fires(kSatVs16, "the 0x4 badge -.--"));   // the mark alone, no letters
}

TEST(Gate, OrdinaryTrafficNeverFires) {
  // The population this has to be quiet against: real messages from a marked sender that
  // happen to contain dashes, dots, or the decoy on its own.
  const char* ordinary[] = {
      "hello",
      "see you at 0x4",
      "running late -- be there soon",
      "0x4 was great...",
      "my callsign is -.-- in morse",
      "-.-- 0x4",
      "0x4 -.-- extra",
      "nothing here at all",
  };
  for (const char* b : ordinary) {
    EXPECT_FALSE(fires(kOwner, b)) << "fired on ordinary traffic: " << b;
  }
}

TEST(Gate, TheOwnersWorkedExampleEndToEnd) {
  // Exactly as the owner specified it: a badge named Strycher-QCC<mark> trails with the
  // Morse for 'y'.
  EXPECT_EQ('Y', triggerLetterFor(kOwner.c_str()));
  EXPECT_STREQ("-.--", morseFor('Y'));
  EXPECT_TRUE(fires(kOwner, "0x4 -.--"));
}

// ---- the channel wire format, and the truncation trap it exists to prevent -------------

TEST(ChannelText, TheOwnersMessageOnTheWireFires) {
  const std::string wire = kOwner + ": the 0x4 badge is great -.--";
  EXPECT_TRUE(messageFiresTriggerOnChannelText(wire.c_str()));
}

TEST(ChannelText, ALongNameStillCarriesItsMark) {
  // THE TRUNCATION TRAP. The badge store caps a sender at 15 bytes and the mark is 7 with
  // its variation selector, so this name keeps the mark on the wire and would lose it in
  // the store. Splitting from the wire text is what makes that impossible to get wrong.
  const std::string longName = "Strycher-Pyramid" + kSatVs16;   // 23 bytes
  ASSERT_GT(longName.size(), 15u) << "pointless unless the store would truncate it";
  ASSERT_LE(longName.size(), 31u) << "pointless unless MeshCore would carry it";
  EXPECT_EQ('Y', triggerLetterFor(longName.c_str()));
  const std::string wire = longName + ": 0x4 -.--";
  EXPECT_TRUE(messageFiresTriggerOnChannelText(wire.c_str()));
}

TEST(ChannelText, ANameLongerThanMeshCoreAllowsIsRefused) {
  // The split only looks for ": " within the first 31 bytes, because that is the longest
  // name MeshCore carries. A longer prefix is not a sender, so the text cannot fire. An
  // earlier version of the test above used a 33-byte name and expected it to work; it
  // could never have arrived that way.
  const std::string tooLong = "Strycher-QCC-Pyramid-Badge" + kSatVs16;   // 33 bytes
  ASSERT_GT(tooLong.size(), 31u);
  EXPECT_FALSE(messageFiresTriggerOnChannelText((tooLong + ": 0x4 -.--").c_str()));
}

TEST(ChannelText, TheFirstColonSpaceIsTheSplit) {
  // A body that itself contains ": " must not re-split; the sender is the first one.
  const std::string wire = kOwner + ": re: 0x4 -.--";
  EXPECT_TRUE(messageFiresTriggerOnChannelText(wire.c_str()));
}

TEST(ChannelText, TextWithNoSenderCannotFire) {
  EXPECT_FALSE(messageFiresTriggerOnChannelText("0x4 -.--"));
  EXPECT_FALSE(messageFiresTriggerOnChannelText(": 0x4 -.--"));
  EXPECT_FALSE(messageFiresTriggerOnChannelText(""));
  EXPECT_FALSE(messageFiresTriggerOnChannelText(nullptr));
}

TEST(ChannelText, ASeparatorPastTheNameLimitIsNotASender) {
  // A MeshCore name is at most 31 bytes, so a ": " beyond that is body content and the
  // text has no sender -- matching splitSender's rule rather than inventing a new one.
  const std::string wire = std::string(40, 'x') + ": 0x4 -.--";
  EXPECT_FALSE(messageFiresTriggerOnChannelText(wire.c_str()));
}

TEST(ChannelText, AnUnmarkedSenderOnTheWireIsRefused) {
  const std::string wire = "Strycher-QCC: the 0x4 badge is great -.--";
  EXPECT_FALSE(messageFiresTriggerOnChannelText(wire.c_str()));
}

TEST(ChannelText, TheTrapAlsoTrapsOnTheWire) {
  const std::string copycat = "Benjamin" + kSatVs16;
  EXPECT_FALSE(messageFiresTriggerOnChannelText((copycat + ": 0x4 -.--").c_str()));
  EXPECT_TRUE(messageFiresTriggerOnChannelText((copycat + ": 0x4 .---").c_str()));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
