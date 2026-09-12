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
//   - the read: how new bytes are taken from the capture ring;
//   - the sink: where each framed datagram goes (a WiFiUDP sender on the
//     device, see CaplogUdpSink.h).
//
// The read comes in one of two modes:
//   - Consume (the repeater): meshLogConsume() removes what it returns, so
//     forwarded lines leave the ring.
//   - Cursor (the observer, #1193): meshLogReadFrom() copies from a position
//     the helper keeps and removes nothing, so the app's caplog download still
//     has every line and the sink gets a copy. If eviction overtakes the
//     cursor, the gap is counted in bytesLost() and announced to the sink as
//     "[caplog] forward lost N bytes".
//
// service() reads ONE chunk of at most kChunkBytes per call, never the whole
// ring. A full-ring drain is hundreds of UDP sends that can block on LwIP
// back-pressure and stall the main loop, starving the LoRa dispatcher (#561
// review: BLOCKER). Callers run it every loop pass, so the ring still drains
// promptly, spread across passes.

namespace offband {

// #1059: RFC 3164 caps a syslog TAG at 32 characters. "caplog-", a tag body of
// up to this many characters, and the ':' come to exactly 32.
constexpr size_t kCaplogTagMax = 24;

// #1059: longest identity announced when a window opens. The observer's
// device_id is its public key as 64 hex digits.
constexpr size_t kCaplogIdentityMax = 64;

// #1059: writes tag into out as a legal syslog TAG body, which the receiver's
// `programname startswith 'caplog-'` filter depends on:
//   - letters, digits, '_' and '-' are kept; anything else (spaces,
//     punctuation, non-ASCII bytes) becomes '-';
//   - at most kCaplogTagMax characters, and at most out_cap - 1;
//   - a null or empty tag becomes "unknown".
void caplogSanitizeTag(const char* tag, char* out, size_t out_cap);

// #1059: the observer's tag: the first 8 bytes of its public key as 16
// uppercase hex digits. That is also the start of its MQTT device_id, so a
// caplog line can be matched to the node's MQTT feed. out needs 17 bytes for
// all 16 digits.
void caplogPubKeyTag(const uint8_t* pub_key, char* out, size_t out_cap);

// Consume mode: takes up to out_cap bytes of captured lines into out, removing
// them from the ring; returns bytes taken.
using CaplogReadFn = size_t (*)(uint8_t* out, size_t out_cap);

// Cursor mode: copies up to out_cap bytes of whole lines from the absolute
// position *cursor into out and advances *cursor, removing nothing. Adds any
// bytes eviction skipped to *lost, unless lost is nullptr; returns bytes copied.
using CaplogCursorReadFn = size_t (*)(uint64_t* cursor, uint8_t* out, size_t out_cap,
                                      uint64_t* lost);

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
    static constexpr size_t kTagMax     = kCaplogTagMax;
    static_assert(kTagMax + sizeof("<134>caplog-: ") <= kPrefixMax,
                  "the longest prefix must fit in prefix_");

    // Each datagram starts with "<134>caplog-<tag>: " (PRI 134 = local0.info).
    // The tag is sanitized once, here or in setTag(), never per line.
    CaplogForward(const char* tag, CaplogReadFn read, CaplogDatagramSink& sink);
    CaplogForward(const char* tag, CaplogCursorReadFn read, CaplogDatagramSink& sink);

    // #1059: replaces the tag, for a role whose identity is loaded after this
    // object is constructed (the observer's public key).
    void setTag(const char* tag);

    // #1059: an identity to announce each time a window opens, as one
    // datagram ahead of the lines: "[caplog] forward on: id=<identity>
    // sink=<host>:<port>". The tag is short, so this is where the sink learns
    // the node's full id. Empty, the default, announces nothing. Longer than
    // kCaplogIdentityMax is cut.
    void setIdentity(const char* identity);

    // Opens a window of window_sec seconds from now_ms. 0 disarms.
    //
    // Cursor mode: from the first arm on, the sink gets a copy of the capture
    // with every gap marked. The very first read starts at the oldest line held,
    // so the ring's backlog goes first and nothing counts as lost. After that
    // the cursor carries across windows: a later window starts where the last
    // one stopped, and anything evicted in between, window open or not, is
    // reported as lost.
    void armFor(uint32_t window_sec, uint32_t now_ms);
    void disarm();

    // True while a window is open; closes it once its deadline has passed.
    bool armed(uint32_t now_ms);

    // Sends one chunk's worth of lines when armed, a host is set and the link
    // is up. Otherwise it reads nothing, so lines wait in the ring.
    void service(const char* host, uint16_t port, bool link_up, uint32_t now_ms);

    const char* prefix() const { return prefix_; }
    uint32_t    linesSent() const { return lines_sent_; }
    // Cursor mode: bytes eviction took before they could be sent, saturating.
    uint32_t    bytesLost() const { return bytes_lost_; }

private:
    void reportLoss(const char* host, uint16_t port, uint64_t lost);
    void sendNote(const char* host, uint16_t port, const char* text, int len);

    CaplogReadFn        read_        = nullptr;
    CaplogCursorReadFn  cursor_read_ = nullptr;
    CaplogDatagramSink& sink_;
    char                prefix_[kPrefixMax];
    char                identity_[kCaplogIdentityMax + 1] = {};
    uint8_t             buf_[kChunkBytes];
    bool                armed_       = false;
    bool                announce_    = false;  // the open window is not announced yet
    uint32_t            until_ms_    = 0;
    uint32_t            lines_sent_  = 0;
    uint64_t            cursor_      = 0;
    bool                started_     = false;  // a cursor read has happened
    uint32_t            bytes_lost_  = 0;
};

}  // namespace offband
