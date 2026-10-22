// #1316: a stream that gives up must SAY SO, on a build a tester actually runs.
//
// THE DEFECT THIS PINS
//   D1 (#1301) added stall-aborts to the contacts, block-list and observer-config drains,
//   so a stream that cannot make progress stops instead of spinning. Each abort reported --
//   through MESH_DEBUG_PRINTLN, which tests g_meshLogEnabled before it reaches any sink.
//   Capture is OFF by default on a shipping image (#428). So on every build a tester runs,
//   abandoning a stream and losing data was SILENT.
//
//   Not hypothetical: #1283 survived six observed occurrences across two diagnosis rounds
//   because the firmware dropped contacts and said nothing. The owner found it by counting
//   what arrived on the host, never from device output.
//
//   The report now goes to crashLogf -- retained ring, present on a stock build, survives a
//   reset, and since #1087 it suppresses its own Serial echo when the console carries the
//   framed protocol, so it cannot corrupt the stream it is reporting on.
//
// WHAT IS TESTED HERE
//   The two decisions that make the report useful rather than harmful, extracted into
//   StreamAbandonReport.h precisely so they are reachable natively (the same reason
//   meshLogRoute() is a free function): WHETHER to emit, and WHAT the line says.
//
//   Several cases below exist because adversarial review found the first design wrong:
//   the limiter was global across all nine sites, so a host hang that stalled every stream
//   reported one of them and silently swallowed the rest.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "helpers/StreamAbandonReport.h"

using offband::AbandonDecision;
using offband::AbandonLimiter;
using offband::abandonFormat;
using offband::abandonTake;

namespace {
constexpr uint32_t kWindow = 30000;
constexpr uint16_t kCap    = 12;

std::string render(const char* stream, const char* phase, uint32_t stalled,
                   uint32_t progress, const char* unit, uint32_t total,
                   uint16_t suppressed) {
  char buf[176];
  abandonFormat(buf, sizeof(buf), stream, phase, stalled, progress, unit, total, suppressed);
  return std::string(buf);
}
}  // namespace

// The first report for a stream always goes out. A limiter that ate the first one would
// reproduce the very silence this task removes.
TEST(AbandonLimiter, FirstReportAlwaysEmits) {
  AbandonLimiter st;
  const AbandonDecision d = abandonTake(st, "contacts", 0, kWindow, kCap);
  EXPECT_TRUE(d.emit) << "a limiter must never eat the first report";
  EXPECT_EQ(d.suppressed, 0u);
}

// `ever` exists so a report at millis()==0 is not confused with "nothing reported yet".
// Without it, last_ms==0 is ambiguous and the next report inside the window would emit.
TEST(AbandonLimiter, ReportAtTimeZeroStillArmsTheWindow) {
  AbandonLimiter st;
  ASSERT_TRUE(abandonTake(st, "contacts", 0, kWindow, kCap).emit);
  EXPECT_FALSE(abandonTake(st, "contacts", 1, kWindow, kCap).emit)
      << "a first report at t=0 must still start the window";
}

TEST(AbandonLimiter, SuppressesInsideTheWindowAndReportsTheCountOnTheNextLine) {
  AbandonLimiter st;
  ASSERT_TRUE(abandonTake(st, "contacts", 1000, kWindow, kCap).emit);

  int swallowed = 0;
  for (uint32_t t = 1001; t < 1000 + kWindow; t += 1000) {
    if (!abandonTake(st, "contacts", t, kWindow, kCap).emit) ++swallowed;
  }
  ASSERT_GT(swallowed, 0);

  // At exactly window_ms the window has elapsed, and the count rides out on that line.
  const AbandonDecision d = abandonTake(st, "contacts", 1000 + kWindow, kWindow, kCap);
  EXPECT_TRUE(d.emit) << "the window is elapsed at exactly window_ms and must reopen";
  EXPECT_EQ((int)d.suppressed, swallowed)
      << "the swallowed count must reach the log, or rate limiting reads as quiet";
}

// THE REGRESSION CASE FROM REVIEW. A host that hangs its read endpoint stalls EVERY open
// stream, and they abort within milliseconds of each other. The first design used one global
// limiter, so it named whichever aborted first and swallowed the others -- turning "all four
// streams died at once", which identifies a host hang, into "contacts died", which does not.
TEST(AbandonLimiter, ConcurrentStreamsEachGetTheirOwnLine) {
  AbandonLimiter st;

  EXPECT_TRUE(abandonTake(st, "contacts",  15000, kWindow, kCap).emit);
  EXPECT_TRUE(abandonTake(st, "blocklist", 15100, kWindow, kCap).emit)
      << "a second stream aborting 100ms later must not be swallowed by the first";
  EXPECT_TRUE(abandonTake(st, "caplog",    15200, kWindow, kCap).emit);
  EXPECT_TRUE(abandonTake(st, "obconfig",  15300, kWindow, kCap).emit);

  // ...while each stream is still individually rate limited.
  EXPECT_FALSE(abandonTake(st, "contacts", 15400, kWindow, kCap).emit);
}

