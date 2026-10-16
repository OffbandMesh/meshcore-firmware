#pragma once

// Radio and contact metrics the GUI shows (#1455, PLAN.md §3 B4). The
// SF->demod-floor table is SignalLevel's demodFloorDb (Epic A).

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

// Median SNR of the last kSnrWindow packets heard.
constexpr size_t kSnrWindow = 8;

class SnrMedian {
public:
  void add(float snr_db);  // a non-finite value is ignored
  bool empty() const { return _n == 0; }
  size_t size() const { return _n; }   // values held, up to kSnrWindow
  // The median of what has been added, up to the last kSnrWindow values; an
  // even count averages the middle two. 0 when empty.
  float median() const;

private:
  float _v[kSnrWindow] = {};
  size_t _n = 0;     // values held, up to kSnrWindow
  size_t _next = 0;  // slot the next value overwrites
};

// TX airtime over the last hour, in one-minute buckets, so a duty-cycle
// figure ages out smoothly instead of resetting on the hour.
class AirtimeWindow {
public:
  void addTx(uint32_t now_ms, uint32_t airtime_ms);
  // Airtime sent in the hour ending at now_ms.
  uint32_t lastHourMs(uint32_t now_ms);
  // That as a share of the hour, in tenths of a percent (0..1000).
  uint16_t dutyPermille(uint32_t now_ms);

private:
  void advance(uint32_t now_ms);
  uint32_t _bucket_ms[60] = {};
  uint32_t _minute = 0;  // minute index (now_ms / 60000) of the current bucket
  bool _started = false;
};

// Packets heard by payload type (the 4-bit type in the packet header).
class HeardCounts {
public:
  void add(uint8_t payload_type);
  uint32_t count(uint8_t payload_type) const;
  uint32_t total() const;

private:
  uint32_t _n[16] = {};
};

// When our own advert last went out, and whether it was flooded.
struct LastAdvert {
  bool sent = false;
  bool flood = false;
  uint32_t at_ms = 0;
  void record(uint32_t now_ms, bool flooded) { sent = true; flood = flooded; at_ms = now_ms; }
};

// Per-contact last signal and unread direct messages, keyed by the first 6
// bytes of the contact's public key (the key the phone protocol uses).
struct ContactStat {
  uint8_t key[6];
  bool heard;      // a packet from this contact has been heard since boot
  float snr_db;    // last heard
  int16_t rssi;    // last heard, dBm
  uint16_t unread;
};

class ContactStats {
public:
  // Room for `capacity` contacts. The GUI task calls this once at start with
  // MAX_CONTACTS; deleted contacts are forgotten, so the table can't fill
  // with stale entries.
  bool begin(size_t capacity);
  void onHeard(const uint8_t key[6], float snr_db, int16_t rssi);
  void onDirectMessage(const uint8_t key[6]);
  void markRead(const uint8_t key[6]);
  void forget(const uint8_t key[6]);  // the contact was deleted
  // Null if nothing is known about this contact.
  const ContactStat* find(const uint8_t key[6]) const;
  uint32_t totalUnread() const;
  size_t size() const { return _n; }
  ~ContactStats();

private:
  ContactStat* slot(const uint8_t key[6], bool create);
  ContactStat* _t = nullptr;
  size_t _cap = 0;
  size_t _n = 0;
};

// Everything above, as MyMesh keeps it for the GUI.
struct MetricsHub {
  SnrMedian snr;          // every packet received
  AirtimeWindow airtime;  // every packet sent
  HeardCounts heard;      // every packet received, by payload type
  LastAdvert advert;      // our own adverts
  ContactStats contacts;  // allocated in MyMesh::begin
};

}  // namespace gui
}  // namespace offband
