// src/helpers/wifi_observer/ApSetupForm.h
//
// SECONDARY first-contact WiFi setup path. Default OFF. When the
// build flag CROSSWIRE_AP_SETUP_FORM_ENABLED is defined non-zero,
// WifiBootstrap's no-creds branch starts a softAP +
// plain-HTTP form on :80 in addition to posting the welcome
// message on the BLE system channel (Task 10). Whichever path the
// user uses first wins: both write to the same NVS namespace
// "wifi" keys "ssid" / "pwd" via the Task 10 helpers, the device
// reboots into STA mode after either, and the second path is no
// longer relevant because the no-creds branch is not re-entered.
//
// Intentionally minimal:
//   GET  /  -- 3-field HTML form (SSID + PSK + optional broker URL)
//   POST /  -- parse application/x-www-form-urlencoded body,
//              validate, write NVS, render "rebooting in 3s",
//              ESP.restart() after a brief delay
//
// The form is plain HTTP on a softAP-only attack surface (the
// device is not yet on the user's LAN). HTTPS commissioning would
// require the user to import a self-signed cert into their phone
// before they can connect, which is an unacceptable first-contact
// experience. Polished broker / cert / auth config happens via the
// HTTPS UI (Plan 3 Tasks 1-9) once STA is up and the user has
// authenticated.
//
// File body wrapped in the build-flag guard: when the flag is
// unset, both this header (after #pragma once) and the matching
// .cpp produce an empty translation unit. WifiBootstrap.cpp's call
// sites are wrapped in the same guard so they do not reference
// undefined symbols.
//
// Plan 3 Task 10b (Strycher/LoRa#272).

#pragma once

#if defined(CROSSWIRE_AP_SETUP_FORM_ENABLED) && CROSSWIRE_AP_SETUP_FORM_ENABLED

#include <stdint.h>
#include <stddef.h>

namespace crosswire {

// Bring up WiFi.softAP + WiFiServer on :80. SSID derived via
// WifiBootstrap::deriveApSsid; no PSK (open network). Idempotent --
// calling twice without an intervening Stop is a no-op and returns
// the prior success/failure result.
//
// Returns true on success, false on softAP / WiFiServer bring-up
// failure (Serial-logged). On false, the caller (WifiBootstrap)
// continues in ApMode without the form -- the BLE system channel
// path remains available.
bool apSetupFormStart();

// Tear down WiFiServer + softAP. Safe to call when not started.
// Called from WifiBootstrap::loop() once apSetupFormCompleted()
// flips true, immediately before ESP.restart().
void apSetupFormStop();

// Poll-style completion check. Returns true exactly once: after a
// POST / has successfully written NVS, the internal flag is set so
// the next call to this function returns true. The caller is
// expected to invoke apSetupFormStop() + reboot in response;
// re-arming is not supported (the device is about to restart).
bool apSetupFormCompleted();

// Service one iteration of the WiFiServer accept loop. Called from
// WifiBootstrap::loop() while in ApMode and the form is active.
// Non-blocking: each call handles at most one accepted client,
// reads the request line + headers + body up to a fixed cap, and
// responds. A long POST body or slow client will be rejected with
// a small fixed timeout so we never hang the main loop.
void apSetupFormService();

// Returns a pointer to a static buffer holding the AP SSID for the
// system-channel post. Valid for the lifetime of the program. NULL
// if apSetupFormStart() has not been called or failed. Used by
// WifiBootstrap to compose the "AP form available at ..." message.
const char* apSetupFormSsid();

}  // namespace crosswire

#endif  // CROSSWIRE_AP_SETUP_FORM_ENABLED
