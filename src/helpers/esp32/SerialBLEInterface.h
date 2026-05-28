#pragma once

#include "../BaseSerialInterface.h"
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
  uint32_t _pin_code;
  unsigned long _last_write;
  unsigned long adv_restart_time;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  4
  int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() { recv_queue_len = 0; send_queue_len = 0; }

protected:
  // Security callback methods (merged into NimBLEServerCallbacks in NimBLE).
  // TODO(N4): port security callbacks per plan Task N4. NimBLE-Arduino v2.x
  // signatures differ from Bluedroid BLESecurityCallbacks; N4 will finalize
  // the exact override declarations after reading the installed library's
  // NimBLEServerCallbacks header. Likely renames:
  //   onPassKeyRequest()            -> onPassKeyDisplay() returning uint32_t
  //   onPassKeyNotify(uint32_t)     -> consumed via onPassKeyDisplay() return
  //   onConfirmPIN(uint32_t)        -> onConfirmPasskey(NimBLEConnInfo&, uint32_t)
  //   onSecurityRequest()           -> no direct equivalent (handled by setSecurityAuth)
  //   onAuthenticationComplete(esp_ble_auth_cmpl_t)
  //                                 -> onAuthenticationComplete(NimBLEConnInfo&)
  // The .cpp implementations of these are likewise N4 scope. Leaving the
  // declarations out of the header here would prevent compilation of the
  // existing .cpp; N3 + N4 are sequenced to land .cpp changes together.

  // NimBLEServerCallbacks methods
  // TODO(N4): port connection callbacks per plan Task N4. NimBLE-Arduino v2.x
  // signatures are roughly:
  //   onConnect(NimBLEConnInfo& connInfo)
  //   onDisconnect(NimBLEConnInfo& connInfo, int reason)
  //   onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo)
  // The Bluedroid esp_ble_gatts_cb_param_t* parameter has no NimBLE equivalent;
  // conn_id / mtu / peer info are reachable via NimBLEConnInfo accessors.

  // NimBLECharacteristicCallbacks methods
  // TODO(N4): port characteristic write callback per plan Task N4. NimBLE-Arduino
  // v2.x signature is:
  //   onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo)
  // The Bluedroid esp_ble_gatts_cb_param_t* parameter has no NimBLE equivalent.

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
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
