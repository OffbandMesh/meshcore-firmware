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

// The list the owner approved (#1447). Pinned by name and by position, because the ids
// are what a shipped badge has in its prefs: inserting rather than appending would
// re-point someone's chosen tune, and this is the test that would say so.
TEST(ShippedChimes, TheApprovedListInTheApprovedOrder) {
  // #1422 appended an eighth entry with no name: the trigger's, un-listed on purpose.
  const char* expected[] = {"Silent", "Chime", "Kerplop", "Tick", "Bloop", "Drop",
                            "Knock", nullptr};
  ASSERT_EQ(sizeof(expected) / sizeof(expected[0]), (size_t)kBuiltinCount)
      << "the table grew or shrank without this test being updated";
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    if (expected[id] == nullptr) {
      EXPECT_EQ(nullptr, tuneName(id)) << "id " << (int)id << " should be un-listed";
    } else {
      EXPECT_STREQ(expected[id], tuneName(id)) << "id " << (int)id << " moved";
    }
  }
  // The owner asked for four to six choices. Six, plus Silent, which is not a choice of
  // tune so much as the absence of one. The trigger entry is not among them.
  EXPECT_EQ(7, visibleTuneCount());
}

// Every tune the picker OFFERS is short enough to be a notification rather than a song.
// The note count is the proxy the host can check without the player's timing: all six
// are 1-3 sounded notes, where the melodies measured during sourcing ran 10 to 66.
//
// Scoped to listed entries on purpose. The trigger's payload (#1422) is deliberately
// longer than a chime -- a hall of badges firing at once should read as an event, not as
// six people getting messages -- so holding it to a chime's budget would be wrong.
TEST(ShippedChimes, EveryOfferedOneIsAChimeAndNotASong) {
  for (uint8_t nth = 0; nth < visibleTuneCount(); nth++) {
    const uint8_t id = visibleTuneId(nth);
    const char* r = tuneRtttl(id);
    if (r == nullptr) continue;  // Silent
    int commas = 0;
    for (const char* p = r; *p; p++) {
      if (*p == ',') commas++;
    }
    // Two commas separate the three defaults, so sounded notes are commas - 1 at most.
    EXPECT_LE(commas, 5) << tuneName(id) << " has too many notes for a chime: " << r;
    EXPECT_LT(strlen(r), 45u) << tuneName(id) << " is longer than a chime should be";
  }
}

// ---------------------------------------------- the un-listed trigger entry

// Reachable by id, invisible in the menu. That combination IS the mechanism epic #1191
// needs, and either half failing defeats it: a listed trigger spoils the surprise, an
// unreachable one cannot fire.
TEST(TriggerTune, ReachableByIdAndInvisibleInTheMenu) {
  EXPECT_TRUE(isBuiltinTune(kTuneTrigger));
  ASSERT_NE(nullptr, tuneRtttl(kTuneTrigger)) << "a trigger that plays nothing is useless";
  EXPECT_EQ(RtttlProblem::Ok, rtttlProblem(tuneRtttl(kTuneTrigger)));
  EXPECT_EQ(nullptr, tuneName(kTuneTrigger)) << "it must not be listed";

  for (uint8_t nth = 0; nth < visibleTuneCount(); nth++) {
    EXPECT_NE(kTuneTrigger, visibleTuneId(nth)) << "the picker enumerated the trigger";
  }
}

// No event defaults to it. Someone would otherwise hear the CTF payload as their DM
// tone, which both spoils it and is not a notification.
TEST(TriggerTune, NoEventDefaultsToIt) {
  for (uint8_t i = 0; i < (uint8_t)ToneEvent::Count; i++) {
    EXPECT_NE(kTuneTrigger, defaultTuneFor((ToneEvent)i)) << "event " << (int)i;
  }
}

