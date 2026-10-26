#pragma once
#include <stdint.h>

// #1830: the pure half of the BLE trace that diag GUI builds compile in
// (OFFBAND_BLE_TRACE) to find why a companion stops advertising (#1828).
// SerialBLEInterface (ESP32) records every NimBLE GAP event and each change of
// the advertising state; this header names what it logs, so a capture reads
// without a spec lookup, and holds the ring that carries records from the
// host task to the main loop.
//
// The numbers restate NimBLE's (ble_gap.h BLE_GAP_EVENT_*, ble_hs.h BLE_HS_E*)
// so this header needs no NimBLE include and is tested natively in
// test/test_ble_trace. SerialBLEInterface.cpp static_asserts each one against
// NimBLE's own define, so a library update that renumbers them fails the build.

namespace ble_trace {

// BLE_GAP_EVENT_* (NimBLE ble_gap.h).
enum GapEvent : uint8_t {
  kConnect = 0,
  kDisconnect = 1,
  kConnUpdate = 3,
  kConnUpdateReq = 4,
  kL2capUpdateReq = 5,
  kTermFailure = 6,
  kAdvComplete = 9,
  kEncChange = 10,
  kPasskeyAction = 11,
  kNotifyRx = 12,
  kNotifyTx = 13,
  kSubscribe = 14,
  kMtu = 15,
  kIdentityResolved = 16,
  kRepeatPairing = 17,
  kPhyUpdateComplete = 18,
  kDataLenChg = 34,
  kLinkEstab = 38,
};

// Trace records that are not GAP events (GAP event types stop well below these).
enum TraceKind : uint8_t {
  kHostReset = 250,   // ble_hs_cfg.reset_cb; status = the reason
  kHostSync = 251,    // ble_hs_cfg.sync_cb, after NimBLE-Arduino's own onSync
};

inline const char* gapEventName(uint8_t type) {
  switch (type) {
    case kHostReset:         return "host-reset";
    case kHostSync:          return "host-sync";
    case kConnect:           return "connect";
    case kDisconnect:        return "disconnect";
    case kConnUpdate:        return "conn-update";
    case kConnUpdateReq:     return "conn-update-req";
    case kL2capUpdateReq:    return "l2cap-update-req";
    case kTermFailure:       return "term-failure";
    case kAdvComplete:       return "adv-complete";
    case kEncChange:         return "enc-change";
    case kPasskeyAction:     return "passkey-action";
    case kNotifyRx:          return "notify-rx";
    case kNotifyTx:          return "notify-tx";
    case kSubscribe:         return "subscribe";
    case kMtu:               return "mtu";
    case kIdentityResolved:  return "identity-resolved";
    case kRepeatPairing:     return "repeat-pairing";
    case kPhyUpdateComplete: return "phy-update";
    case kDataLenChg:        return "data-len";
    case kLinkEstab:         return "link-estab";
    default:                 return "other";
  }
}

// One line per frame sent or received would bury the events that matter,
// so the per-frame notifications are not logged.
inline bool logged(uint8_t type) { return type != kNotifyTx && type != kNotifyRx; }

// BLE_HS_E* (NimBLE ble_hs.h): the host return codes an adv-complete reason,
// a connect status or a host reset carries.
enum HostError : int {
  kOk = 0,
  kENoMem = 6,
  kEOs = 11,
  kEController = 12,
  kETimeout = 13,
  kEDone = 14,
  kETimeoutHci = 19,
  kENoMemEvt = 20,
  kENotSynced = 22,
  kEPreempted = 29,
  kEStalled = 31,
};

// NimBLE ble_gap.h documents the adv-complete reasons: 0 is a connection,
// ETIMEOUT the duration ran out, EPREEMPTED the host stopped it to configure
// a peer's identity. EOS, ECONTROLLER, ETIMEOUT_HCI and ENOTSYNCED are the
// four NimBLE-Arduino treats as a host reset.
inline const char* hostErrorName(int rc) {
  switch (rc) {
    case kOk:          return "ok";
    case kENoMem:      return "no-mem";
    case kEOs:         return "os";
    case kEController: return "controller";
    case kETimeout:    return "timeout";
    case kEDone:       return "done";
    case kETimeoutHci: return "hci-timeout";
    case kENoMemEvt:   return "no-mem-evt";
    case kENotSynced:  return "not-synced";
    case kEPreempted:  return "preempted";
    case kEStalled:    return "stalled";
    default:           return "host-error";
  }
}

// One trace record, captured on the NimBLE host task. Formatting and printing
// happen later on the main loop (EventRing), because the host task's stack is
// small and the UART log mirror busy-waits on every byte: logging there would
// both risk the stack and stall the stack's own timing.
struct Event {
  uint32_t at_ms;
  int32_t status;     // connect/enc/term status, disconnect or adv-complete reason
  uint16_t handle;
  uint16_t value;     // the MTU, for an MTU event
  uint8_t type;       // a GapEvent or a TraceKind
  int8_t host_adv;    // the host's advertising bookkeeping (ble_gap_adv_active), -1 when stale
};

// The host task pushes, the main loop pops. Not synchronized itself: the
// device code holds a critical section around each push and pop. When full, a
// push is dropped and counted, so the log says how much it lost.
template <uint8_t N>
class EventRing {
public:
  bool push(const Event& e) {
    if (_count == N) {
      if (_dropped != UINT16_MAX) _dropped++;
      return false;
    }
    _buf[(_head + _count) % N] = e;
    _count++;
    return true;
  }
  bool pop(Event* out) {
    if (_count == 0) return false;
    *out = _buf[_head];
    _head = (_head + 1) % N;
    _count--;
    return true;
  }
  // The pushes dropped since the last call, then zero.
  uint16_t takeDropped() {
    const uint16_t d = _dropped;
    _dropped = 0;
    return d;
  }
  uint8_t count() const { return _count; }

private:
  Event _buf[N];
  uint8_t _head = 0;
  uint8_t _count = 0;
  uint16_t _dropped = 0;
};

// Reports each change of a sampled on/off state once, and the first sample.
class Transition {
public:
  // True when `now` differs from the last sample, or on the first sample.
  bool update(bool now) {
    const bool changed = !_known || now != _last;
    _known = true;
    _last = now;
    return changed;
  }

private:
  bool _known = false;
  bool _last = false;
};

}  // namespace ble_trace