// Two call sites share a stream name ("contacts" from both the sync and the terminator
// path). They must share one window, so a stream cannot double its budget by having two
// abort sites -- and the key compare must be by VALUE, since whether two identical string
// literals share an address is a toolchain decision, not a guarantee.
TEST(AbandonLimiter, SameStreamNameFromDifferentPointersSharesOneWindow) {
  AbandonLimiter st;
  char first[]  = "contacts";   // distinct addresses, equal contents
  char second[] = "contacts";
  ASSERT_NE(static_cast<const void*>(first), static_cast<const void*>(second));

  ASSERT_TRUE(abandonTake(st, first, 1000, kWindow, kCap).emit);
  EXPECT_FALSE(abandonTake(st, second, 1500, kWindow, kCap).emit)
      << "keyed by pointer, not value -- one stream would get two windows";
}

// THE OTHER REGRESSION FROM REVIEW. The ring holds ~50 lines and is the only copy on a stock
// build. A persistently failing host aborts forever, and with only a window the reporter
// fills the ring by itself in minutes, destroying the boot lines and reset reason that give
// an abandonment its meaning. Past the cap it must keep counting and stop writing.
TEST(AbandonLimiter, PerBootCapStopsTheReporterFillingTheRing) {
  AbandonLimiter st;
  uint32_t t = 0;
  int emitted = 0;

  // Well past the cap, each one a fresh window so only the cap can stop it.
  for (int i = 0; i < kCap * 5; ++i) {
    t += kWindow + 1;
    if (abandonTake(st, "contacts", t, kWindow, kCap).emit) ++emitted;
  }

  EXPECT_EQ(emitted, (int)kCap) << "the reporter wrote more than its per-boot budget";
  EXPECT_EQ(st.emitted, kCap);
  EXPECT_GT(st.slots[0].suppressed, 0u) << "past the cap it must still COUNT";
}

// A wedged host can abort for a long time. The counter must saturate rather than wrap:
// 65535 and "more than 65535" support the same conclusion, but a wrapped count reading 3
// after a storm would actively mislead whoever reads the ring.
TEST(AbandonLimiter, SuppressedCountSaturatesRatherThanWrapping) {
  AbandonLimiter st;
  ASSERT_TRUE(abandonTake(st, "contacts", 0, kWindow, kCap).emit);
  for (uint32_t i = 0; i < 70000; ++i) {
    abandonTake(st, "contacts", 1, kWindow, kCap);   // always inside the window
  }
  EXPECT_EQ(st.slots[0].suppressed, 0xFFFFu) << "must clamp, never roll to a small number";
}

// millis() wraps about every 49.7 days and these nodes run for months. A naive comparison
// would read the rollover as an enormous elapsed time and emit on every abort, flooding the
// ring exactly once per wrap.
TEST(AbandonLimiter, SurvivesMillisRollover) {
  AbandonLimiter st;
  const uint32_t before = 0xFFFFFFFFu - 1000;
  ASSERT_TRUE(abandonTake(st, "contacts", before, kWindow, kCap).emit);

  EXPECT_FALSE(abandonTake(st, "contacts", before + 500, kWindow, kCap).emit)
      << "a rollover must not be read as a huge elapsed time";
  EXPECT_TRUE(abandonTake(st, "contacts", before + kWindow + 1, kWindow, kCap).emit)
      << "and a genuinely elapsed window across the wrap must still reopen";
}

// A fifth stream name has nowhere to be tracked. It must be counted, not dropped on the
// floor -- silently losing it is how a table that needs another slot stays unnoticed.
TEST(AbandonLimiter, MoreStreamsThanSlotsIsCountedNotLost) {
  AbandonLimiter st;
  ASSERT_TRUE(abandonTake(st, "contacts",  0, kWindow, kCap).emit);
  ASSERT_TRUE(abandonTake(st, "blocklist", 0, kWindow, kCap).emit);
  ASSERT_TRUE(abandonTake(st, "caplog",    0, kWindow, kCap).emit);
  ASSERT_TRUE(abandonTake(st, "obconfig",  0, kWindow, kCap).emit);

  EXPECT_FALSE(abandonTake(st, "somethingnew", 0, kWindow, kCap).emit);
  EXPECT_EQ(st.unslotted, 1u) << "an untrackable stream must leave a trace";
}

