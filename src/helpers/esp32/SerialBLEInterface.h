#pragma once

#include "../BaseSerialInterface.h"
#include "../BleFrameSizing.h"   // #711: testable ATT frame-sizing arithmetic
#include <NimBLEDevice.h>

// NimBLE merges security callbacks INTO NimBLEServerCallbacks (Bluedroid had
// a separate BLESecurityCallbacks class). Inheritance reflects that merge.
// BLE2902 / BLEUtils includes from the Bluedroid Arduino API have no NimBLE
// equivalent at this level -- NimBLE handles 0x2902 CCCD internally on
// NOTIFY/INDICATE characteristics, so no explicit descriptor add is required.
class SerialBLEInterface : public BaseSerialInterface, NimBLEServerCallbacks, NimBLECharacteristicCallbacks {
  NimBLEServer *pServer;
  NimBLEService *pService;
  NimBLECharacteristic * pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  uint16_t last_conn_id;
  uint16_t _att_mtu;          // #711: EFFECTIVE ATT MTU = min(peer, our own configured MTU)
  uint32_t _pin_code;
  unsigned long _last_write;
  unsigned long adv_restart_time;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  // #178: was 4. Absorb the connect-time command/response burst so the client's
  // uncapped-retry (connect-thrash) never gets a dropped frame and re-storms.
  // Sizes BOTH recv + send queues; nrf52 already uses 12. ~a few KB static .bss.
  #define FRAME_QUEUE_SIZE  12
  int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() { recv_queue_len = 0; send_queue_len = 0; }

protected:
  // Security + connection callbacks (merged into NimBLEServerCallbacks in NimBLE).
  // Bluedroid had a separate BLESecurityCallbacks base; NimBLE folds them into
  // NimBLEServerCallbacks. Mapping from the Bluedroid overrides this class used
  // to inherit:
  //   onPassKeyRequest()                          -> onPassKeyDisplay() (PIN by return)
  //   onPassKeyNotify(uint32_t)                   -> removed (merged into onPassKeyDisplay)
  //   onConfirmPIN(uint32_t)                      -> onConfirmPassKey(NimBLEConnInfo&, uint32_t)
  //   onSecurityRequest()                         -> removed (peripheral handles internally)
  //   onAuthenticationComplete(esp_ble_auth_cmpl_t)
  //                                               -> onAuthenticationComplete(NimBLEConnInfo&)
  //   onConnect(BLEServer*) + (BLEServer*, esp_ble_gatts_cb_param_t*)
  //                                               -> onConnect(NimBLEServer*, NimBLEConnInfo&)
  //   onDisconnect(BLEServer*)                    -> onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason)
  //   onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t*)
  //                                               -> onMTUChange(uint16_t, NimBLEConnInfo&)

  // NimBLEServerCallbacks: connection events
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override;

  // #711: record the EFFECTIVE connection MTU, i.e. min(reported, our own configured
  // local MTU). A peer-reported value alone is not the connection's MTU: ATT settles
  // on the minimum of the two sides. Called from onConnect and onMTUChange.
  void setEffectiveMtu(uint16_t reported);

  // NimBLEServerCallbacks: security/pairing events (merged from BLESecurityCallbacks)
  uint32_t onPassKeyDisplay() override;
  void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override;
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;

  // NimBLECharacteristicCallbacks: characteristic write
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;

public:
  SerialBLEInterface() {
    pServer = NULL;
    pService = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    adv_restart_time = 0;
    _isEnabled = false;
    _last_write = 0;
    last_conn_id = 0;
    _att_mtu = 23;   // #453: BLE minimum until negotiation (onMTUChange) bumps it
    send_queue_len = recv_queue_len = 0;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  // #453/#711: BLE deliverable frame = EFFECTIVE ATT MTU - 3 (ATT notify header),
  // never above MAX_FRAME_SIZE (the frame buffer bound).
  //
  // #711: `_att_mtu` MUST already be the effective (both-sides) MTU. The connection
  // MTU is min(local, peer), and begin() pins our local side to MAX_FRAME_SIZE via
  // NimBLEDevice::setMTU(), so on this transport the deliverable is at most
  // MAX_FRAME_SIZE - 3 and the MAX_FRAME_SIZE ceiling below is unreachable in
  // practice. Caching the PEER's MTU here (which can be far larger, e.g. Android's
  // 517) made this saturate at MAX_FRAME_SIZE and clipped 3 bytes off every full
  // frame -- the #450 bug, reintroduced. See setEffectiveMtu().
  size_t maxFrameSize() const override {
    return ble_frame::deliverableFrame(_att_mtu, MAX_FRAME_SIZE);
  }
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
