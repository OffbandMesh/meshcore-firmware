#include "CaplogForward.h"

#include <stdio.h>

namespace offband {

void caplogSanitizeTag(const char* tag, char* out, size_t out_cap) {
    if (out == nullptr || out_cap == 0) return;
    if (tag == nullptr || tag[0] == '\0') tag = "unknown";
    size_t limit = out_cap - 1;
    if (limit > kCaplogTagMax) limit = kCaplogTagMax;
    size_t n = 0;
    for (; n < limit && tag[n] != '\0'; ++n) {
        const char c = tag[n];
        const bool legal = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '-';
        out[n] = legal ? c : '-';
    }
    out[n] = '\0';
}

void caplogPubKeyTag(const uint8_t* pub_key, char* out, size_t out_cap) {
    static const char kHex[] = "0123456789ABCDEF";
    if (out == nullptr || out_cap == 0) return;
    size_t n = 0;
    if (pub_key != nullptr) {
        for (size_t i = 0; i < 8 && n + 2 < out_cap; ++i) {
            out[n++] = kHex[pub_key[i] >> 4];
            out[n++] = kHex[pub_key[i] & 0x0F];
        }
    }
    out[n] = '\0';
}

CaplogForward::CaplogForward(const char* tag, CaplogReadFn read, CaplogDatagramSink& sink)
    : read_(read), sink_(sink) {
    setTag(tag);
}

CaplogForward::CaplogForward(const char* tag, CaplogCursorReadFn read, CaplogDatagramSink& sink)
    : cursor_read_(read), sink_(sink) {
    setTag(tag);
}

void CaplogForward::setTag(const char* tag) {
    char clean[kTagMax + 1];
    caplogSanitizeTag(tag, clean, sizeof(clean));
    snprintf(prefix_, sizeof(prefix_), "<134>caplog-%s: ", clean);
}

void CaplogForward::setIdentity(const char* identity) {
    snprintf(identity_, sizeof(identity_), "%.*s", (int)kCaplogIdentityMax,
             identity != nullptr ? identity : "");
}

void CaplogForward::armFor(uint32_t window_sec, uint32_t now_ms) {
    if (window_sec == 0) {
        disarm();
        return;
    }
    // Opening a window, as opposed to extending an open one.
    if (!armed(now_ms)) announce_ = true;
    until_ms_ = now_ms + window_sec * 1000UL;
    armed_ = true;
}

void CaplogForward::disarm() {
    armed_ = false;
}

bool CaplogForward::armed(uint32_t now_ms) {
    if (!armed_) return false;
    // Wrap-safe: true once now_ms has reached the deadline, across a millis() wrap.
    if ((int32_t)(now_ms - until_ms_) >= 0) {
        armed_ = false;
        return false;
    }
    return true;
}

void CaplogForward::sendNote(const char* host, uint16_t port, const char* text, int len) {
    if (len <= 0) return;
    sink_.send(host, port, prefix_, reinterpret_cast<const uint8_t*>(text), (size_t)len);
}

void CaplogForward::reportLoss(const char* host, uint16_t port, uint64_t lost) {
    const uint32_t n = lost > UINT32_MAX ? UINT32_MAX : (uint32_t)lost;
    bytes_lost_ = (n > UINT32_MAX - bytes_lost_) ? UINT32_MAX : bytes_lost_ + n;
    char note[48];
    const int len = snprintf(note, sizeof(note), "[caplog] forward lost %lu bytes", (unsigned long)n);
    sendNote(host, port, note, len < (int)sizeof(note) ? len : (int)sizeof(note) - 1);
}

void CaplogForward::service(const char* host, uint16_t port, bool link_up, uint32_t now_ms) {
    if (!armed(now_ms)) return;
    if (!link_up) return;
    if (host == nullptr || host[0] == '\0') return;
    if (announce_) {
        announce_ = false;
        if (identity_[0] != '\0') {
            char line[192];
            const int len = snprintf(line, sizeof(line), "[caplog] forward on: id=%s sink=%s:%u",
                                     identity_, host, (unsigned)port);
            sendNote(host, port, line, len < (int)sizeof(line) ? len : (int)sizeof(line) - 1);
        }
    }
    size_t n;
    if (cursor_read_ != nullptr) {
        // The first read starts at the oldest line held, so what was evicted
        // before forwarding ever began is not a gap in what the sink receives.
        uint64_t lost = 0;
        n = cursor_read_(&cursor_, buf_, sizeof(buf_), started_ ? &lost : nullptr);
        started_ = true;
        if (lost > 0) reportLoss(host, port, lost);
    } else {
        n = read_(buf_, sizeof(buf_));
    }
    caplogForEachLine(buf_, n, [&](const uint8_t* line, size_t len) {
        sink_.send(host, port, prefix_, line, len);
        ++lines_sent_;
    });
}

}  // namespace offband
