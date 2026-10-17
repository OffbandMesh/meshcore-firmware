#pragma once

// What the Nodes screens show (#1429, PLAN.md §3 Epic E): the list's
// filters, search, order and row text; node detail's telemetry and trace
// lines; the button-only letter wheel; and the per-node password book.
// Design of record: ObScreen.dc.html and the GUI spec, plus the
// owner-approved gap mockups (epic-e-gap-mockups.html, 2026-10-16).
//
// Pure: no Arduino, no LVGL, no mesh. Text output follows GuiViews' rules:
// NUL-terminated, nothing written when n is 0, cut at a whole character.

#include <stddef.h>
#include <stdint.h>

#include "MsgStore.h"   // ByteSink / ByteSource

namespace offband {
namespace gui {

// ---------------------------------------------------------------- the list

enum class NodeKind : uint8_t { Chat, Repeater, Room, Sensor, Other };
NodeKind nodeKind(uint8_t adv_type);   // ADV_TYPE_* to a kind

// The type chips: All / Chat / Rpt / Room / Sens.
enum class TypeFilter : uint8_t { All, Chat, Repeater, Room, Sensor };

// One node as the list sees it; the caller fills it from a ContactInfo and
// the GUI's per-contact stats.
struct NodeRow {
  char name[32];           // a copy: the row outlives the contact it came from
  const uint8_t* key;      // 32-byte public key, in the contacts table
  NodeKind kind;
  uint8_t out_path_len;    // ContactInfo::out_path_len; 0xFF = no known route
  bool heard;              // a last-heard time is known (the contact's advert time)
  uint32_t heard_age_s;    // seconds since, when heard
  float snr_db;            // last heard
  uint16_t unread;         // unread DMs
  bool snr_known;          // heard since boot, so snr_db is real (bars and SNR show)
};

bool passesFilter(const NodeRow& r, TypeFilter f, bool unread_only);
// Name prefix, ignoring case; or, when the query is all hex digits, a prefix
// of the public key in hex. An empty query matches everything.
bool searchMatches(const NodeRow& r, const char* query);
// The rows that pass, most recently heard first, then the never-heard A-Z.
// Returns the count written to out (at most max).
size_t nodeOrder(const NodeRow* rows, size_t n, TypeFilter f, bool unread_only, const char* query,
                 uint16_t* out, size_t max);

// The header count: "12", or "2 of 12" while a type or Unread filter is on.
void nodeCount(size_t shown, size_t total, bool filtered, char* out, size_t n);
// "direct", "1 hop", "3 hops", or "flood" with no known route.
void hopsText(uint8_t out_path_len, char* out, size_t n);
// "now", "4m", "2h", "3d"; "—" when never heard.
void agoText(bool heard, uint32_t age_s, char* out, size_t n);
// The row's second line: "2 hops · 4m".
void rowSub(const NodeRow& r, char* out, size_t n);
// "+7.5", "-2.5": SNR without the unit, as the row shows it.
void rowSnr(float snr_db, char* out, size_t n);
// The unread badge: "3", "99+".
void badgeText(uint16_t unread, char* out, size_t n);
// Search's line above the results: "2 matches · all types", "1 match · repeaters".
void matchLine(size_t matches, TypeFilter f, char* out, size_t n);
// The empty list: "No nodes heard yet", "No repeaters heard yet", ...
const char* emptyTitle(TypeFilter f);

// ----------------------------------------------------------------- detail

// The detail's first line: "Repeater · 1 hop · +7.5 dB".
void detailKind(NodeKind k, uint8_t out_path_len, bool heard, float snr_db, char* out, size_t n);

// A telemetry cell, key and value as the grid shows them.
struct Cell {
  char key[12];
  char value[16];
};
// The repeater, room or sensor grid from a status reply's body (the phone
// frame's layout: 6-byte key prefix, then the node's stats), plus the round
// trip in ms (0: not known). Battery, Uptime, Airtime, Relayed (flood +
// direct sent), Noise, RTT. Heap is dropped (PLAN.md §9 choice 5). Returns
// the cells written; 0 if the body is too short to be a status reply.
size_t statusCells(const uint8_t* body, size_t len, uint32_t rtt_ms, Cell* out, size_t max);
// The same keys with "—" before any reply.
size_t emptyStatusCells(Cell* out, size_t max);
// "2,318".
void thousands(uint32_t v, char* out, size_t n);

// The per-hop SNRs from a trace reply's body (path_len, flags, tag, auth,
// hashes, SNRs x4, the final SNR x4), as "snr: +6.2 › +4.0 › +7.5". Empty
// if the body doesn't parse.
void traceSnrLine(const uint8_t* body, size_t len, char* out, size_t n);

// ------------------------------------------------------------ letter wheel

// Button-only search (mockup 1): A-Z, 0-9, space, ⌫, Done, round again.
class LetterWheel {
public:
  enum class Pick : uint8_t { Char, Backspace, Done };
  void next();
  void prev();
  Pick pick() const;
  char ch() const;           // the character, for Pick::Char
  const char* label() const; // "A", "0", "space", "⌫", "Done"
  // The label `offset` places away (negative: before), for the strip.
  const char* labelAt(int offset) const;
  void reset() { _i = 0; }

private:
  int _i = 0;
};

// -------------------------------------------------------- password book

// Admin and room passwords remembered per node after a successful login
// (mockup 5). Keyed by the whole 32-byte public key, so no two nodes can
// share an entry; the least recently set entry goes when it's full.
// Persisted through ByteSink / ByteSource with a versioned header, like the
// store. Plain text on internal flash, like the node's own admin password.
constexpr size_t kPasswordSlots = 16;
constexpr size_t kPasswordLen = 16;   // bytes including the terminator
class PasswordBook {
public:
  void set(const uint8_t* key, const char* password);   // a longer password is cut
  const char* get(const uint8_t* key) const;            // null if none
  void forget(const uint8_t* key);
  size_t size() const;
  bool save(ByteSink& out) const;
  LoadResult load(ByteSource& in);

private:
  struct Entry {
    uint8_t key[32];
    uint8_t used;      // 1: holds an entry
    uint8_t pad[3];
    uint32_t stamp;    // larger = more recently set
    char pw[kPasswordLen];
  };
  Entry _e[kPasswordSlots] = {};
  uint32_t _clock = 0;
};

}  // namespace gui
}  // namespace offband
