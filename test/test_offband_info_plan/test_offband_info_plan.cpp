// Offband #2214: which records the 0xC5 0x1C advert carries, and in what order.
//
// WHAT SHIPPED, and what these tests are for. #2068's two tags were appended to a list of
// `if` blocks in MyMesh.cpp that ended at 0x03, so 1.7.1 emitted 0x01, 0x02, 0x04, 0x05,
// 0x03. InfoAdvert::add() refuses a non-ascending tag -- deliberately, because the resume
// cursor names a tag -- and refuses by returning false and writing nothing. The call site
// discarded the return. Every board with a display or a controllable LED therefore sent an
// advert with ACK_CAPACITY missing and NO CONTINUE record announcing the loss, which a
// client reads as a complete list. The absolute-offset scheme the advert replaced failed
// the same way: a wrong answer with nothing surfacing it.
//
// These run the REAL plan builder, the REAL writer and the REAL walker. Nothing re-encodes
// the record format; a test that did would keep passing while the headers were broken.
//
// SCOPE, stated so it is not overclaimed: the ordering itself is now data, so these cover
// it exhaustively over the whole input space. What they CANNOT reach is MyMesh.cpp's
// collection of that input space from `_has_display`, `board.canControlLed()` and
// PIN_BUZZER -- that needs Arduino and stays a build-and-bench matter. TheNineteenSeventyOne
// -shaped tests below encode the shipped order as data and assert what it produced, which is
// the empirical record of the defect rather than a re-derivation of it.

#include <gtest/gtest.h>

#include <helpers/OffbandInfoPlan.h>

#include <cstdio>
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

// Named `parse`, not `run`: gtest's Test::Run() is private and a helper called Run collides
// with it (the same trap test_offband_info_advert.cpp records).
std::vector<Rec> parse(const uint8_t* buf, size_t len, bool* ok = nullptr) {
  std::vector<Rec> out;
  const bool r = infoAdvertWalk(buf, len, &collect, &out);
  if (ok != nullptr) *ok = r;
  return out;
}

// The four gated inputs as a bit pattern, so a test can sweep all sixteen states and name
// the one that failed.
InfoPlanInputs inputsFor(unsigned bits, uint8_t ack = 7) {
  InfoPlanInputs in;
  in.buzzer_fitted     = (bits & 1u) != 0;
  in.supports_rotation = (bits & 2u) != 0;
  in.has_display       = (bits & 4u) != 0;
  in.can_control_led   = (bits & 8u) != 0;
  in.ack_capacity      = ack;
  return in;
}

const char* describe(unsigned bits) {
  static char buf[96];
  snprintf(buf, sizeof buf, "buzzer=%d rotation=%d display=%d led=%d",
           (bits & 1u) ? 1 : 0, (bits & 2u) ? 1 : 0, (bits & 4u) ? 1 : 0, (bits & 8u) ? 1 : 0);
  return buf;
}

bool hasTag(const std::vector<Rec>& recs, uint8_t tag) {
  for (const auto& r : recs) {
    if (r.tag == tag) return true;
  }
  return false;
}

constexpr size_t kRoomy = 64;      // far above any plan: 5 records is at most 11 bytes

}  // namespace

// ------------------------------------------------------- the order, over every input

TEST(InfoPlan, EveryCombinationAscendsStrictly) {
  for (unsigned bits = 0; bits < 16; bits++) {
    InfoRecord plan[kInfoPlanMax];
    const size_t n = buildInfoPlan(inputsFor(bits), plan);
    ASSERT_GE(n, 1u) << describe(bits) << ": ACK_CAPACITY is unconditional";
    ASSERT_LE(n, kInfoPlanMax) << describe(bits);
    for (size_t i = 1; i < n; i++) {
      EXPECT_LT(plan[i - 1].tag, plan[i].tag)
          << describe(bits) << ": tag " << (int)plan[i].tag << " at index " << i
          << " does not exceed its predecessor -- InfoAdvert::add() will refuse it";
    }
  }
}

