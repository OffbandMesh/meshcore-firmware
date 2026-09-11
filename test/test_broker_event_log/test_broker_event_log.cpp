#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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
              "[mqtt] s0 connecting host=mqtt1.okimesh.org tcp by=pool");
    EXPECT_EQ(fmt(ev(BrokerEventKind::MqttConnected, 2)), "[mqtt] s2 up");
}

TEST(BrokerEventFormat, ConnectingSaysWhoStartedTheAttempt) {
    EXPECT_EQ(fmt(offband::makeMqttConnectingEvent(1, 1000, false), "mqtt2.okimesh.org", "wss"),
              "[mqtt] s1 connecting host=mqtt2.okimesh.org wss by=pool");
    EXPECT_EQ(fmt(offband::makeMqttConnectingEvent(1, 1000, true), "mqtt2.okimesh.org", "wss"),
              "[mqtt] s1 connecting host=mqtt2.okimesh.org wss by=auto-reconnect");
}

TEST(BrokerEventFormat, ErrorCarriesTheFullErrorDetail) {
    BrokerEvent e = ev(BrokerEventKind::MqttError, 0);
    e.cls = 1;             // tcp
    e.b0  = 1;             // esp-mqtt error_type
    e.n0  = 113;           // EHOSTUNREACH
    e.w0  = 0x8006;        // tls_esp_err
    e.n1  = 0x2700;        // tls_stack_err, stored positive
    e.w1  = 0x00000008;    // cert_flags
    e.b1  = 2;             // connack
    EXPECT_EQ(fmt(e),
              "[mqtt] s0 error class=tcp type=1 sock_errno=113 tls_esp_err=0x8006 "
              "tls_stack_err=-0x2700 cert_flags=0x00000008 connack=2");
}

TEST(BrokerEventFormat, DisconnectSaysWhatEndedAndWhatErrorCameWithIt) {
    // A session that was up, dropped with a TLS error event just before it.
    EXPECT_EQ(fmt(offband::makeMqttDisconnectedEvent(0, 1000, 3, true, 312400)),
              "[mqtt] s0 down phase=session up=312s error=tls");
    // A session that dropped without any error event (e.g. a missed keepalive).
    EXPECT_EQ(fmt(offband::makeMqttDisconnectedEvent(0, 1000, 0, true, 5000)),
              "[mqtt] s0 down phase=session up=5s error=none");
    // A connect that never came up; the up time passed in is meaningless and ignored.
    EXPECT_EQ(fmt(offband::makeMqttDisconnectedEvent(1, 1000, 2, false, 999999)),
              "[mqtt] s1 down phase=connect error=auth");
}

TEST(BrokerEventFormat, ErrorAndDisconnectAreDistinguished) {
    EXPECT_NE(fmt(ev(BrokerEventKind::MqttError)).find("s0 error "), std::string::npos);
    EXPECT_NE(fmt(ev(BrokerEventKind::MqttDisconnected)).find("s0 down "), std::string::npos);
}

TEST(BrokerEventFormat, AMissingErrorHandleIsSaidSoNotZeroed) {
    const std::string missing = fmt(offband::makeMqttErrorNoHandleEvent(1, 1000, 1));
    EXPECT_EQ(missing, "[mqtt] s1 error class=tcp (no error_handle)");
    const std::string zeroed = fmt(offband::makeMqttErrorEvent(1, 1000, 1, 0, 0, 0, 0, 0, 0));
    EXPECT_NE(missing, zeroed) << "no handle must never read like a handle of zeros";
}

TEST(BrokerEventFormat, StartFailureCarriesTheEspError) {
    EXPECT_EQ(fmt(offband::makeMqttStartFailedEvent(2, 1000, 0x103)),
              "[mqtt] s2 start failed esp_err=0x103");
    // ESP_FAIL is -1; ESP-IDF itself prints it as 0xffffffff.
    EXPECT_EQ(fmt(offband::makeMqttStartFailedEvent(2, 1000, -1)),
              "[mqtt] s2 start failed esp_err=0xFFFFFFFF");
}

// ---------------------------------------------------------------- builders

