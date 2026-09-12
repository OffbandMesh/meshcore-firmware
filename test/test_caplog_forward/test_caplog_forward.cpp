#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "CaptureRing.h"
#include "helpers/CaplogForward.h"

using offband::CaplogDatagramSink;
using offband::CaplogForward;

namespace {

// Chunks the fake ring hands out, one per read call.
std::deque<std::string> g_chunks;
int g_reads = 0;

size_t fakeRead(uint8_t* out, size_t out_cap) {
    ++g_reads;
    if (g_chunks.empty()) return 0;
    std::string c = g_chunks.front();
    g_chunks.pop_front();
    EXPECT_LE(c.size(), out_cap) << "a chunk larger than the helper's buffer";
    const size_t n = c.size() < out_cap ? c.size() : out_cap;
    std::memcpy(out, c.data(), n);
    return n;
}

struct Datagram {
    std::string host;
    uint16_t port;
    std::string prefix;
    std::string line;
};

class FakeSink : public CaplogDatagramSink {
public:
    std::vector<Datagram> sent;
    void send(const char* host, uint16_t port, const char* prefix,
              const uint8_t* line, size_t len) override {
        sent.push_back({host, port, prefix, std::string(reinterpret_cast<const char*>(line), len)});
    }
};

class CaplogForwardTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_chunks.clear();
        g_reads = 0;
    }
    FakeSink sink;
};

std::vector<std::string> lines(const FakeSink& s) {
    std::vector<std::string> out;
    for (const auto& d : s.sent) out.push_back(d.line);
    return out;
}

}  // namespace

// ------------------------------------------------------------ framing

TEST_F(CaplogForwardTest, OneLineIsOneDatagramWithThePrefixAndNoNewline) {
    CaplogForward fwd("wsmj898-ltb", fakeRead, sink);
    fwd.armFor(300, 1000);
    g_chunks.push_back("[123] hello\n");
    fwd.service("sink.example.net", 514, true, 2000);
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent[0].host, "sink.example.net");
    EXPECT_EQ(sink.sent[0].port, 514);
    EXPECT_EQ(sink.sent[0].prefix, "<134>caplog-wsmj898-ltb: ");
    EXPECT_EQ(sink.sent[0].line, "[123] hello");
}

TEST_F(CaplogForwardTest, ManyLinesInAChunkLeaveInOrder) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    g_chunks.push_back("a\nbb\nccc\n");
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"a", "bb", "ccc"}));
    EXPECT_EQ(fwd.linesSent(), 3u);
}

TEST_F(CaplogForwardTest, AFinalLineWithoutNewlineStillLeaves) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    g_chunks.push_back("first\nsecond");
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"first", "second"}));
}

TEST_F(CaplogForwardTest, EmptyInputSendsNothing) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);   // the ring has nothing
    EXPECT_TRUE(sink.sent.empty());
    EXPECT_EQ(g_reads, 1);
}

TEST_F(CaplogForwardTest, EmptyLinesAreSkipped) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    g_chunks.push_back("\n\nabc\n\n");
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"abc"}));
}

TEST_F(CaplogForwardTest, ALineCrossingTheChunkBoundaryLeavesAsTwoDatagrams) {
    // When no newline fits in a chunk, consume hands over a full chunk of one
    // line; the rest arrives with the next read. Each part leaves on its own.
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    g_chunks.push_back(std::string(CaplogForward::kChunkBytes, 'x'));
    g_chunks.push_back("tail\n");
    fwd.service("h", 514, true, 1);
    fwd.service("h", 514, true, 2);
    ASSERT_EQ(sink.sent.size(), 2u);
    EXPECT_EQ(sink.sent[0].line, std::string(CaplogForward::kChunkBytes, 'x'));
    EXPECT_EQ(sink.sent[1].line, "tail");
}

TEST_F(CaplogForwardTest, ForEachLineMatchesTheRepeatersOriginalSplit) {
    std::vector<std::string> got;
    const char* buf = "one\n\ntwo\nthree";
    offband::caplogForEachLine(reinterpret_cast<const uint8_t*>(buf), std::strlen(buf),
                               [&](const uint8_t* l, size_t n) {
                                   got.emplace_back(reinterpret_cast<const char*>(l), n);
                               });
    EXPECT_EQ(got, (std::vector<std::string>{"one", "two", "three"}));
}

// ------------------------------------------------------------ bounded drain

TEST_F(CaplogForwardTest, EachServiceCallReadsExactlyOneChunk) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    g_chunks.push_back("a\n");
    g_chunks.push_back("b\n");
    g_chunks.push_back("c\n");
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(g_reads, 1) << "one read per call, never the whole ring";
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"a"}));
    fwd.service("h", 514, true, 2);
    fwd.service("h", 514, true, 3);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"a", "b", "c"}));
}

