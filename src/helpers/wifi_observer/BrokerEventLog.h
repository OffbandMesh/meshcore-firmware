#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MeshLog.h"   // MLOG_* levels; Arduino-free by design

// Broker lifecycle event log.
//
// The MQTT broker subsystem changes state on three tasks: mqtt_worker (connect
// attempts, deferrals, begin, terminal reap, rotation), the esp-mqtt event task
// (connected, disconnected, error, escalation), and loopTask (operator actions).
// Every one of those transitions must reach the capture ring.
//
// Formatting a line costs MLOG_LINE_MAX bytes of stack plus a vsnprintf frame on
// whichever task formats it. mqtt_worker and the esp-mqtt event task are the two
// tasks in this subsystem with little stack to spare, so transitions are not
// formatted where they happen. The producing task copies a fixed 24-byte record
// into this queue under a spinlock -- no formatting, no allocation, no strings --
// and loopTask drains it, formats each record, and hands it to mesh_log_line().
//
// Records carry no strings. Host and transport are looked up from the slot's
// config at format time, which is also what keeps the formatter pure and
// host-testable.
//
// Capacity is 32: a whole-link outage flips every enabled slot at once (one
// disconnect plus one backoff each), which loopTask clears within a pass. A full
// queue drops the newest record and counts it; the drain reports the count as
// its own line, so a drop is never silent.
//
// Dependency-free apart from the ESP32 spinlock, so it builds under [env:native].

#if defined(ARDUINO) && defined(ESP_PLATFORM)
  #include <freertos/FreeRTOS.h>
#endif

namespace offband {

enum class BrokerEventKind : uint8_t {
    // [mqtt] -- MQTT protocol events only.
    MqttConnecting   = 1,
    MqttConnected    = 2,
    MqttDisconnected = 3,   // cls, b0=esp-mqtt error_type, n0=sock_errno,
    MqttError        = 4,   //   w0=tls_esp_err, w1=cert_flags, n1=-tls_stack_err, b1=connack

    // [broker] -- the pool's decisions about a slot.
    Backoff          = 10,  // b0=penalty, n0=seconds until the next attempt
    Failed           = 11,  // b0=penalty that made it terminal
    Resurrected      = 12,  // b0=penalty it held while terminal
    OperatorDisabled = 13,
    OperatorEnabled  = 14,
    Held             = 15,  // b0=BrokerHoldReason
    Released         = 16,  // b0=BrokerHoldReason
    Rehab            = 17,  // n0=clean-dwell streak, b0=streak needed, w0=penalty
    PenaltyCleared   = 18,
    ReconcileFailed  = 19,  // n0=broker state, cls
    Dropped          = 20,  // n0=records dropped since the last report (no slot)