TEST(AbandonLimiter, NullStreamIsIgnoredRatherThanCrashing) {
  AbandonLimiter st;
  EXPECT_FALSE(abandonTake(st, nullptr, 0, kWindow, kCap).emit);
}

// A stream that knows what it promised reports delivered/total -- the form that answers
// whether the client died one item in or three hundred.
TEST(AbandonFormat, StreamWithATotalReportsDeliveredOverTotal) {
  const std::string line = render("contacts", "sync", 15000, 347, " contacts", 350, 0);

  EXPECT_NE(line.find("ABANDONED contacts/sync"), std::string::npos) << line;
  EXPECT_NE(line.find("stalled=15000ms"), std::string::npos) << line;
  EXPECT_NE(line.find("delivered=347/350 contacts"), std::string::npos) << line;
  EXPECT_NE(line.find("#1316"), std::string::npos) << line;
}

// A stream tracking a byte or slot offset has no item count. It must NOT print a
// denominator -- "/0" reads like a bug, and any other number would be invented.
TEST(AbandonFormat, StreamWithoutATotalOmitsTheDenominator) {
  const std::string line = render("caplog", "download", 15000, 545, " bytes", 0, 0);

  EXPECT_NE(line.find("progress=545 bytes"), std::string::npos) << line;
  EXPECT_EQ(line.find("/0"), std::string::npos)
      << "printed a denominator it does not have: " << line;
  EXPECT_EQ(line.find("delivered="), std::string::npos)
      << "'delivered' claims a known total: " << line;
}

// Review's point: `progress=1024` across four streams that count items, bytes, a buffer
// offset and a slot index sends the reader back to the source to find out which. The unit
// has to travel with the number.
TEST(AbandonFormat, TheUnitTravelsWithTheNumber) {
  EXPECT_NE(render("caplog", "download", 1, 1024, " bytes", 0, 0).find("1024 bytes"),
            std::string::npos);
  EXPECT_NE(render("blocklist", "list", 1, 3, " slot", 0, 0).find("3 slot"),
            std::string::npos);
  EXPECT_NE(render("contacts", "sync", 1, 10, " contacts", 20, 0).find("10/20 contacts"),
            std::string::npos);
}

TEST(AbandonFormat, CarriesTheSuppressedCount) {
  EXPECT_NE(render("obconfig", "view", 15000, 12, " bytes", 0, 417).find("suppressed=417"),
            std::string::npos);
  EXPECT_NE(render("contacts", "terminator", 15000, 350, " contacts", 350, 0)
                .find("suppressed=0"),
            std::string::npos);
}

// Every site's identity must survive into the line, so the ring says WHICH stream died.
TEST(AbandonFormat, NamesTheStreamAndPhase) {
  struct { const char* stream; const char* phase; } kSites[] = {
    {"contacts",  "sync"},
    {"contacts",  "terminator"},
    {"blocklist", "list"},
    {"blocklist", "terminator"},
    {"caplog",    "download"},
    {"obconfig",  "view"},
    {"obconfig",  "view-terminator"},
    {"obconfig",  "brokers"},
    {"obconfig",  "brokers-terminator"},
  };
  for (const auto& s : kSites) {
    const std::string want = std::string(s.stream) + "/" + s.phase;
    const std::string line = render(s.stream, s.phase, 15000, 1, " bytes", 0, 0);
    EXPECT_NE(line.find(want), std::string::npos)
        << "site " << want << " lost its identity: " << line;
  }
}

TEST(AbandonFormat, NullUnitDoesNotCrashOrPrintGarbage) {
  const std::string line = render("caplog", "download", 1, 7, nullptr, 0, 0);
  EXPECT_NE(line.find("progress=7"), std::string::npos) << line;
  EXPECT_NE(line.find("#1316"), std::string::npos) << line;
}

// The real buffer is 176 bytes. The longest site name with saturated counters and ten-digit
// values must still fit, or the issue number is cut off the end -- the part that tells a
// reader where to look.
TEST(AbandonFormat, LongestRealisticLineIsNotTruncated) {
  char buf[176];
  const int would_be = abandonFormat(buf, sizeof(buf), "obconfig", "brokers-terminator",
                                     4294967295u, 4294967295u, " contacts", 4294967295u,
                                     0xFFFF);

  EXPECT_LT(would_be, (int)sizeof(buf))
      << "worst-case line needs " << would_be << " bytes, buffer is " << sizeof(buf);
  EXPECT_NE(std::string(buf).find("#1316"), std::string::npos)
      << "the issue reference was truncated away: " << buf;
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