// ------------------------------------------------------------ when it does nothing

TEST_F(CaplogForwardTest, NotArmedReadsNothing) {
    CaplogForward fwd("n", fakeRead, sink);
    g_chunks.push_back("a\n");
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(g_reads, 0);
    EXPECT_TRUE(sink.sent.empty());
}

TEST_F(CaplogForwardTest, LinkDownReadsNothingSoLinesWait) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    g_chunks.push_back("a\n");
    fwd.service("h", 514, false, 1);
    EXPECT_EQ(g_reads, 0) << "a down link must not take lines out of the ring";
    fwd.service("h", 514, true, 2);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"a"}));
}

TEST_F(CaplogForwardTest, NoSinkHostReadsNothing) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    g_chunks.push_back("a\n");
    fwd.service("", 514, true, 1);
    fwd.service(nullptr, 514, true, 2);
    EXPECT_EQ(g_reads, 0);
    EXPECT_TRUE(sink.sent.empty());
}

// ------------------------------------------------------------ the window

TEST_F(CaplogForwardTest, TheWindowClosesAtItsDeadline) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(30, 1000);
    EXPECT_TRUE(fwd.armed(1000));
    EXPECT_TRUE(fwd.armed(30999));
    EXPECT_FALSE(fwd.armed(31000));
    EXPECT_FALSE(fwd.armed(1000)) << "a closed window stays closed";
}

TEST_F(CaplogForwardTest, TheWindowSurvivesAMillisWrap) {
    CaplogForward fwd("n", fakeRead, sink);
    const uint32_t start = 0xFFFFFFFFu - 1000u;   // 1 s before the wrap
    fwd.armFor(30, start);
    EXPECT_TRUE(fwd.armed(start + 20000u)) << "past the wrap, inside the window";
    EXPECT_FALSE(fwd.armed(start + 30000u));
}

TEST_F(CaplogForwardTest, DisarmStopsImmediately) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    fwd.disarm();
    g_chunks.push_back("a\n");
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(g_reads, 0);
}

TEST_F(CaplogForwardTest, ArmingForZeroDisarms) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.armFor(300, 0);
    fwd.armFor(0, 10);
    EXPECT_FALSE(fwd.armed(11));
}

TEST_F(CaplogForwardTest, ADeadlineThatLandsOnZeroStillCounts) {
    // The repeater once used 0 as "off", so a deadline that happened to wrap to
    // exactly 0 read as disarmed. The helper keeps an explicit armed flag.
    CaplogForward fwd("n", fakeRead, sink);
    const uint32_t start = 0u - 30000u;   // start + 30 s == 0
    fwd.armFor(30, start);
    EXPECT_TRUE(fwd.armed(start + 1000u));
}

// ------------------------------------------------------------ the prefix

TEST_F(CaplogForwardTest, ATagAtTheLimitFitsWhole) {
    std::string tag(CaplogForward::kTagMax, 't');
    CaplogForward fwd(tag.c_str(), fakeRead, sink);
    EXPECT_EQ(std::string(fwd.prefix()), "<134>caplog-" + tag + ": ");
    EXPECT_EQ(CaplogForward::kTagMax, 33u);
}

TEST_F(CaplogForwardTest, AnOverlongTagIsCutButKeepsTheSeparator) {
    // Cutting the ": " would run the tag into the message, and the receiver
    // would no longer parse the syslog TAG.
    std::string tag(100, 't');
    CaplogForward fwd(tag.c_str(), fakeRead, sink);
    EXPECT_EQ(std::string(fwd.prefix()),
              "<134>caplog-" + std::string(CaplogForward::kTagMax, 't') + ": ");
    EXPECT_LT(std::strlen(fwd.prefix()), CaplogForward::kPrefixMax);
}

TEST_F(CaplogForwardTest, PercentSignsInATagAreCopiedLiterally) {
    // The tag is an argument to %s, not part of the format string.
    CaplogForward fwd("a%s%n%x", fakeRead, sink);
    EXPECT_STREQ(fwd.prefix(), "<134>caplog-a%s%n%x: ");
}

TEST_F(CaplogForwardTest, ANullTagStillFormsAPrefix) {
    CaplogForward fwd(nullptr, fakeRead, sink);
    EXPECT_STREQ(fwd.prefix(), "<134>caplog-: ");
}

// ------------------------------------------------------------ cursor mode (#1193)