    // [rot] -- rotation. Wording of the two existing lines is kept verbatim.
    RotateIn         = 30,
    RotateOut        = 31,  // n0=age seconds, w0=free heap before, n1=cooldown seconds
    PostShutdown     = 32,  // w0=free heap after
};

enum class BrokerHoldReason : uint8_t {
    NoClock   = 1,   // wss/TLS waits for a sane wall clock
    NoHeap    = 2,   // free heap below the TLS floor
    TlsBudget = 3,   // the pool's live-TLS budget is taken
};

// Error class codes, mirroring BrokerErrorClass in MqttBroker.h (kept as plain
// integers here so this header does not depend on the broker header).
inline const char* brokerEventErrorClassName(uint8_t cls) {
    switch (cls) {
        case 0:  return "none";
        case 1:  return "tcp";
        case 2:  return "auth";
        case 3:  return "tls";
        case 4:  return "other";
        default: return "unknown";
    }
}

inline const char* brokerHoldReasonName(uint8_t r) {
    switch (static_cast<BrokerHoldReason>(r)) {
        case BrokerHoldReason::NoClock:   return "no-clock";
        case BrokerHoldReason::NoHeap:    return "no-heap";
        case BrokerHoldReason::TlsBudget: return "tls-budget";
    }
    return "unknown";
}

struct BrokerEvent {
    uint32_t at_ms;   // capture-time millis(); survives the one-pass drain delay
    uint32_t w0;      // kind-specific word (see BrokerEventKind)
    uint32_t w1;      // kind-specific word
    int32_t  n0;      // kind-specific signed value
    int16_t  n1;      // kind-specific short
    uint8_t  slot;
    uint8_t  kind;    // BrokerEventKind
    uint8_t  cls;     // error class code, where the kind carries one
    uint8_t  b0;      // kind-specific byte
    int8_t   b1;      // kind-specific signed byte
    uint8_t  reserved;
};
static_assert(sizeof(BrokerEvent) == 24, "BrokerEvent must stay a fixed 24-byte record");

inline BrokerEvent makeBrokerEvent(BrokerEventKind kind, uint8_t slot, uint32_t at_ms) {
    BrokerEvent e;
    std::memset(&e, 0, sizeof(e));
    e.kind  = static_cast<uint8_t>(kind);
    e.slot  = slot;
    e.at_ms = at_ms;
    return e;
}

// Failures and the terminal give-up are errors; every other transition is debug.
// Both pass the default UART mirror ceiling (MLOG_DEBUG).
inline uint8_t brokerEventLevel(const BrokerEvent& e) {
    switch (static_cast<BrokerEventKind>(e.kind)) {
        case BrokerEventKind::MqttDisconnected:
        case BrokerEventKind::MqttError:
        case BrokerEventKind::Failed:
        case BrokerEventKind::ReconcileFailed:
        case BrokerEventKind::Dropped:
            return MLOG_ERROR;
        default:
            return MLOG_DEBUG;
    }
}

// Copies the host part of a broker URL ("wss://mqtt.example.net:443/mqtt" ->
// "mqtt.example.net"). An empty or scheme-only URL yields "".
//
// Userinfo is skipped: credentials typed into a URL ("wss://user:pass@host")
// must never reach a log line. Only an '@' before the first '/' belongs to the
// authority, and the last one is used because a password may contain '@'.
inline void brokerEventExtractHost(const char* url, char* out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    if (url == nullptr) return;
    const char* p = std::strstr(url, "://");
    p = (p != nullptr) ? p + 3 : url;
    const char* slash = std::strchr(p, '/');
    const char* at = nullptr;
    for (const char* q = p; *q != '\0' && (slash == nullptr || q < slash); ++q) {
        if (*q == '@') at = q;
    }
    if (at != nullptr) p = at + 1;
    size_t n = 0;
    while (p[n] != '\0' && p[n] != ':' && p[n] != '/' && n + 1 < cap) {
        out[n] = p[n];
        ++n;
    }
    out[n] = '\0';
}

// Formats one record into `out` without a trailing newline. Returns the length
// written, or 0 for an unknown kind. `host` and `transport` are used only by
// MqttConnecting and may be null.
inline size_t formatBrokerEvent(const BrokerEvent& e, const char* host,
                                const char* transport, char* out, size_t cap) {
    if (cap == 0) return 0;
    const unsigned s = e.slot;
    int n = 0;
    switch (static_cast<BrokerEventKind>(e.kind)) {
        case BrokerEventKind::MqttConnecting:
            n = std::snprintf(out, cap, "[mqtt] s%u connecting host=%s %s", s,
                              (host && host[0]) ? host : "?",
                              (transport && transport[0]) ? transport : "?");
            break;
        case BrokerEventKind::MqttConnected:
            n = std::snprintf(out, cap, "[mqtt] s%u up", s);
            break;
        case BrokerEventKind::MqttDisconnected:
        case BrokerEventKind::MqttError:
            n = std::snprintf(out, cap,
                              "[mqtt] s%u %s class=%s type=%u sock_errno=%ld "
                              "tls_esp_err=0x%lX tls_stack_err=-0x%04X cert_flags=0x%08lX connack=%d",
                              s,
                              e.kind == static_cast<uint8_t>(BrokerEventKind::MqttError) ? "error" : "down",
                              brokerEventErrorClassName(e.cls), (unsigned)e.b0, (long)e.n0,
                              (unsigned long)e.w0, (unsigned)(uint16_t)e.n1,
                              (unsigned long)e.w1, (int)e.b1);
            break;
        case BrokerEventKind::Backoff:
            n = std::snprintf(out, cap, "[broker] s%u backoff penalty=%u next=%lds",
                              s, (unsigned)e.b0, (long)e.n0);
            break;
        case BrokerEventKind::Failed:
            n = std::snprintf(out, cap,
                              "[broker] s%u FAILED penalty=%u -- gave up; needs operator enable or reboot",
                              s, (unsigned)e.b0);
            break;
        case BrokerEventKind::Resurrected:
            n = std::snprintf(out, cap, "[broker] s%u resurrected by operator (was failed, penalty=%u)",
                              s, (unsigned)e.b0);
            break;
        case BrokerEventKind::OperatorDisabled:
            n = std::snprintf(out, cap, "[broker] s%u disabled by operator", s);
            break;
        case BrokerEventKind::OperatorEnabled:
            n = std::snprintf(out, cap, "[broker] s%u enabled by operator", s);
            break;
        case BrokerEventKind::Held:
            n = std::snprintf(out, cap, "[broker] s%u held reason=%s", s, brokerHoldReasonName(e.b0));
            break;
        case BrokerEventKind::Released:
            n = std::snprintf(out, cap, "[broker] s%u released reason=%s", s, brokerHoldReasonName(e.b0));
            break;
        case BrokerEventKind::Rehab:
            n = std::snprintf(out, cap, "[broker] s%u rehab streak=%ld/%u penalty=%lu",
                              s, (long)e.n0, (unsigned)e.b0, (unsigned long)e.w0);
            break;
        case BrokerEventKind::PenaltyCleared:
            n = std::snprintf(out, cap, "[broker] s%u penalty cleared -- rehabilitated", s);
            break;
        case BrokerEventKind::ReconcileFailed:
            n = std::snprintf(out, cap, "[broker] s%u reconcile begin failed state=%ld class=%s",
                              s, (long)e.n0, brokerEventErrorClassName(e.cls));
            break;
        case BrokerEventKind::Dropped:
            n = std::snprintf(out, cap, "[broker] event queue full -- %ld events dropped", (long)e.n0);
            break;
        case BrokerEventKind::RotateIn:
            n = std::snprintf(out, cap, "[rot] ROTATE-IN s%u", s);
            break;
        case BrokerEventKind::RotateOut:
            n = std::snprintf(out, cap, "[rot] ROTATE-OUT s%u age=%lds heapBefore=%lu -> cooldown %ds",
                              s, (long)e.n0, (unsigned long)e.w0, (int)e.n1);
            break;
        case BrokerEventKind::PostShutdown:
            n = std::snprintf(out, cap, "[rot] post-shutdown s%u heapAfter=%lu", s, (unsigned long)e.w0);
            break;
        default:
            out[0] = '\0';
            return 0;
    }
    if (n < 0) { out[0] = '\0'; return 0; }
    return (static_cast<size_t>(n) < cap) ? static_cast<size_t>(n) : cap - 1;
}

// Fixed-capacity FIFO of broker events. push() may be called from any task;
// pop() and takeDropped() are called by the single drain on loopTask.
template <size_t N>
class BrokerEventQueue {
    static_assert(N > 0 && N <= 255, "capacity must fit the uint8_t indices");

public:
    // Returns false and counts a drop when the queue is full. The newest record
    // is the one dropped, so the order of what was already queued is preserved.
    bool push(const BrokerEvent& e) {
        lock();
        bool ok = count_ < N;
        if (ok) {
            buf_[static_cast<uint8_t>((head_ + count_) % N)] = e;
            ++count_;
        } else if (dropped_ != UINT32_MAX) {
            ++dropped_;
        }
        unlock();
        return ok;
    }

