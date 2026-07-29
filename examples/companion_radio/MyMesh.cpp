#include "MyMesh.h"

#include <Arduino.h> // needed for PlatformIO
#include <Mesh.h>
#include <MeshLog.h>  // #396: serial-capture buffer access for the caplog download

// Offband fork-only companion-API frame codes (config 0xC0 / GPS 0xC1 / block 0xC2)
// + shared enums. Self-contained (only <stdint.h>); included unconditionally so the
// block cap bit and 0xC2 codes resolve on EVERY companion build, not just observer
// ones. The config *backend* (ConfigSchema/ObserverCli) stays observer-gated below. #241.
#include "OffbandConfigProtocol.h"

#ifdef OFFBAND_OBSERVER_BLE_COMPANION
// Plan 3 Task 10 (Strycher/LoRa#272): reserved-slot CLI intercepts +
// system-channel status posting. Build-flag-gated so the upstream
// MyMesh translation unit is unchanged when the observer is not
// compiled in.
#include "helpers/wifi_observer/SystemChannelCli.h"
#endif

#ifdef OFFBAND_OBSERVER
// Strycher/LoRa#325: the observer config CLI must be reachable over
// ANY connection type, not just the BLE _sys channel. cliPassthrough
// is the same allowlist+dispatch surface the _sys channel uses; here
// we feed it plain-text lines typed into the USB serial console so a
// USB-cabled observer can be configured without a phone. Gated on the
// broad OFFBAND_OBSERVER flag (not the BLE-companion flag) so it is
// present on every observer build, including future BLE-disabled ones.
#include "helpers/wifi_observer/CliPassthrough.h"
// Epic F: the config command codes (#160 contract) live in OffbandConfigProtocol.h,
// now included unconditionally above (#241). Its typed dispatch backend (#165):
#include "helpers/wifi_observer/ObserverCli.h"   // dispatchObserverCli + broker enumeration
#include "helpers/config/ConfigDispatch.h"       // #364: role-agnostic config set/get
// wifiObserverPool() lives in WifiObserver.h (heavy transitive includes); forward-
// declare it here as CliPassthrough.cpp does. Still needed by the VIEW passthrough
// and the F3 broker enumeration below; the config set/get path no longer takes a
// pool ref (#364 -- the shared dispatcher must not know about MqttBrokerPool, so
// the observer's provider fetches the pool itself).
namespace offband { MqttBrokerPool& wifiObserverPool(); }
#endif

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2
#define CMD_SEND_CHANNEL_TXT_MSG      3
#define CMD_GET_CONTACTS              4 // with optional 'since' (for efficient sync)
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_ADD_UPDATE_CONTACT        9
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_RESET_PATH                13
#define CMD_SET_ADVERT_LATLON         14
#define CMD_REMOVE_CONTACT            15
#define CMD_SHARE_CONTACT             16
#define CMD_EXPORT_CONTACT            17
#define CMD_IMPORT_CONTACT            18
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20   // was CMD_GET_BATTERY_VOLTAGE
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QUERY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_SEND_RAW_DATA             25
#define CMD_SEND_LOGIN                26
#define CMD_SEND_STATUS_REQ           27
#define CMD_HAS_CONNECTION            28
#define CMD_LOGOUT                    29 // 'Disconnect'
#define CMD_GET_CONTACT_BY_KEY        30
#define CMD_GET_CHANNEL               31
#define CMD_SET_CHANNEL               32
#define CMD_SIGN_START                33
#define CMD_SIGN_DATA                 34
#define CMD_SIGN_FINISH               35
#define CMD_SEND_TRACE_PATH           36
#define CMD_SET_DEVICE_PIN            37
#define CMD_SET_OTHER_PARAMS          38
#define CMD_SEND_TELEMETRY_REQ        39  // can deprecate this
#define CMD_GET_CUSTOM_VARS           40
#define CMD_SET_CUSTOM_VAR            41
#define CMD_GET_ADVERT_PATH           42
#define CMD_GET_TUNING_PARAMS         43
// NOTE: CMD range 44..49 parked, potentially for WiFi operations
#define CMD_SEND_BINARY_REQ           50
#define CMD_FACTORY_RESET             51
#define CMD_SEND_PATH_DISCOVERY_REQ   52
#define CMD_SET_FLOOD_SCOPE_KEY       54   // v8+
#define CMD_SEND_CONTROL_DATA         55   // v8+
#define CMD_GET_STATS                 56   // v8+, second byte is stats type
#define CMD_SEND_ANON_REQ             57
#define CMD_SET_AUTOADD_CONFIG        58
#define CMD_GET_AUTOADD_CONFIG        59
#define CMD_GET_ALLOWED_REPEAT_FREQ   60
#define CMD_SET_PATH_HASH_MODE        61
#define CMD_SEND_CHANNEL_DATA         62
#define CMD_SET_DEFAULT_FLOOD_SCOPE   63
#define CMD_GET_DEFAULT_FLOOD_SCOPE   64
#define CMD_SEND_RAW_PACKET           65

// Offband fork-only frame codes -- 0xC0+ extension space (see OffbandConfigProtocol.h),
// far above upstream's command max (65) so upstream growth never collides. The whole
// pair is companion-available (no observer gate) and is never submitted upstream.
#define CMD_OFFBAND_GPS               0xC1  // request: GPS status query
#define RESP_CODE_OFFBAND_GPS         0xC1  // reply: ASCII "enabled=.. detected=.. fix=.. lat=.. ..."
// #396: Offband fork-only serial-capture download (companion-only, never on the
// mesh). Request 0xC4 -> reply START[total 4B LE] -> CHUNK* -> END, streamed one
// frame per idle main-loop pass (F8 #169 queue-safe). Sub-code in reply byte[1].
// NOTE (#408): 0xC4, NOT 0xC3 -- 0xC3 is CMD_OFFBAND_FEM_LNA (OffbandConfigProtocol.h).
// The 0xC-range allocation map lives in that header; keep new codes in sync there.
#define CMD_OFFBAND_CAPLOG            0xC4  // request: [0xC4, req_sub, args...]
#define RESP_CODE_OFFBAND_CAPLOG      0xC4  // reply:   [0xC4, resp_sub, ...]
// #417: request sub-code in cmd_frame[1] (absent/len==1 => DOWNLOAD, back-compat).
// Control ops let a COMPANION enable/erase/inspect capture (it has no CLI; the
// #395 CommonCLI verbs cover repeater/room-server only).
#define CAPLOG_REQ_DOWNLOAD           0x01  // [0xC4] or [0xC4,0x01]      -> START/CHUNK*/END
#define CAPLOG_REQ_ENABLE             0x02  // [0xC4,0x02,(level)]        -> ACK  (level: MLOG_*; default DEBUG)
#define CAPLOG_REQ_DISABLE            0x03  // [0xC4,0x03]                -> ACK
#define CAPLOG_REQ_ERASE              0x04  // [0xC4,0x04]                -> ACK
#define CAPLOG_REQ_STATUS             0x05  // [0xC4,0x05]                -> STATUS
// response sub-code in out_frame[1]:
#define CAPLOG_SUB_START              0x01  // download: [0xC4,0x01, total_len(4B LE)]
#define CAPLOG_SUB_CHUNK              0x02  // download: [0xC4,0x02, <up to MAX_FRAME_SIZE-2 bytes>]
#define CAPLOG_SUB_END                0x03  // download: [0xC4,0x03]
#define CAPLOG_RESP_ACK               0x10  // control:  [0xC4,0x10, req_op, ok(0|1)]
#define CAPLOG_RESP_STATUS            0x11  // status:   [0xC4,0x11, enabled, level, used(4B LE), cap(4B LE)]

// Stats sub-types for CMD_GET_STATS
#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS             2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2  // first reply to CMD_GET_CONTACTS
#define RESP_CODE_CONTACT             3  // multiple of these (after CMD_GET_CONTACTS)
#define RESP_CODE_END_OF_CONTACTS     4  // last reply to CMD_GET_CONTACTS
#define RESP_CODE_SELF_INFO           5  // reply to CMD_APP_START
#define RESP_CODE_SENT                6  // reply to CMD_SEND_TXT_MSG
#define RESP_CODE_CONTACT_MSG_RECV    7  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CHANNEL_MSG_RECV    8  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CURR_TIME           9  // a reply to CMD_GET_DEVICE_TIME
#define RESP_CODE_NO_MORE_MESSAGES    10 // a reply to CMD_SYNC_NEXT_MESSAGE
#define RESP_CODE_EXPORT_CONTACT      11
#define RESP_CODE_BATT_AND_STORAGE    12 // a reply to a CMD_GET_BATT_AND_STORAGE
#define RESP_CODE_DEVICE_INFO         13 // a reply to CMD_DEVICE_QUERY
#define RESP_CODE_PRIVATE_KEY         14 // a reply to CMD_EXPORT_PRIVATE_KEY
#define RESP_CODE_DISABLED            15
#define RESP_CODE_CONTACT_MSG_RECV_V3 16 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_MSG_RECV_V3 17 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_INFO        18 // a reply to CMD_GET_CHANNEL
#define RESP_CODE_SIGN_START          19
#define RESP_CODE_SIGNATURE           20
#define RESP_CODE_CUSTOM_VARS         21
#define RESP_CODE_ADVERT_PATH         22
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24   // v8+, second byte is stats type
#define RESP_CODE_AUTOADD_CONFIG      25
#define RESP_ALLOWED_REPEAT_FREQ      26
#define RESP_CODE_CHANNEL_DATA_RECV   27
#define RESP_CODE_DEFAULT_FLOOD_SCOPE 28

#define MAX_CHANNEL_DATA_LENGTH       (MAX_FRAME_SIZE - 9)

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250
#define LAZY_CONTACTS_WRITE_DELAY       5000

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

// these are _pushed_ to client app at any time
#define PUSH_CODE_ADVERT                0x80
#define PUSH_CODE_PATH_UPDATED          0x81
#define PUSH_CODE_SEND_CONFIRMED        0x82
#define PUSH_CODE_MSG_WAITING           0x83
#define PUSH_CODE_RAW_DATA              0x84
#define PUSH_CODE_LOGIN_SUCCESS         0x85
#define PUSH_CODE_LOGIN_FAIL            0x86
#define PUSH_CODE_STATUS_RESPONSE       0x87
#define PUSH_CODE_LOG_RX_DATA           0x88
#define PUSH_CODE_TRACE_DATA            0x89
#define PUSH_CODE_NEW_ADVERT            0x8A
#define PUSH_CODE_TELEMETRY_RESPONSE    0x8B
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define PUSH_CODE_PATH_DISCOVERY_RESPONSE 0x8D
#define PUSH_CODE_CONTROL_DATA          0x8E   // v8+
#define PUSH_CODE_CONTACT_DELETED       0x8F // used to notify client app of deleted contact when overwriting oldest
#define PUSH_CODE_CONTACTS_FULL         0x90 // used to notify client app that contacts storage is full

#define ERR_CODE_UNSUPPORTED_CMD        1
#define ERR_CODE_NOT_FOUND              2
#define ERR_CODE_TABLE_FULL             3
#define ERR_CODE_BAD_STATE              4
#define ERR_CODE_FILE_IO_ERROR          5
#define ERR_CODE_ILLEGAL_ARG            6

#define MAX_SIGN_DATA_LEN               (8 * 1024) // 8K

// Auto-add config bitmask
// Bit 0: If set, overwrite oldest non-favourite contact when contacts file is full
// Bits 1-4: these indicate which contact types to auto-add when manual_contact_mode = 0x01
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

void MyMesh::writeOKFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_OK;
  _serial->writeFrame(buf, 1);
}
void MyMesh::writeErrFrame(uint8_t err_code) {
  uint8_t buf[2];
  buf[0] = RESP_CODE_ERR;
  buf[1] = err_code;
  _serial->writeFrame(buf, 2);
}

void MyMesh::writeDisabledFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_DISABLED;
  _serial->writeFrame(buf, 1);
}