TEST(InfoPlan, EveryCombinationReachesTheWireWholeAndInOrder) {
  for (unsigned bits = 0; bits < 16; bits++) {
    uint8_t buf[kRoomy] = {0};
    InfoAdvert advert(buf, sizeof buf);
    InfoRecord plan[kInfoPlanMax];
    const size_t n = buildInfoPlan(inputsFor(bits), plan);

    const InfoPlanOutcome outcome = writeInfoPlan(advert, plan, n, 0);
    ASSERT_EQ(InfoPlanOutcome::Complete, outcome) << describe(bits);

    const size_t area = advert.finish();
    bool ok = false;
    const std::vector<Rec> recs = parse(buf, area, &ok);
    ASSERT_TRUE(ok) << describe(bits) << ": the written area does not walk back";
    ASSERT_EQ(n, recs.size()) << describe(bits) << ": a planned record did not reach the wire";
    for (size_t i = 0; i < n; i++) {
      EXPECT_EQ(plan[i].tag, recs[i].tag) << describe(bits) << " at index " << i;
    }
    EXPECT_FALSE(hasTag(recs, InfoAdvert::kInfoTagContinue))
        << describe(bits) << ": a complete advert must carry no resume cursor";
  }
}

// THE REGRESSION. A display board's advert lost ACK_CAPACITY in 1.7.1, because 0x03 was
// emitted after 0x04. This is the assertion that would have caught it.
TEST(InfoPlan, ADisplayBoardStillCarriesAckCapacity) {
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);
  InfoPlanInputs in;
  in.has_display = true;
  in.ack_capacity = 12;

  InfoRecord plan[kInfoPlanMax];
  const size_t n = buildInfoPlan(in, plan);
  ASSERT_EQ(InfoPlanOutcome::Complete, writeInfoPlan(advert, plan, n, 0));

  const std::vector<Rec> recs = parse(buf, advert.finish());
  ASSERT_EQ(2u, recs.size());
  EXPECT_EQ(kPlanTagAckCapacity, recs[0].tag) << "0x03 must precede 0x04";
  EXPECT_EQ(kPlanTagDisplayControl, recs[1].tag);
  ASSERT_EQ(1u, recs[0].value.size());
  EXPECT_EQ(12u, recs[0].value[0]);
}

TEST(InfoPlan, AckCapacityIsPresentWithItsValueOnEveryCombination) {
  for (unsigned bits = 0; bits < 16; bits++) {
    uint8_t buf[kRoomy] = {0};
    InfoAdvert advert(buf, sizeof buf);
    InfoRecord plan[kInfoPlanMax];
    const size_t n = buildInfoPlan(inputsFor(bits, 31), plan);
    ASSERT_EQ(InfoPlanOutcome::Complete, writeInfoPlan(advert, plan, n, 0)) << describe(bits);

    const std::vector<Rec> recs = parse(buf, advert.finish());
    bool found = false;
    for (const auto& r : recs) {
      if (r.tag != kPlanTagAckCapacity) continue;
      found = true;
      ASSERT_EQ(1u, r.value.size()) << describe(bits);
      EXPECT_EQ(31u, r.value[0]) << describe(bits);
    }
    EXPECT_TRUE(found) << describe(bits) << ": ACK_CAPACITY is unconditional";
  }
}

TEST(InfoPlan, EveryTagFitsInTheDeclaredMaximum) {
  InfoRecord plan[kInfoPlanMax];
  const size_t n = buildInfoPlan(inputsFor(0xF), plan);
  EXPECT_EQ(kInfoPlanMax, n) << "all four gates open must yield exactly kInfoPlanMax records";
  EXPECT_EQ(kPlanTagChanNotify, plan[0].tag);
  EXPECT_EQ(kPlanTagDisplayRotation, plan[1].tag);
  EXPECT_EQ(kPlanTagAckCapacity, plan[2].tag);
  EXPECT_EQ(kPlanTagDisplayControl, plan[3].tag);
  EXPECT_EQ(kPlanTagLedControl, plan[4].tag);
}

// ------------------------------------------------------- each gate, present and absent

