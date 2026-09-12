#pragma once

// The device-side sink for CaplogForward: one UDP datagram per line.
// Header-only, and included only by roles that forward, so builds that do not
// forward never pull in WiFi. It sends; it never brings the link up or down.

#include <WiFiUdp.h>

#include "CaplogForward.h"

namespace offband {

class CaplogUdpSink : public CaplogDatagramSink {
public:
    void send(const char* host, uint16_t port, const char* prefix,
              const uint8_t* line, size_t len) override {
        udp_.beginPacket(host, port);
        udp_.print(prefix);
        udp_.write(line, len);
        udp_.endPacket();
    }

private:
    WiFiUDP udp_;
};

}  // namespace offband
