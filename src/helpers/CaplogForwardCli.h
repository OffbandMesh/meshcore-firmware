#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CaplogForward.h"

// The `caplog forward` command and the forward fields of `caplog status`,
// shared by every role that forwards (#1060 repeater, #1194 observer) so they
// answer in the same words. Pure: the role supplies its prefs, link state and
// clock, which also keeps this testable on the native env.

namespace offband {

// Provided by each role built with OFFBAND_CAPLOG_FORWARD. The command reaches
// the forwarder and the link state through these two and nothing else, so a
// role needs no telemetry stack to host it.
CaplogForward& caplogForwarder();
bool           caplogForwardLinkUp();

constexpr uint32_t kCaplogDefaultWindowSec = 300;   // bare `caplog forward`
constexpr uint32_t kCaplogMinWindowSec     = 30;    // floor: a focused test

struct CaplogForwardArg {
    enum Kind : uint8_t { Off, UntilOff, Bounded, TooLong, Invalid };
    Kind     kind;
    uint32_t seconds;   // Bounded only
};

// Parses the text after "caplog forward". Surrounding spaces are ignored.
//   ""           Bounded, kCaplogDefaultWindowSec
//   "on"         UntilOff
//   "off", "0"   Off
//   digits       Bounded, at least kCaplogMinWindowSec; TooLong past
//                kCaplogMaxWindowSec
//   else         Invalid
CaplogForwardArg caplogParseForwardArg(const char* arg);

// Applies a parsed argument to fwd. Returns the until-off state the role
// persists (1 on, 0 off), or -1 when nothing changed: TooLong and Invalid
// leave a running forward alone, so a typo never disarms it.
int caplogApplyForwardArg(CaplogForward& fwd, const CaplogForwardArg& arg, uint32_t now_ms);

// The reply once arg has been applied. An arm names the first thing that would
// keep lines from the sink: capture off, then no sink, then no link.
// link_hint follows the no-link text; the repeater passes " (wifi on <min>)".
void caplogForwardReply(char* out, size_t out_cap, const CaplogForwardArg& arg,
                        bool capture_on, bool sink_set, bool link_up, const char* link_hint);

// `set syslog.host`: true when host (null reads as "", which clears the sink)
// fits in max characters. Otherwise writes "Error, host max <max> chars" to
// reply and returns false, and the caller must save nothing: a cut name would
// send to the wrong place while the reply said OK.
bool caplogCheckSinkHost(const char* host, size_t max, char* reply, size_t reply_cap);

// What `caplog status` reports about the forwarder.
struct CaplogForwardStatus {
    CaplogForwardMode mode;
    uint32_t    seconds_left;
    const char* host;      // null or empty: no sink
    uint16_t    port;
    uint32_t    sent;
    uint32_t    lost;
    bool        link_up;
};

CaplogForwardStatus caplogForwardStatusOf(CaplogForward& fwd, uint32_t now_ms,
                                          const char* host, uint16_t port, bool link_up);

// "caplog: on level=debug used=812/16384", then, when fwd is not null,
// " fwd=<off|<n>s|until-off>[ link=down] sink=<host:port|none> sent=<n> lost=<n>",
// with link=down when armed without a link.
void caplogStatusLine(char* out, size_t out_cap, bool capture_on, const char* level,
                      unsigned used, unsigned capacity, const CaplogForwardStatus* fwd);

}  // namespace offband