void MyMesh::writeContactRespFrame(uint8_t code, const ContactInfo &contact) {
  int i = 0;
  out_frame[i++] = code;
  memcpy(&out_frame[i], contact.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_frame[i++] = contact.type;
  out_frame[i++] = contact.flags;
  out_frame[i++] = contact.out_path_len;
  memcpy(&out_frame[i], contact.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  StrHelper::strzcpy((char *)&out_frame[i], contact.name, 32);
  i += 32;
  memcpy(&out_frame[i], &contact.last_advert_timestamp, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lat, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lon, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.lastmod, 4);
  i += 4;
  _serial->writeFrame(out_frame, i);
}

void MyMesh::updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len) {
  int i = 0;
  uint8_t code = frame[i++]; // eg. CMD_ADD_UPDATE_CONTACT
  memcpy(contact.id.pub_key, &frame[i], PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  contact.type = frame[i++];
  contact.flags = frame[i++];
  contact.out_path_len = frame[i++];
  memcpy(contact.out_path, &frame[i], MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  memcpy(contact.name, &frame[i], 32);
  i += 32;
  memcpy(&contact.last_advert_timestamp, &frame[i], 4);
  i += 4;
  if (len >= i + 8) { // optional fields
    memcpy(&contact.gps_lat, &frame[i], 4);
    i += 4;
    memcpy(&contact.gps_lon, &frame[i], 4);
    i += 4;
    if (len >= i + 4) {
      memcpy(&last_mod, &frame[i], 4);
    }
  }
}

bool MyMesh::Frame::isChannelMsg() const {
  return buf[0] == RESP_CODE_CHANNEL_MSG_RECV || buf[0] == RESP_CODE_CHANNEL_MSG_RECV_V3 ||
         buf[0] == RESP_CODE_CHANNEL_DATA_RECV;
}

void MyMesh::addToOfflineQueue(const uint8_t frame[], int len) {
  if (offline_queue_len >= OFFLINE_QUEUE_SIZE) {
    MESH_DEBUG_PRINTLN("WARN: offline_queue is full!");
    int pos = 0;
    while (pos < offline_queue_len) {
      if (offline_queue[pos].isChannelMsg()) {
        for (int i = pos; i < offline_queue_len - 1; i++) { // delete oldest channel msg from queue
          offline_queue[i] = offline_queue[i + 1];
        }
        MESH_DEBUG_PRINTLN("INFO: removed oldest channel message from queue.");
        offline_queue[offline_queue_len - 1].len = len;
        memcpy(offline_queue[offline_queue_len - 1].buf, frame, len);
        return;
      }
      pos++;
    }
    MESH_DEBUG_PRINTLN("INFO: no channel messages to remove from queue.");
  } else {
    offline_queue[offline_queue_len].len = len;
    memcpy(offline_queue[offline_queue_len].buf, frame, len);
    offline_queue_len++;
  }
}

int MyMesh::getFromOfflineQueue(uint8_t frame[]) {
  if (offline_queue_len > 0) {         // check offline queue
    size_t len = offline_queue[0].len; // take from top of queue
    memcpy(frame, offline_queue[0].buf, len);

    offline_queue_len--;
    for (int i = 0; i < offline_queue_len; i++) { // delete top item from queue
      offline_queue[i] = offline_queue[i + 1];
    }
    return len;
  }
  return 0; // queue is empty
}

float MyMesh::getAirtimeBudgetFactor() const {
  return _prefs.airtime_factor;
}

int MyMesh::getInterferenceThreshold() const {
  return 0; // disabled for now, until currentRSSI() problem is resolved
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.5f);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.2f);
  return getRNG()->nextInt(0, 5*t + 1);
}

uint8_t MyMesh::getExtraAckTransmitCount() const {
  return _prefs.multi_acks;
}

#ifdef OFFBAND_OBSERVER
  #include "helpers/wifi_observer/ObserverPipeline.h"
#endif

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {
    int i = 0;
    out_frame[i++] = PUSH_CODE_LOG_RX_DATA;
    out_frame[i++] = (int8_t)(snr * 4);
    out_frame[i++] = (int8_t)(rssi);
    memcpy(&out_frame[i], raw, len);
    i += len;

    _serial->writeFrame(out_frame, i);
  }

#ifdef OFFBAND_OBSERVER
  // Plan 2 v2 Task 9: tee every raw RX into the observer pipeline.
  // Non-blocking: trampoline -> ring-buffer copy + pool.publishRaw.
  // Pool publishes are MQTT enqueue (non-blocking even if broker is Down).
  offband::observerLogRxTrampoline(snr, rssi, raw, len);
#endif
}

#ifdef OFFBAND_OBSERVER
// Strycher/LoRa#335: tee every PARSED RX packet into the observer pipeline's
// /packets path. Dispatcher::checkRecv calls logRx (Dispatcher.cpp:237) for
// every successfully-parsed packet BEFORE routing, so it is promiscuous (sees
// all heard traffic) and -- unlike logRxRaw -- hands us the parsed mesh::Packet,
// which the /packets JSON needs for route/payload_type/path + the dedupe hash
// CoreScope keys on. RSSI/SNR/airtime are still valid here: same checkRecv
// iteration, no new RX between the radio read and this call. Score is scaled to
// MeshCore's milli convention (matches the serial RX log + processRecvPacket).
void MyMesh::logRx(mesh::Packet* pkt, int len, float score) {
  if (pkt == nullptr) return;
  int   rssi        = (int)_radio->getLastRSSI();
  float snr         = _radio->getLastSNR();
  int   score_milli = (int)(score * 1000.0f);
  int   duration    = (int)_radio->getEstAirtimeFor(len);
  offband::observerLogRxParsedTrampoline(*pkt, rssi, snr, score_milli, duration);
}
#endif

bool MyMesh::isAutoAddEnabled() const {
  return (_prefs.manual_add_contacts & 1) == 0;
}

bool MyMesh::shouldAutoAddContactType(uint8_t contact_type) const {
  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;
  }

  uint8_t type_bit = 0;
  switch (contact_type) {
    case ADV_TYPE_CHAT:
      type_bit = AUTO_ADD_CHAT;
      break;
    case ADV_TYPE_REPEATER:
      type_bit = AUTO_ADD_REPEATER;
      break;
    case ADV_TYPE_ROOM:
      type_bit = AUTO_ADD_ROOM_SERVER;
      break;
    case ADV_TYPE_SENSOR:
      type_bit = AUTO_ADD_SENSOR;
      break;
    default:
      return false;  // Unknown type, don't auto-add
  }

  return (_prefs.autoadd_config & type_bit) != 0;
}

bool MyMesh::shouldOverwriteWhenFull() const {
  return (_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

uint8_t MyMesh::getAutoAddMaxHops() const {
  return _prefs.autoadd_max_hops;
}

void MyMesh::onContactOverwrite(const uint8_t* pub_key) {
    _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE); // delete from storage
  // _serial is NULL during boot-time loadContacts (transport not set up yet);
  // guard it so the overwrite-oldest path during a populated-store load -- e.g.
  // after MAX_CONTACTS is reduced (#42) -- does not NULL-deref.
  if (_serial && _serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    memcpy(&out_frame[1], pub_key, PUB_KEY_SIZE);
    _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
  }
}

void MyMesh::onContactsFull() {
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACTS_FULL;
    _serial->writeFrame(out_frame, 1);
  }
}

void MyMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (_serial->isConnected()) {
    if (is_new) {
      writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact);
    } else {
      out_frame[0] = PUSH_CODE_ADVERT;
      memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
      _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
    }
  } else {
#ifdef DISPLAY_CLASS
    if (_ui) _ui->notify(UIEventType::newContactMessage);
#endif
  }

  // add inbound-path to mem cache
  if (path && mesh::Packet::isValidPathLen(path_len)) {  // check path is valid
    AdvertPath* p = advert_paths;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {   // check if already in table, otherwise evict oldest
      if (memcmp(advert_paths[i].pubkey_prefix, contact.id.pub_key, sizeof(AdvertPath::pubkey_prefix)) == 0) {
        p = &advert_paths[i];   // found
        break;
      }
      if (advert_paths[i].recv_timestamp < oldest) {
        oldest = advert_paths[i].recv_timestamp;
        p = &advert_paths[i];
      }
    }

    memcpy(p->pubkey_prefix, contact.id.pub_key, sizeof(p->pubkey_prefix));
    strcpy(p->name, contact.name);
    p->recv_timestamp = getRTCClock()->getCurrentTime();
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }

  if (!is_new) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); // only schedule lazy write for contacts that are in contacts[]
}

static int sort_by_recent(const void *a, const void *b) {
  return ((AdvertPath *) b)->recv_timestamp - ((AdvertPath *) a)->recv_timestamp;
}

int MyMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  if (max_num > ADVERT_PATH_TABLE_SIZE) max_num = ADVERT_PATH_TABLE_SIZE;
  qsort(advert_paths, ADVERT_PATH_TABLE_SIZE, sizeof(advert_paths[0]), sort_by_recent);

  for (int i = 0; i < max_num; i++) {
    dest[i] = advert_paths[i];
  }
  return max_num;
}

void MyMesh::onContactPathUpdated(const ContactInfo &contact) {
  out_frame[0] = PUSH_CODE_PATH_UPDATED;
  memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE); // NOTE: app may not be connected

  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

ContactInfo*  MyMesh::processAck(const uint8_t *data) {
  // see if matches any in a table
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++) {
    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient
      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
      memcpy(&out_frame[1], data, 4);
      uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;
      memcpy(&out_frame[5], &trip_time, 4);
      _serial->writeFrame(out_frame, 9);

      // NOTE: the same ACK can be received multiple times!
      expected_ack_table[i].ack = 0; // clear expected hash, now that we have received ACK
      return expected_ack_table[i].contact;
    }
  }
  return checkConnectionsAck(data);
}

void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, int extra_len, const char *text) {
  // #241: receive-side user block -- the ONLY enforcement point. Drop a DM from a
  // blocked pubkey before it reaches the app queue/notification. This is the
  // app-push layer (after routing decisions); forwarding, relaying, adverts, and
  // channels are all untouched (contract §11). We only suppress local delivery to
  // the phone, so interoperability with stock MeshCore is unchanged.
  if (_blocks.contains(from.id.pub_key)) return;

  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], from.id.pub_key, 6);
  i += 6; // just 6-byte prefix
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = txt_type;
  memcpy(&out_frame[i], &sender_timestamp, 4);
  i += 4;
  if (extra_len > 0) {
    memcpy(&out_frame[i], extra, extra_len);
    i += extra_len;
  }
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

#ifdef DISPLAY_CLASS
  // we only want to show text messages on display, not cli data
  bool should_display = txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN;
  if (should_display && _ui) {
    _ui->newMsg(path_len, from.name, text, offline_queue_len);
    if (!_serial->isConnected()) {
      _ui->notify(UIEventType::contactMessage);
    }
  }
#endif
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // REVISIT: try to determine which Region (from transport_codes[1]) that Sender is indicating for replies/responses
  //    if unknown, fallback to finding Region from transport_codes[0], the 'scope' used by Sender
  return false;
}

bool MyMesh::allowPacketForward(const mesh::Packet* packet) {
  return _prefs.client_repeat != 0;
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, _prefs.path_hash_mode + 1);
  }
}

void MyMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: dynamic send_scope, depending on recipient and current 'home' Region
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}
void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}

void MyMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                               const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                                 const uint8_t *sender_prefix, const char *text) {
  markConnectionActive(from);
  // from.sync_since change needs to be persisted
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);
}

void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  } else {
#ifdef DISPLAY_CLASS
    if (_ui) _ui->notify(UIEventType::channelMessage);
#endif
  }
#ifdef DISPLAY_CLASS
  // Get the channel name from the channel index
  const char *channel_name = "Unknown";
  ChannelDetails channel_details;
  if (getChannel(channel_idx, channel_details)) {
    channel_name = channel_details.name;
  }
  if (_ui) _ui->newMsg(path_len, channel_name, text, offline_queue_len);
#endif
}

#ifdef OFFBAND_OBSERVER_BLE_COMPANION
// Plan 3 Task 10 (Strycher/LoRa#272): post a status / CLI-reply
// message onto the locked system channel slot. Same frame layout
// as onChannelMessageRecv() so the MeshCore app renders it as a
// normal incoming channel message; the source distinguishes only
// in that it never traversed LoRa (RX SNR field is 0 + path_len
// is 0xFF "direct").
//
// Frame layout (v>=3):
//   [0]  RESP_CODE_CHANNEL_MSG_RECV_V3 (17)
//   [1]  snr*4 as int8_t (0 for synthesized)
//   [2]  reserved1
//   [3]  reserved2
//   [4]  channel_idx (kSystemChannelSlot)
//   [5]  path_len (0xFF = no path, direct)
//   [6]  TXT_TYPE_PLAIN
//   [7..10]  timestamp (uint32 little-endian; getRTCClock seconds)
//   [11..]   text
//
// Pre-v3 (RESP_CODE_CHANNEL_MSG_RECV = 8) drops the snr +
// reserved bytes; layout is the same minus 3 bytes.
void MyMesh::postSystemChannelText(const char* text, size_t text_len) {
  if (text == nullptr || text_len == 0) return;
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = 0;  // snr*4 -- synthesized (no real RX)
    out_frame[i++] = 0;  // reserved1
    out_frame[i++] = 0;  // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }
  out_frame[i++] = offband::kSystemChannelSlot;
  out_frame[i++] = 0xFF;             // path_len: 0xFF = direct
  out_frame[i++] = TXT_TYPE_PLAIN;
  uint32_t ts = (uint32_t)getRTCClock()->getCurrentTime();
  memcpy(&out_frame[i], &ts, 4);
  i += 4;
  // Clamp text to leave room for the header. MAX_FRAME_SIZE is
  // the upstream cap; the per-channel-message payload limit is
  // checked the same way in onChannelMessageRecv above.
  size_t avail = (size_t)MAX_FRAME_SIZE - (size_t)i;
  if (text_len > avail) text_len = avail;
  memcpy(&out_frame[i], text, text_len);
  i += (int)text_len;
  addToOfflineQueue(out_frame, i);

  if (_serial != nullptr && _serial->isConnected()) {
    uint8_t frame[1] = { PUSH_CODE_MSG_WAITING };
    _serial->writeFrame(frame, 1);
  }
}
#endif

void MyMesh::onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                               const uint8_t *data, size_t data_len) {
  if (data_len > MAX_CHANNEL_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("onChannelDataRecv: dropping payload_len=%d exceeds frame limit=%d",
                       (uint32_t)data_len, (uint32_t)MAX_CHANNEL_DATA_LENGTH);
    return;
  }

  int i = 0;
  out_frame[i++] = RESP_CODE_CHANNEL_DATA_RECV;
  out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
  out_frame[i++] = 0; // reserved1
  out_frame[i++] = 0; // reserved2

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = (uint8_t)(data_type & 0xFF);
  out_frame[i++] = (uint8_t)(data_type >> 8);
  out_frame[i++] = (uint8_t)data_len;

  int copy_len = (int)data_len;
  if (copy_len > 0) {
    memcpy(&out_frame[i], data, copy_len);
    i += copy_len;
  }
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
}

