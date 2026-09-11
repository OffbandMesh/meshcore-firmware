#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "helpers/wifi_observer/BrokerEventLog.h"

using offband::BrokerEvent;
using offband::BrokerEventKind;
using offband::BrokerEventQueue;
using offband::BrokerHoldReason;
using offband::brokerEventExtractHost;
using offband::brokerEventLevel;
using offband::formatBrokerEvent;
using offband::makeBrokerEvent;

namespace {

// mesh_log_line() formats into a MLOG_LINE_MAX (240) buffer after its own
// timestamp prefix, and the drain appends a newline. Keep every formatted record
// comfortably inside what is left.
constexpr size_t kLineBudget = 200;

std::string fmt(const BrokerEvent& e, const char* host = nullptr, const char* transport = nullptr) {
    char buf[256];
    size_t n = formatBrokerEvent(e, host, transport, buf, sizeof(buf));
    return std::string(buf, n);
}

BrokerEvent ev(BrokerEventKind k, uint8_t slot = 0) {
    return makeBrokerEvent(k, slot, 1000);
}

}  // namespace

// ---------------------------------------------------------------- queue

TEST(BrokerEventQueue, PreservesFifoOrder) {
    BrokerEventQueue<4> q;
    for (uint8_t s = 0; s < 3; ++s) EXPECT_TRUE(q.push(ev(BrokerEventKind::MqttConnected, s)));
    BrokerEvent out;
    for (uint8_t s = 0; s < 3; ++s) {
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out.slot, s);
    }
    EXPECT_FALSE(q.pop(out)) << "an empty queue must report empty";
}

TEST(BrokerEventQueue, WrapsAroundWithoutLosingOrder) {
    BrokerEventQueue<4> q;
    BrokerEvent out;
    // Advance the head past the midpoint, then fill across the physical end.
    for (uint8_t s = 0; s < 3; ++s) q.push(ev(BrokerEventKind::MqttConnected, s));
    for (int i = 0; i < 3; ++i) q.pop(out);
    for (uint8_t s = 10; s < 14; ++s) EXPECT_TRUE(q.push(ev(BrokerEventKind::MqttConnected, s)));
    for (uint8_t s = 10; s < 14; ++s) {
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out.slot, s) << "order must survive the wrap";
    }
}

TEST(BrokerEventQueue, FullQueueDropsTheNewestAndCountsIt) {
    BrokerEventQueue<2> q;
    EXPECT_TRUE(q.push(ev(BrokerEventKind::MqttConnected, 1)));
    EXPECT_TRUE(q.push(ev(BrokerEventKind::MqttConnected, 2)));
    EXPECT_FALSE(q.push(ev(BrokerEventKind::MqttConnected, 3))) << "a full queue must refuse";
    EXPECT_FALSE(q.push(ev(BrokerEventKind::MqttConnected, 4)));
    EXPECT_EQ(q.takeDropped(), 2u) << "every refused record must be counted";
    EXPECT_EQ(q.takeDropped(), 0u) << "taking the count must reset it";

    BrokerEvent out;
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out.slot, 1) << "what was already queued must be kept, not overwritten";
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out.slot, 2);
}

TEST(BrokerEventQueue, ProductionDepthAbsorbsAWholeOutageBurst) {
    // Six enabled slots each emitting a disconnect and a backoff at once.
    auto& q = offband::brokerEventQueue();
    BrokerEvent out;
    while (q.pop(out)) {}
    q.takeDropped();
    for (uint8_t s = 0; s < 6; ++s) {
        EXPECT_TRUE(q.push(ev(BrokerEventKind::MqttDisconnected, s)));
        EXPECT_TRUE(q.push(ev(BrokerEventKind::Backoff, s)));
    }
    EXPECT_EQ(q.takeDropped(), 0u) << "a whole-outage burst must not drop anything";
    EXPECT_EQ(offband::kBrokerEventQueueDepth, 32u);
    while (q.pop(out)) {}
}

TEST(BrokerEvent, RecordIsFixedSizeAndZeroInitialized) {
    EXPECT_EQ(sizeof(BrokerEvent), 24u);
    BrokerEvent e = makeBrokerEvent(BrokerEventKind::Backoff, 5, 1234);
    EXPECT_EQ(e.slot, 5);
    EXPECT_EQ(e.at_ms, 1234u);
    EXPECT_EQ(e.w0, 0u);
    EXPECT_EQ(e.n0, 0);
    EXPECT_EQ(e.b0, 0);
}

// ---------------------------------------------------------------- formatting

