#include "SerialBLEInterface.h"
#include "esp_mac.h"
#include "../BleReasonStrings.h"   // #1070: shared disconnect-reason decode
#include "../../MeshLog.h"

// #1070: selective BLE diagnostics. Event-level lines only -- connect, MTU,
// auth, disconnect with a decoded reason, advertising state, and a once-per-
// connection queue-full -- so a tester log answers "which device tried, what
// happened, and who dropped whom". Not a frame trace: the per-frame lines stay
// BLE_DEBUG_PRINTLN (compile-gated, Serial only).
//
// MLOG_BOOT so they survive any caplog level, and via mesh_log_line so they
// reach the UART0 wire (#763) even with capture off: the connect that fails is
// exactly the one nobody turned logging on for. These callbacks run on the
// NimBLE host task; mesh_log_line is safe from task context and bounded.
static void blePeer(const NimBLEConnInfo& connInfo, char* out, size_t cap) {
  // Identity address, so a bonded phone keeps the same suffix across its
  // rotating private addresses.
  ble_reason::peerSuffix(connInfo.getIdAddress().getVal(), out, cap);
}

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis

#ifdef OFFBAND_BLE_TRACE
// #1830: a trace of the NimBLE stack itself, for the diag GUI builds, to find
// why a companion stops advertising (#1828). The callbacks above see only a
// completed connect, a disconnect and pairing; this sees every GAP event the
// host raises, a host reset, and the advertising state as it changes.
// NimBLE-Arduino's own logs print through printf, which this console does not
// carry, so the trace goes through mesh_log_line like every [ble] line.
#include "nimble/nimble/host/include/host/ble_hs.h"
#include <esp_heap_caps.h>

// BleTrace.h restates NimBLE's numbers to stay host-testable; hold it to them.
static_assert(ble_trace::kConnect == BLE_GAP_EVENT_CONNECT, "BleTrace.h event numbers");
static_assert(ble_trace::kDisconnect == BLE_GAP_EVENT_DISCONNECT, "BleTrace.h event numbers");
static_assert(ble_trace::kConnUpdate == BLE_GAP_EVENT_CONN_UPDATE, "BleTrace.h event numbers");
static_assert(ble_trace::kConnUpdateReq == BLE_GAP_EVENT_CONN_UPDATE_REQ, "BleTrace.h event numbers");
static_assert(ble_trace::kL2capUpdateReq == BLE_GAP_EVENT_L2CAP_UPDATE_REQ, "BleTrace.h event numbers");
static_assert(ble_trace::kTermFailure == BLE_GAP_EVENT_TERM_FAILURE, "BleTrace.h event numbers");
static_assert(ble_trace::kAdvComplete == BLE_GAP_EVENT_ADV_COMPLETE, "BleTrace.h event numbers");
static_assert(ble_trace::kEncChange == BLE_GAP_EVENT_ENC_CHANGE, "BleTrace.h event numbers");
static_assert(ble_trace::kPasskeyAction == BLE_GAP_EVENT_PASSKEY_ACTION, "BleTrace.h event numbers");
static_assert(ble_trace::kNotifyRx == BLE_GAP_EVENT_NOTIFY_RX, "BleTrace.h event numbers");
static_assert(ble_trace::kNotifyTx == BLE_GAP_EVENT_NOTIFY_TX, "BleTrace.h event numbers");
static_assert(ble_trace::kSubscribe == BLE_GAP_EVENT_SUBSCRIBE, "BleTrace.h event numbers");
static_assert(ble_trace::kMtu == BLE_GAP_EVENT_MTU, "BleTrace.h event numbers");
static_assert(ble_trace::kIdentityResolved == BLE_GAP_EVENT_IDENTITY_RESOLVED, "BleTrace.h event numbers");
static_assert(ble_trace::kRepeatPairing == BLE_GAP_EVENT_REPEAT_PAIRING, "BleTrace.h event numbers");
static_assert(ble_trace::kPhyUpdateComplete == BLE_GAP_EVENT_PHY_UPDATE_COMPLETE, "BleTrace.h event numbers");
static_assert(ble_trace::kDataLenChg == BLE_GAP_EVENT_DATA_LEN_CHG, "BleTrace.h event numbers");
static_assert(ble_trace::kLinkEstab == BLE_GAP_EVENT_LINK_ESTAB, "BleTrace.h event numbers");
static_assert(ble_trace::kENoMem == BLE_HS_ENOMEM, "BleTrace.h host error numbers");
static_assert(ble_trace::kEOs == BLE_HS_EOS, "BleTrace.h host error numbers");
static_assert(ble_trace::kEController == BLE_HS_ECONTROLLER, "BleTrace.h host error numbers");
static_assert(ble_trace::kETimeout == BLE_HS_ETIMEOUT, "BleTrace.h host error numbers");
static_assert(ble_trace::kEDone == BLE_HS_EDONE, "BleTrace.h host error numbers");
static_assert(ble_trace::kETimeoutHci == BLE_HS_ETIMEOUT_HCI, "BleTrace.h host error numbers");
static_assert(ble_trace::kENoMemEvt == BLE_HS_ENOMEM_EVT, "BleTrace.h host error numbers");
static_assert(ble_trace::kENotSynced == BLE_HS_ENOTSYNCED, "BleTrace.h host error numbers");
static_assert(ble_trace::kEPreempted == BLE_HS_EPREEMPTED, "BleTrace.h host error numbers");
static_assert(ble_trace::kEStalled == BLE_HS_ESTALLED, "BleTrace.h host error numbers");

