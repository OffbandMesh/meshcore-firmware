// Native tests for CaplogForwardCli (#1060): the `caplog forward` command and
// the forward fields of `caplog status`, shared by the repeater and observer.
// The reply strings here are the user-facing copy; a change to one is a change
// to what an operator reads.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "MeshLog.h"   // MLOG_* levels
#include "helpers/CaplogForwardCli.h"

using offband::CaplogForward;
using offband::CaplogForwardArg;
using offband::CaplogForwardMode;
using offband::CaplogForwardStatus;

namespace {

size_t nothingToRead(uint8_t*, size_t) { return 0; }

class NullSink : public offband::CaplogDatagramSink {
public:
    void send(const char*, uint16_t, const char*, const uint8_t*, size_t) override {}
};

CaplogForwardArg parse(const char* s) { return offband::caplogParseForwardArg(s); }

std::string reply(const CaplogForwardArg& a, bool capture, bool sink, bool link,
                  const char* hint = " (wifi on <min>)") {
    char out[160];
    offband::caplogForwardReply(out, sizeof(out), a, capture, sink, link, hint);
    return out;
}

std::string status(const CaplogForwardStatus* fwd, bool capture = true) {
    char out[160];
    offband::caplogStatusLine(out, sizeof(out), capture, "debug", 812, 16384, fwd);
    return out;
}

CaplogForwardStatus st(CaplogForwardMode mode, const char* host, bool link,
                       uint32_t left = 0, uint32_t sent = 0, uint32_t lost = 0) {
    return CaplogForwardStatus{mode, left, host, 514, sent, lost, link};
}

}  // namespace

// ------------------------------------------------------------ parse

TEST(CaplogForwardParse, BareIsTheDefaultWindow) {
    EXPECT_EQ(parse("").kind, CaplogForwardArg::Bounded);
    EXPECT_EQ(parse("").seconds, 300u);
    EXPECT_EQ(parse(nullptr).kind, CaplogForwardArg::Bounded);
    EXPECT_EQ(parse("  ").seconds, 300u);
}

TEST(CaplogForwardParse, OnIsUntilOff) {
    EXPECT_EQ(parse("on").kind, CaplogForwardArg::UntilOff);
    EXPECT_EQ(parse(" on ").kind, CaplogForwardArg::UntilOff);
    EXPECT_EQ(parse("on\r\n").kind, CaplogForwardArg::UntilOff);
}

TEST(CaplogForwardParse, OffAndZeroDisarm) {
    EXPECT_EQ(parse("off").kind, CaplogForwardArg::Off);
    EXPECT_EQ(parse("0").kind, CaplogForwardArg::Off) << "0 used to arm 30 s";
    EXPECT_EQ(parse("000").kind, CaplogForwardArg::Off);
}

TEST(CaplogForwardParse, SecondsAreBoundedWithAFloor) {
    EXPECT_EQ(parse("300").kind, CaplogForwardArg::Bounded);
    EXPECT_EQ(parse("300").seconds, 300u);
    EXPECT_EQ(parse("5").seconds, 30u);
    EXPECT_EQ(parse("30").seconds, 30u);
    EXPECT_EQ(parse("31").seconds, 31u);
}

TEST(CaplogForwardParse, TheLimitIsAcceptedAndOneMoreIsTooLong) {
    EXPECT_EQ(parse("2147483").kind, CaplogForwardArg::Bounded);
    EXPECT_EQ(parse("2147483").seconds, 2147483u);
    EXPECT_EQ(parse("2147484").kind, CaplogForwardArg::TooLong);
    EXPECT_EQ(parse("3000000").kind, CaplogForwardArg::TooLong);
    EXPECT_EQ(parse("99999999999999999999999").kind, CaplogForwardArg::TooLong)
        << "no wrap back into range";
}

TEST(CaplogForwardParse, AnythingElseIsInvalid) {
    for (const char* s : {"abc", "12abc", "on please", "-5", "offx", "ON", "1 2"}) {
        EXPECT_EQ(parse(s).kind, CaplogForwardArg::Invalid) << s;
    }
}

// ------------------------------------------------------------ apply

TEST(CaplogForwardApply, EachArgumentArmsAndSaysWhatToPersist) {
    NullSink sink;
    CaplogForward fwd("n", nothingToRead, sink);
    EXPECT_EQ(offband::caplogApplyForwardArg(fwd, parse("on"), 0), 1);
    EXPECT_EQ(fwd.mode(1), CaplogForwardMode::UntilOff);
    EXPECT_EQ(offband::caplogApplyForwardArg(fwd, parse("60"), 2), 0) << "bounded does not survive a reboot";
    EXPECT_EQ(fwd.mode(3), CaplogForwardMode::Bounded);
    EXPECT_EQ(offband::caplogApplyForwardArg(fwd, parse("off"), 4), 0);
    EXPECT_EQ(fwd.mode(5), CaplogForwardMode::Off);
}