TEST(BrokerEventBuild, ConnectBuildersSetKindSlotAndTime) {
    BrokerEvent c = offband::makeMqttConnectingEvent(3, 4321, false);
    EXPECT_EQ(c.kind, static_cast<uint8_t>(BrokerEventKind::MqttConnecting));
    EXPECT_EQ(c.slot, 3);
    EXPECT_EQ(c.at_ms, 4321u);
    EXPECT_EQ(c.flags, 0) << "the pool's own start carries no flag";
    EXPECT_EQ(offband::makeMqttConnectingEvent(3, 4321, true).flags, offband::kBrokerEventAutoReconnect);

    BrokerEvent u = offband::makeMqttConnectedEvent(3, 4322);
    EXPECT_EQ(u.kind, static_cast<uint8_t>(BrokerEventKind::MqttConnected));
    EXPECT_EQ(u.slot, 3);
    EXPECT_EQ(u.at_ms, 4322u);
}

TEST(BrokerEventBuild, ErrorBuilderMapsEveryHandleField) {
    BrokerEvent e = offband::makeMqttErrorEvent(4, 777, 3, 2, 113, 0x8006, -0x2700, 0x8, 5);
    EXPECT_EQ(e.kind, static_cast<uint8_t>(BrokerEventKind::MqttError));
    EXPECT_EQ(e.slot, 4);
    EXPECT_EQ(e.at_ms, 777u);
    EXPECT_EQ(e.cls, 3);
    EXPECT_EQ(e.b0, 2) << "error_type";
    EXPECT_EQ(e.n0, 113) << "sock_errno";
    EXPECT_EQ(e.w0, 0x8006u) << "tls_esp_err";
    EXPECT_EQ(e.n1, 0x2700) << "the mbedTLS stack error is stored negated";
    EXPECT_EQ(e.w1, 0x8u) << "cert_flags";
    EXPECT_EQ(e.b1, 5) << "connack";
    EXPECT_EQ(e.flags, 0) << "a real handle must not carry the no-handle flag";
}

TEST(BrokerEventBuild, DisconnectBuilderCarriesUptimeOnlyForASession) {
    BrokerEvent up = offband::makeMqttDisconnectedEvent(2, 900, 1, true, 61999);
    EXPECT_EQ(up.kind, static_cast<uint8_t>(BrokerEventKind::MqttDisconnected));
    EXPECT_EQ(up.cls, 1);
    EXPECT_EQ(up.flags, offband::kBrokerEventWasUp);
    EXPECT_EQ(up.n0, 61) << "whole seconds, truncated";

    BrokerEvent never = offband::makeMqttDisconnectedEvent(2, 900, 1, false, 61999);
    EXPECT_EQ(never.flags, 0);
    EXPECT_EQ(never.n0, 0) << "a connect that never came up has no uptime";

    // Up time is a millis() difference; the longest one must survive the conversion.
    BrokerEvent longest = offband::makeMqttDisconnectedEvent(2, 900, 0, true, UINT32_MAX);
    EXPECT_EQ(longest.n0, static_cast<int32_t>(UINT32_MAX / 1000u));
}

TEST(BrokerEventBuild, NoHandleBuilderSetsTheFlagAndTheClass) {
    BrokerEvent e = offband::makeMqttErrorNoHandleEvent(1, 50, 4);
    EXPECT_EQ(e.kind, static_cast<uint8_t>(BrokerEventKind::MqttError));
    EXPECT_EQ(e.cls, 4);
    EXPECT_EQ(e.flags, offband::kBrokerEventNoErrorHandle);
}

namespace {

// The whitespace-separated key=value tokens of a line.
std::vector<std::string> keyValues(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        size_t j = line.find(' ', i);
        if (j == std::string::npos) j = line.size();
        std::string tok = line.substr(i, j - i);
        if (tok.find('=') != std::string::npos) out.push_back(tok);
        i = j + 1;
    }
    return out;
}

}  // namespace

