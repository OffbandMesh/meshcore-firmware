#include "SerialBLEInterface.h"
#include "esp_mac.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis

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
  NimBLEDevice::setMTU(MAX_FRAME_SIZE);

  // TODO(N4): port security/pairing init per plan Task N4. Bluedroid block was:
  //   BLEDevice::setSecurityCallbacks(this);
  //   BLESecurity sec;
  //   sec.setStaticPIN(pin_code);
  //   sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  // NimBLE equivalent (to be implemented in N4):
  //   NimBLEDevice::setSecurityAuth(/*bonding*/ true, /*mitm*/ true, /*sc*/ true);
  //   NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  //   NimBLEDevice::setSecurityPasskey(pin_code);
  // Security callbacks are merged into NimBLEServerCallbacks (see TODO(N4) in
  // SerialBLEInterface.h); pServer->setCallbacks(this) is also deferred to N4.

  //NimBLEDevice::setPower(ESP_PWR_LVL_N8);

  // Create the BLE Server
  pServer = NimBLEDevice::createServer();
  // TODO(N4): pServer->setCallbacks(this); -- deferred because NimBLEServerCallbacks
  // overrides (onConnect/onDisconnect/onMTUChange) are not yet declared in the
  // header (see TODO(N4) markers in SerialBLEInterface.h).

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
  // TODO(N4): pRxCharacteristic->setCallbacks(this); -- deferred because the
  // NimBLECharacteristicCallbacks::onWrite override is not yet declared in the
  // header (see TODO(N4) markers in SerialBLEInterface.h).

  NimBLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
}

// -------- Security/pairing callbacks (TODO(N4))
//
// The following Bluedroid BLESecurityCallbacks overrides are deferred to N4.
// Their declarations have been removed from SerialBLEInterface.h (replaced
// with TODO(N4) markers); the definitions below are commented out so they
// do not reference Bluedroid types (esp_ble_auth_cmpl_t) that no longer
// compile under the NimBLE stack.
//
// uint32_t SerialBLEInterface::onPassKeyRequest() {
//   BLE_DEBUG_PRINTLN("onPassKeyRequest()");
//   return _pin_code;
// }
//
// void SerialBLEInterface::onPassKeyNotify(uint32_t pass_key) {
//   BLE_DEBUG_PRINTLN("onPassKeyNotify(%u)", pass_key);
// }
//
// bool SerialBLEInterface::onConfirmPIN(uint32_t pass_key) {
//   BLE_DEBUG_PRINTLN("onConfirmPIN(%u)", pass_key);
//   return true;
// }
//
// bool SerialBLEInterface::onSecurityRequest() {
//   BLE_DEBUG_PRINTLN("onSecurityRequest()");
//   return true;  // allow
// }
//
// void SerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
//   if (cmpl.success) {
//     BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
//     deviceConnected = true;
//   } else {
//     BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");
//     pServer->disconnect(pServer->getConnId());
//     adv_restart_time = millis() + ADVERT_RESTART_DELAY;
//   }
// }

// -------- NimBLEServerCallbacks methods (TODO(N4))
//
// onConnect/onDisconnect/onMTUChange overrides deferred to N4 -- their
// declarations are not yet present in SerialBLEInterface.h (see TODO(N4)
// markers in that file). The Bluedroid versions referenced
// esp_ble_gatts_cb_param_t* which has no NimBLE equivalent; the N4 port
// will adopt NimBLEConnInfo& accessors.
//
// void SerialBLEInterface::onConnect(BLEServer* pServer) {
// }
//
// void SerialBLEInterface::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
//   BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", param->connect.conn_id, pServer->getPeerMTU(param->connect.conn_id));
//   last_conn_id = param->connect.conn_id;
// }
//
// void SerialBLEInterface::onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
//   BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", pServer->getPeerMTU(param->mtu.conn_id));
// }
//
// void SerialBLEInterface::onDisconnect(BLEServer* pServer) {
//   BLE_DEBUG_PRINTLN("onDisconnect()");
//   if (_isEnabled) {
//     adv_restart_time = millis() + ADVERT_RESTART_DELAY;
//     // loop() will detect this on next loop, and set deviceConnected to false
//   }
// }

// -------- NimBLECharacteristicCallbacks methods (TODO(N4))
//
// onWrite override deferred to N4 (declaration not yet present in
// SerialBLEInterface.h). N4 will adopt the NimBLE v2.x signature:
//   onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo)
//
// void SerialBLEInterface::onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
//   uint8_t* rxValue = pCharacteristic->getData();
//   int len = pCharacteristic->getLength();
//
//   if (len > MAX_FRAME_SIZE) {
//     BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
//   } else if (recv_queue_len >= FRAME_QUEUE_SIZE) {
//     BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
//   } else {
//     recv_queue[recv_queue_len].len = len;
//     memcpy(recv_queue[recv_queue_len].buf, rxValue, len);
//     recv_queue_len++;
//   }
// }

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

  NimBLEDevice::getAdvertising()->start();
  adv_restart_time = 0;
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  NimBLEDevice::getAdvertising()->stop();
  pServer->disconnect(last_conn_id);
  pService->stop();
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
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      NimBLEDevice::getAdvertising()->start();  // re-Start advertising
    }
    adv_restart_time = 0;
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}