uint8_t MyMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                 uint8_t len, uint8_t *reply) {
  if (data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t permissions = 0;
    uint8_t cp = contact.flags >> 1; // LSB used as 'favourite' bit (so only use upper bits)

    if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL) {
      permissions = TELEM_PERM_BASE;
    } else if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS) {
      permissions = cp & TELEM_PERM_BASE;
    }

    if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_LOCATION;
    } else if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_LOCATION;
    }

    if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_ENVIRONMENT;
    } else if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_ENVIRONMENT;
    }

    uint8_t perm_mask = ~(data[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions
    permissions &= perm_mask;

    if (permissions & TELEM_PERM_BASE) { // only respond if base permission bit is set
      telemetry.reset();
      telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      // query other sensors -- target specific
      sensors.querySensors(permissions, telemetry);

      memcpy(reply, &sender_timestamp,
             4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

      uint8_t tlen = telemetry.getSize();
      memcpy(&reply[4], telemetry.getBuffer(), tlen);
      return 4 + tlen;
    }
  }
  return 0; // unknown
}

void MyMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
  uint32_t tag;
  memcpy(&tag, data, 4);

  if (pending_login && memcmp(&pending_login, contact.id.pub_key, 4) == 0) { // check for login response
    // yes, is response to pending sendLogin()
    pending_login = 0;

    int i = 0;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = 0; // legacy: is_admin = false
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6;                                     // pub_key_prefix
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = data[6]; // permissions (eg. is_admin)
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
      memcpy(&out_frame[i], &tag, 4);
      i += 4; // NEW: include server timestamp
      out_frame[i++] = data[7]; // NEW (v7): ACL permissions
      out_frame[i++] = data[12]; // FIRMWARE_VER_LEVEL
    } else {
      out_frame[i++] = PUSH_CODE_LOGIN_FAIL;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
    }
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && // check for status response
             pending_status &&
             memcmp(&pending_status, contact.id.pub_key, 4) == 0 // legacy matching scheme
                                                                 // FUTURE: tag == pending_status
  ) {
    pending_status = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_telemetry) {  // check for matching response tag
    pending_telemetry = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_req) {  // check for matching response tag
    pending_req = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], &tag, 4);   // app needs to match this to RESP_CODE_SENT.tag
    i += 4;
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::onContactPathRecv(ContactInfo& contact, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 4) {
    uint32_t tag;
    memcpy(&tag, extra, 4);

    if (tag == pending_discovery) {  // check for matching response tag)
      pending_discovery = 0;

      if (!mesh::Packet::isValidPathLen(in_path_len) || !mesh::Packet::isValidPathLen(out_path_len)) {
        MESH_DEBUG_PRINTLN("onContactPathRecv, invalid path sizes: %d, %d", in_path_len, out_path_len);
      } else {
        int i = 0;
        out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
        out_frame[i++] = 0; // reserved
        memcpy(&out_frame[i], contact.id.pub_key, 6);
        i += 6; // pub_key_prefix
        out_frame[i++] = out_path_len;
        i += mesh::Packet::writePath(&out_frame[i], out_path, out_path_len);
        out_frame[i++] = in_path_len;
        i += mesh::Packet::writePath(&out_frame[i], in_path, in_path_len);
        // NOTE: telemetry data in 'extra' is discarded at present

        _serial->writeFrame(out_frame, i);
      }
      return false;  // DON'T send reciprocal path!
    }
  }
  // let base class handle received path and data
  return BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void MyMesh::onControlDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_CONTROL_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = packet->path_len;
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), data received while app offline");
  }
}

void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_RAW_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = 0xFF; // reserved (possibly path_len in future)
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), data received while app offline");
  }
}

void MyMesh::onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  if (12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onTraceRecv(), path_len is too long: %d", (uint32_t)path_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_TRACE_DATA;
  out_frame[i++] = 0; // reserved
  out_frame[i++] = path_len;
  out_frame[i++] = flags;
  memcpy(&out_frame[i], &tag, 4);
  i += 4;
  memcpy(&out_frame[i], &auth_code, 4);
  i += 4;
  memcpy(&out_frame[i], path_hashes, path_len);
  i += path_len;

  memcpy(&out_frame[i], path_snrs, path_len >> path_sz);
  i += path_len >> path_sz;
  out_frame[i++] = (int8_t)(packet->getSNR() * 4); // extra/final SNR (to this node)

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onTraceRecv(), data received while app offline");
  }
}

uint32_t MyMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t MyMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t path_hash_count = path_len & 63;
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
          (path_hash_count + 1));
}

void MyMesh::onSendTimeout() {}

MyMesh::MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store, AbstractUITask* ui)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables),
      _serial(NULL), telemetry(MAX_PACKET_PAYLOAD - 4), _store(&store), _ui(ui) {
  _iter_started = false;
  _cli_rescue = false;
#ifdef OFFBAND_OBSERVER
  // Strycher/LoRa#325: init the USB-serial observer-CLI accumulator here
  // (matching this constructor's body-init convention) so _obs_cli_len is
  // guaranteed zero before the first checkObserverSerialCli() call.
  _obs_cli_len = 0;
  _obs_cli_redact = false;
  _obs_cli_buf[0] = 0;
  _ob_stream = OB_STREAM_NONE;   // F8 (#169): no config-response stream in flight
#endif
  _blk_listing = false;   // #241: no block-LIST stream in flight
  _caplog_streaming = false;  // #396: no caplog download in flight
  _caplog_off = 0;
  _caplog_resume = false;
  offline_queue_len = 0;
  app_target_ver = 0;
  clearPendingReqs();
  next_ack_idx = 0;
  sign_data = NULL;
  dirty_contacts_expiry = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  memset(send_scope.key, 0, sizeof(send_scope.key));
  send_unscoped = false;

  // defaults
  memset(&_prefs, 0, sizeof(_prefs));
  _prefs.airtime_factor = 1.0;
  strcpy(_prefs.node_name, "NONAME");
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.gps_enabled = 0;       // GPS disabled by default
  _prefs.gps_interval = 0;      // No automatic GPS updates by default
  //_prefs.rx_delay_base = 10.0f;  enable once new algo fixed
#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default
#endif
#endif

  // #298: external FEM LNA enabled by default, matching the repeater default. Set here
  // (before loadPrefs) so a prefs file written before the field existed short-reads to
  // EOF and leaves this default in place. No-op on boards without a controllable FEM.
  _prefs.radio_fem_rxgain = 1;

  // #428: caplog persistence. Default OFF, DEBUG level. Set BEFORE loadPrefs so a prefs
  // file written before these fields existed short-reads to EOF and leaves caplog OFF
  // (a pre-#428 device must never spuriously auto-capture after an upgrade).
  _prefs.caplog_enabled = 0;
  _prefs.caplog_level = MLOG_DEBUG;
}

void MyMesh::begin(bool has_display) {
  BaseChatMesh::begin();

  if (!_store->loadMainIdentity(self_id)) {
    self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      self_id = radio_new_identity();
      count++;
    }
    _store->saveMainIdentity(self_id);
  }

// if name is provided as a build flag, use that as default node name instead
#ifdef ADVERT_NAME
  strcpy(_prefs.node_name, ADVERT_NAME);
#else
  // use hex of first 4 bytes of identity public key as default node name
  char pub_key_hex[10];
  mesh::Utils::toHex(pub_key_hex, self_id.pub_key, 4);
  strcpy(_prefs.node_name, pub_key_hex);
#endif

  // if build provides default-scope, init with that
#ifdef DEFAULT_FLOOD_SCOPE_NAME
  strcpy(_prefs.default_scope_name, DEFAULT_FLOOD_SCOPE_NAME);
  {
    TransportKeyStore temp;
    TransportKey key;
    temp.getAutoKeyFor(0, "#" DEFAULT_FLOOD_SCOPE_NAME, key);
    memcpy(_prefs.default_scope_key, key.key, sizeof(key.key));
  }
#endif

  // load persisted prefs
  _store->loadPrefs(_prefs, sensors.node_lat, sensors.node_lon);

  // sanitise bad pref values
  _prefs.rx_delay_base = constrain(_prefs.rx_delay_base, 0, 20.0f);
  _prefs.airtime_factor = constrain(_prefs.airtime_factor, 0, 9.0f);
  _prefs.freq = constrain(_prefs.freq, 150.0f, 2500.0f);
  _prefs.bw = constrain(_prefs.bw, 7.8f, 500.0f);
  _prefs.sf = constrain(_prefs.sf, 5, 12);
  _prefs.cr = constrain(_prefs.cr, 5, 8);
  _prefs.tx_power_dbm = constrain(_prefs.tx_power_dbm, -9, MAX_LORA_TX_POWER);
  _prefs.gps_enabled = constrain(_prefs.gps_enabled, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_interval = constrain(_prefs.gps_interval, 0, 86400);  // Max 24 hours

  // #428: restore caplog capture across reboot. Done here -- right after loadPrefs and
  // as early as the persisted flag is knowable -- so the sink is live before the mesh /
  // radio / interface boot diagnostics that follow, and they land in the capture ring.
  // The ring itself is a static global (always allocated); only the enabled flag needs
  // restoring. The pre-reboot ring is gone (plain-RAM non-retained); this captures the
  // FRESH boot log forward, per #428. Level is clamped to a valid MLOG_* band.
  _prefs.caplog_level = constrain(_prefs.caplog_level, MLOG_BOOT, MLOG_PACKET);
  if (_prefs.caplog_enabled) {
    meshLogSetLevel(_prefs.caplog_level);
    meshLogSetEnabled(true);
  }

#ifdef BLE_PIN_CODE // 123456 by default
  if (_prefs.ble_pin == 0) {
#ifdef DISPLAY_CLASS
    if (has_display && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      _active_ble_pin = rng.nextInt(100000, 999999); // random pin each session
    } else {
      _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
    }
#else
    _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
#endif
  } else {
    _active_ble_pin = _prefs.ble_pin;
  }
#else
  _active_ble_pin = 0;
#endif

  resetContacts();
  _store->loadContacts(this);
  loadBlocks();   // #241: restore the persisted block list at boot
  bootstrapRTCfromContacts();
  addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  _store->loadChannels(this);

#ifdef OFFBAND_OBSERVER_BLE_COMPANION
  // Plan 3 Task 10 (Strycher/LoRa#272): provision the locked
  // system CLI channel at slot 40. Idempotent: only persists
  // when the slot actually changed (returns true), to avoid
  // flash wear on every boot.
  if (offband::systemChannelInit(self_id)) {
    saveChannels();
  }
#endif

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");

  // #298: the companion role never applied the external FEM LNA setting -- only
  // simple_repeater did (MyMesh.cpp:973-974) -- so Heltec V4 companions fell
  // through to the class default (lna_enabled=false, LNA bypassed) and ran with
  // degraded RX sensitivity. That is exactly the 1.16-base behavior reported
  // upstream (meshcore-dev/MeshCore#2732). Enable the LNA at boot to match the
  // repeater default.
  //
  // No-op on boards without a controllable FEM: base MainBoard::canControlLoRaFemLna()
  // returns false, and today only HeltecV4Board overrides it -- where the answer is
  // itself runtime-gated by the auto-detected FEM type (KCT8103L vs GC1109), so it
  // is a per-unit capability, not a per-model one.
  //
  // Driven by the persisted radio_fem_rxgain pref (default ON). The user-facing toggle
  // arrives with the #298 wire contract (capability bit + setter + device-info value),
  // which needs a matching client change; until that ships the pref stays at its default.
  if (board.canControlLoRaFemLna()) {
    board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain != 0);
  }
}

const char *MyMesh::getNodeName() {
  return _prefs.node_name;
}
NodePrefs *MyMesh::getNodePrefs() {
  return &_prefs;
}
uint32_t MyMesh::getBLEPin() {
  return _active_ble_pin;
}

struct FreqRange {
  uint32_t lower_freq, upper_freq;
};

static FreqRange repeat_freq_ranges[] = {
  #ifdef ALLOWED_REPEAT_FREQ_RANGE
  ALLOWED_REPEAT_FREQ_RANGE
  #else
  { 433000, 433000 },
  { 869495, 869495 },
  { 918000, 918000 }
  #endif
};

bool MyMesh::isValidClientRepeatFreq(uint32_t f) const {
  for (int i = 0; i < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]); i++) {
    auto r = &repeat_freq_ranges[i];
    if (f >= r->lower_freq && f <= r->upper_freq) return true;
  }
  return false;
}

void MyMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();

#ifdef OFFBAND_OBSERVER_BLE_COMPANION
  // Plan 3 Task 10 (Strycher/LoRa#272): wire the system-channel
  // post callback so SystemChannelCli can push messages into the
  // BLE offline queue without #include'ing MyMesh.h itself. The
  // trampoline keeps SystemChannelCli ignorant of MyMesh's type;
  // the_mesh is the file-scope singleton declared above.
  offband::systemChannelSetPostFunction(
      [](const char* text, size_t text_len) {
        the_mesh.postSystemChannelText(text, text_len);
      });
#endif
}