// The [mqtt-err] print this replaces is the one diagnostic the subsystem already
// had. Moving it into the ring must not lose any of it: every key=value it
// printed must come out of the new line with the same value.
TEST(BrokerEventFormat, ErrorLineLosesNothingTheRetiredPrintCarried) {
    struct Case { unsigned slot; int type, sock_errno; uint32_t tls_esp_err; int tls_stack_err;
                  uint32_t cert_flags; int connack, cls; };
    const Case cases[] = {
        {0, 1, 113, 0x8006, -0x2700, 0x00000008, 0, 3},     // a TLS handshake failure
        {5, 0, 0, 0, 0, 0, 0, 0},                           // every field zero
        {3, 3, 104, 0xFFFFFFFF, -0x7FFF, 0xFFFFFFFF, 5, 4}, // the top of each documented range
        {1, 2, 0, 0, 0, 0, 2, 2},                           // a CONNACK refusal (#506)
    };
    for (const Case& c : cases) {
        char retired[200];
        std::snprintf(retired, sizeof(retired),
                      "[mqtt-err] s%u type=%d sock_errno=%d tls_esp_err=0x%X "
                      "tls_stack_err=-0x%04X cert_flags=0x%08X connack=%d -> class=%d",
                      c.slot, c.type, c.sock_errno, (unsigned)c.tls_esp_err,
                      (unsigned)(-c.tls_stack_err), (unsigned)c.cert_flags, c.connack, c.cls);
        const std::string now = fmt(offband::makeMqttErrorEvent(
            static_cast<uint8_t>(c.slot), 1000, static_cast<uint8_t>(c.cls), c.type, c.sock_errno,
            c.tls_esp_err, c.tls_stack_err, c.cert_flags, c.connack));
        const std::vector<std::string> have = keyValues(now);

        for (const std::string& kv : keyValues(retired)) {
            if (kv.rfind("class=", 0) == 0) continue;   // numeric then, named now; checked below
            EXPECT_NE(std::find(have.begin(), have.end(), kv), have.end())
                << kv << " is missing from: " << now;
        }
        const std::string cls = std::string("class=") + offband::brokerEventErrorClassName(c.cls);
        EXPECT_NE(std::find(have.begin(), have.end(), cls), have.end()) << now;
        EXPECT_EQ(now.rfind("[mqtt] s" + std::to_string(c.slot) + " error ", 0), 0u) << now;
    }
}

TEST(BrokerEventFormat, BrokerLinesUseTheBrokerPrefix) {
    BrokerEvent b = ev(BrokerEventKind::Backoff, 0);
    b.b0 = 3;
    b.n0 = 60;
    EXPECT_EQ(fmt(b), "[broker] s0 backoff penalty=3 interval=60s");

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
    BrokerEvent e = ev(BrokerEventKind::MqttError, 255);
    e.cls = 250;
    e.b0  = 255;
    e.n0  = INT32_MIN;
    e.w0  = UINT32_MAX;
    e.n1  = -1;
    e.w1  = UINT32_MAX;
    e.b1  = INT8_MIN;
    EXPECT_LT(fmt(e).size(), kLineBudget);

    BrokerEvent f = offband::makeMqttStartFailedEvent(255, 1000, INT32_MIN);
    EXPECT_LT(fmt(f).size(), kLineBudget);

    BrokerEvent d = offband::makeMqttDisconnectedEvent(255, 1000, 250, true, UINT32_MAX);
    EXPECT_LT(fmt(d).size(), kLineBudget);

    std::string host(64, 'h');
    EXPECT_LT(fmt(offband::makeMqttConnectingEvent(255, 1000, true), host.c_str(), "wss").size(),
              kLineBudget);
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
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::MqttStartFailed)), MLOG_ERROR);
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

// ---------------------------------------------------------------- [broker] (#1135)

TEST(BrokerPolicyBuild, BackoffCarriesPenaltyAndIntervalInSeconds) {
    BrokerEvent e = offband::makeBackoffEvent(2, 1000, 3, 60000);
    EXPECT_EQ(e.kind, static_cast<uint8_t>(BrokerEventKind::Backoff));
    EXPECT_EQ(e.b0, 3);
    EXPECT_EQ(e.n0, 60);
    EXPECT_EQ(fmt(e), "[broker] s2 backoff penalty=3 interval=60s");
}

TEST(BrokerPolicyBuild, PenaltyClampsInsteadOfWrapping) {
    EXPECT_EQ(offband::makeBackoffEvent(0, 0, 300, 5000).b0, 255);
    EXPECT_EQ(offband::makeFailedEvent(0, 0, 0xFFFF).b0, 255);
    EXPECT_EQ(offband::makeResurrectedEvent(0, 0, 256).b0, 255);
    EXPECT_EQ(offband::makeFailedEvent(0, 0, 8).b0, 8);
}

TEST(BrokerPolicyBuild, TerminalAndResurrectionCarryThePenalty) {
    EXPECT_EQ(fmt(offband::makeFailedEvent(1, 0, 8)),
              "[broker] s1 FAILED penalty=8 -- gave up; needs operator enable or reboot");
    EXPECT_EQ(fmt(offband::makeResurrectedEvent(1, 0, 9)),
              "[broker] s1 resurrected by operator (was failed, penalty=9)");
}