TEST(BrokerEventFormat, MqttLinesUseTheMqttPrefix) {
    EXPECT_EQ(fmt(ev(BrokerEventKind::MqttConnecting, 0), "mqtt1.okimesh.org", "tcp"),
              "[mqtt] s0 connecting host=mqtt1.okimesh.org tcp");
    EXPECT_EQ(fmt(ev(BrokerEventKind::MqttConnected, 2)), "[mqtt] s2 up");
}

TEST(BrokerEventFormat, DisconnectCarriesTheFullErrorDetail) {
    BrokerEvent e = ev(BrokerEventKind::MqttDisconnected, 0);
    e.cls = 1;             // tcp
    e.b0  = 1;             // esp-mqtt error_type
    e.n0  = 113;           // EHOSTUNREACH
    e.w0  = 0x8006;        // tls_esp_err
    e.n1  = 0x2700;        // tls_stack_err, stored positive
    e.w1  = 0x00000008;    // cert_flags
    e.b1  = 2;             // connack
    EXPECT_EQ(fmt(e),
              "[mqtt] s0 down class=tcp type=1 sock_errno=113 tls_esp_err=0x8006 "
              "tls_stack_err=-0x2700 cert_flags=0x00000008 connack=2");
}

TEST(BrokerEventFormat, ErrorAndDisconnectAreDistinguished) {
    EXPECT_NE(fmt(ev(BrokerEventKind::MqttError)).find("s0 error "), std::string::npos);
    EXPECT_NE(fmt(ev(BrokerEventKind::MqttDisconnected)).find("s0 down "), std::string::npos);
}

TEST(BrokerEventFormat, BrokerLinesUseTheBrokerPrefix) {
    BrokerEvent b = ev(BrokerEventKind::Backoff, 0);
    b.b0 = 3;
    b.n0 = 60;
    EXPECT_EQ(fmt(b), "[broker] s0 backoff penalty=3 next=60s");

    BrokerEvent f = ev(BrokerEventKind::Failed, 0);
    f.b0 = 8;
    EXPECT_EQ(fmt(f), "[broker] s0 FAILED penalty=8 -- gave up; needs operator enable or reboot");

    BrokerEvent r = ev(BrokerEventKind::Resurrected, 0);
    r.b0 = 8;
    EXPECT_EQ(fmt(r), "[broker] s0 resurrected by operator (was failed, penalty=8)");

    EXPECT_EQ(fmt(ev(BrokerEventKind::OperatorDisabled, 1)), "[broker] s1 disabled by operator");
    EXPECT_EQ(fmt(ev(BrokerEventKind::OperatorEnabled, 1)), "[broker] s1 enabled by operator");
    EXPECT_EQ(fmt(ev(BrokerEventKind::PenaltyCleared, 1)), "[broker] s1 penalty cleared -- rehabilitated");
}

TEST(BrokerEventFormat, DeferralsNameTheirReason) {
    BrokerEvent h = ev(BrokerEventKind::Held, 3);
    h.b0 = static_cast<uint8_t>(BrokerHoldReason::NoClock);
    EXPECT_EQ(fmt(h), "[broker] s3 held reason=no-clock");

    BrokerEvent r = ev(BrokerEventKind::Released, 3);
    r.b0 = static_cast<uint8_t>(BrokerHoldReason::TlsBudget);
    EXPECT_EQ(fmt(r), "[broker] s3 released reason=tls-budget");
}

TEST(BrokerEventFormat, RehabShowsProgressTowardTheBar) {
    BrokerEvent e = ev(BrokerEventKind::Rehab, 0);
    e.n0 = 2;
    e.b0 = 3;
    e.w0 = 4;
    EXPECT_EQ(fmt(e), "[broker] s0 rehab streak=2/3 penalty=4");
}

TEST(BrokerEventFormat, DroppedLineReportsTheCount) {
    BrokerEvent e = ev(BrokerEventKind::Dropped, 0);
    e.n0 = 7;
    EXPECT_EQ(fmt(e), "[broker] event queue full -- 7 events dropped");
}

TEST(BrokerEventFormat, ExistingRotationWordingIsKept) {
    BrokerEvent o = ev(BrokerEventKind::RotateOut, 1);
    o.n0 = 61;
    o.w0 = 98304;
    o.n1 = 60;
    EXPECT_EQ(fmt(o), "[rot] ROTATE-OUT s1 age=61s heapBefore=98304 -> cooldown 60s");

    BrokerEvent p = ev(BrokerEventKind::PostShutdown, 1);
    p.w0 = 102400;
    EXPECT_EQ(fmt(p), "[rot] post-shutdown s1 heapAfter=102400");

    EXPECT_EQ(fmt(ev(BrokerEventKind::RotateIn, 1)), "[rot] ROTATE-IN s1");
}