TEST(InfoPlan, EachGateOmitsItsTagWhenShut) {
  struct Case {
    unsigned bits;
    uint8_t tag;
    const char* name;
  } cases[] = {
      {1u, kPlanTagChanNotify, "buzzer"},
      {2u, kPlanTagDisplayRotation, "rotation"},
      {4u, kPlanTagDisplayControl, "display"},
      {8u, kPlanTagLedControl, "led"},
  };
  for (const auto& c : cases) {
    InfoRecord open_plan[kInfoPlanMax];
    const size_t open_n = buildInfoPlan(inputsFor(c.bits), open_plan);
    bool open_has = false;
    for (size_t i = 0; i < open_n; i++) open_has |= (open_plan[i].tag == c.tag);
    EXPECT_TRUE(open_has) << c.name << " open must emit its tag";

    InfoRecord shut_plan[kInfoPlanMax];
    const size_t shut_n = buildInfoPlan(inputsFor(0), shut_plan);
    bool shut_has = false;
    for (size_t i = 0; i < shut_n; i++) shut_has |= (shut_plan[i].tag == c.tag);
    EXPECT_FALSE(shut_has) << c.name << " shut must omit its tag, not send a zero";
  }
}

// A bare board -- no buzzer, no rotation, no screen, no controllable LED -- is still a
// device with capabilities. The advert must not be empty, because an empty one reads as
// "this device has nothing".
TEST(InfoPlan, ABareBoardStillSendsOneRecord) {
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);
  InfoRecord plan[kInfoPlanMax];
  const size_t n = buildInfoPlan(inputsFor(0), plan);
  ASSERT_EQ(1u, n);
  ASSERT_EQ(InfoPlanOutcome::Complete, writeInfoPlan(advert, plan, n, 0));
  EXPECT_EQ(3u, advert.finish()) << "one one-byte-valued record is tag + len + value";
}

// ------------------------------------------------------- the resume cursor

TEST(InfoPlan, ACursorYieldsExactlyTheSuffixAtOrAboveIt) {
  for (unsigned bits = 0; bits < 16; bits++) {
    InfoRecord plan[kInfoPlanMax];
    const size_t n = buildInfoPlan(inputsFor(bits), plan);
    for (uint8_t cursor = 0; cursor <= kPlanTagLedControl + 1; cursor++) {
      uint8_t buf[kRoomy] = {0};
      InfoAdvert advert(buf, sizeof buf);
      ASSERT_EQ(InfoPlanOutcome::Complete, writeInfoPlan(advert, plan, n, cursor))
          << describe(bits) << " cursor=" << (int)cursor;

      std::vector<uint8_t> want;
      for (size_t i = 0; i < n; i++) {
        if (plan[i].tag >= cursor) want.push_back(plan[i].tag);
      }
      const std::vector<Rec> recs = parse(buf, advert.finish());
      ASSERT_EQ(want.size(), recs.size()) << describe(bits) << " cursor=" << (int)cursor;
      for (size_t i = 0; i < want.size(); i++) {
        EXPECT_EQ(want[i], recs[i].tag) << describe(bits) << " cursor=" << (int)cursor;
      }
    }
  }
}

// ------------------------------------------------------- truncation is NOT misordering

TEST(InfoPlan, ARealTruncationReportsTruncatedAndNamesTheResumeTag) {
  // Room for the first record plus the reserved CONTINUE, and nothing more.
  uint8_t buf[6] = {0};
  InfoAdvert advert(buf, sizeof buf);
  InfoRecord plan[kInfoPlanMax];
  const size_t n = buildInfoPlan(inputsFor(0xF), plan);

  const InfoPlanOutcome outcome = writeInfoPlan(advert, plan, n, 0);
  EXPECT_EQ(InfoPlanOutcome::Truncated, outcome)
      << "running out of room is ordinary, and must not read as a plan bug";

  const std::vector<Rec> recs = parse(buf, advert.finish());
  ASSERT_FALSE(recs.empty());
  const Rec& last = recs.back();
  ASSERT_EQ(InfoAdvert::kInfoTagContinue, last.tag) << "a cut advert must announce itself";
  ASSERT_EQ(1u, last.value.size());
  EXPECT_EQ(plan[recs.size() - 1].tag, last.value[0])
      << "the cursor names the first tag that did NOT fit";
}

