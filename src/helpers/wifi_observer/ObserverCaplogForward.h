// src/helpers/wifi_observer/ObserverCaplogForward.h
//
// #1194: the observer's caplog forward. One CaplogForward in cursor mode, so
// forwarded lines stay in caplog for the app's download (#1057 decision 2),
// sending over the observer's STA link through a WiFiUDP sink. It also provides
// the role hooks the shared `caplog forward` command reaches
// (caplogForwarder(), caplogForwardLinkUp()).
//
// The sink address is read from NVS once, at begin, and cached; `set
// syslog.host/port` update the cache after a successful write, so the per-pass
// service() never touches flash. The forward reads the link and never changes
// it (#1045).
//
// Compiled only with OFFBAND_OBSERVER and OFFBAND_CAPLOG_FORWARD.

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace offband {

// Loads the sink and re-arms `caplog forward on` if it was on at the last
// shutdown (#1057 decision 5). Call once, after NVS is usable.
void observerCaplogForwardBegin(uint32_t now_ms);

// The tag (first 16 hex of the public key, #1059) and the full device_id the
// sink is told when a window opens. Call once the identity is loaded.
void observerCaplogForwardSetIdentity(const uint8_t* pub_key, const char* device_id);

// One bounded pass: sends at most one chunk. Call every observer loop pass.
void observerCaplogForwardService(uint32_t now_ms);

// The cached sink, and its update after `set syslog.host/port` has saved.
void        observerCaplogForwardSetSink(const char* host, uint16_t port);
const char* observerCaplogForwardHost();
uint16_t    observerCaplogForwardPort();

// #1194 (option A): switch capture on at level, or off, and persist it -- what
// `caplog start|stop` do. Provided by the companion (MyMesh.cpp), which owns the
// saved setting, so it behaves exactly like the app's caplog enable/disable.
// Returns false when the setting could not be saved (it holds until reboot).
bool observerCaplogSetCapture(bool on, uint8_t level);

}  // namespace offband
