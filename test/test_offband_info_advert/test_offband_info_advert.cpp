// Offband #1994: the 0xC5 0x1C capability advert writer.
//
// These run the REAL writer and the REAL walker from helpers/OffbandInfoAdvert.h. Nothing
// is re-implemented here: a test that encoded the record format itself would keep passing
// while the header was broken, which is the failure mode that matters for a wire format.
//
// The weight is on TRUNCATION. The advert replaces a scheme where three features silently
// collided on one device-info offset, and the thing that made that dangerous was not the
// collision but that nothing surfaced it. A truncated advert that cannot announce itself
// has exactly that shape: the client reads a short list as a complete one and concludes the
// device lacks features it has.

#include <gtest/gtest.h>

#include <helpers/OffbandInfoAdvert.h>

#include <string>
#include <vector>

using namespace offband;

namespace {

struct Rec {
  uint8_t tag;
  std::vector<uint8_t> value;
};

void collect(uint8_t tag, const uint8_t* value, uint8_t len, void* ctx) {
  auto* out = static_cast<std::vector<Rec>*>(ctx);
  Rec r;
  r.tag = tag;
  for (uint8_t i = 0; i < len; i++) r.value.push_back(value[i]);
  out->push_back(r);
}

// Walks a written area back into records. Named `parse` rather than `run` -- gtest's
// Test::Run() is private and a helper called Run collides with it.
std::vector<Rec> parse(const uint8_t* buf, size_t len, bool* ok = nullptr) {
  std::vector<Rec> out;
  const bool r = infoAdvertWalk(buf, len, &collect, &out);
  if (ok != nullptr) *ok = r;
  return out;
}

constexpr uint8_t kTagA = 0x01;
constexpr uint8_t kTagB = 0x02;
constexpr uint8_t kTagC = 0x03;

}  // namespace

// ------------------------------------------------------------------ writing records

TEST(InfoAdvert, AFlagIsTagAndZeroLength) {
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  EXPECT_TRUE(a.addFlag(kTagA));
  const size_t len = a.finish();

  EXPECT_EQ(2u, len) << "a flag record is exactly tag + len";
  EXPECT_TRUE(a.complete());

  bool ok = false;
  const auto recs = parse(buf, len, &ok);
  ASSERT_TRUE(ok);
  ASSERT_EQ(1u, recs.size());
  EXPECT_EQ(kTagA, recs[0].tag);
  EXPECT_TRUE(recs[0].value.empty()) << "zero length must not be confused with a zero byte";
}

TEST(InfoAdvert, AByteValueRoundTrips) {
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  EXPECT_TRUE(a.addByte(kTagC, 0x2A));
  const size_t len = a.finish();

  const auto recs = parse(buf, len);
  ASSERT_EQ(1u, recs.size());
  EXPECT_EQ(kTagC, recs[0].tag);
  ASSERT_EQ(1u, recs[0].value.size());
  EXPECT_EQ(0x2A, recs[0].value[0]);
}

TEST(InfoAdvert, ZeroLengthAndAZeroByteAreDifferentRecords) {
  // The distinction the contract draws between "absent", "present with nothing to report"
  // and "present, value 0". Two of those are easy to conflate in a parser.
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  ASSERT_TRUE(a.addFlag(kTagA));
  ASSERT_TRUE(a.addByte(kTagB, 0));
  const size_t len = a.finish();

  const auto recs = parse(buf, len);
  ASSERT_EQ(2u, recs.size());
  EXPECT_TRUE(recs[0].value.empty());
  ASSERT_EQ(1u, recs[1].value.size());
  EXPECT_EQ(0u, recs[1].value[0]);
}

// ------------------------------------------------------------------ ordering

TEST(InfoAdvert, OutOfOrderIsRefusedAndWritesNothing) {
  // Enforced rather than documented, because the resume cursor names a TAG: a client told
  // "resume from 0x03" can only act on it if everything below 0x03 was already sent. An
  // unordered advert would make the cursor silently drop records.
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  ASSERT_TRUE(a.addFlag(kTagB));
  const size_t before = a.length();

  EXPECT_FALSE(a.addFlag(kTagA)) << "descending tag must be refused";
  EXPECT_EQ(before, a.length()) << "a refused record must write nothing";
  EXPECT_TRUE(a.complete()) << "a programming error is not a truncation";
}

TEST(InfoAdvert, TheSameTagTwiceIsRefused) {
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  ASSERT_TRUE(a.addFlag(kTagA));
  EXPECT_FALSE(a.addFlag(kTagA)) << "ascending means strictly ascending";
}

TEST(InfoAdvert, TagZeroIsNeverValid) {
  // Reserved so that a zeroed buffer cannot be read as a record.
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  EXPECT_FALSE(a.addFlag(kInfoTagInvalid));
  EXPECT_EQ(0u, a.length());
}