TEST(BrokerPolicyBuild, HoldAndReleaseNameTheReason) {
    EXPECT_EQ(fmt(offband::makeHoldEvent(BrokerEventKind::Held, 4, 0, BrokerHoldReason::NoHeap)),
              "[broker] s4 held reason=no-heap");
    EXPECT_EQ(fmt(offband::makeHoldEvent(BrokerEventKind::Released, 4, 0, BrokerHoldReason::NoClock)),
              "[broker] s4 released reason=no-clock");
}

TEST(BrokerPolicyBuild, RehabShowsStreakAgainstTheBar) {
    EXPECT_EQ(fmt(offband::makeRehabEvent(0, 0, 2, 3, 5)), "[broker] s0 rehab streak=2/3 penalty=5");
}

namespace {

std::string operatorLine(bool was_cfg, bool was_en, bool now_cfg, bool now_en) {
    BrokerEvent e;
    if (!offband::makeOperatorReloadEvent(3, 0, was_cfg, was_en, now_cfg, now_en, e)) return "(none)";
    return fmt(e);
}

}  // namespace

TEST(BrokerPolicyBuild, OperatorReloadSaysWhichChangeItApplied) {
    EXPECT_EQ(operatorLine(true, false, true, true),   "[broker] s3 enabled by operator");
    EXPECT_EQ(operatorLine(true, true, true, false),   "[broker] s3 disabled by operator");
    EXPECT_EQ(operatorLine(true, true, true, true),    "[broker] s3 reloaded by operator (enabled)");
    EXPECT_EQ(operatorLine(true, false, true, false),  "[broker] s3 reloaded by operator (disabled)");
    EXPECT_EQ(operatorLine(true, true, false, false),  "[broker] s3 cleared by operator");
    EXPECT_EQ(operatorLine(true, false, false, false), "[broker] s3 cleared by operator");
    // A slot configured for the first time.
    EXPECT_EQ(operatorLine(false, false, true, true),  "[broker] s3 enabled by operator");
    EXPECT_EQ(operatorLine(false, false, true, false), "[broker] s3 reloaded by operator (disabled)");
    // Clearing a slot that never held a config changes nothing.
    EXPECT_EQ(operatorLine(false, false, false, false), "(none)");
}

TEST(BrokerPolicyFormat, DiscardsSayWhyTheConnectWasThrownAway) {
    EXPECT_EQ(fmt(offband::makeConnectDiscardedEvent(2, 0, offband::BrokerDiscardReason::SlotFailed)),
              "[broker] s2 connect discarded -- slot is failed");
    EXPECT_EQ(fmt(offband::makeConnectDiscardedEvent(2, 0, offband::BrokerDiscardReason::ClientReleased)),
              "[broker] s2 connect discarded -- client already released");
}

TEST(BrokerPolicyFormat, CredentialLinesSayWhatTheFailureCost) {
    EXPECT_EQ(fmt(offband::makeCredentialFailedEvent(1, 0, offband::BrokerCredentialPhase::BeforeConnect)),
              "[broker] s1 credential refresh failed -- attempt abandoned");
    EXPECT_EQ(fmt(offband::makeCredentialFailedEvent(1, 0, offband::BrokerCredentialPhase::MidSession)),
              "[broker] s1 credential refresh failed -- session keeps its current token");
    EXPECT_EQ(fmt(offband::makeCredentialRefreshedEvent(1, 0, 0)), "[broker] s1 credential refreshed");
    EXPECT_EQ(fmt(offband::makeCredentialRefreshedEvent(1, 0, 42)),
              "[broker] s1 credential refreshed after 42 failed tries");
}

TEST(BrokerPolicyFormat, SetupLinesNameTheReasonAndCountTheRecovery) {
    EXPECT_EQ(fmt(offband::makeSetupFailedEvent(5, 0, offband::BrokerSetupFailure::AuthConfig)),
              "[broker] s5 setup failed reason=auth-config");
    EXPECT_EQ(fmt(offband::makeSetupFailedEvent(5, 0, offband::BrokerSetupFailure::AuthApply)),
              "[broker] s5 setup failed reason=auth-apply");
    EXPECT_EQ(fmt(offband::makeSetupFailedEvent(5, 0, offband::BrokerSetupFailure::ClientInit)),
              "[broker] s5 setup failed reason=client-init");
    EXPECT_EQ(fmt(offband::makeSetupRecoveredEvent(5, 0, 7)), "[broker] s5 setup ok after 7 failed tries");
    EXPECT_EQ(fmt(offband::makeBrokerEvent(BrokerEventKind::NoClient, 5, 0)),
              "[broker] s5 cannot connect: no client");
}