static ble_gap_event_listener s_trace_listener;
static ble_hs_reset_fn* s_prev_reset_cb = nullptr;
static ble_hs_sync_fn* s_prev_sync_cb = nullptr;

// The host task only records; checkRecvFrame() prints on the main loop. The
// host task has ~1.25 KB of stack to spare, and mesh_log_line both formats a
// line on the stack and busy-waits on the UART mirror per byte, which would
// stall the stack's own connection handling: the timing being diagnosed.
static ble_trace::EventRing<24> s_trace_ring;
static portMUX_TYPE s_trace_mux = portMUX_INITIALIZER_UNLOCKED;

static void traceRecord(uint8_t type, int32_t status, uint16_t handle, uint16_t value, int8_t host_adv) {
  ble_trace::Event ev = {(uint32_t)millis(), status, handle, value, type, host_adv};
  portENTER_CRITICAL(&s_trace_mux);
  s_trace_ring.push(ev);
  portEXIT_CRITICAL(&s_trace_mux);
}

// A status or reason: an HCI code (0x200 + code) decoded by name, or a host
// error by name. ATT, L2CAP and SM codes print raw as host-error.
static const char* traceStatus(int rc) {
  if (rc >= ble_reason::kNimbleHciBase) return ble_reason::fromNimble(rc).name;
  return ble_trace::hostErrorName(rc);
}

// Every GAP event the host raises, before NimBLE-Arduino's own handler runs.
// host_adv is the host's advertising bookkeeping (ble_gap_adv_active), not the
// controller's; on adv-complete it is read before the host clears it, so it
// is recorded as unknown there.
static int traceGapEvent(struct ble_gap_event* e, void*) {
  if (!ble_trace::logged(e->type)) return 0;
  const int8_t adv = (int8_t)ble_gap_adv_active();
  switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
      traceRecord(e->type, e->connect.status, e->connect.conn_handle, 0, adv);
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      traceRecord(e->type, e->disconnect.reason, e->disconnect.conn.conn_handle, 0, adv);
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      traceRecord(e->type, e->adv_complete.reason, 0, 0, -1);
      break;
    case BLE_GAP_EVENT_CONN_UPDATE:
      traceRecord(e->type, e->conn_update.status, e->conn_update.conn_handle, 0, adv);
      break;
    case BLE_GAP_EVENT_ENC_CHANGE:
      traceRecord(e->type, e->enc_change.status, e->enc_change.conn_handle, 0, adv);
      break;
    case BLE_GAP_EVENT_TERM_FAILURE:
      traceRecord(e->type, e->term_failure.status, e->term_failure.conn_handle, 0, adv);
      break;
    case BLE_GAP_EVENT_MTU:
      traceRecord(e->type, 0, e->mtu.conn_handle, e->mtu.value, adv);
      break;
    default:
      traceRecord(e->type, 0, 0, 0, adv);
      break;
  }
  return 0;
}

// A host reset tears down every connection and advertising until the host and
// controller sync again. NimBLE-Arduino's own handlers still run, after ours.
// (A reset NimBLE-Arduino detects from an adv-complete reason calls its onReset
// directly, not through here: it shows as that adv-complete.)
static void traceHostReset(int reason) {
  traceRecord(ble_trace::kHostReset, reason, 0, 0, (int8_t)ble_gap_adv_active());
  if (s_prev_reset_cb) s_prev_reset_cb(reason);
}