// ------------------------------------------------------------------ truncation

TEST(InfoAdvert, StopsEarlyAndSaysSoWithTheTagToResumeFrom) {
  // THE case. Capacity fits two flag records plus the reserved CONTINUE and no more.
  uint8_t buf[2 + 2 + 3] = {0};
  InfoAdvert a(buf, sizeof buf);
  ASSERT_TRUE(a.addFlag(kTagA));
  ASSERT_TRUE(a.addFlag(kTagB));
  EXPECT_FALSE(a.addFlag(kTagC)) << "third record must not fit";

  EXPECT_FALSE(a.complete());
  EXPECT_EQ(kTagC, a.nextTag()) << "resume from the FIRST record refused, not the last sent";

  const size_t len = a.finish();
  bool ok = false;
  const auto recs = parse(buf, len, &ok);
  ASSERT_TRUE(ok);
  ASSERT_EQ(3u, recs.size());
  EXPECT_EQ(kTagA, recs[0].tag);
  EXPECT_EQ(kTagB, recs[1].tag);
  EXPECT_EQ(InfoAdvert::kInfoTagContinue, recs[2].tag);
  ASSERT_EQ(1u, recs[2].value.size());
  EXPECT_EQ(kTagC, recs[2].value[0]);
}

TEST(InfoAdvert, ThereIsALWAYSRoomToAnnounceATruncation) {
  // THE REGRESSION. An earlier draft lent the reserved CONTINUE space to "the last record",
  // which add() cannot identify. Lending it and then refusing a later record leaves a
  // truncated advert with no room to say so -- and a client reads a short list as complete,
  // concluding the device lacks features it has. No error on either side.
  //
  // Swept across every capacity from nothing to comfortably large: whenever the writer
  // reports !complete(), a CONTINUE record must be present in the output.
  //
  // The sweep found the floor this invariant actually has. Below kInfoMinCapacity (3) there
  // is no room for a record OR for the CONTINUE that would announce its absence, so the
  // writer can only emit nothing -- which a client would read as "no capabilities", a wrong
  // answer rather than a refusal. That is why the caller must check kInfoMinCapacity and
  // reply with an error instead of an advert; asserted separately below.
  for (size_t cap = 0; cap <= 24; cap++) {
    std::vector<uint8_t> buf(cap + 8, 0);
    InfoAdvert a(buf.data(), cap);
    for (uint8_t tag = 1; tag <= 6; tag++) a.addFlag(tag);
    const size_t len = a.finish();

    bool ok = false;
    const auto recs = parse(buf.data(), len, &ok);
    ASSERT_TRUE(ok) << "cap=" << cap << " produced a malformed area";
    ASSERT_LE(len, cap) << "cap=" << cap << " overran the buffer";

    if (cap < kInfoMinCapacity) {
      EXPECT_EQ(0u, len) << "cap=" << cap << " cannot carry a record and must emit nothing";
      EXPECT_FALSE(a.complete()) << "cap=" << cap << " must not claim completeness";
      continue;
    }

    if (a.stalled()) {
      // Nothing was sent, so there is nothing to continue FROM. A cursor here would name
      // a tag the device refuses again at this capacity and loop the client forever, so
      // the writer must stay silent and the caller must error. This branch is the
      // correction to what this test originally asserted -- see the note below.
      for (const auto& r : recs) {
        EXPECT_NE(InfoAdvert::kInfoTagContinue, r.tag)
            << "cap=" << cap << " sent a cursor with no progress behind it";
      }
    } else if (!a.complete()) {
      ASSERT_FALSE(recs.empty()) << "cap=" << cap << " truncated with no records at all";
      EXPECT_EQ(InfoAdvert::kInfoTagContinue, recs.back().tag)
          << "cap=" << cap << " truncated WITHOUT announcing it -- a client would read this"
             " short advert as complete";
    } else {
      for (const auto& r : recs) {
        EXPECT_NE(InfoAdvert::kInfoTagContinue, r.tag)
            << "cap=" << cap << " announced a continuation it did not need";
      }
    }
  }
}

