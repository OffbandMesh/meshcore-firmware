#include "CaplogForward.h"

#include <stdio.h>

namespace offband {

CaplogForward::CaplogForward(const char* tag, CaplogReadFn read, CaplogDatagramSink& sink)
    : read_(read), sink_(sink) {
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

void CaplogForward::service(const char* host, uint16_t port, bool link_up, uint32_t now_ms) {
    if (!armed(now_ms)) return;
    if (!link_up) return;
    if (host == nullptr || host[0] == '\0') return;
    const size_t n = read_(buf_, sizeof(buf_));
    caplogForEachLine(buf_, n, [&](const uint8_t* line, size_t len) {
        sink_.send(host, port, prefix_, line, len);
        ++lines_sent_;
    });
}

}  // namespace offband
