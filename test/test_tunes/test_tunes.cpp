// Native unit tests for the tune table and the RTTTL validator (#1418).
//
// Two things are pinned here. First, the table's invariants: id 0 is silence, ids are
// in range, the picker lists only named entries, and nothing can index past the end --
// because a stored pref holds an id and a badge can be downgraded to a firmware with a
// shorter table. Second, the validator, which is what stands between a string typed on
// a tiny keyboard (#1413) and the player.

#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include <string>
#include "helpers/ui/Tunes.h"

using namespace offband;

// ---------------------------------------------------------------- the table

TEST(TuneTable, SilentIsIdZeroAndHasNoMelody) {
  EXPECT_EQ(0, kTuneSilent);
  EXPECT_EQ(nullptr, tuneRtttl(kTuneSilent));
  EXPECT_STREQ("Silent", tuneName(kTuneSilent));
}

// Every shipped melody must actually be RTTTL. A typo in the table is a tune that
// never plays and gives no clue why.
TEST(TuneTable, EveryShippedMelodyIsValidRtttl) {
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    const char* r = tuneRtttl(id);
    if (id == kTuneSilent) {
      EXPECT_EQ(nullptr, r) << "id " << (int)id << " should be silence";
      continue;
    }
    ASSERT_NE(nullptr, r) << "id " << (int)id << " has no melody";
    EXPECT_EQ(RtttlProblem::Ok, rtttlProblem(r))
        << "id " << (int)id << " (" << (tuneName(id) ? tuneName(id) : "<unlisted>")
        << ") is not valid RTTTL: " << rtttlProblemText(rtttlProblem(r));
  }
}

// The three tones that were literals at their call sites are still here, so moving
// them into the table lost nothing a user could notice.
TEST(TuneTable, TheToneAlreadyInUseSurvived) {
  std::set<std::string> melodies;
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    if (tuneRtttl(id)) melodies.insert(tuneRtttl(id));
  }
  EXPECT_EQ(1u, melodies.count("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7"));
  EXPECT_EQ(1u, melodies.count("kerplop:d=16,o=6,b=120:32g#,32c#"));
  EXPECT_EQ(1u, melodies.count("ack:d=32,o=8,b=120:c"));
}

// An id from a newer firmware's longer table, left in prefs after a downgrade, must
// not index past the array. This is the downgrade case the review called out.
TEST(TuneTable, AnIdPastTheTableResolvesToNothingRatherThanReadingPastIt) {
  EXPECT_FALSE(isBuiltinTune(kBuiltinCount));
  EXPECT_EQ(nullptr, tuneRtttl(kBuiltinCount));
  EXPECT_EQ(nullptr, tuneName(kBuiltinCount));
  EXPECT_EQ(nullptr, tuneRtttl(200));
  EXPECT_EQ(nullptr, tuneRtttl(255));
  for (int id = 0; id <= 255; id++) {
    // No id, in range or not, may crash or return a non-null name while out of range.
    const char* n = tuneName((uint8_t)id);
    if (id >= kBuiltinCount) EXPECT_EQ(nullptr, n) << "id " << id;
  }
}

TEST(TuneTable, EveryBuiltinIsInRangeAndEveryListedOneHasAName) {
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    EXPECT_TRUE(isBuiltinTune(id));
  }
  EXPECT_LE(visibleTuneCount(), kBuiltinCount);
  for (uint8_t nth = 0; nth < visibleTuneCount(); nth++) {
    const uint8_t id = visibleTuneId(nth);
    EXPECT_TRUE(isBuiltinTune(id));
    EXPECT_NE(nullptr, tuneName(id)) << "listed entry " << (int)nth << " has no name";
  }
}

// Walking off the end of the picker returns silence, not an out-of-range id.
TEST(TuneTable, VisibleIdPastTheEndIsSilent) {
  EXPECT_EQ(kTuneSilent, visibleTuneId(visibleTuneCount()));
  EXPECT_EQ(kTuneSilent, visibleTuneId(255));
}