TEST(InfoAdvert, ACursorIsNeverSentWhenNoProgressWasMade) {
  // THE INFINITE LOOP, found by review. If nothing fits, a CONTINUE would name a tag the
  // device refuses again at the same capacity -- and a client doing exactly what the
  // contract says would re-request from that cursor forever.
  //
  // Raising a capacity floor does NOT fix this, which is why the guard is "did anything
  // get sent" and not a size: records vary in length, so any floor that admits a 2-byte
  // flag can still stall on a 3-byte one. Both cases are swept here.
  for (size_t cap = 0; cap <= 6; cap++) {
    // A flag record (2 bytes) and a valued record (3 bytes), separately.
    for (int valued = 0; valued < 2; valued++) {
      std::vector<uint8_t> buf(cap + 8, 0);
      InfoAdvert a(buf.data(), cap);
      const uint8_t v = 7;
      const bool added = valued ? a.addByte(kTagA, v) : a.addFlag(kTagA);
      const size_t len = a.finish();
      const auto recs = parse(buf.data(), len);

      if (!added) {
        EXPECT_TRUE(a.stalled()) << "cap=" << cap << " valued=" << valued
                                 << ": nothing was sent but the writer does not say so";
        for (const auto& r : recs) {
          EXPECT_NE(InfoAdvert::kInfoTagContinue, r.tag)
              << "cap=" << cap << " valued=" << valued
              << ": sent a cursor with no progress -- the client would loop forever";
        }
      } else {
        EXPECT_FALSE(a.stalled()) << "cap=" << cap << " valued=" << valued;
      }
    }
  }
}

TEST(InfoAdvert, AStalledAdvertIsDistinctFromAnIncompleteOne) {
  // Incomplete means "more to come, here is where" and is recoverable by the cursor.
  // Stalled means "nothing could be sent" and is NOT recoverable -- the caller must
  // error. Conflating them is what produced the loop.
  uint8_t small[3] = {0};
  InfoAdvert stalled(small, sizeof small);
  EXPECT_FALSE(stalled.addFlag(kTagA));
  stalled.finish();
  EXPECT_TRUE(stalled.stalled());
  EXPECT_FALSE(stalled.complete());

  uint8_t roomy[2 + 3] = {0};
  InfoAdvert partial(roomy, sizeof roomy);
  EXPECT_TRUE(partial.addFlag(kTagA));
  EXPECT_FALSE(partial.addFlag(kTagB));
  partial.finish();
  EXPECT_FALSE(partial.stalled()) << "one record went out, so the cursor is real";
  EXPECT_FALSE(partial.complete());
}

TEST(InfoAdvert, BelowTheMinimumCapacityNothingCanBeSaid) {
  // The floor the sweep above exposed, asserted on its own so the reason is not buried in a
  // loop. The caller's obligation: check kInfoMinCapacity and return ERR rather than send an
  // empty advert, because an empty advert is a confident claim that the device has nothing.
  EXPECT_EQ(3u, kInfoMinCapacity);
  for (size_t cap = 0; cap < kInfoMinCapacity; cap++) {
    uint8_t buf[8] = {0};
    InfoAdvert a(buf, cap);
    EXPECT_FALSE(a.addFlag(kTagA)) << "cap=" << cap;
    EXPECT_EQ(0u, a.finish()) << "cap=" << cap;
    EXPECT_FALSE(a.complete()) << "cap=" << cap;
  }
  // At exactly the minimum, a flag record would fit only by borrowing the reserve, so it
  // is refused -- and because NOTHING was sent, no cursor may be emitted either.
  //
  // THIS TEST ORIGINALLY ASSERTED THE OPPOSITE: that the space be spent announcing the
  // truncation. That was the bug. A cursor with no progress behind it names a tag the
  // device refuses again at the same capacity, and a client doing what the contract says
  // re-requests from it forever. Review caught it; the assertion is inverted here on
  // purpose so the loop cannot be reintroduced by restoring the "helpful" cursor.
  uint8_t buf[8] = {0};
  InfoAdvert a(buf, kInfoMinCapacity);
  EXPECT_FALSE(a.addFlag(kTagA)) << "the reserve is never lent, even to the first record";
  const size_t len = a.finish();
  EXPECT_TRUE(a.stalled()) << "nothing fit, and the writer must say so";
  EXPECT_EQ(0u, len) << "a cursor with no progress behind it loops the client forever";
  EXPECT_TRUE(parse(buf, len).empty());
}

TEST(InfoAdvert, NothingFollowsAStop) {
  // Once truncated, a later record must not sneak in behind the stop -- it would be ordered
  // after a CONTINUE that claims everything from its tag onward is still to come.
  uint8_t buf[2 + 3] = {0};
  InfoAdvert a(buf, sizeof buf);
  ASSERT_TRUE(a.addFlag(kTagA));
  ASSERT_FALSE(a.addByte(kTagB, 0xFF));      // value makes it too big
  EXPECT_FALSE(a.addFlag(kTagC)) << "a small record must not pass after a stop";
  EXPECT_EQ(kTagB, a.nextTag()) << "the resume point stays the FIRST thing refused";
}

TEST(InfoAdvert, ZeroCapacityIsNotACrash) {
  uint8_t dummy = 0;
  InfoAdvert a(&dummy, 0);
  EXPECT_FALSE(a.addFlag(kTagA));
  EXPECT_EQ(0u, a.finish());
  EXPECT_FALSE(a.complete());
}