// Recorded after NimBLE-Arduino's onSync, which restarts advertising on a
// clean sync. host_adv=0 here means it did not: onSync bails out early when
// it cannot set an address.
static void traceHostSync() {
  if (s_prev_sync_cb) s_prev_sync_cb();
  traceRecord(ble_trace::kHostSync, 0, 0, 0, (int8_t)ble_gap_adv_active());
}

static void tracePrint(const ble_trace::Event& ev) {
  const char* name = ble_trace::gapEventName(ev.type);
  switch (ev.type) {
    case BLE_GAP_EVENT_MTU:
      mesh_log_line(MLOG_BOOT, "[ble-trace] t=%lu %s value=%u handle=%u host_adv=%d\n",
                    (unsigned long)ev.at_ms, name, (unsigned)ev.value, (unsigned)ev.handle, ev.host_adv);
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
    case ble_trace::kHostReset:
      mesh_log_line(MLOG_BOOT, "[ble-trace] t=%lu %s reason=%ld (%s) host_adv=%d\n",
                    (unsigned long)ev.at_ms, name, (long)ev.status, traceStatus(ev.status), ev.host_adv);
      break;
    case BLE_GAP_EVENT_CONNECT:
    case BLE_GAP_EVENT_DISCONNECT:
    case BLE_GAP_EVENT_CONN_UPDATE:
    case BLE_GAP_EVENT_ENC_CHANGE:
    case BLE_GAP_EVENT_TERM_FAILURE:
      mesh_log_line(MLOG_BOOT, "[ble-trace] t=%lu %s status=0x%03lX (%s) handle=%u host_adv=%d\n",
                    (unsigned long)ev.at_ms, name, (unsigned long)ev.status, traceStatus(ev.status),
                    (unsigned)ev.handle, ev.host_adv);
      break;
    default:
      mesh_log_line(MLOG_BOOT, "[ble-trace] t=%lu %s type=%u host_adv=%d\n",
                    (unsigned long)ev.at_ms, name, (unsigned)ev.type, ev.host_adv);
      break;
  }
}