TEST(InfoPlan, NothingFittingReportsStalled) {
  uint8_t buf[kInfoMinCapacity] = {0};
  InfoAdvert advert(buf, sizeof buf);
  InfoRecord plan[kInfoPlanMax];
  const size_t n = buildInfoPlan(inputsFor(0xF), plan);

  EXPECT_EQ(InfoPlanOutcome::Stalled, writeInfoPlan(advert, plan, n, 0))
      << "no progress means there is no cursor worth sending; the caller must refuse";
  EXPECT_EQ(0u, advert.finish()) << "a stalled advert carries no CONTINUE record";
}

// ------------------------------------------------------- the negative half: bad plans

// The order 1.7.1 actually emitted, as data. This is the assertion that the shipped
// sequence is refused rather than silently sent one record short.
TEST(InfoPlan, TheOrderThatShippedIsReportedMisordered) {
  const InfoRecord shipped[] = {
      {kPlanTagChanNotify, false, 0},
      {kPlanTagDisplayRotation, false, 0},
      {kPlanTagDisplayControl, false, 0},
      {kPlanTagLedControl, false, 0},
      {kPlanTagAckCapacity, true, 7},     // 0x03 after 0x05: add() refuses this
  };
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);

  EXPECT_EQ(InfoPlanOutcome::Misordered,
            writeInfoPlan(advert, shipped, sizeof shipped / sizeof shipped[0], 0));
}

// What that same order produced when the refusal was discarded -- the 1.7.1 wire, recorded
// so the cost of ignoring the return value is not a story but a test. ACK_CAPACITY gone,
// and no CONTINUE record saying anything was lost.
TEST(InfoPlan, DiscardingTheRefusalSendsAShortAdvertWithNoCursor) {
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);
  advert.addFlag(kPlanTagChanNotify);
  advert.addFlag(kPlanTagDisplayRotation);
  advert.addFlag(kPlanTagDisplayControl);
  advert.addFlag(kPlanTagLedControl);
  advert.addByte(kPlanTagAckCapacity, 7);     // refused, return dropped

  const std::vector<Rec> recs = parse(buf, advert.finish());
  EXPECT_EQ(4u, recs.size());
  EXPECT_FALSE(hasTag(recs, kPlanTagAckCapacity)) << "the record the client never saw";
  EXPECT_FALSE(hasTag(recs, InfoAdvert::kInfoTagContinue))
      << "and nothing on the wire said it was missing -- the whole defect";
  EXPECT_TRUE(advert.complete()) << "the writer reported success, because it was not out of room";
}

TEST(InfoPlan, ARepeatedTagIsMisordered) {
  const InfoRecord dup[] = {
      {kPlanTagAckCapacity, true, 7},
      {kPlanTagAckCapacity, true, 7},
  };
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);
  EXPECT_EQ(InfoPlanOutcome::Misordered, writeInfoPlan(advert, dup, 2, 0))
      << "equal is not ascending";
}

TEST(InfoPlan, AZeroTagIsMisordered) {
  const InfoRecord bad[] = {{kInfoTagInvalid, false, 0}};
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);
  EXPECT_EQ(InfoPlanOutcome::Misordered, writeInfoPlan(advert, bad, 1, 0))
      << "tag 0 is reserved so a zeroed buffer cannot read as a record";
}

TEST(InfoPlan, ANullPlanIsMisordered) {
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);
  EXPECT_EQ(InfoPlanOutcome::Misordered, writeInfoPlan(advert, nullptr, 3, 0));
}

TEST(InfoPlan, AnAdvertWithNoBufferIsMisorderedNotTruncated) {
  InfoAdvert advert(nullptr, 64);
  InfoRecord plan[kInfoPlanMax];
  const size_t n = buildInfoPlan(inputsFor(0xF), plan);
  EXPECT_EQ(InfoPlanOutcome::Misordered, writeInfoPlan(advert, plan, n, 0))
      << "a null buffer is a caller bug, not a capacity shortfall";
}

TEST(InfoPlan, AnEmptyPlanWritesNothingAndIsComplete) {
  uint8_t buf[kRoomy] = {0};
  InfoAdvert advert(buf, sizeof buf);
  InfoRecord plan[kInfoPlanMax];
  EXPECT_EQ(InfoPlanOutcome::Complete, writeInfoPlan(advert, plan, 0, 0))
      << "a zero-length plan is not a stall: the writer was never asked for anything";
  EXPECT_EQ(0u, advert.finish());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