// #154: companion-API firmware-version string. The standard MeshCore app shows
// this field, so surface Offband over the base: "<offband>-<meshcore>" (e.g.
// "1.0.0-1.16.0"). <offband> is the offband-v* tag core; <meshcore> the upstream
// FIRMWARE_VERSION core (a leading 'v' and any -rc/-dev suffix trimmed). Fits the
// 20-char device-info field. Falls back to bare FIRMWARE_VERSION when no Offband
// tag is available (an untagged build, or a non-Offband upstream build).
static const char* offbandClientVersion() {
  static char buf[20];
  // MeshCore base core: strip a leading 'v', then take up to the first '-'.
  const char* mc = FIRMWARE_VERSION;
  if (*mc == 'v' || *mc == 'V') mc++;
  char mc_core[12];
  size_t m = 0;
  while (mc[m] && mc[m] != '-' && m < sizeof(mc_core) - 1) { mc_core[m] = mc[m]; m++; }
  mc_core[m] = '\0';
#ifdef OFFBAND_VERSION
  // Offband core: chars after "offband-v" up to the first '-' (drops the
  // git-describe -N-g<sha>/-dirty suffix and any -rc tag suffix).
  const char* ob = strstr(OFFBAND_VERSION, "offband-v");
  if (ob != nullptr) {
    ob += 9;  // strlen("offband-v")
    char ob_core[12];
    size_t n = 0;
    while (ob[n] && ob[n] != '-' && n < sizeof(ob_core) - 1) { ob_core[n] = ob[n]; n++; }
    ob_core[n] = '\0';
    if (n > 0) {
      char vers[20];
      snprintf(vers, sizeof(vers), "%s-%s", ob_core, mc_core);
#ifdef OFFBAND_BUILD_TAG
      // #222: a settable build tag lets flag-only variants (same git commit,
      // different compile flags) self-identify in the 20-char app device-info
      // field. Reserve room so the TAG (the variant discriminator) always
      // survives; the version cores truncate first if the field is tight.
      if (OFFBAND_BUILD_TAG[0] != '\0') {
        int tagroom = (int)strlen(OFFBAND_BUILD_TAG) + 1;   // "-<tag>"
        int vmax = (int)sizeof(buf) - 1 - tagroom;
        if (vmax < 0) vmax = 0;
        snprintf(buf, sizeof(buf), "%.*s-%s", vmax, vers, OFFBAND_BUILD_TAG);
        return buf;
      }
#endif
      StrHelper::strzcpy(buf, vers, sizeof(buf));
      return buf;
    }
  }
#endif
  StrHelper::strzcpy(buf, FIRMWARE_VERSION, sizeof(buf));
  return buf;
}

#ifdef OFFBAND_OBSERVER
// Emit one Offband-config scalar reply frame: [0]=RESP code, [1]=sub-type,
// [2..]=NUL-terminated text (truncated to the frame).
void MyMesh::writeOffbandConfigScalar(uint8_t sub, const char* text) {
  out_frame[0] = offband::RESP_CODE_OFFBAND_CONFIG;
  out_frame[1] = sub;
  size_t n = strlen(text);
  const size_t cap = MAX_FRAME_SIZE - 3;   // [0],[1] header + trailing NUL
  if (n > cap) n = cap;
  memcpy(&out_frame[2], text, n);
  out_frame[2 + n] = 0;
  _serial->writeFrame(out_frame, 2 + n + 1);
}

// Offband config command (Epic F / F2, #161). Parse the wire frame and dispatch
// to the typed configSet/configGet (#165) -- NOT the CLI string parser, so the
// CLI grammar never enters the wire path. VIEW (a human text dump) routes to a
// whitelisted read-only dispatchObserverCli selector, chunked. The paginated
// broker GET (OCFG_BROKERS) lands in F3 (#162). Observer-only. Contract:
// OffbandConfigProtocol.h.
void MyMesh::handleOffbandConfigCmd(size_t len) {
  if (len < 2) { writeOffbandConfigScalar(offband::OCFG_R_ERR, "missing sub-type"); return; }
  uint8_t op = cmd_frame[1];
  cmd_frame[len] = 0;                                // NUL-terminate the payload
  char* payload = (char*)&cmd_frame[2];
  char reply[512];

  if (op == offband::OCFG_SET) {
    char* sp = strchr(payload, ' ');                 // split on FIRST space; value = remainder
    if (sp == nullptr) { writeOffbandConfigScalar(offband::OCFG_R_ERR, "set: expected '<key> <value>'"); return; }
    *sp = '\0';
    if (!offband::config::dispatchSet(payload, sp + 1, reply, sizeof(reply))) {
      writeOffbandConfigScalar(offband::OCFG_R_ERR, "unknown config key"); return;
    }
    bool err = (strncmp(reply, "ERROR", sizeof("ERROR") - 1) == 0);
    writeOffbandConfigScalar(err ? offband::OCFG_R_ERR : offband::OCFG_R_ACK, reply);
    return;
  }

  if (op == offband::OCFG_GET) {
    if (!offband::config::dispatchGet(payload, reply, sizeof(reply))) {
      writeOffbandConfigScalar(offband::OCFG_R_ERR, "unknown config key"); return;
    }
    bool err = (strncmp(reply, "ERROR", sizeof("ERROR") - 1) == 0);
    writeOffbandConfigScalar(err ? offband::OCFG_R_ERR : offband::OCFG_R_VALUE, reply);
    return;
  }

  if (op == offband::OCFG_VIEW) {
    // Read-only dumps only -- never a mutating selector through VIEW.
    bool ok = (strcmp(payload, "mqtt status") == 0) ||
              (strncmp(payload, "mqtt view ", 10) == 0) ||
              (strcmp(payload, "wifi status") == 0);
    if (!ok) { writeOffbandConfigScalar(offband::OCFG_R_ERR, "view: allowed: mqtt status | mqtt view <N> | wifi status"); return; }
    // Render into the streaming buffer, send START, then STREAM the chunks one
    // frame per main-loop pass (offbandStreamDrain) -- a synchronous flood would
    // overrun the 4-deep BLE queue and drop the tail incl. END (F8 #169).
    if (!offband::dispatchObserverCli(payload, _ob_buf, sizeof(_ob_buf), offband::wifiObserverPool())) {
      writeOffbandConfigScalar(offband::OCFG_R_ERR, "view: unrecognized selector"); return;
    }
    uint8_t hdr[2] = { offband::RESP_CODE_OFFBAND_CONFIG, offband::OCFG_R_VIEW_START };
    _serial->writeFrame(hdr, 2);
    _ob_off = 0;
    _ob_stream = OB_STREAM_VIEW;
    return;
  }

  if (op == offband::OCFG_BROKERS) {
    // Paginated broker-pool dump: START(count) -> BROKER_KV x N -> END, STREAMED one
    // frame per main-loop pass (offbandStreamDrain). ~30 frames written synchronously
    // would overrun the 4-deep BLE queue and drop the tail incl. END (F8 #169).
    int maxSlots = offband::configBrokerSlotCount();
    uint8_t count = 0;
    for (int s = 0; s < maxSlots; ++s)
      if (offband::configBrokerSlotPopulated((uint8_t)s)) count++;
    uint8_t hdr[3] = { offband::RESP_CODE_OFFBAND_CONFIG, offband::OCFG_R_BROKERS_START, count };
    _serial->writeFrame(hdr, 3);
    _ob_slot   = -1;          // drain advances to the first populated slot
    _ob_buf[0] = '\0';        // empty -> drain renders the next slot before emitting
    _ob_off    = 0;
    _ob_stream = OB_STREAM_BROKERS;
    return;
  }

  writeOffbandConfigScalar(offband::OCFG_R_ERR, "bad config sub-type");
}

// F8 (#169): emit ONE frame of an in-flight multi-frame config response. Called
// from checkSerialInterface once per idle main-loop pass while !isWriteBusy(), so
// the BLE send queue (FRAME_QUEUE_SIZE=4, drops-when-full) drains between frames
// instead of being flooded. Mirrors the ContactsIterator streaming pattern.
void MyMesh::offbandStreamDrain() {
  if (_ob_stream == OB_STREAM_VIEW) {
    if (_ob_buf[_ob_off] == '\0') {                  // text exhausted -> END, done
      uint8_t hdr[2] = { offband::RESP_CODE_OFFBAND_CONFIG, offband::OCFG_R_VIEW_END };
      _serial->writeFrame(hdr, 2);
      _ob_stream = OB_STREAM_NONE;
      return;
    }
    const size_t cap = MAX_FRAME_SIZE - 3;
    size_t remaining = strlen(&_ob_buf[_ob_off]);
    size_t take = remaining < cap ? remaining : cap;
    out_frame[0] = offband::RESP_CODE_OFFBAND_CONFIG;
    out_frame[1] = offband::OCFG_R_VIEW_CHUNK;
    memcpy(&out_frame[2], &_ob_buf[_ob_off], take);
    out_frame[2 + take] = 0;
    _serial->writeFrame(out_frame, 2 + take + 1);
    _ob_off += take;
    return;
  }

  if (_ob_stream == OB_STREAM_BROKERS) {
    // Advance to the next populated slot whenever the current buffer is exhausted.
    while (_ob_buf[_ob_off] == '\0') {
      int maxSlots = offband::configBrokerSlotCount();
      int s = _ob_slot + 1;
      while (s < maxSlots && !offband::configBrokerSlotPopulated((uint8_t)s)) s++;
      if (s >= maxSlots) {                           // all populated slots streamed -> END
        uint8_t hdr[2] = { offband::RESP_CODE_OFFBAND_CONFIG, offband::OCFG_R_BROKERS_END };
        _serial->writeFrame(hdr, 2);
        _ob_stream = OB_STREAM_NONE;
        return;
      }
      _ob_slot = s;
      // #172/#173: pass the live runtime state + the device's default jwt_owner
      // (pubkey hex) so the dump also carries state/last_error + resolved-default
      // hints (additive -- old clients ignore the extra lines).
      const offband::BrokerRuntimeState& _rt =
          offband::wifiObserverPool().broker((uint8_t)s).runtime();
      char _owner_hex[2 * PUB_KEY_SIZE + 1];
      offband::wifiObserverPool().deviceOwnerHex(_owner_hex, sizeof(_owner_hex));
      offband::configRenderBrokerSlot((uint8_t)s, _ob_buf, sizeof(_ob_buf), &_rt, _owner_hex);
      _ob_off = 0;
    }
    // Emit ONE "key=value" line as a BROKER_KV frame for the current slot.
    char* line = &_ob_buf[_ob_off];
    char* nl = strchr(line, '\n');
    size_t linelen = nl ? (size_t)(nl - line) : strlen(line);
    const size_t cap = MAX_FRAME_SIZE - 4;           // [0],[1],[2]=slot + trailing NUL
    if (linelen > cap) linelen = cap;
    if (linelen > 0) {
      out_frame[0] = offband::RESP_CODE_OFFBAND_CONFIG;
      out_frame[1] = offband::OCFG_R_BROKER_KV;
      out_frame[2] = (uint8_t)_ob_slot;
      memcpy(&out_frame[3], line, linelen);
      out_frame[3 + linelen] = 0;
      _serial->writeFrame(out_frame, 3 + linelen + 1);
    }
    _ob_off += nl ? (size_t)(nl - line) + 1 : strlen(line);
    return;
  }
}
#endif  // OFFBAND_OBSERVER