// It is longer than the chimes, which is the point -- but still bounded, because the
// badge should not be stuck playing for a quarter of a minute.
TEST(TriggerTune, LongerThanAChimeButStillBounded) {
  const char* r = tuneRtttl(kTuneTrigger);
  ASSERT_NE(nullptr, r);
  int commas = 0;
  for (const char* p = r; *p; p++) {
    if (*p == ',') commas++;
  }
  EXPECT_GT(commas, 5) << "a trigger that sounds like a chime will be mistaken for one";
  EXPECT_LT(strlen(r), 120u) << "still not a song";
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

// ------------------------------------------------- stored choice vs default

// The distinction the whole prefs design turns on: Silent is a CHOICE with an id, so
// "nothing chosen" needs its own value. If unset were 0, an un-configured badge would
// be permanently silent and indistinguishable from one deliberately silenced.
TEST(StoredTune, UnsetIsNotSilent) {
  EXPECT_NE(kTuneUnset, kTuneSilent);
  EXPECT_GE(kTuneUnset, kBuiltinCount) << "the sentinel must not name a real tune";
  EXPECT_EQ(nullptr, tuneRtttl(kTuneUnset)) << "and must never resolve to a melody";
}

// Unset falls through to this board's compiled default -- which is what lets a later
// firmware improve a default rather than being frozen out by prefs written once.
TEST(StoredTune, UnsetTakesTheCompiledDefault) {
  for (uint8_t i = 0; i < (uint8_t)ToneEvent::Count; i++) {
    const ToneEvent e = (ToneEvent)i;
    EXPECT_EQ(defaultTuneFor(e), resolveTune(kTuneUnset, e)) << "event " << (int)i;
  }
}

// A stored choice wins, including a stored Silent. This is the case a zero-filled blob
// could not express, and the reason the prefs are six keys instead of one array.
TEST(StoredTune, AStoredChoiceWinsIncludingSilence) {
  EXPECT_EQ(kTuneSilent, resolveTune(kTuneSilent, ToneEvent::ContactMessage));
  EXPECT_EQ(nullptr, tuneRtttl(resolveTune(kTuneSilent, ToneEvent::ContactMessage)));
  EXPECT_EQ(kTuneKerplop, resolveTune(kTuneKerplop, ToneEvent::Ack));
  EXPECT_EQ(kTuneChime, resolveTune(kTuneChime, ToneEvent::Sent));
}

// A stored id from a longer table -- a badge given a tune then rolled back -- passes
// through and becomes silence at playback rather than indexing the array. #1413 will
// resolve ids in this range as custom slots instead.
TEST(StoredTune, AnIdPastTheTablePassesThroughAndPlaysNothing) {
  const uint8_t beyond = kBuiltinCount;
  EXPECT_EQ(beyond, resolveTune(beyond, ToneEvent::ContactMessage));
  EXPECT_EQ(nullptr, tuneRtttl(resolveTune(beyond, ToneEvent::ContactMessage)));
  EXPECT_FALSE(isBuiltinTune(beyond));
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

// #1467: the picker's diagnosis rests on this. When a highlighted row makes no sound,
// the cause has to be one the badge can name -- the buzzer pref ("Buzzer: OFF"), a real
// notification holding the buzzer ("busy"), or the hardware MUTE switch, which firmware
// cannot see. "This tune simply has no melody" must NOT be a fourth possibility for any
// row the picker shows, or an unexplained silence stops being diagnosable at all.
//
// Silent is the one exception, and it is exempt because its row says the word.
TEST(TuneTable, EveryListedTuneExceptSilentHasAMelody) {
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    const char* name = tuneName(id);
    if (name == nullptr) continue;          // un-listed (#1422): never shown, never picked
    if (id == kTuneSilent) {
      EXPECT_EQ(nullptr, tuneRtttl(id)) << "Silent must not acquire a melody";
      continue;
    }
    EXPECT_NE(nullptr, tuneRtttl(id))
        << "'" << name << "' is offered in the picker but would make no sound";
  }
}

// #1467: the picker auditions whatever it opens on, so the row it opens on decides what
// gets PLAYED. The un-listed entry must therefore not be openable -- otherwise the one
// tune the UI is forbidden to reach becomes reachable by merely being stored.
TEST(PickerRow, NeverOpensOnAnUnlistedTune) {
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    const uint8_t row = pickerRowForBuiltin(id);
    EXPECT_NE(nullptr, tuneName(row))
        << "stored id " << (int)id << " would open the picker on an un-listed row";
  }
  // Specifically the trigger (#1422): stored, it must land on Silent, not play itself.
  EXPECT_EQ(nullptr, tuneName(kTuneTrigger)) << "kTuneTrigger stopped being un-listed";
  EXPECT_EQ(kTuneSilent, pickerRowForBuiltin(kTuneTrigger));
}

// A listed tune opens on itself -- the fallback must not swallow the normal case, which
// is what makes the picker open on "the tune in use" at all.
TEST(PickerRow, OpensOnTheTuneInUseWhenItIsListed) {
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    if (tuneName(id) == nullptr) continue;
    EXPECT_EQ(id, pickerRowForBuiltin(id));
  }
}

// Past the table: a custom slot (#1413) or an id from a longer table after a downgrade.
TEST(PickerRow, OutOfRangeIdsOpenOnSilent) {
  EXPECT_EQ(kTuneSilent, pickerRowForBuiltin(kBuiltinCount));
  EXPECT_EQ(kTuneSilent, pickerRowForBuiltin(kTuneUnset));
  EXPECT_EQ(kTuneSilent, pickerRowForBuiltin(200));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