// The enumerator must be a bijection onto the named entries: no id listed twice, none
// of the named ones skipped. A picker that shows a tune twice or hides one is the
// symptom of getting this wrong.
TEST(TuneTable, TheEnumeratorVisitsEveryNamedEntryExactlyOnce) {
  std::set<int> seen;
  for (uint8_t nth = 0; nth < visibleTuneCount(); nth++) {
    EXPECT_TRUE(seen.insert(visibleTuneId(nth)).second) << "id listed twice";
  }
  int named = 0;
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    if (tuneName(id) != nullptr) {
      named++;
      EXPECT_EQ(1u, seen.count(id)) << "named id " << (int)id << " is not listed";
    }
  }
  EXPECT_EQ(named, (int)seen.size());
  EXPECT_EQ(named, (int)visibleTuneCount());
}

// ------------------------------------------------- the event-to-tune mapping

// The named ids the defaults are written in terms of must be the melodies they claim.
// This is the test that would catch someone inserting a row in the middle of the table
// rather than appending, which would silently re-point every shipped badge's prefs.
TEST(ToneEvents, TheNamedIdsPointAtTheMelodiesTheyName) {
  EXPECT_STREQ("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7", tuneRtttl(kTuneChime));
  EXPECT_STREQ("kerplop:d=16,o=6,b=120:32g#,32c#", tuneRtttl(kTuneKerplop));
  EXPECT_STREQ("ack:d=32,o=8,b=120:c", tuneRtttl(kTuneTick));
}

// The point of this task: the two events that fell through the UI's switch silently now
// have slots. They are still silent by default -- having a slot is not having a sound.
TEST(ToneEvents, EveryEventHasASlotAndNoneResolvesOutOfRange) {
  for (uint8_t i = 0; i < (uint8_t)ToneEvent::Count; i++) {
    const uint8_t id = defaultTuneFor((ToneEvent)i);
    EXPECT_TRUE(isBuiltinTune(id)) << "event " << (int)i << " defaults to id " << (int)id
                                   << ", which is not in the table";
  }
}

// The defaults reproduce today's behavior exactly. If one of these changes, a board
// that merely took an update starts making a different noise, which is a different kind
// of change from adding a setting.
TEST(ToneEvents, DefaultsReproduceTodaysBehavior) {
  EXPECT_EQ(kTuneChime, defaultTuneFor(ToneEvent::ContactMessage));
  EXPECT_EQ(kTuneKerplop, defaultTuneFor(ToneEvent::ChannelMessage));
  EXPECT_EQ(kTuneTick, defaultTuneFor(ToneEvent::Ack));
  // Silent today because their cases fall through the UI's switch with no sound.
  EXPECT_EQ(kTuneSilent, defaultTuneFor(ToneEvent::RoomMessage));
  EXPECT_EQ(kTuneSilent, defaultTuneFor(ToneEvent::NewContact));
}

// The send tone is off fleet-wide and the badge env turns it on, so this test asserts
// the fleet default. It compiles without the badge's -D, which is the case being pinned.
TEST(ToneEvents, TheSendToneIsSilentUnlessABoardAsksForIt) {
  EXPECT_EQ(kTuneSilent, defaultTuneFor(ToneEvent::Sent));
}

// An out-of-range event is silent rather than landing on another event's tune. `Count`
// is the sentinel a loop would reach if someone extended the enum and forgot the switch.
TEST(ToneEvents, AnEventWithNoSlotIsSilent) {
  EXPECT_EQ(kTuneSilent, defaultTuneFor(ToneEvent::Count));
  EXPECT_EQ(kTuneSilent, defaultTuneFor((ToneEvent)200));
}

// ------------------------------------------------------------ the validator