void MyMesh::handleCmdFrame(size_t len) {
#ifdef OFFBAND_OBSERVER
  // Epic F (#161): the Offband config command routes to its own observer-only handler.
  if (cmd_frame[0] == offband::CMD_OFFBAND_CONFIG) {
    handleOffbandConfigCmd(len);
    return;
  }
#endif
  // Offband fork-only GPS status query (companion-available, never upstream). The
  // stock protocol can't poll GPS on demand -- position only ships in SELF_INFO at
  // connect. This returns the live state as ASCII for the client to render. #149.
  if (cmd_frame[0] == CMD_OFFBAND_GPS) {
    out_frame[0] = RESP_CODE_OFFBAND_GPS;
    char* txt = (char*)&out_frame[1];
    const size_t cap = (size_t)MAX_FRAME_SIZE - 2;   // [0] header + trailing NUL
    int n = snprintf(txt, cap, "enabled=%d ", (int)_prefs.gps_enabled);
    if (n < 0 || (size_t)n >= cap) n = 0;
    sensors.getGpsStatusText(txt + n, cap - (size_t)n);
    _serial->writeFrame(out_frame, 1 + strlen(txt) + 1);  // +1: NUL-terminated (Offband convention)
    return;
  }
  // #396/#417: serial-capture (companion-only). cmd_frame[1] selects the op:
  // ENABLE/DISABLE/ERASE/STATUS are single-frame acks; DOWNLOAD (default) freezes
  // the ring and streams START -> CHUNK* -> END one frame per idle pass.
  if (cmd_frame[0] == CMD_OFFBAND_CAPLOG) {
    uint8_t req = (len >= 2) ? cmd_frame[1] : CAPLOG_REQ_DOWNLOAD;

    // STATUS is read-only and always allowed. Every other op either mutates the
    // capture state or starts a stream, so reject it while any streamed response
    // is in flight -- racing caplogDrain() (e.g. an ERASE mid-download) would
    // corrupt the stream or the ring. Client sees RESP_CODE_ERR and can retry.
    if (req != CAPLOG_REQ_STATUS &&
        (_caplog_streaming || _blk_listing || _iter_started
#ifdef OFFBAND_OBSERVER
         || _ob_stream != OB_STREAM_NONE
#endif
        )) {
      out_frame[0] = RESP_CODE_ERR;
      _serial->writeFrame(out_frame, 1);
      return;
    }

    if (req == CAPLOG_REQ_STATUS) {
      uint32_t used = (uint32_t)meshLogBytesUsed();
      uint32_t cap = (uint32_t)meshLogCapacity();
      out_frame[0] = RESP_CODE_OFFBAND_CAPLOG;
      out_frame[1] = CAPLOG_RESP_STATUS;
      out_frame[2] = meshLogIsEnabled() ? 1 : 0;
      out_frame[3] = meshLogGetLevel();
      out_frame[4] = (uint8_t)(used & 0xFF);         // little-endian, explicit
      out_frame[5] = (uint8_t)((used >> 8) & 0xFF);
      out_frame[6] = (uint8_t)((used >> 16) & 0xFF);
      out_frame[7] = (uint8_t)((used >> 24) & 0xFF);
      out_frame[8] = (uint8_t)(cap & 0xFF);
      out_frame[9] = (uint8_t)((cap >> 8) & 0xFF);
      out_frame[10] = (uint8_t)((cap >> 16) & 0xFF);
      out_frame[11] = (uint8_t)((cap >> 24) & 0xFF);
      _serial->writeFrame(out_frame, 12);
      return;
    }
    if (req == CAPLOG_REQ_ENABLE) {
      uint8_t level = (len >= 3) ? cmd_frame[2] : (uint8_t)MLOG_DEBUG;
      meshLogSetLevel(level);
      meshLogSetEnabled(true);
      // #428: persist enabled+level SYNCHRONOUSLY so a client "enable then reboot"
      // sequence can't lose the flag -- by the time the reboot fires it's on disk, and
      // MyMesh::begin() restores capture early on the next boot. Only ENABLE/DISABLE
      // persist; the transient DOWNLOAD freeze (below) must not touch the stored flag.
      _prefs.caplog_enabled = 1;
      _prefs.caplog_level = level;
      savePrefs();
      out_frame[0] = RESP_CODE_OFFBAND_CAPLOG; out_frame[1] = CAPLOG_RESP_ACK;
      out_frame[2] = CAPLOG_REQ_ENABLE; out_frame[3] = 1;
      _serial->writeFrame(out_frame, 4);
      return;
    }
    if (req == CAPLOG_REQ_DISABLE) {
      meshLogSetEnabled(false);
      // #428: clear the persisted flag so capture does NOT auto-resume on the next boot.
      _prefs.caplog_enabled = 0;
      savePrefs();
      out_frame[0] = RESP_CODE_OFFBAND_CAPLOG; out_frame[1] = CAPLOG_RESP_ACK;
      out_frame[2] = CAPLOG_REQ_DISABLE; out_frame[3] = 1;
      _serial->writeFrame(out_frame, 4);
      return;
    }
    if (req == CAPLOG_REQ_ERASE) {
      meshLogClear();
      out_frame[0] = RESP_CODE_OFFBAND_CAPLOG; out_frame[1] = CAPLOG_RESP_ACK;
      out_frame[2] = CAPLOG_REQ_ERASE; out_frame[3] = 1;
      _serial->writeFrame(out_frame, 4);
      return;
    }
    if (req == CAPLOG_REQ_DOWNLOAD) {
      _caplog_resume = meshLogIsEnabled();          // restore this state at END
      meshLogSetEnabled(false);                     // freeze for a clean download
      uint32_t total = (uint32_t)meshLogBytesUsed();
      out_frame[0] = RESP_CODE_OFFBAND_CAPLOG;
      out_frame[1] = CAPLOG_SUB_START;
      out_frame[2] = (uint8_t)(total & 0xFF);
      out_frame[3] = (uint8_t)((total >> 8) & 0xFF);
      out_frame[4] = (uint8_t)((total >> 16) & 0xFF);
      out_frame[5] = (uint8_t)((total >> 24) & 0xFF);
      _serial->writeFrame(out_frame, 6);
      _caplog_off = 0;
      _caplog_streaming = true;
      return;
    }
    // unknown sub-code -> explicit error (never silently fall through to download)
    out_frame[0] = RESP_CODE_ERR;
    _serial->writeFrame(out_frame, 1);
    return;
  }
  // #241: block-list sync (companion-API only; NEVER on the mesh). 0xC2.
  // Receive-side store maintenance: ADD/REMOVE/CLEAR are single-frame acks;
  // LIST streams (START -> one key/frame -> END) via blockListDrain so it never
  // bursts the send queue (drops-when-full, #169). Forwarding/relay untouched (§11).
  if (cmd_frame[0] == offband::CMD_OFFBAND_BLOCK && len >= 2) {
    uint8_t sub = cmd_frame[1];
    if (sub == offband::OFFBAND_BLOCK_ADD && len >= 2 + PUB_KEY_SIZE) {
      bool ok = _blocks.add(&cmd_frame[2]);
      if (ok) saveBlocks();
      out_frame[0] = offband::RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = ok ? 1 : 0;
      _serial->writeFrame(out_frame, 3);
    } else if (sub == offband::OFFBAND_BLOCK_REMOVE && len >= 2 + PUB_KEY_SIZE) {
      bool ok = _blocks.remove(&cmd_frame[2]);
      if (ok) saveBlocks();
      out_frame[0] = offband::RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = ok ? 1 : 0;
      _serial->writeFrame(out_frame, 3);
    } else if (sub == offband::OFFBAND_BLOCK_CLEAR) {
      _blocks.clear(); saveBlocks();
      out_frame[0] = offband::RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub; out_frame[2] = 1;
      _serial->writeFrame(out_frame, 3);
    } else if (sub == offband::OFFBAND_BLOCK_LIST) {
      // START frame [.., 0xFF, count]; keys then stream one-per-idle-pass; then END.
      out_frame[0] = offband::RESP_CODE_OFFBAND_BLOCK; out_frame[1] = sub;
      out_frame[2] = 0xFF; out_frame[3] = _blocks.count();
      _serial->writeFrame(out_frame, 4);
      _blk_list_i = 0;
      _blk_listing = true;
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
    return;
  }
  // #298: external FEM LNA control (companion-API only; NEVER on the mesh -- it changes
  // only this node's own receive front-end, touching no forwarding/relay/advert path).
  // The client emits this only when OFFBAND_CAP_FEM_LNA is set, so the not-capable
  // branch is purely defensive against a mis-gated or stale client.
  if (cmd_frame[0] == offband::CMD_OFFBAND_FEM_LNA && len >= 2) {
    uint8_t sub = cmd_frame[1];
    if (!board.canControlLoRaFemLna()) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    } else if (sub == offband::OFFBAND_FEM_LNA_SET && len >= 3) {
      // Persist ONLY on an actual change: a client slider bound to onChange can emit
      // a burst of SETs, and an unconditional savePrefs() would put that burst
      // straight onto flash. The hardware apply stays unconditional so a pref that
      // already matches still re-asserts the FEM state (cheap, and self-healing).
      uint8_t new_val = cmd_frame[2] ? 1 : 0;           // normalise: any non-zero = on
      if (_prefs.radio_fem_rxgain != new_val) {
        _prefs.radio_fem_rxgain = new_val;
        savePrefs();                                    // survives reboot, like the CLI path
      }
      board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain != 0);
      // Reply the POST-APPLY hardware state rather than echoing the request, so a set
      // the FEM refuses shows the client the truth instead of a silent lie.
      out_frame[0] = offband::RESP_CODE_OFFBAND_FEM_LNA; out_frame[1] = sub;
      out_frame[2] = board.isLoRaFemLnaEnabled() ? 1 : 0;
      _serial->writeFrame(out_frame, 3);
    } else if (sub == offband::OFFBAND_FEM_LNA_GET) {
      out_frame[0] = offband::RESP_CODE_OFFBAND_FEM_LNA; out_frame[1] = sub;
      out_frame[2] = board.isLoRaFemLnaEnabled() ? 1 : 0;
      _serial->writeFrame(out_frame, 3);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
    return;
  }
  if (cmd_frame[0] == CMD_DEVICE_QUERY && len >= 2) { // sent when app establishes connection
    app_target_ver = cmd_frame[1];                    // which version of protocol does app understand

    int i = 0;
    out_frame[i++] = RESP_CODE_DEVICE_INFO;
    out_frame[i++] = FIRMWARE_VER_CODE;
    out_frame[i++] = MAX_CONTACTS / 2;   // v3+
    out_frame[i++] = MAX_GROUP_CHANNELS; // v3+
    memcpy(&out_frame[i], &_prefs.ble_pin, 4);
    i += 4;
    memset(&out_frame[i], 0, 12);
    strcpy((char *)&out_frame[i], FIRMWARE_BUILD_DATE);
    i += 12;
    StrHelper::strzcpy((char *)&out_frame[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char *)&out_frame[i], offbandClientVersion(), 20);  // #154
    i += 20;
    out_frame[i++] = _prefs.client_repeat;   // v9+
    out_frame[i++] = _prefs.path_hash_mode;  // v10+
    // F4 (#163): Offband capability byte. bit0 = WIFI_OBSERVER_SUPPORT -- the
    // config command's backend (wifi_observer) is compiled in. Additive: pre-v14
    // clients read a shorter frame and never see it; v14+ clients gate the Observer
    // config category on this bit (version code alone can't tell observer-in from
    // observer-out builds). 0 on non-observer node types -> no config at all.
    uint8_t offband_caps = 0;
#ifdef OFFBAND_OBSERVER
    offband_caps |= offband::OFFBAND_CAP_WIFI_OBSERVER;
#endif
    offband_caps |= offband::OFFBAND_CAP_BLOCK;  // #241: block list always present on the companion
    // #298: FEM LNA control is PER-UNIT, not per-model -- on Heltec V4 it depends on
    // which FEM chip the runtime probe found, so two V4s can legitimately differ.
    // Deriving the bit from the board keeps the client off model/version guessing.
    if (board.canControlLoRaFemLna()) offband_caps |= offband::OFFBAND_CAP_FEM_LNA;
    offband_caps |= offband::OFFBAND_CAP_CAPLOG;  // #427: caplog is always compiled into the companion
    out_frame[i++] = offband_caps;           // v14+
    // #298: current FEM LNA state (v16+), so the client renders the toggle on connect
    // without a 0xC3 GET round trip. Appended UNCONDITIONALLY -- the frame layout must
    // stay fixed for a given version code; the cap bit above, not this byte's presence,
    // is what tells the client whether to show the control. Reads 0 when not capable.
    out_frame[i++] = board.isLoRaFemLnaEnabled() ? 1 : 0;   // v16+
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_APP_START &&
             len >= 8) { // sent when app establishes connection, respond with node ID
    //  cmd_frame[1..7]  reserved future
    char *app_name = (char *)&cmd_frame[8];
    cmd_frame[len] = 0; // make app_name null terminated
    MESH_DEBUG_PRINTLN("App %s connected", app_name);

    // #178: a reconnect (the client's connect-thrash) can land here mid-stream.
    // Emit the in-flight stream's terminator BEFORE clearing it, so the client's
    // contact / settings read completes instead of hanging forever on an END that
    // never comes. Guarded -> a normal first connect (no stream in flight) is a
    // no-op. Best-effort: if the BLE send queue is full the terminator may drop
    // (no worse than today). Terminator byte sequences mirror the stream's own EOF
    // paths (END_OF_CONTACTS line ~2593, VIEW_END/BROKERS_END in offbandStreamDrain).
    if (_iter_started) {
      uint8_t end[5];
      end[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&end[1], &_most_recent_lastmod, 4);
      _serial->writeFrame(end, 5);
      MESH_DEBUG_PRINTLN("APP_START mid-stream: sent END_OF_CONTACTS terminator (#178)");
    }
    _iter_started = false; // stop any left-over ContactsIterator
    if (_blk_listing) {    // #241: terminate an in-flight block-LIST for the (re)connecting client (#178 pattern)
      uint8_t h[3] = { offband::RESP_CODE_OFFBAND_BLOCK, offband::OFFBAND_BLOCK_LIST, 0xFE };
      _serial->writeFrame(h, 3);
      _blk_listing = false;
      MESH_DEBUG_PRINTLN("APP_START mid-stream: sent block-LIST END terminator (#241)");
    }
    if (_caplog_streaming) {  // #396: terminate an in-flight caplog download for the (re)connecting client
      uint8_t h[2] = { RESP_CODE_OFFBAND_CAPLOG, CAPLOG_SUB_END };
      _serial->writeFrame(h, 2);
      _caplog_streaming = false;
      meshLogSetEnabled(_caplog_resume);   // resume capture if it was on
      MESH_DEBUG_PRINTLN("APP_START mid-stream: sent caplog END terminator (#396)");
    }
#ifdef OFFBAND_OBSERVER
    if (_ob_stream == OB_STREAM_VIEW) {
      uint8_t h[2] = { offband::RESP_CODE_OFFBAND_CONFIG, offband::OCFG_R_VIEW_END };
      _serial->writeFrame(h, 2);
      MESH_DEBUG_PRINTLN("APP_START mid-stream: sent VIEW_END terminator (#178)");
    } else if (_ob_stream == OB_STREAM_BROKERS) {
      uint8_t h[2] = { offband::RESP_CODE_OFFBAND_CONFIG, offband::OCFG_R_BROKERS_END };
      _serial->writeFrame(h, 2);
      MESH_DEBUG_PRINTLN("APP_START mid-stream: sent BROKERS_END terminator (#178)");
    }
    _ob_stream = OB_STREAM_NONE; // F8 (#169): drop any in-flight config-response stream
#endif
    int i = 0;
    out_frame[i++] = RESP_CODE_SELF_INFO;
    out_frame[i++] = ADV_TYPE_CHAT; // what this node Advert identifies as (maybe node's pronouns too?? :-)
    out_frame[i++] = _prefs.tx_power_dbm;
    out_frame[i++] = MAX_LORA_TX_POWER;
    memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat, lon;
    lat = (sensors.node_lat * 1000000.0);
    lon = (sensors.node_lon * 1000000.0);
    memcpy(&out_frame[i], &lat, 4);
    i += 4;
    memcpy(&out_frame[i], &lon, 4);
    i += 4;
    out_frame[i++] = _prefs.multi_acks; // new v7+
    out_frame[i++] = _prefs.advert_loc_policy;
    out_frame[i++] = (_prefs.telemetry_mode_env << 4) | (_prefs.telemetry_mode_loc << 2) |
                     (_prefs.telemetry_mode_base); // v5+
    out_frame[i++] = _prefs.manual_add_contacts;

    uint32_t freq = _prefs.freq * 1000;
    memcpy(&out_frame[i], &freq, 4);
    i += 4;
    uint32_t bw = _prefs.bw * 1000;
    memcpy(&out_frame[i], &bw, 4);
    i += 4;
    out_frame[i++] = _prefs.sf;
    out_frame[i++] = _prefs.cr;

    int tlen = strlen(_prefs.node_name); // revisit: UTF_8 ??
    memcpy(&out_frame[i], _prefs.node_name, tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_TXT_MSG && len >= 14) {
    int i = 1;
    uint8_t txt_type = cmd_frame[i++];
    uint8_t attempt = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    uint8_t *pub_key_prefix = &cmd_frame[i];
    i += 6;
    ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);
    if (recipient && (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_CLI_DATA)) {
      char *text = (char *)&cmd_frame[i];
      int tlen = len - i;
      uint32_t est_timeout;
      text[tlen] = 0; // ensure null
      int result;
      uint32_t expected_ack;
      if (txt_type == TXT_TYPE_CLI_DATA) {
        msg_timestamp = getRTCClock()->getCurrentTimeUnique(); // Use node's RTC instead of app timestamp to avoid tripping replay protection
        result = sendCommandData(*recipient, msg_timestamp, attempt, text, est_timeout);
        expected_ack = 0; // no Ack expected
      } else {
        result = sendMessage(*recipient, msg_timestamp, attempt, text, expected_ack, est_timeout);
      }
      // TODO: add expected ACK to table
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        if (expected_ack) {
          expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis(); // add to circular table
          expected_ack_table[next_ack_idx].ack = expected_ack;
          expected_ack_table[next_ack_idx].contact = recipient;
          next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
        }

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &expected_ack, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(recipient == NULL
                        ? ERR_CODE_NOT_FOUND
                        : ERR_CODE_UNSUPPORTED_CMD); // unknown recipient, or unsupported TXT_TYPE_*
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_TXT_MSG) { // send GroupChannel text msg
    int i = 1;
    uint8_t txt_type = cmd_frame[i++]; // should be TXT_TYPE_PLAIN
    uint8_t channel_idx = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    const char *text = (char *)&cmd_frame[i];

#ifdef OFFBAND_OBSERVER_BLE_COMPANION
    // Plan 3 Task 10 intercept (Strycher/LoRa#272): slot 40 is
    // the system CLI channel. Route the message into the local
    // CLI passthrough instead of broadcasting it over LoRa.
    // Reply is enqueued and posted back on the same slot via
    // RESP_CODE_CHANNEL_MSG_RECV_V3 from systemChannelDrain().
    if (channel_idx == offband::kSystemChannelSlot &&
        txt_type == TXT_TYPE_PLAIN) {
      size_t text_len = (len > (size_t)i) ? (len - (size_t)i) : 0;
      if (offband::systemChannelInterceptMsg(text, text_len)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_BAD_STATE);
      }
      return;
    }
#endif

    if (txt_type != TXT_TYPE_PLAIN) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    } else {
      ChannelDetails channel;
      bool success = getChannel(channel_idx, channel);
      if (success && sendGroupMessage(msg_timestamp, channel.channel, _prefs.node_name, text, len - i)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
      }
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_DATA) { // send GroupChannel datagram
    if (len < 4) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }
    int i = 1;
    uint8_t channel_idx = cmd_frame[i++];
    uint8_t path_len = cmd_frame[i++];

    // validate path len, allowing 0xFF for flood
    if (!mesh::Packet::isValidPathLen(path_len) && path_len != OUT_PATH_UNKNOWN) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA invalid path size: %d", path_len);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }

    // parse provided path if not flood
    uint8_t path[MAX_PATH_SIZE];
    if (path_len != OUT_PATH_UNKNOWN) {
      i += mesh::Packet::writePath(path, &cmd_frame[i], path_len);
    }

    uint16_t data_type = ((uint16_t)cmd_frame[i]) | (((uint16_t)cmd_frame[i + 1]) << 8);
    i += 2;
    const uint8_t *payload = &cmd_frame[i];
    int payload_len = (len > (size_t)i) ? (int)(len - i) : 0;

    ChannelDetails channel;
    if (!getChannel(channel_idx, channel)) {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    } else if (data_type == DATA_TYPE_RESERVED) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (payload_len > MAX_CHANNEL_DATA_LENGTH) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA payload too long: %d > %d", payload_len, MAX_CHANNEL_DATA_LENGTH);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (sendGroupData(channel.channel, path, path_len, data_type, payload, payload_len)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACTS) { // get Contact list
    if (_iter_started) {
      writeErrFrame(ERR_CODE_BAD_STATE); // iterator is currently busy
    } else {
      if (len >= 5) { // has optional 'since' param
        memcpy(&_iter_filter_since, &cmd_frame[1], 4);
      } else {
        _iter_filter_since = 0;
      }

      uint8_t reply[5];
      reply[0] = RESP_CODE_CONTACTS_START;
      uint32_t count = getNumContacts(); // total, NOT filtered count
      memcpy(&reply[1], &count, 4);
      _serial->writeFrame(reply, 5);

      // start iterator
      _iter = startContactsIterator();
      _iter_started = true;
      _most_recent_lastmod = 0;
    }
  } else if (cmd_frame[0] == CMD_SET_ADVERT_NAME && len >= 2) {
    int nlen = len - 1;
    if (nlen > sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1; // max len
    memcpy(_prefs.node_name, &cmd_frame[1], nlen);
    _prefs.node_name[nlen] = 0; // null terminator
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_ADVERT_LATLON && len >= 9) {
    int32_t lat, lon, alt = 0;
    memcpy(&lat, &cmd_frame[1], 4);
    memcpy(&lon, &cmd_frame[5], 4);
    if (len >= 13) {
      memcpy(&alt, &cmd_frame[9], 4); // for FUTURE support
    }
    if (lat <= 90 * 1E6 && lat >= -90 * 1E6 && lon <= 180 * 1E6 && lon >= -180 * 1E6) {
      sensors.node_lat = ((double)lat) / 1000000.0;
      sensors.node_lon = ((double)lon) / 1000000.0;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid geo coordinate
    }
  } else if (cmd_frame[0] == CMD_GET_DEVICE_TIME) {
    uint8_t reply[5];
    reply[0] = RESP_CODE_CURR_TIME;
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply[1], &now, 4);
    _serial->writeFrame(reply, 5);
  } else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t secs;
    memcpy(&secs, &cmd_frame[1], 4);
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (secs >= curr) {
      getRTCClock()->setCurrentTime(secs);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SEND_SELF_ADVERT) {
    mesh::Packet* pkt;
    if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
      pkt = createSelfAdvert(_prefs.node_name);
    } else {
      pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
    }
    if (pkt) {
      if (len >= 2 && cmd_frame[1] == 1) { // optional param (1 = flood, 0 = zero hop)
        unsigned long delay_millis = 0;
        TransportKey default_scope;
        memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));
        sendFloodScoped(default_scope, pkt, delay_millis);
      } else {
        sendZeroHop(pkt);
      }
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_RESET_PATH && len >= 1 + 32) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      // recipient->lastmod = ??   shouldn't be needed, app already has this version of contact
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // unknown contact
    }
  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    uint32_t last_mod = getRTCClock()->getCurrentTime();  // fallback value if not present in cmd_frame
    if (recipient) {
      updateContactFromFrame(*recipient, last_mod, cmd_frame, len);
      recipient->lastmod = last_mod;
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      ContactInfo contact;
      updateContactFromFrame(contact, last_mod, cmd_frame, len);
      contact.lastmod = last_mod;
      contact.sync_since = 0;
      if (addContact(contact)) {
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_REMOVE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient && removeContact(*recipient)) {
      _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE);
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found, or unable to remove
    }
  } else if (cmd_frame[0] == CMD_SHARE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      if (shareContactZeroHop(*recipient)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // unable to send
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACT_BY_KEY) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (contact) {
      writeContactRespFrame(RESP_CODE_CONTACT, *contact);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found
    }
  } else if (cmd_frame[0] == CMD_EXPORT_CONTACT) {
    if (len < 1 + PUB_KEY_SIZE) {
      // export SELF
      mesh::Packet* pkt;
      if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
        pkt = createSelfAdvert(_prefs.node_name);
      } else {
        pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
      }
      if (pkt) {
        pkt->header |= ROUTE_TYPE_FLOOD; // would normally be sent in this mode

        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        uint8_t out_len = pkt->writeTo(&out_frame[1]);
        releasePacket(pkt); // undo the obtainNewPacket()
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // Error
      }
    } else {
      uint8_t *pub_key = &cmd_frame[1];
      ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
      uint8_t out_len;
      if (recipient && (out_len = exportContact(*recipient, &out_frame[1])) > 0) {
        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // not found
      }
    }
  } else if (cmd_frame[0] == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
    if (importContact(&cmd_frame[1], len - 1)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SYNC_NEXT_MESSAGE) {
    int out_len;
    if ((out_len = getFromOfflineQueue(out_frame)) > 0) {
      _serial->writeFrame(out_frame, out_len);
#ifdef DISPLAY_CLASS
      if (_ui) _ui->msgRead(offline_queue_len);
#endif
    } else {
      out_frame[0] = RESP_CODE_NO_MORE_MESSAGES;
      _serial->writeFrame(out_frame, 1);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_PARAMS) {
    int i = 1;
    uint32_t freq;
    memcpy(&freq, &cmd_frame[i], 4);
    i += 4;
    uint32_t bw;
    memcpy(&bw, &cmd_frame[i], 4);
    i += 4;
    uint8_t sf = cmd_frame[i++];
    uint8_t cr = cmd_frame[i++];
    uint8_t repeat = 0;  // default - false
    if (len > i) {
      repeat = cmd_frame[i++];   // FIRMWARE_VER_CODE  9+
    }

    if (repeat && !isValidClientRepeatFreq(freq)) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      _prefs.client_repeat = repeat;
      savePrefs();

      radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    int8_t power = (int8_t)cmd_frame[1];
    if (power < -9 || power > MAX_LORA_TX_POWER) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.tx_power_dbm = power;
      savePrefs();
      radio_driver.setTxPower(_prefs.tx_power_dbm);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SET_TUNING_PARAMS) {
    int i = 1;
    uint32_t rx, af;
    memcpy(&rx, &cmd_frame[i], 4);
    i += 4;
    memcpy(&af, &cmd_frame[i], 4);
    i += 4;
    _prefs.rx_delay_base = ((float)rx) / 1000.0f;
    _prefs.airtime_factor = ((float)af) / 1000.0f;
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_TUNING_PARAMS) {
    uint32_t rx = _prefs.rx_delay_base * 1000, af = _prefs.airtime_factor * 1000;
    int i = 0;
    out_frame[i++] = RESP_CODE_TUNING_PARAMS;
    memcpy(&out_frame[i], &rx, 4); i += 4;
    memcpy(&out_frame[i], &af, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SET_OTHER_PARAMS) {
    _prefs.manual_add_contacts = cmd_frame[1];
    if (len >= 3) {
      _prefs.telemetry_mode_base = cmd_frame[2] & 0x03; // v5+
      _prefs.telemetry_mode_loc = (cmd_frame[2] >> 2) & 0x03;
      _prefs.telemetry_mode_env = (cmd_frame[2] >> 4) & 0x03;

      if (len >= 4) {
        _prefs.advert_loc_policy = cmd_frame[3];
        if (len >= 5) {
          _prefs.multi_acks = cmd_frame[4];
        }
      }
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_PATH_HASH_MODE && cmd_frame[1] == 0 && len >= 3) {
    if (cmd_frame[2] >= 3) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.path_hash_mode = cmd_frame[2];
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_REBOOT && memcmp(&cmd_frame[1], "reboot", 6) == 0) {
    if (dirty_contacts_expiry) { // is there are pending dirty contacts write needed?
      saveContacts();
    }
    board.reboot();
  } else if (cmd_frame[0] == CMD_GET_BATT_AND_STORAGE) {
    uint8_t reply[11];
    int i = 0;
    reply[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t battery_millivolts = board.getBattMilliVolts();
    uint32_t used = _store->getStorageUsedKb();
    uint32_t total = _store->getStorageTotalKb();
    memcpy(&reply[i], &battery_millivolts, 2); i += 2;
    memcpy(&reply[i], &used, 4); i += 4;
    memcpy(&reply[i], &total, 4); i += 4;
    _serial->writeFrame(reply, i);
  } else if (cmd_frame[0] == CMD_EXPORT_PRIVATE_KEY) {
#if ENABLE_PRIVATE_KEY_EXPORT
    uint8_t reply[65];
    reply[0] = RESP_CODE_PRIVATE_KEY;
    self_id.writeTo(&reply[1], 64);
    _serial->writeFrame(reply, 65);
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if ENABLE_PRIVATE_KEY_IMPORT
    if (!mesh::LocalIdentity::validatePrivateKey(&cmd_frame[1])) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid key
    } else {
        mesh::LocalIdentity identity;
        identity.readFrom(&cmd_frame[1], 64);
        if (_store->saveMainIdentity(identity)) {
          self_id = identity;
          writeOKFrame();
          // re-load contacts, to invalidate ecdh shared_secrets
          resetContacts();
          _store->loadContacts(this);
        } else {
          writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        }
    }
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_SEND_RAW_DATA && len >= 6) {
    int i = 1;
    int8_t path_len = cmd_frame[i++];
    if (path_len >= 0 && i + path_len + 4 <= len) { // minimum 4 byte payload
      uint8_t *path = &cmd_frame[i];
      i += path_len;
      auto pkt = createRawData(&cmd_frame[i], len - i);
      if (pkt) {
        sendDirect(pkt, path, path_len);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    } else {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // flood, not supported (yet)
    }
  } else if (cmd_frame[0] == CMD_SEND_LOGIN && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    char *password = (char *)&cmd_frame[1 + PUB_KEY_SIZE];
    cmd_frame[len] = 0; // ensure null terminator in password
    if (recipient) {
      uint32_t est_timeout;
      int result = sendLogin(*recipient, password, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        memcpy(&pending_login, recipient->id.pub_key, 4); // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &pending_login, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_ANON_REQ && len > 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    ContactInfo anon;
    if (recipient == NULL) { // FIRMWARE_VER_CODE 13+,  allow non-contact requests
      memset(&anon, 0, sizeof(anon));
      memcpy(anon.id.pub_key, pub_key, PUB_KEY_SIZE);
      anon.out_path_len = 0;   // default to zero-hop direct
      anon.type = ADV_TYPE_NONE;  // unknown

      if (addContact(anon)) recipient = &anon;
    }
    uint8_t *data = &cmd_frame[1 + PUB_KEY_SIZE];
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendAnonReq(*recipient, data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL); // contacts full
    }
  } else if (cmd_frame[0] == CMD_SEND_STATUS_REQ && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        // FUTURE:  pending_status = tag;  // match this in onContactResponse()
        memcpy(&pending_status, recipient->id.pub_key, 4); // legacy matching scheme
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_PATH_DISCOVERY_REQ && cmd_frame[1] == 0 && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[2];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      // 'Path Discovery' is just a special case of flood + Telemetry req
      uint8_t req_data[9];
      req_data[0] = REQ_TYPE_GET_TELEMETRY_DATA;
      req_data[1] = ~(TELEM_PERM_BASE);  // NEW: inverse permissions mask (ie. we only want BASE telemetry)
      memset(&req_data[2], 0, 3);  // reserved
      getRNG()->random(&req_data[5], 4);   // random blob to help make packet-hash unique
      auto save = recipient->out_path_len;    // temporarily force sendRequest() to flood
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      int result = sendRequest(*recipient, req_data, sizeof(req_data), tag, est_timeout);
      recipient->out_path_len = save;
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_discovery = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len >= 4 + PUB_KEY_SIZE) {  // can deprecate, in favour of CMD_SEND_BINARY_REQ
    uint8_t *pub_key = &cmd_frame[4];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_telemetry = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len == 4) {  // 'self' telemetry request
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // query other sensors -- target specific
    sensors.querySensors(0xFF, telemetry);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], self_id.pub_key, 6);
    i += 6; // pub_key_prefix
    uint8_t tlen = telemetry.getSize();
    memcpy(&out_frame[i], telemetry.getBuffer(), tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_BINARY_REQ && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint8_t *req_data = &cmd_frame[1 + PUB_KEY_SIZE];
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, req_data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_HAS_CONNECTION && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    if (hasConnectionTo(pub_key)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_LOGOUT && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    stopConnection(pub_key);
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_CHANNEL && len >= 2) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    if (getChannel(channel_idx, channel)) {
      int i = 0;
      out_frame[i++] = RESP_CODE_CHANNEL_INFO;
      out_frame[i++] = channel_idx;
      strcpy((char *)&out_frame[i], channel.name);
      i += 32;
      memcpy(&out_frame[i], channel.channel.secret, 16);
      i += 16; // NOTE: only 128-bit supported
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
#ifdef OFFBAND_OBSERVER_BLE_COMPANION
    // Plan 3 Task 10 intercept: slot 40 is locked even for the
    // (currently unsupported) 256-bit-key variant.
    if (cmd_frame[1] == offband::kSystemChannelSlot &&
        !offband::systemChannelAllowSet()) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
      return;
    }
#endif
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // not supported (yet)
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
    uint8_t channel_idx = cmd_frame[1];
#ifdef OFFBAND_OBSERVER_BLE_COMPANION
    // Plan 3 Task 10 intercept: slot 40 is the locked system
    // channel; refuse SET so the user cannot overwrite or
    // accidentally delete it from their MeshCore app.
    if (channel_idx == offband::kSystemChannelSlot &&
        !offband::systemChannelAllowSet()) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
      return;
    }
#endif
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)&cmd_frame[2], 32);
    memset(channel.channel.secret, 0, sizeof(channel.channel.secret));
    memcpy(channel.channel.secret, &cmd_frame[2 + 32], 16); // NOTE: only 128-bit supported
    if (setChannel(channel_idx, channel)) {
      saveChannels();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  } else if (cmd_frame[0] == CMD_SIGN_START) {
    out_frame[0] = RESP_CODE_SIGN_START;
    out_frame[1] = 0; // reserved
    uint32_t len = MAX_SIGN_DATA_LEN;
    memcpy(&out_frame[2], &len, 4);
    _serial->writeFrame(out_frame, 6);

    if (sign_data) {
      free(sign_data);
    }
    sign_data = (uint8_t *)malloc(MAX_SIGN_DATA_LEN);
    sign_data_len = 0;
  } else if (cmd_frame[0] == CMD_SIGN_DATA && len > 1) {
    if (sign_data == NULL || sign_data_len + (len - 1) > MAX_SIGN_DATA_LEN) {
      writeErrFrame(sign_data == NULL ? ERR_CODE_BAD_STATE : ERR_CODE_TABLE_FULL); // error: too long
    } else {
      memcpy(&sign_data[sign_data_len], &cmd_frame[1], len - 1);
      sign_data_len += (len - 1);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SIGN_FINISH) {
    if (sign_data) {
      self_id.sign(&out_frame[1], sign_data, sign_data_len);

      free(sign_data); // don't need sign_data now
      sign_data = NULL;

      out_frame[0] = RESP_CODE_SIGNATURE;
      _serial->writeFrame(out_frame, 1 + SIGNATURE_SIZE);
    } else {
      writeErrFrame(ERR_CODE_BAD_STATE);
    }
  } else if (cmd_frame[0] == CMD_SEND_TRACE_PATH && len > 10 && len - 10 < MAX_PACKET_PAYLOAD-5) {
    uint8_t path_len = len - 10;
    uint8_t flags = cmd_frame[9];
    uint8_t path_sz = flags & 0x03;  // NEW v1.11+
    if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) { // make sure is multiple of path_sz
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      uint32_t tag, auth;
      memcpy(&tag, &cmd_frame[1], 4);
      memcpy(&auth, &cmd_frame[5], 4);
      auto pkt = createTrace(tag, auth, flags);
      if (pkt) {
        sendDirect(pkt, &cmd_frame[10], path_len);

        uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
        uint32_t est_timeout = calcDirectTimeoutMillisFor(t, path_len >> path_sz);

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_SET_DEVICE_PIN && len >= 5) {

    // get pin from command frame
    uint32_t pin;
    memcpy(&pin, &cmd_frame[1], 4);

    // ensure pin is zero, or a valid 6 digit pin
    if (pin == 0 || (pin >= 100000 && pin <= 999999)) {
      _prefs.ble_pin = pin;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (i > 0) {
        *dp++ = ',';
      }
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
      bool success = sensors.setSettingValue(sp, np);
      if (success) {
        #if ENV_INCLUDE_GPS == 1
        // Update node preferences for GPS settings
        if (strcmp(sp, "gps") == 0) {
          _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
          savePrefs();
        } else if (strcmp(sp, "gps_interval") == 0) {
          uint32_t interval_seconds = atoi(np);
          _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
          savePrefs();
        }
        #endif
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_ADVERT_PATH && len >= PUB_KEY_SIZE+2) {
    // FUTURE use:  uint8_t reserved = cmd_frame[1];
    uint8_t *pub_key = &cmd_frame[2];
    AdvertPath* found = NULL;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      auto p = &advert_paths[i];
      if (memcmp(p->pubkey_prefix, pub_key, sizeof(p->pubkey_prefix)) == 0) {
        found = p;
        break;
      }
    }
    if (found) {
      int i = 0;
      out_frame[i++] = RESP_CODE_ADVERT_PATH;
      memcpy(&out_frame[i], &found->recv_timestamp, 4); i += 4;
      out_frame[i++] = found->path_len;
      i += mesh::Packet::writePath(&out_frame[i], found->path, found->path_len);
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_STATS && len >= 2) {
    uint8_t stats_type = cmd_frame[1];
    if (stats_type == STATS_TYPE_CORE) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_CORE;
      uint16_t battery_mv = board.getBattMilliVolts();
      uint32_t uptime_secs = _ms->getMillis() / 1000;
      uint8_t queue_len = (uint8_t)_mgr->getOutboundTotal();
      memcpy(&out_frame[i], &battery_mv, 2); i += 2;
      memcpy(&out_frame[i], &uptime_secs, 4); i += 4;
      memcpy(&out_frame[i], &_err_flags, 2); i += 2;
      out_frame[i++] = queue_len;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_RADIO) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_RADIO;
      int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
      int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
      int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4); // scaled by 4 for 0.25 dB precision
      uint32_t tx_air_secs = getTotalAirTime() / 1000;
      uint32_t rx_air_secs = getReceiveAirTime() / 1000;
      memcpy(&out_frame[i], &noise_floor, 2); i += 2;
      out_frame[i++] = last_rssi;
      out_frame[i++] = last_snr;
      memcpy(&out_frame[i], &tx_air_secs, 4); i += 4;
      memcpy(&out_frame[i], &rx_air_secs, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_PACKETS) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_PACKETS;
      uint32_t recv = radio_driver.getPacketsRecv();
      uint32_t sent = radio_driver.getPacketsSent();
      uint32_t n_sent_flood = getNumSentFlood();
      uint32_t n_sent_direct = getNumSentDirect();
      uint32_t n_recv_flood = getNumRecvFlood();
      uint32_t n_recv_direct = getNumRecvDirect();
      uint32_t n_recv_errors = radio_driver.getPacketsRecvErrors();
      memcpy(&out_frame[i], &recv, 4); i += 4;
      memcpy(&out_frame[i], &sent, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_errors, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid stats sub-type
    }
  } else if (cmd_frame[0] == CMD_FACTORY_RESET && memcmp(&cmd_frame[1], "reset", 5) == 0) {
    if (_serial) {
      MESH_DEBUG_PRINTLN("Factory reset: disabling serial interface to prevent reconnects (BLE/WiFi)");
      _serial->disable(); // Phone app disconnects before we can send OK frame so it's safe here
    }
    bool success = _store->formatFileSystem();
    if (success) {
      writeOKFrame();
      delay(1000);
      board.reboot();  // doesn't return
    } else {
      writeErrFrame(ERR_CODE_FILE_IO_ERROR);
    }
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 0) {
    if (len >= 2 + 16) {
      memcpy(send_scope.key, &cmd_frame[2], sizeof(send_scope.key));  // set scope override TransportKey
    } else {
      memset(send_scope.key, 0, sizeof(send_scope.key));  // reset scope override
    }
    send_unscoped = false;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 1) {  // ver 12+
    send_unscoped = true;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_DEFAULT_FLOOD_SCOPE && len >= 1) {
    if (len >= 1+31+16) {
      int n = strlen((char *) &cmd_frame[1]);
      if (n > 0 && n < 31) {
        strcpy(_prefs.default_scope_name, (char *) &cmd_frame[1]);
        memcpy(_prefs.default_scope_key, &cmd_frame[1+31], 16);
        savePrefs();
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      memset(_prefs.default_scope_name, 0, sizeof(_prefs.default_scope_name));  // set default scope to null
      memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_GET_DEFAULT_FLOOD_SCOPE) {
    out_frame[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
    if (strlen(_prefs.default_scope_name) > 0) {
      memcpy(&out_frame[1], _prefs.default_scope_name, 31);
      memcpy(&out_frame[1+31], _prefs.default_scope_key, 16);
      _serial->writeFrame(out_frame, 1+31+16);
    } else {
      _serial->writeFrame(out_frame, 1);   // no name or key means null
    }
  } else if (cmd_frame[0] == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_frame[1] & 0x80) != 0) {
    auto resp = createControlData(&cmd_frame[1], len - 1);
    if (resp) {
      sendZeroHop(resp);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_SET_AUTOADD_CONFIG) {
    _prefs.autoadd_config = cmd_frame[1];
    if (len >= 3) {
      _prefs.autoadd_max_hops = min(cmd_frame[2], (uint8_t)64);
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_AUTOADD_CONFIG) {
    int i = 0;
    out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
    out_frame[i++] = _prefs.autoadd_config;
    out_frame[i++] = _prefs.autoadd_max_hops;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_GET_ALLOWED_REPEAT_FREQ) {
    int i = 0;
    out_frame[i++] = RESP_ALLOWED_REPEAT_FREQ;
    for (int k = 0; k < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]) && i + 8 < sizeof(out_frame); k++) {
      auto r = &repeat_freq_ranges[k];
      memcpy(&out_frame[i], &r->lower_freq, 4); i += 4;
      memcpy(&out_frame[i], &r->upper_freq, 4); i += 4;
    }
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_RAW_PACKET && len >= 4) {
    auto pkt = obtainNewPacket();
    if (pkt) {
      uint8_t priority = cmd_frame[1];
      if (tryParsePacket(pkt, &cmd_frame[2], len - 2)) {
        sendPacket(pkt, priority, 0);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
  }
}

static bool save_filter(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE;   // don't save the transient/anon entries
}

void MyMesh::saveContacts() {
  _store->saveContacts(this, save_filter);
}

// #241: block-list persistence. Flat file "/blocks": [count:1][key:32]*count.
// Streamed (no large stack buffer) via the DataStore FS abstraction so the
// per-platform open flags (nRF52/RP2040/ESP32) are handled in one place and
// not duplicated here (mirrors saveContacts/savePrefs).
void MyMesh::saveBlocks() {
  auto f = _store->openWriteFile("/blocks");
  if (!f) return;
  uint8_t n = _blocks.count();
  f.write(&n, 1);
  for (uint8_t i = 0; i < n; i++) f.write(_blocks.keyAt(i), BLOCK_KEY_SIZE);
  f.close();
}

void MyMesh::loadBlocks() {
  auto f = _store->openRead("/blocks");
  if (!f) return;
  uint8_t n = 0;
  if (f.read(&n, 1) == 1) {
    uint8_t key[BLOCK_KEY_SIZE];
    for (uint8_t i = 0; i < n && i < MAX_BLOCKED_KEYS; i++) {
      if (f.read(key, BLOCK_KEY_SIZE) == (int)BLOCK_KEY_SIZE) _blocks.add(key);
    }
  }
  f.close();
}

// #241: emit ONE frame of an in-flight 0xC2 LIST, called once per idle main-loop
// pass while !isWriteBusy() (like the contacts iterator / config stream) so the
// send queue drains between frames instead of overflowing + dropping the tail.
// Uses the live count()/keyAt() each pass; a concurrent REMOVE mid-stream can only
// shift indices (eventually-consistent snapshot, never a read past the array since
// the index is re-checked against count() here) -- the app re-syncs if it matters.
void MyMesh::blockListDrain() {
  if (_blk_list_i >= _blocks.count()) {              // all keys emitted -> END
    out_frame[0] = offband::RESP_CODE_OFFBAND_BLOCK;
    out_frame[1] = offband::OFFBAND_BLOCK_LIST;
    out_frame[2] = 0xFE;                             // END
    _serial->writeFrame(out_frame, 3);
    _blk_listing = false;
    return;
  }
  out_frame[0] = offband::RESP_CODE_OFFBAND_BLOCK;
  out_frame[1] = offband::OFFBAND_BLOCK_LIST;
  out_frame[2] = _blk_list_i;                        // 0..MAX_BLOCKED_KEYS-1 (never 0xFE/0xFF)
  memcpy(&out_frame[3], _blocks.keyAt(_blk_list_i), PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 3 + PUB_KEY_SIZE);
  _blk_list_i++;
}

// #396: emit ONE chunk frame of an in-flight caplog download. Called from
// checkSerialInterface once per idle main-loop pass while !isWriteBusy(), so the
// companion send queue (drops-when-full, #169) drains between frames instead of
// being flooded. The ring is frozen (capture auto-stopped at START), so the
// offset stream is stable. Snapshot returning 0 signals the buffer is exhausted.
void MyMesh::caplogDrain() {
  // #450: cap CHUNK data so the frame fits ONE BLE notification. A full
  // MAX_FRAME_SIZE (176) frame does NOT fit BLE: the device negotiates MTU 176, so an
  // ATT notification carries only MTU-3 = 173 bytes -- a 176-B frame is clipped to 173,
  // dropping 3 data bytes per full chunk (only bites near-full downloads over BLE;
  // serial/TCP have no ATT header). MAX_FRAME_SIZE-6 = 170 data (172-B frame) clears the
  // 173 BLE limit with margin and matches the '+4 for transport codes' headroom baked
  // into MAX_FRAME_SIZE. Fits BLE + TCP + serial.
  const size_t cap = (size_t)MAX_FRAME_SIZE - 6;   // [0]=code, [1]=sub, -4 BLE ATT/transport headroom
  out_frame[0] = RESP_CODE_OFFBAND_CAPLOG;
  size_t n = meshLogSnapshot(&out_frame[2], cap, _caplog_off);
  if (n == 0) {                                    // buffer exhausted -> END
    out_frame[1] = CAPLOG_SUB_END;
    _serial->writeFrame(out_frame, 2);
    _caplog_streaming = false;
    meshLogSetEnabled(_caplog_resume);             // resume capture if it was on
    return;
  }
  out_frame[1] = CAPLOG_SUB_CHUNK;
  _serial->writeFrame(out_frame, 2 + n);
  _caplog_off += n;
}

void MyMesh::enterCLIRescue() {
  _cli_rescue = true;
  cli_command[0] = 0;
  Serial.println("========= CLI Rescue =========");
}

void MyMesh::checkCLIRescueCmd() {
  int len = strlen(cli_command);
  while (Serial.available() && len < sizeof(cli_command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      cli_command[len++] = c;
      cli_command[len] = 0;
    }
    Serial.print(c);  // echo
  }
  if (len == sizeof(cli_command)-1) {  // command buffer full
    cli_command[sizeof(cli_command)-1] = '\r';
  }

  if (len > 0 && cli_command[len - 1] == '\r') {  // received complete line
    cli_command[len - 1] = 0;  // replace newline with C string null terminator

    if (memcmp(cli_command, "set ", 4) == 0) {
      const char* config = &cli_command[4];
      if (memcmp(config, "pin ", 4) == 0) {
        _prefs.ble_pin = atoi(&config[4]);
        savePrefs();
        Serial.printf("  > pin is now %06d\n", _prefs.ble_pin);
      } else {
        Serial.printf("  Error: unknown config: %s\n", config);
      }
    } else if (strcmp(cli_command, "rebuild") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        _store->saveMainIdentity(self_id);
        savePrefs();
        saveContacts();
        saveChannels();
        Serial.println("  > erase and rebuild done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (strcmp(cli_command, "erase") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        Serial.println("  > erase done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (memcmp(cli_command, "ls", 2) == 0) {

      // get path from command e.g: "ls /adafruit"
      const char *path = &cli_command[3];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }
      Serial.printf("Listing files in %s\n", path);

      // log each file and directory
      File root = _store->openRead(path);
      if (is_fs2 == false) {
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  UserData%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] UserData%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root.openNextFile();
          }
          root.close();
        }
      }

      if (is_fs2 == true || strlen(path) == 0 || strcmp(path, "/") == 0) {
        if (_store->getSecondaryFS() != nullptr) {
          File root2 = _store->openRead(_store->getSecondaryFS(), path);
          File file = root2.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  ExtraFS%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] ExtraFS%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root2.openNextFile();
          }
          root2.close();
        }
      }
    } else if (memcmp(cli_command, "cat", 3) == 0) {

      // get path from command e.g: "cat /contacts3"
      const char *path = &cli_command[4];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      } else {
        Serial.println("Invalid path provided, must start with UserData/ or ExtraFS/");
        cli_command[0] = 0;
        return;
      }

      // log file content as hex
      File file = _store->openRead(path);
      if (is_fs2 == true) {
        file = _store->openRead(_store->getSecondaryFS(), path);
      }
      if(file){

        // get file content
        int file_size = file.available();
        uint8_t buffer[file_size];
        file.read(buffer, file_size);

        // print hex
        mesh::Utils::printHex(Serial, buffer, file_size);
        Serial.print("\n");

        file.close();

      }

    } else if (memcmp(cli_command, "rm ", 3) == 0) {
      // get path from command e.g: "rm /adv_blobs"
      const char *path = &cli_command[3];
      MESH_DEBUG_PRINTLN("Removing file: %s", path);
      // ensure path is not empty, or root dir
      if(!path || strlen(path) == 0 || strcmp(path, "/") == 0){
        Serial.println("Invalid path provided");
      } else {
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }

        // remove file
        bool removed;
        if (is_fs2) {
          MESH_DEBUG_PRINTLN("Removing file from ExtraFS: %s", path);
          removed = _store->removeFile(_store->getSecondaryFS(), path);
        } else {
          MESH_DEBUG_PRINTLN("Removing file from UserData: %s", path);
          removed = _store->removeFile(path);
        }
        if(removed){
          Serial.println("File removed");
        } else {
          Serial.println("Failed to remove file");
        }

      }

    } else if (strcmp(cli_command, "reboot") == 0) {
      board.reboot();  // doesn't return
    } else {
      Serial.println("  Error: unknown command");
    }

    cli_command[0] = 0;  // reset command buffer
  }
}

void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
  if (len > 0) {
    handleCmdFrame(len);
#ifdef OFFBAND_OBSERVER
  } else if (_ob_stream != OB_STREAM_NONE   // F8 (#169): drain an in-flight config
             && !_serial->isWriteBusy()) {  // response, one frame per idle pass
    offbandStreamDrain();
#endif
  } else if (_blk_listing                 // #241: drain an in-flight 0xC2 block-LIST,
             && !_serial->isWriteBusy()) {  // one key frame per idle pass (queue-safe)
    blockListDrain();
  } else if (_caplog_streaming            // #396: drain an in-flight 0xC4 caplog download,
             && !_serial->isWriteBusy()) {  // one chunk frame per idle pass (queue-safe)
    caplogDrain();
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    ContactInfo contact;
    bool found = false;
    while (_iter.hasNext(this, contact)) {
      if (contact.type != ADV_TYPE_NONE) {
        found = true;
        break;
      }
    }

    if (found) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
        if (contact.lastmod > _most_recent_lastmod) {
          _most_recent_lastmod = contact.lastmod; // save for the RESP_CODE_END_OF_CONTACTS frame
        }
      }
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
      _serial->writeFrame(out_frame, 5);
      _iter_started = false;
    }
  //} else if (!_serial->isWriteBusy()) {
  //  checkConnections();    // TODO - deprecate the 'Connections' stuff
  }
}