// On the main loop: print what the host task recorded, then once a second
// sample the host's advertising state, logging each change, and a heartbeat
// every 60 s so a long silence reads as "still stuck", not "loop dead".
void SerialBLEInterface::traceAdvState() {
  ble_trace::Event ev;
  for (;;) {
    portENTER_CRITICAL(&s_trace_mux);
    const bool got = s_trace_ring.pop(&ev);
    portEXIT_CRITICAL(&s_trace_mux);
    if (!got) break;
    tracePrint(ev);
  }
  portENTER_CRITICAL(&s_trace_mux);
  const uint16_t dropped = s_trace_ring.takeDropped();
  portEXIT_CRITICAL(&s_trace_mux);
  if (dropped) mesh_log_line(MLOG_BOOT, "[ble-trace] dropped=%u (ring full)\n", (unsigned)dropped);

  const unsigned long now = millis();
  if (now - _adv_trace_at < 1000) return;
  _adv_trace_at = now;
  const bool adv = ble_gap_adv_active();
  const bool changed = _adv_trace.update(adv);
  const bool heartbeat = now - _adv_trace_beat >= 60000;
  if (!changed && !heartbeat) return;
  _adv_trace_beat = now;
  mesh_log_line(MLOG_BOOT,
                "[ble-trace] adv-state%s host_adv=%d conns=%u enabled=%d connected=%d synced=%d "
                "restart_pending=%d int_free=%u largest=%u\n",
                changed ? "" : " (60 s)", (int)adv, (unsigned)pServer->getConnectedCount(),
                (int)_isEnabled, (int)deviceConnected, ble_hs_synced(), (int)(adv_restart_time != 0),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
#endif

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", prefix, name);

  // Create the BLE Device
  NimBLEDevice::init(dev_name);
#ifdef OFFBAND_BLE_TRACE
  {
    // #1830: chained, so NimBLE-Arduino's reset and sync handlers still run.
    // Once only: a second begin() would chain the wrappers to themselves.
    if (ble_hs_cfg.reset_cb != traceHostReset) {
      s_prev_reset_cb = ble_hs_cfg.reset_cb;
      s_prev_sync_cb = ble_hs_cfg.sync_cb;
      ble_hs_cfg.reset_cb = traceHostReset;
      ble_hs_cfg.sync_cb = traceHostSync;
    }
    const int rc = ble_gap_event_listener_register(&s_trace_listener, traceGapEvent, nullptr);
    mesh_log_line(MLOG_BOOT, "[ble-trace] on listener_rc=%d synced=%d\n", rc, ble_hs_synced());
  }
#endif
  // #711: setMTU() returns false if the stack refused it, and the return was
  // previously discarded -- a silent failure. It matters because
  // getMTU() then reports NimBLE's DEFAULT preferred MTU (256), not ours, and any
  // sizing that trusts it will oversize. The frame ceiling no longer depends on
  // this succeeding (see BleFrameSizing::deliverableFrame), but a failure here is
  // still a real degradation and must be visible rather than inferred from a
  // tester's truncated download.
  if (!NimBLEDevice::setMTU(MAX_FRAME_SIZE)) {
    BLE_DEBUG_PRINTLN("begin(): setMTU(%d) REFUSED -- getMTU() will report the "
                      "NimBLE default (%d), not ours", (int)MAX_FRAME_SIZE,
                      (int)NimBLEDevice::getMTU());
  }

  // Security/pairing init. Bluedroid's setSecurityCallbacks(this) + BLESecurity
  // setStaticPIN/setAuthenticationMode combo becomes three NimBLE static calls
  // plus a setCallbacks(this) on the server below. NimBLE merges security and
  // server-event callbacks into NimBLEServerCallbacks, so the same `this`
  // pointer receives both groups.
  //   bonding=true, mitm=true, sc=true matches Bluedroid's
  //   ESP_LE_AUTH_REQ_SC_MITM_BOND.
  //   BLE_HS_IO_DISPLAY_ONLY matches Bluedroid's ESP_IO_CAP_OUT (display PIN,
  //   no input on the peripheral).
  NimBLEDevice::setSecurityAuth(/*bonding*/ true, /*mitm*/ true, /*sc*/ true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(pin_code);

  //NimBLEDevice::setPower(ESP_PWR_LVL_N8);

  // Create the BLE Server
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this);

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  // NimBLE folds access permissions into the property bitmask; the encrypted +
  // MITM read requirement (was ESP_GATT_PERM_READ_ENC_MITM via setAccessPermissions)
  // becomes NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN added to
  // the property flags at create time. CCCD (0x2902) is auto-added by NimBLE
  // for NOTIFY characteristics, so no explicit BLE2902 descriptor is needed.
  pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN
  );

  NimBLECharacteristic * pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      NIMBLE_PROPERTY::WRITE |
      NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN
  );
  pRxCharacteristic->setCallbacks(this);

  // Advertise the service UUID + the device name. Bluedroid folded the
  // GAP device name into the advert/scan-response automatically on
  // BLEDevice::init(name); NimBLE does NOT -- the advertising payload is
  // fully explicit. The #288 1:1 port dropped the name, so observer
  // devices advertised nameless (Strycher/LoRa#322). The 128-bit NUS
  // UUID + flags already fills most of the 31-byte main adv packet, so
  // the name must ride in the scan-response. enableScanResponse(true)
  // MUST precede setName(): setName() routes into the scan-response data
  // only when m_scanResp is already true at call time (NimBLE v2.5.0
  // NimBLEAdvertising.cpp::setName), otherwise it targets the full main
  // packet and the name silently fails to fit.
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->enableScanResponse(true);
  pAdvertising->setName(dev_name);
}

// -------- NimBLEServerCallbacks: connection events
//
// NimBLE collapses Bluedroid's two onConnect overloads (no-arg + the
// esp_ble_gatts_cb_param_t* variant that carried conn_id) into a single
// callback that receives a NimBLEConnInfo&. conn_id is reachable via
// connInfo.getConnHandle(); peer MTU via pServer->getPeerMTU(handle).

// #711: the connection's ATT MTU is min(local, peer) -- ATT negotiation settles on the
// smaller of the two sides. begin() pins our local side with NimBLEDevice::setMTU(MAX_FRAME_SIZE),
// so a peer advertising more (Android commonly 517) does NOT raise what this link can carry.
// Storing the peer value unclamped made maxFrameSize() saturate at MAX_FRAME_SIZE and emit
// 176-byte frames onto a 173-byte pipe, silently losing 3 bytes per full frame (#450 regression).
void SerialBLEInterface::setEffectiveMtu(uint16_t reported) {
  uint16_t local = NimBLEDevice::getMTU();      // what begin() configured for our side
  _att_mtu = ble_frame::effectiveMtu(reported, local);
  BLE_DEBUG_PRINTLN("setEffectiveMtu(): reported=%d local=%d -> effective=%d (deliverable frame=%d)",
                    (int)reported, (int)local, (int)_att_mtu,
                    (int)ble_frame::deliverableFrame(_att_mtu, MAX_FRAME_SIZE));
  mesh_log_line(MLOG_BOOT, "[ble] mtu reported=%u local=%u effective=%u frame=%u\n",
                (unsigned)reported, (unsigned)local, (unsigned)_att_mtu,
                (unsigned)ble_frame::deliverableFrame(_att_mtu, MAX_FRAME_SIZE));
}