TEST(RtttlValidator, AcceptsTheRealThing) {
  EXPECT_TRUE(rtttlLooksValid("ack:d=32,o=8,b=120:c"));
  EXPECT_TRUE(rtttlLooksValid("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7"));
  // The badge maker's own example, 218 characters, the string this has to accept.
  EXPECT_TRUE(rtttlLooksValid(
      "n:d=4,o=5,b=200:8g,8a,8c6,8a,e6,8p,e6,8p,d6.,p,8p,8g,8a,8c6,8a,d6,8p,d6,8p,c6,"
      "8b,a.,8g,8a,8c6,8a,2c6,d6,b,a,g.,8p,g,2d6,2c6.,p,8g,8a,8c6,8a,e6,8p,e6,8p,d6.,"
      "p,8p,8g,8a,8c6,8a,2g6,b,c6.,8b,a,8g,8a,8c6,8a,2c6,d6,b,a,g.,8p"));
  // A space in the name is fine -- it is before the first colon, which is all the
  // player skips.
  EXPECT_TRUE(rtttlLooksValid("my tune:d=4,o=5,b=63:16e6"));
}

// The defaults are POSITIONAL: the player tests for 'd', then 'o', then 'b', in that
// order, and never goes back. `x:b=120,o=5,d=4:c` makes it consume b=120 and then play
// "o=5,d=4:c" as notes. An earlier version of this test asserted any order was fine,
// which is what the RTTTL spec suggests and not what this player does -- the test was
// wrong, not the validator.
TEST(RtttlValidator, DefaultsMustBeInThePlayersOrder) {
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("x:d=4,o=5,b=120:c"));
  EXPECT_NE(RtttlProblem::Ok, rtttlProblem("x:b=120,o=5,d=4:c"));
  EXPECT_NE(RtttlProblem::Ok, rtttlProblem("x:o=5,d=4:c"));
}

// Defaults are OPTIONAL in the player -- `if(*buffer == 'd')` and so on -- so a tune
// with none, or only some, plays. An earlier version of this validator demanded all
// three and would have refused a large working subset of real RTTTL.
TEST(RtttlValidator, DefaultsAreOptionalBecauseThePlayerTreatsThemThatWay) {
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("Minimal:c,e,g"));
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("OneDefault:d=8:c,e,g"));
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("Two:d=8,o=5:c"));
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("Tempo:b=100:c"));
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("Oct:o=5:c"));
  // A note that merely starts with a digit is unambiguous.
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("Eighth:8d,8e"));
}

// THE CRASH. The player computes `wholenote = (60 * 1000L / bpm) * 4` with no guard,
// so a zero tempo divides by zero on the device. Nothing else in this file matters
// more than these three lines.
TEST(RtttlValidator, RefusesTheTempoThatWouldDivideByZero) {
  EXPECT_EQ(RtttlProblem::ZeroTempo, rtttlProblem("name:d=4,o=5,b=0:c"));
  EXPECT_EQ(RtttlProblem::ZeroTempo, rtttlProblem("name:b=0:c"));
  EXPECT_EQ(RtttlProblem::ZeroTempo, rtttlProblem("name:b=000:c"));
  // `b=` with no digits leaves the player's bpm at 0 just the same.
  EXPECT_EQ(RtttlProblem::ZeroTempo, rtttlProblem("name:d=4,o=5,b=:c"));
}

// A defaults-less tune whose notes begin with d, o or b is mis-parsed: the player
// takes the note for a default key. For `b` that is the divide by zero above, reached
// without anyone typing a tempo at all.
TEST(RtttlValidator, RefusesNotesThePlayerWouldMistakeForDefaults) {
  EXPECT_EQ(RtttlProblem::AmbiguousNote, rtttlProblem("Song:b,c,d"));
  EXPECT_EQ(RtttlProblem::AmbiguousNote, rtttlProblem("Song:d,e,g"));
  EXPECT_EQ(RtttlProblem::AmbiguousNote, rtttlProblem("Song:o,c"));
  // Harmless once any default is present: the player has consumed it by then.
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("Song:d=4:b,c,d"));
}

// The octave is read as a single character, then one more is skipped. `o=10` makes the
// player eat the '0' as a separator and mis-parse everything after it.
TEST(RtttlValidator, OctaveMustBeExactlyOneDigit) {
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem("name:o=5:c"));
  EXPECT_EQ(RtttlProblem::BadDefault, rtttlProblem("name:o=10:c"));
  EXPECT_EQ(RtttlProblem::BadDefault, rtttlProblem("name:o=:c"));
}