#ifdef OFFBAND_OBSERVER
// Strycher/LoRa#325: USB-serial path to the observer config CLI.
// Accumulates a line from the USB Serial console and dispatches it
// through cliPassthroughExecute -- the SAME allowlist + dispatch the
// BLE _sys channel uses (offband::cliPassthroughExecute), so the
// command set, allowlist, and security are identical regardless of
// transport. The reply is echoed back to Serial. Non-blocking: reads
// only what is already buffered each call.
//
// Echo discipline: each typed char is echoed so the operator sees
// what they type, EXCEPT once the accumulated line is recognized as
// "set wifi.pwd " -- from that point the remaining chars (the PSK)
// are NOT echoed, so the secret never lands in the serial log. The
// reply path already redacts the PSK ("wifi.pwd set (N chars entered)").
//
// Strycher/LoRa#325 (Gemini review finding): the prefix match below
// MUST mirror both input tolerances that cliPassthroughExecute applies
// before it runs the command, or the PSK leaks to the echo on inputs
// the dispatcher still accepts:
//   1. leading whitespace -- dispatcher calls trimLeading() (CliPassthrough.cpp)
//   2. verb/field case     -- dispatcher lowercases the first two tokens via
//      normalizeVerbAndFieldToLower() (the Strycher/LoRa#313 phone
//      auto-capitalize fix). A bare case-sensitive index-0 strncmp would
//      execute "Set wifi.pwd hunter2" yet fail to redact, echoing the PSK.
// obsCliIsPwdSetPrefix() skips leading spaces/tabs then case-insensitively
// matches the lowercase literal prefix. (Internal-whitespace-collapsed or
// tab-separated forms are not matched here; the dispatcher's reply-side
// redaction is the authoritative second layer for those rare shapes.)
static bool obsCliIsPwdSetPrefix(const char* s) {
  while (*s == ' ' || *s == '\t') ++s;             // mirror trimLeading()
  static const char* kPrefix = "set wifi.pwd ";    // lowercase, single-spaced
  for (const char* p = kPrefix; *p != 0; ++p, ++s) {
    char a = *s;
    if (a == 0) return false;                       // buffer shorter than prefix
    if (a >= 'A' && a <= 'Z') a += ('a' - 'A');     // fold to lower (mirror #313)
    if (a != *p) return false;
  }
  return true;
}

