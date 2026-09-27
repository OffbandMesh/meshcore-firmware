#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// #1070: decode for the BLE disconnect reason each stack hands its connection
// callback, shared by the ESP32 (NimBLE) and nRF52 (Bluefruit / SoftDevice)
// interfaces so one `[ble] disconnect` line reads the same on every board.
//
// The purpose is to answer "who dropped whom, and why" from one log line: did
// the client close the app, did we throw it off, or did the link die. `by` is
// derived from the code so nobody needs a spec lookup to read a tester log.
//
// The two stacks encode the same HCI reason differently:
//   - SoftDevice passes the raw HCI code (uint8_t): 0x13, 0x16, ...
//   - NimBLE passes a host return code: BLE_HS_ERR_HCI_BASE (0x200) + the HCI
//     code for controller reasons, or a bare host error (BLE_HS_E*, small
//     integers) for host-side failures. A bare NimBLE 0x08 is NOT the HCI 0x08,
//     so each platform has its own entry point; nothing guesses from magnitude.
//
// Header-only and Arduino-free: unit-tested natively in test/test_ble_reason.

namespace ble_reason {

// Who ended the connection, as far as the reason code can say.
enum By : uint8_t {
  BY_UNKNOWN = 0,
  BY_REMOTE  = 1,   // the peer (phone / client) chose to end it
  BY_LOCAL   = 2,   // this device ended it (see the preceding local-disconnect line)
  BY_LINK    = 3,   // nobody chose: timeout, range, sleep, or a link-layer failure
};

struct Decoded {
  bool        is_hci;   // false = NimBLE host error, `hci` is meaningless
  uint8_t     hci;      // the HCI reason code when is_hci
  const char* name;     // short, grep-able, no spaces
  By          by;
};

inline const char* byName(By by) {
  switch (by) {
    case BY_REMOTE: return "remote";
    case BY_LOCAL:  return "local";
    case BY_LINK:   return "link";
    default:        return "unknown";
  }
}

// Decode a raw HCI disconnect reason (Bluetooth Core Spec Vol 1 Part F).
inline Decoded fromHci(uint8_t code) {
  Decoded d = {true, code, "unknown", BY_UNKNOWN};
  switch (code) {
    case 0x05: d.name = "auth-failure";               d.by = BY_LINK;   break;
    case 0x06: d.name = "pin-or-key-missing";         d.by = BY_LINK;   break;  // stale bond on one side
    case 0x08: d.name = "supervision-timeout";        d.by = BY_LINK;   break;  // range, sleep, radio gone
    case 0x13: d.name = "remote-user-terminated";     d.by = BY_REMOTE; break;  // client closed the link
    case 0x14: d.name = "remote-low-resources";       d.by = BY_REMOTE; break;
    case 0x15: d.name = "remote-power-off";           d.by = BY_REMOTE; break;
    case 0x16: d.name = "local-host-terminated";      d.by = BY_LOCAL;  break;  // we dropped it
    case 0x1A: d.name = "unsupported-remote-feature"; d.by = BY_REMOTE; break;
    case 0x22: d.name = "ll-response-timeout";        d.by = BY_LINK;   break;
    case 0x28: d.name = "instant-passed";             d.by = BY_LINK;   break;
    case 0x3B: d.name = "unacceptable-conn-params";   d.by = BY_LINK;   break;
    case 0x3D: d.name = "mic-failure";                d.by = BY_LINK;   break;  // encryption / stale bond
    case 0x3E: d.name = "conn-failed-to-establish";   d.by = BY_LINK;   break;
    default: break;
  }
  return d;
}

// NimBLE's BLE_HS_ERR_HCI_BASE, restated so this header needs no NimBLE include.
static constexpr int kNimbleHciBase = 0x200;

// Decode the `reason` NimBLE passes to NimBLEServerCallbacks::onDisconnect.
inline Decoded fromNimble(int reason) {
  if (reason >= kNimbleHciBase && reason < kNimbleHciBase + 0x100) {
    return fromHci((uint8_t)(reason - kNimbleHciBase));
  }
  // A host-side error (BLE_HS_E*): not an HCI code. Report it raw, undecoded,
  // rather than mislabel it with the HCI meaning of the same number.
  Decoded d = {false, 0, "host-error", BY_UNKNOWN};
  return d;
}

// Last two bytes of a 6-byte little-endian BLE address (NimBLEAddress::getVal()
// and ble_gap_addr_t::addr are both LSB first), printed MSB first as "..B2:A1".
// Deliberately not the whole address: tester logs are pasted into a public
// repo, and two bytes is enough to tell the phones on one bench apart.
inline void peerSuffix(const uint8_t addr[6], char* out, size_t out_cap) {
  if (!out || out_cap == 0) return;
  if (!addr) {
    snprintf(out, out_cap, "..??:??");
    return;
  }
  snprintf(out, out_cap, "..%02X:%02X", addr[1], addr[0]);
}

}  // namespace ble_reason
