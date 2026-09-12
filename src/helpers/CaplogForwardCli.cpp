#include "CaplogForwardCli.h"

#include <stdio.h>
#include <string.h>

namespace offband {

static bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

CaplogForwardArg caplogParseForwardArg(const char* arg) {
    CaplogForwardArg r = {CaplogForwardArg::Invalid, 0};
    if (arg == nullptr) arg = "";
    size_t len = strlen(arg);
    while (len > 0 && isSpace(arg[len - 1])) --len;
    while (len > 0 && isSpace(*arg)) { ++arg; --len; }

    if (len == 0) {
        r.kind = CaplogForwardArg::Bounded;
        r.seconds = kCaplogDefaultWindowSec;
        return r;
    }
    if (len == 2 && memcmp(arg, "on", 2) == 0) {
        r.kind = CaplogForwardArg::UntilOff;
        return r;
    }
    if (len == 3 && memcmp(arg, "off", 3) == 0) {
        r.kind = CaplogForwardArg::Off;
        return r;
    }

    uint32_t value = 0;
    bool too_long = false;
    for (size_t i = 0; i < len; ++i) {
        if (arg[i] < '0' || arg[i] > '9') return r;   // Invalid
        if (!too_long) {
            value = value * 10u + (uint32_t)(arg[i] - '0');
            if (value > kCaplogMaxWindowSec) too_long = true;   // stop before it can wrap
        }
    }
    if (too_long) {
        r.kind = CaplogForwardArg::TooLong;
    } else if (value == 0) {
        r.kind = CaplogForwardArg::Off;
    } else {
        r.kind = CaplogForwardArg::Bounded;
        r.seconds = value < kCaplogMinWindowSec ? kCaplogMinWindowSec : value;
    }
    return r;
}

int caplogApplyForwardArg(CaplogForward& fwd, const CaplogForwardArg& arg, uint32_t now_ms) {
    switch (arg.kind) {
        case CaplogForwardArg::Off:      fwd.disarm();                  return 0;
        case CaplogForwardArg::UntilOff: fwd.armUntilOff(now_ms);       return 1;
        case CaplogForwardArg::Bounded:  fwd.armFor(arg.seconds, now_ms); return 0;
        default:                                                        return -1;
    }
}

void caplogForwardReply(char* out, size_t out_cap, const CaplogForwardArg& arg,
                        bool capture_on, bool sink_set, bool link_up, const char* link_hint) {
    if (out == nullptr || out_cap == 0) return;
    switch (arg.kind) {
        case CaplogForwardArg::Off:
            snprintf(out, out_cap, "caplog forward off");
            return;
        case CaplogForwardArg::TooLong:
            snprintf(out, out_cap, "caplog forward: max %lus; use caplog forward on",
                     (unsigned long)kCaplogMaxWindowSec);
            return;
        case CaplogForwardArg::Invalid:
            snprintf(out, out_cap, "ERR: caplog forward on|off|<seconds>");
            return;
        default:
            break;
    }
    char head[40];
    if (arg.kind == CaplogForwardArg::UntilOff) {
        snprintf(head, sizeof(head), "caplog forward on until off");
    } else {
        snprintf(head, sizeof(head), "caplog forward on %lus", (unsigned long)arg.seconds);
    }
    // The forward never switches capture on or brings a link up (#1045), so say
    // what is missing rather than claim it is streaming.
    if (!capture_on) {
        snprintf(out, out_cap, "%s -- capture is off, nothing to send (caplog start)", head);
    } else if (!sink_set) {
        snprintf(out, out_cap, "%s -- no sink; set syslog.host <host>", head);
    } else if (!link_up) {
        snprintf(out, out_cap, "%s -- no WiFi link; lines send once one is up%s", head,
                 link_hint != nullptr ? link_hint : "");
    } else {
        snprintf(out, out_cap, "%s (streaming to syslog)", head);
    }
}

bool caplogCheckSinkHost(const char* host, size_t max, char* reply, size_t reply_cap) {
    if (host == nullptr || strlen(host) <= max) return true;
    if (reply != nullptr && reply_cap > 0) {
        snprintf(reply, reply_cap, "Error, host max %lu chars", (unsigned long)max);
    }
    return false;
}

CaplogForwardStatus caplogForwardStatusOf(CaplogForward& fwd, uint32_t now_ms,
                                          const char* host, uint16_t port, bool link_up) {
    CaplogForwardStatus s;
    s.mode         = fwd.mode(now_ms);
    s.seconds_left = fwd.secondsLeft(now_ms);
    s.host         = host;
    s.port         = port;
    s.sent         = fwd.linesSent();
    s.lost         = fwd.bytesLost();
    s.link_up      = link_up;
    return s;
}

void caplogStatusLine(char* out, size_t out_cap, bool capture_on, const char* level,
                      unsigned used, unsigned capacity, const CaplogForwardStatus* fwd) {
    if (out == nullptr || out_cap == 0) return;
    const int n = snprintf(out, out_cap, "caplog: %s level=%s used=%u/%u",
                           capture_on ? "on" : "off", level != nullptr ? level : "?",
                           used, capacity);
    if (fwd == nullptr || n < 0 || (size_t)n >= out_cap) return;

    char mode[16];
    switch (fwd->mode) {
        case CaplogForwardMode::UntilOff: snprintf(mode, sizeof(mode), "until-off"); break;
        case CaplogForwardMode::Bounded:
            snprintf(mode, sizeof(mode), "%lus", (unsigned long)fwd->seconds_left);
            break;
        default:                          snprintf(mode, sizeof(mode), "off"); break;
    }
    char sink[80];
    if (fwd->host == nullptr || fwd->host[0] == '\0') {
        snprintf(sink, sizeof(sink), "none");
    } else {
        snprintf(sink, sizeof(sink), "%s:%u", fwd->host, (unsigned)fwd->port);
    }
    // link=down sits right after the mode: it is the one field that says nothing
    // is leaving, and a long sink name must not push it off the 160-byte reply.
    const bool link_down = fwd->mode != CaplogForwardMode::Off && !fwd->link_up;
    snprintf(out + n, out_cap - (size_t)n, " fwd=%s%s sink=%s sent=%lu lost=%lu", mode,
             link_down ? " link=down" : "", sink, (unsigned long)fwd->sent, (unsigned long)fwd->lost);
}

}  // namespace offband