TEST(InfoAdvert, ANullBufferRefusesEverything) {
  InfoAdvert a(nullptr, 64);
  EXPECT_FALSE(a.addFlag(kTagA));
  EXPECT_EQ(0u, a.length());
}

TEST(InfoAdvert, ANonEmptyValueNeedsAPointer) {
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  EXPECT_FALSE(a.add(kTagA, nullptr, 4));
  EXPECT_EQ(0u, a.length());
}

// ------------------------------------------------------------------ the resume round trip

TEST(InfoAdvert, ACursorResumeDeliversEveryRecordExactlyOnce) {
  // What a real client does: request, and while the reply ends in CONTINUE, request again
  // from its cursor. Every tag must arrive once, in order, with no gap and no repeat --
  // a gap means a capability the client never learns about.
  const uint8_t kCap = 9;                 // deliberately small: forces several rounds
  std::vector<uint8_t> seen;
  uint8_t from = 1;
  int rounds = 0;

  while (rounds++ < 20) {
    uint8_t buf[kCap] = {0};
    InfoAdvert a(buf, sizeof buf);
    for (uint8_t tag = from; tag <= 6; tag++) {
      if (!a.add(tag, nullptr, 0)) break;
    }
    const size_t len = a.finish();

    bool ok = false;
    const auto recs = parse(buf, len, &ok);
    ASSERT_TRUE(ok);

    uint8_t next = 0;
    for (const auto& r : recs) {
      if (r.tag == InfoAdvert::kInfoTagContinue) {
        ASSERT_EQ(1u, r.value.size());
        next = r.value[0];
      } else {
        seen.push_back(r.tag);
      }
    }
    if (next == 0) break;
    ASSERT_GT(next, from) << "the cursor must advance or the client loops forever";
    from = next;
  }

  ASSERT_EQ(6u, seen.size()) << "every tag must arrive exactly once across the rounds";
  for (size_t i = 0; i < seen.size(); i++) EXPECT_EQ((uint8_t)(i + 1), seen[i]);
}

// ------------------------------------------------------------------ the walker

TEST(InfoAdvertWalk, AnEmptyAreaIsValid) {
  bool ok = false;
  const auto recs = parse(nullptr, 0, &ok);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(recs.empty());
}

TEST(InfoAdvertWalk, ATruncatedHeaderIsRejected) {
  const uint8_t buf[] = {kTagA};           // tag with no length byte
  bool ok = true;
  parse(buf, sizeof buf, &ok);
  EXPECT_FALSE(ok);
}

TEST(InfoAdvertWalk, ATruncatedValueIsRejected) {
  const uint8_t buf[] = {kTagA, 4, 1, 2};  // says 4 bytes, carries 2
  bool ok = true;
  parse(buf, sizeof buf, &ok);
  EXPECT_FALSE(ok) << "a short value must not be reported as a shorter record";
}

TEST(InfoAdvertWalk, AZeroTagIsRejected) {
  // Why tag 0 is reserved: without this, a zeroed tail reads as an endless run of records.
  const uint8_t buf[] = {0, 0};
  bool ok = true;
  parse(buf, sizeof buf, &ok);
  EXPECT_FALSE(ok);
}

TEST(InfoAdvertWalk, UnknownTagsAreWalkedPastByLength) {
  // The forward-compatibility contract: a client must stay correct against tags it has
  // never heard of, and the only way past one is its length.
  uint8_t buf[64] = {0};
  InfoAdvert a(buf, sizeof buf);
  const uint8_t payload[3] = {9, 9, 9};
  ASSERT_TRUE(a.add(0x40, payload, sizeof payload));   // not in any registry
  ASSERT_TRUE(a.addFlag(0x41));
  const size_t len = a.finish();

  bool ok = false;
  const auto recs = parse(buf, len, &ok);
  ASSERT_TRUE(ok);
  ASSERT_EQ(2u, recs.size());
  EXPECT_EQ(0x40, recs[0].tag);
  EXPECT_EQ(3u, recs[0].value.size());
  EXPECT_EQ(0x41, recs[1].tag) << "the record after an unknown one must still be found";
}

TEST(InfoAdvertWalk, TheContinueTagValueMatchesTheProtocolHeader) {
  // The writer mirrors OFFBAND_INFO_TAG_CONTINUE rather than including the companion-only
  // protocol header. MyMesh.cpp static_asserts the pair; this records the value so a drift
  // is visible in the test output too.
  EXPECT_EQ(0xFF, InfoAdvert::kInfoTagContinue);
  EXPECT_EQ(0u, kInfoTagInvalid);
  EXPECT_EQ(2u, kInfoRecordHeader);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