    bool pop(BrokerEvent& out) {
        lock();
        bool ok = count_ > 0;
        if (ok) {
            out = buf_[head_];
            head_ = static_cast<uint8_t>((head_ + 1) % N);
            --count_;
        }
        unlock();
        return ok;
    }

    // Returns the drops accumulated since the last call and resets the count.
    uint32_t takeDropped() {
        lock();
        uint32_t d = dropped_;
        dropped_ = 0;
        unlock();
        return d;
    }

    static constexpr size_t capacity() { return N; }

private:
#if defined(ARDUINO) && defined(ESP_PLATFORM)
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    void lock()   { portENTER_CRITICAL(&mux_); }
    void unlock() { portEXIT_CRITICAL(&mux_); }
#else
    void lock()   {}
    void unlock() {}
#endif

    BrokerEvent buf_[N];
    uint8_t     head_    = 0;
    uint8_t     count_   = 0;
    uint32_t    dropped_ = 0;
};

constexpr size_t kBrokerEventQueueDepth = 32;

// The single process-wide queue. Producers push; MqttBrokerPool drains it on loopTask.
inline BrokerEventQueue<kBrokerEventQueueDepth>& brokerEventQueue() {
    static BrokerEventQueue<kBrokerEventQueueDepth> q;
    return q;
}

}  // namespace offband