namespace {

CaptureRing* g_ring = nullptr;

size_t ringReadFrom(uint64_t* cursor, uint8_t* out, size_t out_cap, uint64_t* lost) {
    ++g_reads;
    return g_ring->readFrom(cursor, out, out_cap, lost);
}

// A real 16-byte ring behind the cursor read, so these see what the observer
// sees. Every line below is 5 bytes, so three fit and a fourth evicts one.
class CaplogCursorTest : public CaplogForwardTest {
protected:
    void SetUp() override {
        CaplogForwardTest::SetUp();
        g_ring = &ring;
    }
    void add(const char* s) {
        ring.append(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
    }
    std::string held() const {
        uint8_t out[64];
        const size_t n = ring.snapshot(out, sizeof(out));
        return std::string(reinterpret_cast<const char*>(out), n);
    }
    uint8_t store[16];
    CaptureRing ring{store, sizeof(store)};
};

}  // namespace

TEST_F(CaplogCursorTest, ForwardedLinesStayInTheRing) {
    CaplogForward fwd("n", ringReadFrom, sink);
    add("aaaa\nbbbb\n");
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"aaaa", "bbbb"}));
    EXPECT_EQ(held(), "aaaa\nbbbb\n") << "the app's download must still have both lines";
}

TEST_F(CaplogCursorTest, EachLineIsSentOnce) {
    CaplogForward fwd("n", ringReadFrom, sink);
    fwd.armFor(300, 0);
    add("aaaa\n");
    fwd.service("h", 514, true, 1);
    fwd.service("h", 514, true, 2);   // nothing new
    add("bbbb\n");
    fwd.service("h", 514, true, 3);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"aaaa", "bbbb"}));
    EXPECT_EQ(g_reads, 3) << "one read per call";
}

TEST_F(CaplogCursorTest, LossIsAnnouncedWithTheExactCountBeforeTheNextLines) {
    CaplogForward fwd("n", ringReadFrom, sink);
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);    // starts at the empty ring
    add("aaaa\nbbbb\ncccc\n");
    fwd.service("h", 514, false, 2);   // link down: nothing read
    add("dddd\n");                      // evicts "aaaa\n" unsent
    fwd.service("h", 514, true, 3);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{
                               "[caplog] forward lost 5 bytes", "bbbb", "cccc", "dddd"}));
    EXPECT_EQ(sink.sent[0].prefix, "<134>caplog-n: ");
    EXPECT_EQ(fwd.bytesLost(), 5u);
    EXPECT_EQ(fwd.linesSent(), 3u) << "the loss note is not a forwarded line";
}

TEST_F(CaplogCursorTest, TheFirstArmSendsTheBacklogWithoutALossReport) {
    add("aaaa\nbbbb\ncccc\n");
    add("dddd\n");                      // "aaaa\n" evicted before forwarding began
    CaplogForward fwd("n", ringReadFrom, sink);
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"bbbb", "cccc", "dddd"}));
    EXPECT_EQ(fwd.bytesLost(), 0u);
}

TEST_F(CaplogCursorTest, ALaterWindowStartsWhereTheLastStopped) {
    CaplogForward fwd("n", ringReadFrom, sink);
    add("aaaa\n");
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);
    fwd.disarm();
    add("bbbb\n");
    fwd.armFor(300, 10);
    fwd.service("h", 514, true, 11);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"aaaa", "bbbb"})) << "no line twice";
    EXPECT_EQ(fwd.bytesLost(), 0u);
}

TEST_F(CaplogCursorTest, AGapBetweenWindowsIsReported) {
    // The sink must never see a silent jump, even one that happened while
    // forwarding was off.
    CaplogForward fwd("n", ringReadFrom, sink);
    fwd.armFor(300, 0);
    add("aaaa\n");
    fwd.service("h", 514, true, 1);
    fwd.disarm();
    add("bbbb\ncccc\ndddd\n");
    add("eeee\n");                      // evicts sent "aaaa\n" and unsent "bbbb\n"
    fwd.armFor(300, 10);
    fwd.service("h", 514, true, 11);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{
                               "aaaa", "[caplog] forward lost 5 bytes", "cccc", "dddd", "eeee"}));
    EXPECT_EQ(fwd.bytesLost(), 5u);
}

TEST_F(CaplogCursorTest, ErasingUnsentLinesIsReportedAsLoss) {
    CaplogForward fwd("n", ringReadFrom, sink);
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);
    add("abc\n");
    ring.clear();                       // `caplog erase` before the next pass
    fwd.service("h", 514, true, 2);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"[caplog] forward lost 4 bytes"}));
    EXPECT_EQ(fwd.bytesLost(), 4u);
}

TEST_F(CaplogCursorTest, NotArmedOrLinkDownReadsNothing) {
    CaplogForward fwd("n", ringReadFrom, sink);
    add("aaaa\n");
    fwd.service("h", 514, true, 1);    // never armed
    fwd.armFor(300, 0);
    fwd.service("h", 514, false, 2);   // link down
    fwd.service("", 514, true, 3);     // no host
    EXPECT_EQ(g_reads, 0);
    EXPECT_TRUE(sink.sent.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
