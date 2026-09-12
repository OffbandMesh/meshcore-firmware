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
    // "caplog-" + 24 + ':' is the RFC 3164 TAG limit of 32.
    EXPECT_EQ(CaplogForward::kTagMax, 24u);
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

TEST_F(CaplogForwardTest, PercentSignsInATagBecomeDashes) {
    // Never part of the format string, and not legal in a TAG either.
    CaplogForward fwd("a%s%n%x", fakeRead, sink);
    EXPECT_STREQ(fwd.prefix(), "<134>caplog-a-s-n-x: ");
}

TEST_F(CaplogForwardTest, ANullTagBecomesUnknown) {
    CaplogForward fwd(nullptr, fakeRead, sink);
    EXPECT_STREQ(fwd.prefix(), "<134>caplog-unknown: ");
}

TEST_F(CaplogForwardTest, TheRepeaterTagsInUseAreUnchanged) {
    // The repeater keeps WIFI_TELEMETRY_NODE_ID; sanitizing must not alter it.
    CaplogForward a("wsmj898-ltb", fakeRead, sink);
    CaplogForward b("stp-lab", fakeRead, sink);
    EXPECT_STREQ(a.prefix(), "<134>caplog-wsmj898-ltb: ");
    EXPECT_STREQ(b.prefix(), "<134>caplog-stp-lab: ");
}

TEST_F(CaplogForwardTest, SetTagReplacesThePrefix) {
    CaplogForward fwd(nullptr, fakeRead, sink);   // identity not loaded yet
    fwd.setTag("4A1B2C3D4E5F6071");
    EXPECT_STREQ(fwd.prefix(), "<134>caplog-4A1B2C3D4E5F6071: ");
}

TEST_F(CaplogForwardTest, SetTagSanitizesTheNewTag) {
    CaplogForward fwd("old-tag", fakeRead, sink);
    fwd.setTag("a new tag that is over 24 chars long");
    EXPECT_STREQ(fwd.prefix(), "<134>caplog-a-new-tag-that-is-over-2: ");
}

// ------------------------------------------------------------ tag sanitizing (#1059)

namespace {

std::string sanitized(const char* in, size_t cap = 64) {
    char out[64];
    offband::caplogSanitizeTag(in, out, cap);
    return out;
}

}  // namespace

TEST(CaplogTag, LegalCharactersAreKept) {
    EXPECT_EQ(sanitized("Node_01-ab"), "Node_01-ab");
}

TEST(CaplogTag, SpacesBecomeDashes) {
    EXPECT_EQ(sanitized("my node 2"), "my-node-2");
}

TEST(CaplogTag, PunctuationBecomesDashes) {
    EXPECT_EQ(sanitized("a.b:c[1]/d!"), "a-b-c-1--d-");
}

TEST(CaplogTag, NonAsciiBytesBecomeDashes) {
    EXPECT_EQ(sanitized("caf\xC3\xA9"), "caf--");   // one dash per UTF-8 byte
}

TEST(CaplogTag, OverlongInputIsCutAtTheLimit) {
    EXPECT_EQ(sanitized("abcdefghijklmnopqrstuvwxyz0123456789"),
              "abcdefghijklmnopqrstuvwx");   // 24
}

TEST(CaplogTag, EmptyAndNullBecomeUnknown) {
    EXPECT_EQ(sanitized(""), "unknown");
    EXPECT_EQ(sanitized(nullptr), "unknown");
}

TEST(CaplogTag, ASmallOutputBufferIsRespected) {
    EXPECT_EQ(sanitized("abcdefgh", 5), "abcd");
    char one[1] = {'x'};
    offband::caplogSanitizeTag("abc", one, sizeof(one));
    EXPECT_EQ(one[0], '\0');
}

TEST(CaplogTag, PubKeyTagIsTheFirstEightBytesInUppercaseHex) {
    const uint8_t key[32] = {0x4a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71, 0xff, 0xee};
    char out[17];
    offband::caplogPubKeyTag(key, out, sizeof(out));
    // Uppercase, as the MQTT device_id is, so the tag is a prefix of it.
    EXPECT_STREQ(out, "4A1B2C3D4E5F6071");
}

TEST(CaplogTag, PubKeyTagWithoutAKeyIsEmptyAndSanitizesToUnknown) {
    char out[17] = {'x'};
    offband::caplogPubKeyTag(nullptr, out, sizeof(out));
    EXPECT_STREQ(out, "");
    EXPECT_EQ(sanitized(out), "unknown");
}

TEST(CaplogTag, PubKeyTagRespectsASmallOutputBuffer) {
    const uint8_t key[8] = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89};
    char out[6];
    offband::caplogPubKeyTag(key, out, sizeof(out));
    EXPECT_STREQ(out, "ABCD");   // whole bytes only
}

// ------------------------------------------------------------ the announcement (#1059)

TEST_F(CaplogForwardTest, OpeningAWindowAnnouncesTheFullIdAndTheSink) {
    const std::string id(64, 'A');
    CaplogForward fwd("4A1B2C3D4E5F6071", fakeRead, sink);
    fwd.setIdentity(id.c_str());
    fwd.armFor(300, 0);
    g_chunks.push_back("x\n");
    fwd.service("sink.example.net", 514, true, 1);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{
                               "[caplog] forward on: id=" + id + " sink=sink.example.net:514", "x"}));
    EXPECT_EQ(sink.sent[0].prefix, "<134>caplog-4A1B2C3D4E5F6071: ");
    EXPECT_EQ(fwd.linesSent(), 1u) << "the announcement is not a forwarded line";
}

TEST_F(CaplogForwardTest, TheAnnouncementIsOncePerWindow) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.setIdentity("ID");
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);
    fwd.service("h", 514, true, 2);
    fwd.armFor(300, 3);                  // extends the open window
    fwd.service("h", 514, true, 4);
    fwd.disarm();
    fwd.armFor(300, 5);                  // a new window
    fwd.service("h", 514, true, 6);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{
                               "[caplog] forward on: id=ID sink=h:514",
                               "[caplog] forward on: id=ID sink=h:514"}));
}

TEST_F(CaplogForwardTest, TheAnnouncementWaitsForTheLink) {
    CaplogForward fwd("n", fakeRead, sink);
    fwd.setIdentity("ID");
    fwd.armFor(300, 0);
    fwd.service("h", 514, false, 1);
    EXPECT_TRUE(sink.sent.empty());
    fwd.service("h", 514, true, 2);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"[caplog] forward on: id=ID sink=h:514"}));
}

TEST_F(CaplogForwardTest, NoIdentityMeansNoAnnouncement) {
    CaplogForward fwd("n", fakeRead, sink);   // the repeater today
    fwd.armFor(300, 0);
    g_chunks.push_back("x\n");
    fwd.service("h", 514, true, 1);
    EXPECT_EQ(lines(sink), (std::vector<std::string>{"x"}));
}

TEST_F(CaplogForwardTest, AnOverlongIdentityIsCut) {
    const std::string id(100, 'B');
    CaplogForward fwd("n", fakeRead, sink);
    fwd.setIdentity(id.c_str());
    fwd.armFor(300, 0);
    fwd.service("h", 514, true, 1);
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent[0].line,
              "[caplog] forward on: id=" + std::string(offband::kCaplogIdentityMax, 'B') + " sink=h:514");
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