// The player skips exactly one character after each default's digits, so anything
// other than a separator there means a real note is swallowed.
TEST(RtttlValidator, EachDefaultMustEndInASeparator) {
  EXPECT_EQ(RtttlProblem::BadDefault, rtttlProblem("name:d=4o=5,b=120:c"));
  EXPECT_EQ(RtttlProblem::BadDefault, rtttlProblem("name:b=120c"));
}

// The player's own parse is positional and starts immediately after the first colon,
// so whitespace or a colon in the name is not tolerance -- it is a mis-play. The
// validator refuses both rather than letting the device play garbage.
TEST(RtttlValidator, RefusesWhatThePlayerWouldMisparse) {
  // A space before the defaults: the player skips all three and plays the header.
  EXPECT_EQ(RtttlProblem::StrayColon, rtttlProblem("name: d=4,o=5,b=120:c"));
  // A colon in the name: the player takes " Part 1" for the defaults section.
  EXPECT_EQ(RtttlProblem::StrayColon, rtttlProblem("My Tune: Part 1:d=4,o=5,b=120:c"));
  // The invariant underneath both: once the header is consumed, no colon remains.
  EXPECT_EQ(RtttlProblem::StrayColon, rtttlProblem("name:d=4,o=5,b=120:c:d"));
}

TEST(RtttlValidator, NamesEachWayItCanBeWrong) {
  EXPECT_EQ(RtttlProblem::Empty, rtttlProblem(nullptr));
  EXPECT_EQ(RtttlProblem::Empty, rtttlProblem(""));
  // No colon at all is the one that would walk the player past the buffer.
  EXPECT_EQ(RtttlProblem::MissingColon, rtttlProblem("just some text"));
  EXPECT_EQ(RtttlProblem::NoName, rtttlProblem(":d=4,o=5,b=120:c"));
  EXPECT_EQ(RtttlProblem::BadDefault, rtttlProblem("name:d=,o=5,b=120:c"));
  EXPECT_EQ(RtttlProblem::BadDefault, rtttlProblem("name:d=99999,o=5,b=120:c"));
  EXPECT_EQ(RtttlProblem::NoNotes, rtttlProblem("name:d=4,o=5,b=120:"));
  EXPECT_EQ(RtttlProblem::NoNotes, rtttlProblem("name:"));
}

// Every problem has words, and they are not all the same words -- a screen that says
// "Bad tune" for everything is no better than no message.
TEST(RtttlValidator, EveryProblemHasItsOwnText) {
  const RtttlProblem all[] = {
      RtttlProblem::Ok,          RtttlProblem::Empty,
      RtttlProblem::NoName,      RtttlProblem::MissingColon,
      RtttlProblem::BadDefault,  RtttlProblem::ZeroTempo,
      RtttlProblem::NoNotes,     RtttlProblem::AmbiguousNote,
      RtttlProblem::StrayColon,
  };
  std::set<std::string> texts;
  for (RtttlProblem p : all) {
    const char* t = rtttlProblemText(p);
    ASSERT_NE(nullptr, t);
    EXPECT_LT(strlen(t), 24u) << "'" << t << "' is too long for a badge screen";
    texts.insert(t);
  }
  EXPECT_EQ(sizeof(all) / sizeof(all[0]), texts.size()) << "two problems share wording";
}

// The validator reads a caller's buffer and must not run past a terminator, including
// on strings that stop in the middle of the header.
TEST(RtttlValidator, StopsAtTheTerminatorOnTruncatedInput) {
  const char* full = "name:d=4,o=5,b=120:8c,8d";
  char buf[64];
  for (size_t n = 0; n < strlen(full); n++) {
    memcpy(buf, full, n);
    buf[n] = 0;
    // Any answer is fine; not crashing and not reading past buf[n] is the point.
    (void)rtttlProblem(buf);
  }
  memcpy(buf, full, strlen(full) + 1);
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem(buf));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