void SerialBLEInterface::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  uint16_t conn_id = connInfo.getConnHandle();
  uint16_t mtu = pServer->getPeerMTU(conn_id);
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", conn_id, mtu);
  char peer[12];
  blePeer(connInfo, peer, sizeof(peer));
  mesh_log_line(MLOG_BOOT, "[ble] connect peer=%s handle=%u\n", peer, (unsigned)conn_id);
  last_conn_id = conn_id;
  _recv_full_logged = _send_full_logged = false;   // #1070: once per connection
  setEffectiveMtu(mtu);            // #711: clamp to our own configured MTU, never the peer's alone
}

void SerialBLEInterface::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
  BLE_DEBUG_PRINTLN("onDisconnect(), reason=%d", reason);
  {
    char peer[12];
    blePeer(connInfo, peer, sizeof(peer));
    ble_reason::Decoded d = ble_reason::fromNimble(reason);
    // stack_free_min: the NimBLE host task's stack low-water mark, in bytes.
    // These lines format on that task (4096-byte stack, nimconfig.h), so the
    // headroom is measured on the bench rather than assumed. It is a lifetime
    // minimum, so the last callback of a cycle captures the whole cycle.
    mesh_log_line(MLOG_BOOT,
                  "[ble] disconnect peer=%s handle=%u reason=0x%03X (%s) by=%s stack_free_min=%u\n",
                  peer, (unsigned)connInfo.getConnHandle(), (unsigned)reason, d.name,
                  ble_reason::byName(d.by),
                  (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
  }
  _att_mtu = 23;   // #453: drop stale MTU; next connection re-negotiates
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    // loop() will detect this on next loop, and set deviceConnected to false
  }
}

void SerialBLEInterface::onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onMTUChange(), mtu=%d", MTU);
  setEffectiveMtu(MTU);            // #711: same clamp -- a renegotiation cannot exceed our local MTU
}

// -------- NimBLEServerCallbacks: security/pairing events
//
// NimBLE merges what Bluedroid split between BLESecurityCallbacks and
// BLEServerCallbacks. Specific mappings:
//   - onPassKeyRequest (return PIN) + onPassKeyNotify (notify of PIN being
//     displayed) collapse into onPassKeyDisplay() which returns the PIN to
//     show.
//   - onSecurityRequest has no NimBLE equivalent; the peripheral side handles
//     security requests internally via the setSecurityAuth configuration done
//     in begin().
//   - onConfirmPIN(uint32_t) becomes onConfirmPassKey(NimBLEConnInfo&, uint32_t)
//     and is a void return; previously the bool return signalled accept/reject.
//     We continue to accept any value the peer presents (matches prior
//     behavior of returning true unconditionally).
//   - onAuthenticationComplete now takes a NimBLEConnInfo&; success is
//     determined from connInfo.isEncrypted() && connInfo.isAuthenticated()
//     rather than esp_ble_auth_cmpl_t::success.

uint32_t SerialBLEInterface::onPassKeyDisplay() {
  BLE_DEBUG_PRINTLN("onPassKeyDisplay()");
  return _pin_code;
}

void SerialBLEInterface::onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) {
  BLE_DEBUG_PRINTLN("onConfirmPassKey(%u)", pin);
  // Accept the displayed key (matches prior Bluedroid onConfirmPIN -> true).
  NimBLEDevice::injectConfirmPasskey(connInfo, true);
}

