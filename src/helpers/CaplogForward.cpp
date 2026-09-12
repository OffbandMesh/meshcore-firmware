#include "CaplogForward.h"

#include <stdio.h>

namespace offband {

CaplogForward::CaplogForward(const char* tag, CaplogReadFn read, CaplogDatagramSink& sink)
    : read_(read), sink_(sink) {
    initPrefix(tag);
}

CaplogForward::CaplogForward(const char* tag, CaplogCursorReadFn read, CaplogDatagramSink& sink)
    : cursor_read_(read), sink_(sink) {
    initPrefix(tag);
}

void CaplogForward::initPrefix(const char* tag) {
    snprintf(prefix_, sizeof(prefix_), "<134>caplog-%.*s: ", (int)kTagMax, tag ? tag : "");
}

void CaplogForward::armFor(uint32_t window_sec, uint32_t now_ms) {
    if (window_sec == 0) {
        disarm();
        return;
    }
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

void CaplogForward::reportLoss(const char* host, uint16_t port, uint64_t lost) {
    const uint32_t n = lost > UINT32_MAX ? UINT32_MAX : (uint32_t)lost;
    bytes_lost_ = (n > UINT32_MAX - bytes_lost_) ? UINT32_MAX : bytes_lost_ + n;
    char note[48];
    const int len = snprintf(note, sizeof(note), "[caplog] forward lost %lu bytes", (unsigned long)n);
    if (len > 0) {
        const size_t sent = (size_t)len < sizeof(note) ? (size_t)len : sizeof(note) - 1;
        sink_.send(host, port, prefix_, reinterpret_cast<const uint8_t*>(note), sent);
    }
}

void CaplogForward::service(const char* host, uint16_t port, bool link_up, uint32_t now_ms) {
    if (!armed(now_ms)) return;
    if (!link_up) return;
    if (host == nullptr || host[0] == '\0') return;
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