TEST(CaplogForwardApply, ATypoLeavesARunningForwardAlone) {
    NullSink sink;
    CaplogForward fwd("n", nothingToRead, sink);
    offband::caplogApplyForwardArg(fwd, parse("on"), 0);
    EXPECT_EQ(offband::caplogApplyForwardArg(fwd, parse("of"), 1), -1);
    EXPECT_EQ(offband::caplogApplyForwardArg(fwd, parse("3000000"), 2), -1);
    EXPECT_EQ(fwd.mode(3), CaplogForwardMode::UntilOff);
}

// ------------------------------------------------------------ replies (user-facing copy)

TEST(CaplogForwardReply, Streaming) {
    EXPECT_EQ(reply(parse("on"), true, true, true), "caplog forward on until off (streaming to syslog)");
    EXPECT_EQ(reply(parse("300"), true, true, true), "caplog forward on 300s (streaming to syslog)");
}

TEST(CaplogForwardReply, NoLink) {
    EXPECT_EQ(reply(parse("300"), true, true, false),
              "caplog forward on 300s -- no WiFi link; lines send once one is up (wifi on <min>)");
    EXPECT_EQ(reply(parse("on"), true, true, false),
              "caplog forward on until off -- no WiFi link; lines send once one is up (wifi on <min>)");
    EXPECT_EQ(reply(parse("on"), true, true, false, ""),
              "caplog forward on until off -- no WiFi link; lines send once one is up")
        << "the observer has no `wifi on <min>`";
}

TEST(CaplogForwardReply, NoSink) {
    EXPECT_EQ(reply(parse("on"), true, false, true), "caplog forward on until off -- no sink; set syslog.host <host>");
}

TEST(CaplogForwardReply, CaptureOff) {
    EXPECT_EQ(reply(parse("on"), false, true, true),
              "caplog forward on until off -- capture is off, nothing to send (caplog start)");
}

TEST(CaplogForwardReply, TheFirstProblemWins) {
    // Capture off, then no sink, then no link.
    EXPECT_EQ(reply(parse("60"), false, false, false),
              "caplog forward on 60s -- capture is off, nothing to send (caplog start)");
    EXPECT_EQ(reply(parse("60"), true, false, false), "caplog forward on 60s -- no sink; set syslog.host <host>");
}

TEST(CaplogForwardReply, OffTooLongAndInvalid) {
    EXPECT_EQ(reply(parse("off"), true, true, true), "caplog forward off");
    EXPECT_EQ(reply(parse("0"), false, false, false), "caplog forward off");
    EXPECT_EQ(reply(parse("3000000"), true, true, true), "caplog forward: max 2147483s; use caplog forward on");
    EXPECT_EQ(reply(parse("soon"), true, true, true), "ERR: caplog forward on|off|<seconds>");
}

// ------------------------------------------------------------ set syslog.host

TEST(CaplogSinkHost, ANameAtTheLimitFits) {
    const std::string host(63, 'h');
    char out[160] = "untouched";
    EXPECT_TRUE(offband::caplogCheckSinkHost(host.c_str(), 63, out, sizeof(out)));
    EXPECT_STREQ(out, "untouched");
}

TEST(CaplogSinkHost, OneOverIsRefusedNotCut) {
    // Before #1194 both roles cut the name to 63 and replied OK.
    const std::string host(64, 'h');
    char out[160] = "";
    EXPECT_FALSE(offband::caplogCheckSinkHost(host.c_str(), 63, out, sizeof(out)));
    EXPECT_STREQ(out, "Error, host max 63 chars");
}

TEST(CaplogSinkHost, EmptyAndNullClearTheSink) {
    char out[160] = "";
    EXPECT_TRUE(offband::caplogCheckSinkHost("", 63, out, sizeof(out)));
    EXPECT_TRUE(offband::caplogCheckSinkHost(nullptr, 63, out, sizeof(out)));
}

// ------------------------------------------------------------ caplog start|stop (#1194, option A)
// The observer answers with the repeater's words (CommonCLI's caplog start/stop).

TEST(CaplogCapture, StartLevels) {
    uint8_t lvl = 99;
    EXPECT_TRUE(offband::caplogParseStartLevel("", &lvl));
    EXPECT_EQ(lvl, (uint8_t)MLOG_DEBUG) << "bare start captures at debug, like the repeater";
    EXPECT_TRUE(offband::caplogParseStartLevel(nullptr, &lvl));
    EXPECT_EQ(lvl, (uint8_t)MLOG_DEBUG);
    EXPECT_TRUE(offband::caplogParseStartLevel("packet", &lvl));
    EXPECT_EQ(lvl, (uint8_t)MLOG_PACKET);
    EXPECT_TRUE(offband::caplogParseStartLevel(" error ", &lvl));
    EXPECT_EQ(lvl, (uint8_t)MLOG_ERROR);
}

