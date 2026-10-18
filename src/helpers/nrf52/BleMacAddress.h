#pragma once

// #1598: the BLE address, read before the SoftDevice is running.
//
// THE PROBLEM. The fresh-flash node name is chosen inside the_mesh.begin(), and BLE starts
// well below that via interface_manager. sd_ble_gap_addr_get() needs the SoftDevice, so at
// naming time the address does not exist yet by the usual route.
//
// THE WAY ROUND IT. On nRF52 the address lives in factory information registers that are
// readable with nothing running at all. helpers/nrf52/EthernetMac.h already reads
// NRF_FICR->DEVICEID[0] for the same class of reason, so this is the house precedent rather
// than a new trick.
//
// WHY THE TWO AGREE. The SoftDevice's default address is a static random one: it takes
// FICR DEVICEADDR and forces the top two bits of the most significant byte to 1. That
// touches bit 47 and bit 46 and nothing else, so every lower byte is identical -- which is
// why the last 5 hex characters, the ones the name is built from, match what a phone shows.
//
// That is a derivation, not a measurement, and #1598 requires it be confirmed on hardware
// rather than trusted. bleMacFromSoftDevice() exists for exactly that: once BLE is up, read
// it the normal way and compare. Do not delete it once the check passes -- it is what makes
// the claim above re-checkable on the next board.

#include <Arduino.h>
#include <stdint.h>

#include "ble_gap.h"   // ble_gap_addr_t / sd_ble_gap_addr_get, as SerialBLEInterface.cpp does

#include <helpers/NodeNameFromMac.h>

namespace offband {

// Both functions return the address in DISPLAY order, most significant byte first -- the
// order a phone shows and the order NodeNameFromMac.h expects. Note that the nRF's own
// representations are the other way round: ble_gap_addr_t::addr is LSB first, which is why
// every print site in this tree indexes addr[5] down to addr[0].

// Read from FICR. Needs no SoftDevice, so this is callable at any point in setup.
//
// Returns false when FICR holds no address, and the test is made on the RAW register pair
// rather than on the bytes written out. That distinction is load-bearing: the static-random
// forcing below ORs 0xC0 into the top byte, so a blank all-zero FICR would leave
// {C0,00,00,00,00,00} -- which is not all-zero, would pass a generic
// bleMacIsUsable() check, and would name every single badge QCC00000. Exactly the
// collision #1598 says must not happen, reintroduced by the forcing itself.
inline bool bleMacFromFicr(uint8_t out[kBleMacBytes]) {
  const uint32_t lo = NRF_FICR->DEVICEADDR[0];          // address bits 31:0
  const uint32_t hi = NRF_FICR->DEVICEADDR[1] & 0xFFFF; // address bits 47:32

  // Blank (never programmed) or erased flash. 0xFFFF.. is what an unprogrammed part reads.
  if (bleRawAddrIsBlank(lo, (uint16_t)hi)) return false;

  // The static-random forcing the SoftDevice applies, reproduced so the two reads can be
  // compared byte for byte instead of "all but the top one".
  const bool is_random = (NRF_FICR->DEVICEADDRTYPE & 1u) != 0;
  uint8_t msb = (uint8_t)((hi >> 8) & 0xFF);
  if (is_random) msb |= 0xC0;

  out[0] = msb;
  out[1] = (uint8_t)(hi & 0xFF);
  out[2] = (uint8_t)((lo >> 24) & 0xFF);
  out[3] = (uint8_t)((lo >> 16) & 0xFF);
  out[4] = (uint8_t)((lo >> 8) & 0xFF);
  out[5] = (uint8_t)(lo & 0xFF);
  return true;
}

// Read the address the SoftDevice is actually advertising. Only valid once BLE has begun;
// returns false before that, so a caller cannot mistake a failed read for an address.
//
// ble_gap.h is the same header SerialBLEInterface.cpp uses for this type; it comes from the
// SoftDevice headers, not from Bluefruit's C++ layer, so including it here pulls in nothing
// that needs BLE to be running.
inline bool bleMacFromSoftDevice(uint8_t out[kBleMacBytes]) {
  ble_gap_addr_t addr;
  if (sd_ble_gap_addr_get(&addr) != NRF_SUCCESS) return false;
  for (size_t i = 0; i < kBleMacBytes; i++) {
    out[i] = addr.addr[kBleMacBytes - 1 - i];  // LSB first -> display order
  }
  return true;
}

}  // namespace offband
