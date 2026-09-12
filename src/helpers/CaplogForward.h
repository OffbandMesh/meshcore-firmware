#pragma once

#include <stddef.h>
#include <stdint.h>

// Caplog forward: streams captured log lines off the device as syslog
// datagrams, so a failure's lead-up survives on a sink even when the node's
// own RAM ring does not (#561). Role-neutral since #1058, so any role that
// has a WiFi link can use it.
//
// This core is Arduino-free and never touches WiFi. Whether a link is up
// comes in as an argument to service(); the caller owns the link (#1045).
// Two things are injected, which also keeps the core testable on the native
// env:
//   - the read: how new bytes are taken from the capture ring
//     (meshLogConsume() on the repeater);
//   - the sink: where each framed datagram goes (a WiFiUDP sender on the
//     device, see CaplogUdpSink.h).
//
// service() reads ONE chunk of at most kChunkBytes per call, never the whole
// ring. A full-ring drain is hundreds of UDP sends that can block on LwIP
// back-pressure and stall the main loop, starving the LoRa dispatcher (#561
// review: BLOCKER). Callers run it every loop pass, so the ring still drains
// promptly, spread across passes.

namespace offband {

// Takes up to out_cap bytes of captured lines into out; returns bytes taken.
using CaplogReadFn = size_t (*)(uint8_t* out, size_t out_cap);

// Receives one framed datagram: the syslog prefix, then one line without its
// trailing newline.
class CaplogDatagramSink {
public:
    virtual ~CaplogDatagramSink() = default;
    virtual void send(const char* host, uint16_t port, const char* prefix,
                      const uint8_t* line, size_t len) = 0;
};

// Calls emit(line, len) for each line in buf[0..n), without its '\n'. Empty
// lines are skipped. A final line with no '\n' is emitted as it is, so a line
// longer than one chunk leaves as two datagrams.
template <typename Emit>
void caplogForEachLine(const uint8_t* buf, size_t n, Emit emit) {
    size_t start = 0;
    for (size_t i = 0; i < n; ++i) {
        const bool eol = (buf[i] == '\n');
        if (eol || i + 1 == n) {
            const size_t end = eol ? i : i + 1;
            if (end > start) emit(buf + start, end - start);
            start = i + 1;
        }
    }
}

class CaplogForward {
public:
    static constexpr size_t kChunkBytes = 512;
    static constexpr size_t kPrefixMax  = 48;
    // Room left for the tag once "<134>caplog-" and ": " are in: 33 characters.
    static constexpr size_t kTagMax     = kPrefixMax - sizeof("<134>caplog-: ");

    // tag must already be a legal syslog TAG body. Each datagram starts with
    // "<134>caplog-<tag>: " (PRI 134 = local0.info), which the receiver's
    // `programname startswith 'caplog-'` filter routes on. A longer tag is cut
    // to kTagMax so the ": " separator always survives.
    CaplogForward(const char* tag, CaplogReadFn read, CaplogDatagramSink& sink);

    // Opens a window of window_sec seconds from now_ms. 0 disarms.
    void armFor(uint32_t window_sec, uint32_t now_ms);
    void disarm();

    // True while a window is open; closes it once its deadline has passed.
    bool armed(uint32_t now_ms);

    // Sends one chunk's worth of lines when armed, a host is set and the link
    // is up. Otherwise it reads nothing, so lines wait in the ring.
    void service(const char* host, uint16_t port, bool link_up, uint32_t now_ms);

    const char* prefix() const { return prefix_; }
    uint32_t    linesSent() const { return lines_sent_; }

private:
    CaplogReadFn        read_;
    CaplogDatagramSink& sink_;
    char                prefix_[kPrefixMax];
    uint8_t             buf_[kChunkBytes];
    bool                armed_      = false;
    uint32_t            until_ms_   = 0;
    uint32_t            lines_sent_ = 0;
};

}  // namespace offband