void SerialBLEInterface::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
  const bool ok = connInfo.isEncrypted() && connInfo.isAuthenticated();
  {
    char peer[12];
    blePeer(connInfo, peer, sizeof(peer));
    mesh_log_line(MLOG_BOOT,
                  "[ble] auth peer=%s result=%s encrypted=%d authenticated=%d bonded=%d key_size=%u\n",
                  peer, ok ? "ok" : "fail", (int)connInfo.isEncrypted(),
                  (int)connInfo.isAuthenticated(), (int)connInfo.isBonded(),
                  (unsigned)connInfo.getSecKeySize());
  }
  if (ok) {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
    deviceConnected = true;
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");
    // #1070: explains the local-host-terminated (0x216) on the next line.
    mesh_log_line(MLOG_BOOT, "[ble] local-disconnect cause=auth-failed handle=%u\n",
                  (unsigned)connInfo.getConnHandle());
    pServer->disconnect(connInfo.getConnHandle());
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- NimBLECharacteristicCallbacks: characteristic write
//
// NimBLE merges Bluedroid's two onWrite overloads into a single callback that
// receives a NimBLEConnInfo& alongside the characteristic pointer. Written
// bytes come from getValue() (NimBLEAttValue, implicit-convertible to
// std::string / raw pointer); length from getValue().length().

void SerialBLEInterface::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  NimBLEAttValue value = pCharacteristic->getValue();
  size_t len = value.length();
  const uint8_t* rxValue = value.data();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", (int)len);
  } else if (recv_queue_len >= FRAME_QUEUE_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
    if (!_recv_full_logged) {
      _recv_full_logged = true;
      mesh_log_line(MLOG_BOOT, "[ble] queue-full recv depth=%d (once per connection)\n",
                    (int)FRAME_QUEUE_SIZE);
    }
  } else {
    recv_queue[recv_queue_len].len = (uint8_t)len;
    memcpy(recv_queue[recv_queue_len].buf, rxValue, len);
    recv_queue_len++;
  }
}

// ---------- public methods

void SerialBLEInterface::enable() {
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();

  // Start the service
  pService->start();

  // Start advertising

  //NimBLEDevice::getAdvertising()->setMinInterval(500);
  //NimBLEDevice::getAdvertising()->setMaxInterval(1000);

  bool adv_ok = NimBLEDevice::getAdvertising()->start();
  mesh_log_line(MLOG_BOOT, "[ble] adv start cause=enable ok=%d\n", (int)adv_ok);
  adv_restart_time = 0;
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  NimBLEDevice::getAdvertising()->stop();
  mesh_log_line(MLOG_BOOT, "[ble] adv stop cause=disable\n");
  if (pServer->getConnectedCount() > 0) {
    // #1070: explains the local-host-terminated (0x216) that follows.
    mesh_log_line(MLOG_BOOT, "[ble] local-disconnect cause=disabled handle=%u\n",
                  (unsigned)last_conn_id);
  }
  pServer->disconnect(last_conn_id);
  // pService->stop() removed: NimBLE-Arduino v2.x has no NimBLEService::stop().
  // Services live for the BLE stack's lifetime; stop-advertising +
  // disconnect (the two lines above) already accomplishes the conceptual
  // "stop accepting on this service" intent. Surfaced by N6 V3 build (#288).
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      if (!_send_full_logged) {
        _send_full_logged = true;
        mesh_log_line(MLOG_BOOT, "[ble] queue-full send depth=%d (once per connection)\n",
                      (int)FRAME_QUEUE_SIZE);
      }
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   60

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
#ifdef OFFBAND_BLE_TRACE
  traceAdvState();   // first: the returns below skip the rest while frames flow
#endif
  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }

  if (recv_queue_len > 0) {   // check recv queue
    size_t len = recv_queue[0].len;   // take from top of queue
    memcpy(dest, recv_queue[0].buf, len);

    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", len, (uint32_t) dest[0]);

    recv_queue_len--;
    for (int i = 0; i < recv_queue_len; i++) {   // delete top item from queue
      recv_queue[i] = recv_queue[i + 1];
    }
    return len;
  }

  if (pServer->getConnectedCount() == 0)  deviceConnected = false;

  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();

      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");

      //NimBLEDevice::getAdvertising()->setMinInterval(500);
      //NimBLEDevice::getAdvertising()->setMaxInterval(1000);

      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> stopping advertising");
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      // connecting
      // do stuff here on connecting
      NimBLEDevice::getAdvertising()->stop();
      mesh_log_line(MLOG_BOOT, "[ble] adv stop cause=connected\n");
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      bool adv_ok = NimBLEDevice::getAdvertising()->start();  // re-Start advertising
      mesh_log_line(MLOG_BOOT, "[ble] adv restart cause=disconnect ok=%d\n", (int)adv_ok);
    }
    adv_restart_time = 0;
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}