void MyMesh::checkObserverSerialCli() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (_obs_cli_len == 0) continue;          // ignore blank lines / CRLF pair
      _obs_cli_buf[_obs_cli_len] = 0;
      Serial.println();                          // finish the echoed line
      char reply[256];
      offband::cliPassthroughExecute(_obs_cli_buf, reply, sizeof(reply));
      Serial.println(reply);
      _obs_cli_len = 0;
      _obs_cli_redact = false;
      continue;
    }
    if (_obs_cli_len < sizeof(_obs_cli_buf) - 1) {
      _obs_cli_buf[_obs_cli_len++] = c;
      _obs_cli_buf[_obs_cli_len] = 0;
      // Once the line is unambiguously "set wifi.pwd " (case- and
      // leading-whitespace-tolerant, matching the dispatcher), stop
      // echoing so the PSK that follows is not written to the serial log.
      if (!_obs_cli_redact && obsCliIsPwdSetPrefix(_obs_cli_buf)) {
        _obs_cli_redact = true;
      }
      if (!_obs_cli_redact) Serial.print(c);     // echo (suppressed after the pwd prefix)
    }
    // else: line overflow -- silently drop extra chars until a newline
    // resets the buffer. cliPassthrough bounds the meaningful surface.
  }
}
#endif

void MyMesh::loop() {
  BaseChatMesh::loop();

  if (_cli_rescue) {
    checkCLIRescueCmd();
  } else {
    checkSerialInterface();
#ifdef OFFBAND_OBSERVER
    // Strycher/LoRa#325: also accept observer config commands typed on
    // the USB serial console (independent of the compiled transport, so
    // a USB-cabled observer is configurable without a phone). Only runs
    // in normal mode; the _cli_rescue branch above owns Serial when active.
    checkObserverSerialCli();
#endif
  }

#ifdef OFFBAND_OBSERVER_BLE_COMPANION
  // Plan 3 Task 10 (Strycher/LoRa#272): drain any pending
  // system-channel status / CLI-reply messages enqueued by
  // SystemChannelCli (from WifiBootstrap, intercept dispatch,
  // etc.). Cheap when the queue is empty (single load + branch).
  offband::systemChannelDrain();
#endif

  // is there are pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    saveContacts();
    dirty_contacts_expiry = 0;
  }

#ifdef DISPLAY_CLASS
  if (_ui) _ui->setHasConnection(_serial->isConnected());
#endif
}

bool MyMesh::advert() {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (pkt) {
    sendZeroHop(pkt);
    return true;
  } else {
    return false;
  }
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
  return _mgr->getOutboundTotal() > 0 || dirty_contacts_expiry != 0;
}