TEST(BrokerEventFormat, UnknownKindFormatsNothing) {
    BrokerEvent e = ev(BrokerEventKind::MqttConnected, 0);
    e.kind = 250;
    char buf[64];
    EXPECT_EQ(formatBrokerEvent(e, nullptr, nullptr, buf, sizeof(buf)), 0u);
    EXPECT_STREQ(buf, "");
}

TEST(BrokerEventFormat, WorstCaseLinesFitTheBudget) {
    BrokerEvent e = ev(BrokerEventKind::MqttDisconnected, 255);
    e.cls = 250;
    e.b0  = 255;
    e.n0  = INT32_MIN;
    e.w0  = UINT32_MAX;
    e.n1  = -1;
    e.w1  = UINT32_MAX;
    e.b1  = INT8_MIN;
    EXPECT_LT(fmt(e).size(), kLineBudget);

    std::string host(64, 'h');
    EXPECT_LT(fmt(ev(BrokerEventKind::MqttConnecting, 255), host.c_str(), "wss").size(), kLineBudget);
}

TEST(BrokerEventFormat, TruncatesSafelyIntoASmallBuffer) {
    BrokerEvent e = ev(BrokerEventKind::Failed, 0);
    char buf[10];
    size_t n = formatBrokerEvent(e, nullptr, nullptr, buf, sizeof(buf));
    EXPECT_EQ(n, sizeof(buf) - 1);
    EXPECT_EQ(buf[sizeof(buf) - 1], '\0');
}

// ---------------------------------------------------------------- levels

TEST(BrokerEventLevel, FailuresAreErrorsAndTransitionsAreDebug) {
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::MqttDisconnected)), MLOG_ERROR);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::MqttError)), MLOG_ERROR);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::Failed)), MLOG_ERROR);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::ReconcileFailed)), MLOG_ERROR);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::Dropped)), MLOG_ERROR);

    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::MqttConnecting)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::MqttConnected)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::Backoff)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::RotateIn)), MLOG_DEBUG);
}

// ---------------------------------------------------------------- host extraction

TEST(BrokerEventHost, ExtractsTheHostFromEachUrlShape) {
    char h[64];
    brokerEventExtractHost("mqtt://mqtt1.okimesh.org:1883", h, sizeof(h));
    EXPECT_STREQ(h, "mqtt1.okimesh.org");
    brokerEventExtractHost("wss://mqtt2.okimesh.org:9002/mqtt", h, sizeof(h));
    EXPECT_STREQ(h, "mqtt2.okimesh.org");
    brokerEventExtractHost("wss://mqtt.meshmapper.net", h, sizeof(h));
    EXPECT_STREQ(h, "mqtt.meshmapper.net");
    brokerEventExtractHost("bare.example.net", h, sizeof(h));
    EXPECT_STREQ(h, "bare.example.net");
}

TEST(BrokerEventHost, NeverEmitsCredentialsTypedIntoTheUrl) {
    char h[64];
    brokerEventExtractHost("wss://myuser:mypass@mqtt.example.com:443/mqtt", h, sizeof(h));
    EXPECT_STREQ(h, "mqtt.example.com");
    brokerEventExtractHost("mqtt://onlyuser@broker.example.net", h, sizeof(h));
    EXPECT_STREQ(h, "broker.example.net");
    // A password may itself contain '@'; the last one in the authority wins.
    brokerEventExtractHost("wss://u:p@ss@host.example.org/x", h, sizeof(h));
    EXPECT_STREQ(h, "host.example.org");
}

TEST(BrokerEventHost, AnAtSignInThePathIsNotUserinfo) {
    char h[64];
    brokerEventExtractHost("wss://host.example.org/path@notauth", h, sizeof(h));
    EXPECT_STREQ(h, "host.example.org");
}

TEST(BrokerEventHost, HandlesEmptyNullAndTinyBuffers) {
    char h[8];
    brokerEventExtractHost("", h, sizeof(h));
    EXPECT_STREQ(h, "");
    brokerEventExtractHost(nullptr, h, sizeof(h));
    EXPECT_STREQ(h, "");
    brokerEventExtractHost("wss://a-very-long-hostname.example", h, sizeof(h));
    EXPECT_EQ(std::strlen(h), sizeof(h) - 1) << "must truncate, never overrun";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
