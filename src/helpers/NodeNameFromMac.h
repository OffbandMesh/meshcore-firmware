#pragma once

// #1598: the name a fresh badge gives itself, derived from its BLE MAC.
//
// WHY A MAC AND NOT THE PUBLIC KEY. The stock fresh-node default is the first 4 bytes of
// the identity pubkey in hex, which is unique but matches nothing the person can see. At a
// conference the name on the badge has to match the device in the phone's pairing list, and
// the pairing list shows the BLE address. Same width as before -- 8 characters -- so
// anything sized against the old default still fits.
//
// WHAT THIS FILE IS. The formatting rule only, with no Arduino and no nRF52 in it, so the
// host tests can walk the edges that matter: a MAC that reads back as zeros, a prefix that
// does not leave room, a buffer that is too small. Reading the MAC is the caller's job and
// is per-platform; see helpers/nrf52/BleMacAddress.h.

#include <stddef.h>
#include <stdint.h>

namespace offband {

// The owner's format: "QCC" + the last 5 hex characters of the BLE MAC, 8 characters total.
constexpr const char* kQccNamePrefix = "QCC";
constexpr size_t kQccNameMacChars = 5;

// BLE addresses are six bytes, twelve hex characters as displayed.
constexpr size_t kBleMacBytes = 6;
constexpr size_t kBleMacHexChars = kBleMacBytes * 2;

// Does this MAC carry any identity at all?
//
// An all-zero or all-ones read is what a failed or unimplemented accessor returns, and it is
// the same on every unit. Naming from it would hand every badge in the room the identical
// name -- strictly worse than the pubkey default it replaced, because that one at least
// differs per device. So this is checked and the caller falls back instead.
inline bool bleMacIsUsable(const uint8_t* mac, size_t mac_len) {
  if (mac == nullptr || mac_len != kBleMacBytes) return false;
  bool all_zero = true, all_ones = true;
  for (size_t i = 0; i < mac_len; i++) {
    if (mac[i] != 0x00) all_zero = false;
    if (mac[i] != 0xFF) all_ones = false;
  }
  return !(all_zero || all_ones);
}

// Is a raw 48-bit device address blank -- never programmed, or erased flash?
//
// This takes the RAW register halves rather than a formatted MAC, and that is the whole
// point of it existing. On nRF52 the address written out has the top two bits of its most
// significant byte forced to 1 for a static random address, so a blank all-zero part
// produces {C0,00,00,00,00,00}: not all-zero, so bleMacIsUsable() above would accept it and
// every badge in the room would be named alike. The forcing defeats the check, so the check
// has to happen before the forcing. Kept here, with the rest of the rules, so it is
// host-testable -- the FICR read itself is not.
inline bool bleRawAddrIsBlank(uint32_t lo32, uint16_t hi16) {
  return (lo32 == 0x00000000u && hi16 == 0x0000u) ||
         (lo32 == 0xFFFFFFFFu && hi16 == 0xFFFFu);
}

// Write `prefix` followed by the last `want_chars` hex characters of `mac` into `out`.
//
// `mac` is in DISPLAY order -- most-significant byte first, the way a phone shows it. That
// contract lives here rather than in the caller because getting it backwards produces a
// plausible-looking name that simply identifies the wrong end of the address, which no test
// of this function could catch on its own. The platform accessor converts; this does not.
//
// Returns false and leaves `out` untouched if the inputs cannot produce the exact name
// asked for. Never truncates: a short buffer is a build-time mistake, and half a name is
// worse than the pubkey default.
inline bool formatNodeNameFromMac(char* out, size_t out_len,
                                  const uint8_t* mac, size_t mac_len,
                                  const char* prefix, size_t want_chars) {
  if (out == nullptr || out_len == 0 || prefix == nullptr) return false;
  if (want_chars == 0 || want_chars > kBleMacHexChars) return false;
  if (!bleMacIsUsable(mac, mac_len)) return false;

  size_t prefix_len = 0;
  while (prefix[prefix_len] != '\0') prefix_len++;
  if (prefix_len + want_chars + 1 > out_len) return false;

  static const char kHex[] = "0123456789ABCDEF";  // uppercase, matching Utils::toHex
  char hex[kBleMacHexChars];
  for (size_t i = 0; i < kBleMacBytes; i++) {
    hex[i * 2] = kHex[(mac[i] >> 4) & 0x0F];
    hex[i * 2 + 1] = kHex[mac[i] & 0x0F];
  }

  size_t w = 0;
  for (size_t i = 0; i < prefix_len; i++) out[w++] = prefix[i];
  for (size_t i = kBleMacHexChars - want_chars; i < kBleMacHexChars; i++) out[w++] = hex[i];
  out[w] = '\0';
  return true;
}

// The badge's own rule, so the two constants are applied in one place rather than at each
// call site.
inline bool formatQccNodeName(char* out, size_t out_len, const uint8_t* mac, size_t mac_len) {
  return formatNodeNameFromMac(out, out_len, mac, mac_len, kQccNamePrefix, kQccNameMacChars);
}

}  // namespace offband
