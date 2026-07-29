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

void SerialBLEInterface::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  uint16_t conn_id = connInfo.getConnHandle();
  uint16_t mtu = pServer->getPeerMTU(conn_id);
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", conn_id, mtu);
  last_conn_id = conn_id;
  if (mtu >= 23) _att_mtu = mtu;   // #453: initial MTU; onMTUChange updates if it grows
}

void SerialBLEInterface::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
  BLE_DEBUG_PRINTLN("onDisconnect(), reason=%d", reason);
  _att_mtu = 23;   // #453: drop stale MTU; next connection re-negotiates
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    // loop() will detect this on next loop, and set deviceConnected to false
  }
}

void SerialBLEInterface::onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onMTUChange(), mtu=%d", MTU);
  if (MTU >= 23) _att_mtu = MTU;   // #453: track negotiated MTU for maxFrameSize()
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
  if (connInfo.isEncrypted() && connInfo.isAuthenticated()) {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
    deviceConnected = true;
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");
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

  NimBLEDevice::getAdvertising()->start();
  adv_restart_time = 0;
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  NimBLEDevice::getAdvertising()->stop();
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
