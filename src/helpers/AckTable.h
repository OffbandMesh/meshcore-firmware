#pragma once

#include <stdint.h>

// Offband #999: reusing a slot in the circular expected-ACK table.
//
// A companion records every DM it is awaiting an ACK for in a fixed table of
// EXPECTED_ACK_TABLE_SIZE entries (8 today). The table is circular and overwrite-oldest
// with NO free-slot search, so the 9th outstanding send reuses slot 0 -- and if that
// message's ACK has not arrived yet, its delivery confirmation is destroyed. processAck()
// then finds no match and pushes no PUSH_CODE_SEND_CONFIRMED, so a message that really was
// delivered shows as unconfirmed forever. On a GUI build the same write also overwrites
// _gui_ack_seq[], orphaning that message's Delivery::Sending state in the store.
//
// The decision is one line at the call site, which is exactly why it lived there and went
// untested. It is pure arithmetic on three numbers, so it belongs here where it can be
// pinned -- the same shape as the other pure helpers in this directory.
//
// DIAGNOSTIC ONLY. Nothing here prevents the eviction, and nothing here should start to:
// late-ACK acceptance depends on entries persisting (#994), and expiring them is the
// "fix" that would break it. Surfacing this to the client is #1000.

namespace offband {

// What reusing a slot costs, and how long the ACK it is about to discard had been waiting.
struct AckEviction {
  bool loses_confirmation;   // the slot still held a pending ACK
  uint32_t age_ms;           // how long it had been pending; 0 when nothing is lost
};

// A slot is PENDING while its ack hash is non-zero. processAck() zeroes the hash when the
// ACK arrives, so zero means "free or already confirmed" and anything else means "still
// waiting". Any non-zero value counts, including 0xFFFFFFFF -- the hash is opaque and no
// value is reserved.
inline bool ackSlotPending(uint32_t slot_ack) { return slot_ack != 0; }

// Age by UNSIGNED subtraction, which is correct across the millis() rollover. getMillis()
// returns unsigned long -- 32-bit on both targets -- so it wraps about every 49.7 days, and
// a slot sent just before the wrap is read just after it. now - sent in unsigned arithmetic
// still yields the true elapsed time there; the same expression in a signed type yields a
// large negative, which as a %lu would print as an absurd age and send a reader hunting a
// clock bug instead of an eviction.
inline uint32_t ackSlotAgeMs(uint32_t now_ms, uint32_t sent_ms) {
  return (uint32_t)(now_ms - sent_ms);
}

// The whole decision the call site makes. age_ms is deliberately 0 when nothing is lost:
// a free slot's msg_sent is stale or never set, so an age computed from it is meaningless
// and must never reach a log line.
inline AckEviction ackEvictionOn(uint32_t slot_ack, uint32_t slot_sent_ms, uint32_t now_ms) {
  AckEviction e;
  e.loses_confirmation = ackSlotPending(slot_ack);
  e.age_ms = e.loses_confirmation ? ackSlotAgeMs(now_ms, slot_sent_ms) : 0u;
  return e;
}

// The next slot in the circular table. Guarded against a non-positive size so a bad build
// configuration cannot produce a negative index into the array.
inline int ackSlotAfter(int idx, int size) {
  if (size <= 0) return 0;
  return (idx + 1) % size;
}

}  // namespace offband