TEST(CaplogCapture, NotALevelIsRefused) {
    uint8_t lvl = 0;
    for (const char* s : {"verbose", "debug please", "packetpacketpacketpacket", "DEBUG"}) {
        EXPECT_FALSE(offband::caplogParseStartLevel(s, &lvl)) << s;
    }
}

TEST(CaplogCapture, RepliesAreTheRepeatersWords) {
    char out[160];
    offband::caplogCaptureReply(out, sizeof(out), true, true, MLOG_DEBUG);
    EXPECT_STREQ(out, "caplog on (level debug)");
    offband::caplogCaptureReply(out, sizeof(out), true, true, MLOG_PACKET);
    EXPECT_STREQ(out, "caplog on (level packet)");
    offband::caplogCaptureReply(out, sizeof(out), true, false, 0);
    EXPECT_STREQ(out, "caplog off");
    offband::caplogCaptureReply(out, sizeof(out), false, true, 0);
    EXPECT_STREQ(out, "ERR: level = boot|error|debug|packet");
}

// ------------------------------------------------------------ IP or hostname (#1061)

TEST(CaplogSinkHost, DottedQuadsAreIpLiterals) {
    for (const char* s : {"10.0.0.5", "192.0.2.10", "0.0.0.0", "255.255.255.255"}) {
        EXPECT_TRUE(offband::caplogHostIsIpv4(s)) << s;
    }
}

TEST(CaplogSinkHost, EverythingElseIsAHostname) {
    for (const char* s : {"sink.example.net", "pi5.local", "256.1.1.1", "1.2.3", "1.2.3.4.5",
                          "1..2.3", ".1.2.3", "1.2.3.", "1.2.3.4 ", "1.2.3.4a", "1234.1.1.1", ""}) {
        EXPECT_FALSE(offband::caplogHostIsIpv4(s)) << '"' << s << '"';
    }
    EXPECT_FALSE(offband::caplogHostIsIpv4(nullptr));
}

// ------------------------------------------------------------ status

TEST(CaplogStatus, WithoutTheForwardBuildTheLineIsUnchanged) {
    EXPECT_EQ(status(nullptr), "caplog: on level=debug used=812/16384");
    EXPECT_EQ(status(nullptr, false), "caplog: off level=debug used=812/16384");
}

TEST(CaplogStatus, OffWithNoSink) {
    const auto s = st(CaplogForwardMode::Off, "", false);
    EXPECT_EQ(status(&s), "caplog: on level=debug used=812/16384 fwd=off sink=none sent=0 lost=0")
        << "no link=down while off";
}

TEST(CaplogStatus, UntilOffStreaming) {
    const auto s = st(CaplogForwardMode::UntilOff, "sink.example.net", true, 0, 40, 0);
    EXPECT_EQ(status(&s),
              "caplog: on level=debug used=812/16384 fwd=until-off sink=sink.example.net:514 sent=40 lost=0");
}

TEST(CaplogStatus, BoundedWithTheLinkDown) {
    const auto s = st(CaplogForwardMode::Bounded, "sink.example.net", false, 287, 12, 5);
    EXPECT_EQ(status(&s),
              "caplog: on level=debug used=812/16384 fwd=287s link=down sink=sink.example.net:514 sent=12 lost=5");
}

TEST(CaplogStatus, ALongSinkNameCannotHideLinkDown) {
    // The worst case overflows the 160-byte reply; what gets cut is the tail
    // of the counters, never the link state.
    const std::string host(63, 'h');
    const auto s = st(CaplogForwardMode::UntilOff, host.c_str(), false, 0, 4294967295u, 4294967295u);
    char out[160];
    offband::caplogStatusLine(out, sizeof(out), false, "packet", 16384, 16384, &s);
    EXPECT_EQ(std::strlen(out), 159u);
    EXPECT_EQ(std::string(out).rfind(
                  "caplog: off level=packet used=16384/16384 fwd=until-off link=down sink=hhh", 0), 0u);
}

TEST(CaplogStatus, StatusOfReadsTheForwarder) {
    NullSink sink;
    CaplogForward fwd("n", nothingToRead, sink);
    fwd.armFor(300, 1000);
    const CaplogForwardStatus s = offband::caplogForwardStatusOf(fwd, 1500, "h", 514, true);
    EXPECT_EQ(s.mode, CaplogForwardMode::Bounded);
    EXPECT_EQ(s.seconds_left, 300u);
    EXPECT_STREQ(s.host, "h");
    EXPECT_TRUE(s.link_up);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