TEST(BrokerPolicyFormat, RepeatCountsSaturateInsteadOfGoingNegative) {
    EXPECT_EQ(offband::makeCredentialRefreshedEvent(0, 0, UINT32_MAX).n0, INT32_MAX);
    EXPECT_EQ(offband::makeSetupRecoveredEvent(0, 0, UINT32_MAX).n0, INT32_MAX);
}

TEST(BrokerHoldTracker, RecordsAHoldOnceAndItsReasonChanges) {
    offband::HoldTracker h;
    EXPECT_TRUE(h.enter(BrokerHoldReason::NoClock)) << "a new hold is recorded";
    EXPECT_FALSE(h.enter(BrokerHoldReason::NoClock)) << "the same hold re-driven next pass is not";
    EXPECT_FALSE(h.enter(BrokerHoldReason::NoClock));
    EXPECT_TRUE(h.enter(BrokerHoldReason::TlsBudget)) << "a change of reason is recorded";
    EXPECT_EQ(h.release(), static_cast<uint8_t>(BrokerHoldReason::TlsBudget))
        << "release reports the hold that ended";
    EXPECT_EQ(h.release(), 0) << "nothing to release twice";
    EXPECT_TRUE(h.enter(BrokerHoldReason::TlsBudget)) << "a hold after a release is new again";
    h.forget();
    EXPECT_EQ(h.release(), 0) << "a forgotten hold has no release line";
}

TEST(BrokerRepeatedFailure, RecordsTheStartAndCountsEveryRepeat) {
    offband::RepeatedFailure f;
    uint32_t tries = 99;
    EXPECT_FALSE(f.clear(tries)) << "no recovery without a failure";
    EXPECT_EQ(tries, 99u) << "and the count is left alone";

    EXPECT_TRUE(f.fail(2)) << "the first failure is recorded";
    EXPECT_FALSE(f.fail(2));
    EXPECT_FALSE(f.fail(2));
    EXPECT_FALSE(f.fail(2));
    ASSERT_TRUE(f.clear(tries)) << "the recovery is recorded";
    EXPECT_EQ(tries, 4u) << "and counts every failed try, the recorded one included";
    EXPECT_FALSE(f.clear(tries)) << "a second success is not another recovery";

    EXPECT_TRUE(f.fail(2)) << "a failure after recovery is recorded afresh";
    EXPECT_TRUE(f.fail(3)) << "so is a change of reason";
    ASSERT_TRUE(f.clear(tries));
    EXPECT_EQ(tries, 1u) << "a change of reason restarts the count";

    EXPECT_TRUE(f.fail(1));
    f.forget();
    EXPECT_FALSE(f.clear(tries)) << "a forgotten failure has no recovery line";
    EXPECT_TRUE(f.fail(1)) << "and the next failure is recorded";
}

TEST(BrokerPolicyLevel, NewFailuresAreErrorsAndProgressIsDebug) {
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::ConnectDiscarded)), MLOG_ERROR);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::CredentialFailed)), MLOG_ERROR);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::SetupFailed)), MLOG_ERROR);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::NoClient)), MLOG_ERROR);

    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::OperatorReloaded)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::OperatorCleared)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::CredentialRefreshed)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::SetupRecovered)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::Held)), MLOG_DEBUG);
    EXPECT_EQ(brokerEventLevel(ev(BrokerEventKind::Rehab)), MLOG_DEBUG);
}

TEST(BrokerPolicyFormat, LongestLinesFitTheBudget) {
    const BrokerEvent longest[] = {
        offband::makeCredentialFailedEvent(255, 0, offband::BrokerCredentialPhase::MidSession),
        offband::makeCredentialRefreshedEvent(255, 0, UINT32_MAX),
        offband::makeSetupRecoveredEvent(255, 0, UINT32_MAX),
        offband::makeRehabEvent(255, 0, UINT32_MAX, 255, UINT32_MAX),
        offband::makeBackoffEvent(255, 0, UINT32_MAX, UINT32_MAX),
        offband::makeFailedEvent(255, 0, UINT32_MAX),
        offband::makeResurrectedEvent(255, 0, UINT32_MAX),
    };
    for (const BrokerEvent& e : longest) {
        EXPECT_LT(fmt(e).size(), kLineBudget) << fmt(e);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
