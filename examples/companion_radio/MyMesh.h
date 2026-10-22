#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <helpers/gui/GuiCommands.h>
#ifdef OFFBAND_GUI
  #include <helpers/gui/hw/StoreFlash.h>
  #include <helpers/gui/hw/GuiSd.h>
  #include <helpers/gui/MsgStore.h>
  #include <helpers/gui/ChunkedSave.h>
  #include <helpers/gui/GuiGps.h>
  #include <helpers/gui/GuiMetrics.h>
  #include <helpers/gui/GuiReplies.h>
  #include <helpers/gui/GuiViews.h>
#endif
#include "AbstractUITask.h"
#include <helpers/StreamAbandonReport.h>   // #1316: AbandonLimiter, held as a member below

/*------------ Frame Protocol --------------*/
// FIRMWARE_VER_CODE is a MERGE-ORDERED monotonic contract number, not per-feature:
// each in-flight branch that changes the device-info caps byte claims the NEXT code
// AT MERGE, not at branch time. Do not assume a code from an unmerged branch is yours
// -- check what has actually merged (same split-allocation trap as the cap bits; see
// the canonical table in OffbandConfigProtocol.h). In-flight claimants after 17:
// #508 (caps byte 2) MERGED and took 18. Still in flight and NOT yet allocated:
// #365 WIFI_COMPANION (0x08) and display-config (0x10) -- they take 20 and beyond in
// merge order, NOT 19, which this branch is claiming. The value below is PROVISIONAL
// for this branch -- reconcile it against firmware-base at merge.
//
// Why a bump at all when byte 2 already exists: precedent is that ADDING A CAP BIT
// bumps the code even without a frame-layout change -- 17 was "+ caplog cap bit
// (0x20)", 16 was "FEM LNA cap 0x04 / 0xC3". This change sets caps2 bit 0x01 and adds
// the 0xC5 command, so it follows the same rule.
#define FIRMWARE_VER_CODE 22   // #611: + caps2 bit 0x08 PKT_HASH + 0xC6 packet-hash query. Prior 21 (#542): caps2 0x04 INDICATORS + 0xC5 led/display sub-codes + led/display state in device-info. Prior 20 (#509): caps2 0x02 BUTTON_MATRIX. Prior 19 (#510): caps2 0x01 NOTIFY_SCOPE + 0xC5. Prior 18 (#508): second caps byte at offset 84

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "9 Aug 2026"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.17.0"
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
#include <LittleFS.h>
#elif defined(ESP32)
#include <SPIFFS.h>
#endif

#include "DataStore.h"
#include "NodePrefs.h"

#include <RTClib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/IdentityStore.h>
#include <helpers/BlockStore.h>   // #241: portable pubkey block list (receive-side)
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#ifndef LORA_FREQ
#define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
#define LORA_BW 250
#endif
#ifndef LORA_SF
#define LORA_SF 10
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif
#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER LORA_TX_POWER
#endif
// #1236: whether the GPS starts on when no settings are saved yet (a first boot).
#ifndef DEFAULT_GPS_ENABLED
#define DEFAULT_GPS_ENABLED 0
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef OFFLINE_QUEUE_SIZE
#define OFFLINE_QUEUE_SIZE 16
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "Offband-MC-"   // #154: clean-flash BLE name (was "MeshCore-")
#endif

#include <helpers/BaseChatMesh.h>
#include <helpers/TransportKeyStore.h>
// #611: the packet-hash ring is sized from the canonical wire contract
// (OFFBAND_PKTHASH_RING_SLOTS) rather than a duplicated literal. Safe to include
// here -- this header pulls only <stdint.h> and defines no types of its own.
#include "OffbandConfigProtocol.h"
#if UI_HAS_CARDKB
#include <helpers/BadgeSendTracker.h>   // #1227: DMs typed on the badge
#include <helpers/BadgeStore.h>         // #1229: the badge's inbox and threads
#include <helpers/HeardRepeats.h>       // #1232: a tick when a repeater passes ours on
#include <helpers/ClockJump.h>          // #1233: the clock set under stored messages
#endif
#if defined(OFFBAND_GUI) && !UI_HAS_CARDKB
#include <helpers/HeardRepeats.h>       // #1464: "heard xN" on the GUI's own channel sends
#include <helpers/BadgeStore.h>         // #1465: splitSender, for the store's channel messages
#endif

/* -------------------------------------------------------------------------------------- */

#define REQ_TYPE_GET_STATUS             0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE             0x02
#define REQ_TYPE_GET_TELEMETRY_DATA     0x03

struct AdvertPath {
  uint8_t pubkey_prefix[7];
  uint8_t path_len;
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
};

// #611: one retained on-air packet hash for an outgoing channel message, so the
// client can correlate its own send with an observer's sighting (0xC6 query).
// Keyed by (msg_timestamp, channel_idx) -- both already arrive in
// CMD_SEND_CHANNEL_TXT_MSG, so no change to that (stock-client) frame.
// 13 bytes per entry; OFFBAND_PKTHASH_RING_SLOTS of them = 104 bytes.
struct SentPktHash {
  uint32_t msg_timestamp;
  uint8_t  channel_idx;
  uint8_t  hash[MAX_HASH_SIZE];
  bool     used;               // false = empty slot (a hash of all-zero is legal)
};

class MyMesh : public BaseChatMesh, public DataStoreHost {
public:
  MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store, AbstractUITask* ui=NULL);

  void begin(bool has_display);
  void startInterface(BaseSerialInterface &serial);

  const char *getNodeName();
  NodePrefs *getNodePrefs();
  // #1194: capture on (at level) or off, persisted -- the app's caplog enable/
  // disable and the observer CLI's caplog start/stop. During a download it sets
  // the state the download restores at its end instead of unfreezing mid-stream.
  // Returns false when the setting could not be saved (it still holds until reboot).
  bool setCaplogCapture(bool on, uint8_t level);
  uint32_t getBLEPin();

  void loop();
  void handleCmdFrame(size_t len);
  bool advert();
  void enterCLIRescue();

  // Command API (#1453): the actions behind the phone's command frames, so
  // the on-device GUI and the phone do exactly the same thing. The phone
  // handlers turn each result into the reply code they have always sent.
  offband::gui::CmdResult cmdSendDirect(ContactInfo& to, uint32_t timestamp, uint8_t attempt,
                                        const char* text, bool cli_data, bool* flood,
                                        uint32_t* expected_ack, uint32_t* est_timeout);
  offband::gui::CmdResult cmdSendChannel(uint8_t idx, uint32_t timestamp, const char* text, size_t len);
  offband::gui::CmdResult cmdSendAdvert(bool flood);
  offband::gui::CmdResult cmdSetChannel(uint8_t idx, const char* name, const uint8_t secret[16]);
  offband::gui::CmdResult cmdSetRadio(uint32_t freq_khz, uint32_t bw_hz, uint8_t sf, uint8_t cr, bool repeat);
  offband::gui::CmdResult cmdSetTxPower(int8_t dbm);          // -9..MAX_LORA_TX_POWER
  offband::gui::CmdResult cmdSetPathHashMode(uint8_t mode);   // 0..2: 1- to 3-byte hashes
  offband::gui::CmdResult cmdResetPath(ContactInfo& contact);
  offband::gui::CmdResult cmdShareContact(ContactInfo& contact);
  void cmdSetTime(uint32_t secs);
  void cmdReboot();
  void flushPending();   // pending contacts and messages to flash (reboot, power-off)

  // Requests (#1454). There is one request slot, shared with the phone: a new
  // request replaces whatever was outstanding, from either side.
  offband::gui::CmdResult cmdLogin(ContactInfo& to, const char* password, bool* flood, uint32_t* tag,
                                   uint32_t* est_timeout);
  offband::gui::CmdResult cmdStatusReq(ContactInfo& to, bool* flood, uint32_t* tag, uint32_t* est_timeout);
  offband::gui::CmdResult cmdPathDiscovery(ContactInfo& to, bool* flood, uint32_t* tag, uint32_t* est_timeout);
  offband::gui::CmdResult cmdTrace(uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* path,
                                   uint8_t path_len, uint32_t* est_timeout);
#ifdef OFFBAND_GUI
  // Every login, status, trace and path reply, phone connected or not.
  offband::gui::ReplyBox& guiReplies() { return _gui_replies; }
  // Radio and contact metrics (#1455); the GUI calls contacts.markRead().
  offband::gui::MetricsHub& guiMetrics() { return _gui_metrics; }
  // GPS fix: None, 2D or 3D, and how long ago it was lost (#1456).
  offband::gui::FixType guiFixType();
  const offband::gui::FixTracker& guiFix() const { return _gui_fix; }
  // The on-device message store (#1427) and its flash backend (#1463).
  offband::gui::MsgStore& guiStore() { return _gui_store; }
  StoreFlash& guiStoreFlash() { return _gui_store_flash; }
#ifdef PIN_SD_CS
  // The SD card and its archive of the store (Epic I #1433).
  GuiSd& guiSd() { return _gui_sd; }
#endif
  // A stored message's delivery or echo count changed: the card's copy too.
  void guiStoreChanged(uint32_t seq) {
#ifdef PIN_SD_CS
    _gui_sd.noteChanged(seq);
#else
    (void)seq;
#endif
  }
  // The UI says whether anyone is looking: internal-flash writes (the store,
  // the contacts save) wait for a dim or dark screen (#1472).
  void setGuiScreenIdle(bool idle) { _gui_screen_idle = idle; }
  // A contact's conversation, made on first use (Nodes DM, #1533); -1 if the
  // thread table is full.
  int guiThreadForContact(const ContactInfo& contact) { return guiThreadFor(contact); }
  int guiThreadForChannelSlot(uint8_t idx) { return guiThreadForChannel(idx); }   // Epic R
  // The store, for the GUI's own small files (its node passwords, #1533).
  DataStore* guiDataStore() { return _store; }
#endif

#if UI_HAS_CARDKB
  // #1227: messages typed on the badge, sent with the same mesh calls as the phone path.
  // A DM gets up to kBadgeDmAttempts attempts; uiSendStatus() reports how it ended.
  static constexpr int kBadgeDmSlots = 4;
  static constexpr int kBadgeDmAttempts = 3;
  static constexpr uint32_t kBadgeRepeatWaitSecs = 30;   // #1232: a channel send's wait for a repeat
  bool uiSendChannel(int channel_idx, const char* text);
  uint16_t uiSendDirect(const ContactInfo& contact, const char* text);
  offband::BadgeSend uiSendStatus(uint16_t handle) const { return _badge_dms.status(handle); }

  // #1229: the badge's own copy of recent messages, for its inbox and threads. Filled
  // on receive and on every badge send.
  static constexpr int kBadgeConvos = 24;
  static constexpr int kBadgeMsgs = 32;
  using BadgeMsgStore = offband::BadgeStore<kBadgeConvos, kBadgeMsgs, MAX_TEXT_LEN, PUB_KEY_SIZE>;
  BadgeMsgStore& badgeStore() { return _badge_store; }

  // #1474: the store, for UI code that owns its own small files -- the badge's custom
  // tune slots. Exposed rather than adding another member here, so the slot store stays
  // in ui-new and this header, which every companion variant compiles, learns nothing
  // about it.
  DataStore* dataStore() { return _store; }
  // #1233: whether the phone or the GPS has set the clock since boot. Until then it runs
  // from the newest contact's last-heard time (bootstrapRTCfromContacts), which can be
  // hours or days behind, so the badge shows ages rather than clock times.
  bool badgeClockTrusted() const;

  // #1230: sends to conversation c of the badge store. Its channel is found by its
  // secret and its contact by its whole key, so the send goes where the thread says.
  enum class UiSend : uint8_t { Sent, Gone, NotSent, Busy };
  UiSend uiSendTo(int convo, const char* text);
  // #1230: a failed DM, sent again. The new one replaces it in the thread.
  bool uiResend(uint32_t seq);
  // #1233: the badge's own advert, zero-hop or flood, as the phone's command sends it.
  bool uiAdvert(bool flood);
  // #1234: each node in the advert table (the last few heard, contacts or not), in
  // place. Sized by sizeof: the table's macro is defined further down.
  template <class Visit>
  void uiEachHeard(Visit visit) const {
    for (size_t i = 0; i < sizeof(advert_paths) / sizeof(advert_paths[0]); i++) {
      if (advert_paths[i].recv_timestamp != 0) visit(advert_paths[i]);
    }
  }
#endif

  int  getRecentlyHeard(AdvertPath dest[], int max_num);

#if defined(OFFBAND_OBSERVER) || defined(OFFBAND_POWER_TELEMETRY)
  // #31 Task C: live Dispatcher stats for the /status snapshot, and (#1071)
  // the [radio] telemetry line. Guarded to the builds that call them, to keep
  // this shared-lineage header's diff minimal against upstream MeshCore.
  // _err_flags and _mgr are protected on Dispatcher; MyMesh can read them
  // directly as a subclass. _mgr is initialised from a reference in the
  // Dispatcher constructor and is guaranteed non-null for the lifetime of
  // the mesh object.
  uint16_t getErrFlags()         const { return _err_flags; }
  int      getOutboundQueueLen() const { return _mgr->getOutboundTotal(); }
#endif

#ifdef OFFBAND_OBSERVER
  // Epic F (#161): Offband config command handler (CMD_OFFBAND_CONFIG). Observer-
  // only -- the backend (configSet/configGet, ConfigSchema) compiles only under
  // OFFBAND_OBSERVER. Wire contract: OffbandConfigProtocol.h.
  void handleOffbandConfigCmd(size_t len);
  void writeOffbandConfigScalar(uint8_t sub, const char* text);
  // F8 (#169): multi-frame config responses (OCFG_BROKERS / OCFG_VIEW) STREAM one
  // frame per idle main-loop pass via offbandStreamDrain() -- mirroring
  // ContactsIterator -- instead of flooding the 4-deep BLE send queue synchronously,
  // which dropped frames including the *_END terminator (F5 device test, mc #79).
  void offbandStreamDrain();
#endif

#ifdef OFFBAND_OBSERVER_BLE_COMPANION
  // Plan 3 Task 10 (Strycher/LoRa#272): post a status message
  // onto the locked system channel slot. Builds a
  // RESP_CODE_CHANNEL_MSG_RECV_V3 frame (or the pre-v3
  // RESP_CODE_CHANNEL_MSG_RECV variant if the connected app is
  // older), enqueues it into the offline queue, and tickles the
  // BLE client with PUSH_CODE_MSG_WAITING if currently
  // connected. Called by SystemChannelCli::systemChannelDrain()
  // via a registered function pointer. Public so the wifi_observer
  // helper can reach it without #include'ing MyMesh.h (the
  // function pointer hides the class type from the caller).
  void postSystemChannelText(const char* text, size_t text_len);
#endif

protected:
  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
  bool getCADEnabled() const override;
  int calcRxDelay(float score, uint32_t air_time) const override;
  uint32_t getRetransmitDelay(const mesh::Packet *packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet *packet) override;
  uint8_t getExtraAckTransmitCount() const override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;
  bool allowPacketForward(const mesh::Packet* packet) override;

  // #510: notification-scope helpers. textMentionsSelf() implements the `@[name]`
  // bracketed, case-insensitive rule that is a CONTRACT with the client -- see the
  // definition in MyMesh.cpp before changing it.
  bool textMentionsSelf(const char* text) const;
  bool channelMsgShouldNotify(const char* text) const;

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis);
  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
#ifdef OFFBAND_GUI
  void logTx(mesh::Packet* packet, int len) override;  // TX airtime for the GUI (#1455)
#endif
#if defined(OFFBAND_OBSERVER) || UI_HAS_CARDKB || defined(OFFBAND_GUI)
  // Strycher/LoRa#335: the observer's /packets path. #1232: the badge's heard repeats.
  // #1455: the GUI's heard counts by type.
  void logRx(mesh::Packet* pkt, int len, float score) override;
#endif
  bool isAutoAddEnabled() const override;
  bool shouldAutoAddContactType(uint8_t type) const override;
  bool shouldOverwriteWhenFull() const override;
  uint8_t getAutoAddMaxHops() const override;
  void onContactsFull() override;
  void onContactOverwrite(const uint8_t* pub_key) override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo &contact) override;
  ContactInfo* processAck(const uint8_t *data) override;
  void queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt, uint32_t sender_timestamp,
                    const uint8_t *extra, int extra_len, const char *text);

  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override;
  void onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text) override;
  void onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const uint8_t *sender_prefix, const char *text) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override;
  void onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                         const uint8_t *data, size_t data_len) override;

  uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                           uint8_t len, uint8_t *reply) override;
  void onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;
  void onControlDataRecv(mesh::Packet *packet) override;
  void onRawDataRecv(mesh::Packet *packet) override;
  void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) override;

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;

  // DataStoreHost methods
  bool onContactLoaded(const ContactInfo& contact) override { return addContact(contact); }
  bool getContactForSave(uint32_t idx, ContactInfo& contact) override { return getContactByIdx(idx, contact); }
  bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) override { return setChannel(channel_idx, ch); }
  bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) override { return getChannel(channel_idx, ch); }

  void clearPendingReqs() {
    pending_login = pending_status = pending_telemetry = pending_discovery = pending_req = 0;
  }

public:
  // #1525: set the tune one channel plays, and persist it. `stored` is the ENCODED byte
  // from offband::encodeChannelTune, so kChannelTuneUnset clears the choice.
  //
  // Here rather than in the screen because saveChannels() is private -- the mesh decides
  // when the channel file is written -- and because the read-modify-write matters:
  // setChannel() copies the record whole, so the name and the secret have to come back
  // out of the existing one. That is the #1523 lesson, and it only has to be learned once
  // if there is one place that does it. False when the slot holds no channel.
  bool setChannelTune(uint8_t idx, uint8_t stored);

  bool savePrefs() {   // #1194: false when the store could not write them
    _prefs.node_lat = sensors.node_lat;
    _prefs.node_lon = sensors.node_lon;
    return _store->savePrefs(_prefs);
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled ? "1" : "0");
    if (_prefs.gps_interval > 0) {
      char interval_str[12];  // uint32_t gps_interval: up to 10 digits + null
      snprintf(interval_str, sizeof(interval_str), "%u", _prefs.gps_interval);
      sensors.setSettingValue("gps_interval", interval_str);
    }
    gpsModeTouched();   // #1556: the power machine re-reads and restarts from this
  }

  // #1557: the mode in effect, for the GPS screen and the Settings row. Available
  // wherever GPS is, since it needs only the prefs and a pure rule.
  offband::GpsMode gpsMode() const {
    return offband::gpsModeFromPrefs(_prefs.gps_mode, _prefs.gps_enabled);
  }

  // Can this build actually run Power save? The screen must not offer a mode nothing
  // drives -- GPS would simply stay on while the screen claimed otherwise.
  static bool gpsPowerSaveAvailable() {
#if defined(OFFBAND_GPS_POWER_SAVE)
    return true;
#else
    return false;
#endif
  }

  // #1557: what GPS is doing right now, and how long the current wait has left.
  //
  // Without the duty cycle compiled there are only two answers, derived from the mode, so
  // the screen has one code path either way.
  offband::GpsActivity gpsActivity() const {
#if defined(OFFBAND_GPS_POWER_SAVE)
    if (gpsMode() == offband::GpsMode::PowerSave) return _gps_activity;
#endif
    return gpsMode() == offband::GpsMode::Off ? offband::GpsActivity::Off
                                              : offband::GpsActivity::On;
  }

  uint32_t gpsWaitRemainingS() const {
#if defined(OFFBAND_GPS_POWER_SAVE)
    if (gpsMode() == offband::GpsMode::PowerSave && _gps_wait_ms > 0) {
      const int32_t left = (int32_t)(_gps_wait_until - millis());
      return left <= 0 ? 0 : (uint32_t)left / 1000U;
    }
#endif
    return 0;
  }

  // #1557: store a mode the user has chosen, and apply it.
  //
  // Writes BOTH fields: gps_enabled keeps meaning on-or-off for every reader that knows
  // nothing about modes, and the mode byte records how it runs while on. Picking Off
  // leaves that byte alone so Off-then-On returns to Power save (#1555).
  void setGpsMode(offband::GpsMode m) {
    _prefs.gps_mode = offband::gpsStoredModeForPick(m, _prefs.gps_mode);
    _prefs.gps_enabled = offband::gpsEnabledForMode(m);
    savePrefs();
    applyGpsPrefs();   // pushes the hardware and bumps the generation, so the machine re-arms
  }

#if defined(OFFBAND_GPS_POWER_SAVE)
  // #1556: the duty cycle's tick. Called every loop; does nothing outside Power save.
  void gpsPowerLoop();
#endif

  // #1556: tell the GPS power machine to pick the mode up again and start fresh.
  //
  // A counter rather than a flag, because it has to fire even when the resolved mode has
  // NOT changed. That is #1557's force-retry: someone looking at "backed off, waiting 4h"
  // selects Power save again, and the machine must clear the failure count and attempt now.
  // A loop that only watched for a mode change would make that gesture a silent no-op and
  // leave the user with no way out but a reboot.
  //
  // Every writer of the GPS prefs calls this -- applyGpsPrefs covers the app's toggle, the
  // CLI and boot; the badge's GPS screen calls it directly.
  void gpsModeTouched() { _gps_mode_gen++; }
  uint32_t gpsModeGeneration() const { return _gps_mode_gen; }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

private:
#if ENV_INCLUDE_GPS == 1
  // #1556: bumped by every writer of the GPS prefs, including a re-selection of the mode
  // already in effect. The power machine watches it.
  uint32_t _gps_mode_gen = 0;
#endif
#if defined(OFFBAND_GPS_POWER_SAVE)
  // #1556/#1557: the duty cycle, here rather than as a static in the main loop so the GPS
  // screen can say what it is doing.
  offband::GpsPowerMachine _gps_machine;
  offband::GpsActivity _gps_activity = offband::GpsActivity::On;
  uint32_t _gps_wait_until = 0;      // millis() the current wait ends
  uint32_t _gps_wait_ms = 0;         // its length; 0 when not waiting
  uint32_t _gps_seen_gen = 0xFFFFFFFFu;
  offband::GpsMode _gps_seen_mode = offband::GpsMode::On;
  int8_t _gps_applied = -1;          // -1 = not yet applied, so the first tick applies
  uint32_t _gps_mismatch_ms = 0;     // rate limit on the hardware-drift complaint
  bool _gps_primed = false;
#endif

  // Hands a reply to the GUI (#1454); `body` is the phone frame after its
  // code and reserved bytes. A no-op without the GUI. The kinds mirror
  // offband::gui::ReplyKind, which non-GUI builds don't include.
  enum GuiReply : uint8_t { GUI_REPLY_LOGIN, GUI_REPLY_STATUS, GUI_REPLY_TRACE, GUI_REPLY_PATH };
  void postGuiReply(GuiReply kind, const uint8_t* pub_key, uint32_t tag, bool ok, const uint8_t* body,
                    size_t len);
#ifdef OFFBAND_GUI
  offband::gui::ReplyBox _gui_replies;
  offband::gui::MetricsHub _gui_metrics;
  offband::gui::FixTracker _gui_fix;

  offband::gui::MsgStore _gui_store;
  StoreFlash _gui_store_flash;
#ifdef PIN_SD_CS
  GuiSd _gui_sd;
#endif
  bool _gui_screen_idle = false;
  uint32_t _contacts_wait_since = 0;   // a due contacts save held for the screen; 0: none

  // #1458: the contact list is saved a slice per loop tick, not in one pass.
  void contactsSaveTick();
  offband::gui::ChunkedSave _contacts_save;
  int _contacts_save_count = 0;      // contact slots when the save began (anon slots included)
  uint32_t _contacts_save_t0 = 0;
  uint32_t _contacts_slice_max_ms = 0;
#endif

  // #611: retained hashes of recent outgoing channel messages, for the 0xC6 query.
  // FIFO by _pkthash_next; a duplicate (timestamp, channel) overwrites in place so a
  // repeat key never yields a stale hash. Not persisted -- correlation is only useful
  // for the current session, and CoreScope sightings are queried promptly.
  SentPktHash _pkthash_ring[offband::OFFBAND_PKTHASH_RING_SLOTS];
  uint8_t     _pkthash_next = 0;
  void recordSentPktHash(uint32_t msg_timestamp, uint8_t channel_idx, const uint8_t* hash);
  const uint8_t* lookupSentPktHash(uint32_t msg_timestamp, uint8_t channel_idx) const;

  void writeOKFrame();
  void writeErrFrame(uint8_t err_code);
  void writeDisabledFrame();
  // Returns whether the whole frame reached the transport. Behavior-neutral today --
  // every caller ignores it (see the definition; acting on it is the #1283 fix).
  bool writeContactRespFrame(uint8_t code, const ContactInfo &contact);
  void updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len);
  void addToOfflineQueue(const uint8_t frame[], int len);
  int getFromOfflineQueue(uint8_t frame[]);
  int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override { 
    return _store->getBlobByKey(key, key_len, dest_buf);
  }
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override {
    return _store->putBlobByKey(key, key_len, src_buf, len);
  }

  void checkCLIRescueCmd();
  void checkSerialInterface();
#ifdef OFFBAND_OBSERVER
  void checkObserverSerialCli();   // Strycher/LoRa#325: USB-serial -> observer CLI
#endif
  bool isValidClientRepeatFreq(uint32_t f) const;

  // helpers, short-cuts
  void saveChannels() { _store->saveChannels(this); }
  void saveContacts();

  // #241: block-list persistence (flat /blocks file: [count][key0..keyN-1]).
  void loadBlocks();
  void saveBlocks();
  void blockListDrain();   // emit one key frame of an in-flight 0xC2 LIST
  void caplogDrain();      // #396: emit one chunk frame of an in-flight 0xC4 caplog download

  DataStore* _store;
  BlockStore _blocks;   // #241: blocked pubkeys (in-memory; persisted via load/saveBlocks)
  NodePrefs _prefs;
  uint32_t pending_login;
  uint32_t pending_status;
  uint32_t pending_telemetry, pending_discovery;   // pending _TELEMETRY_REQ
  uint32_t pending_req;   // pending _BINARY_REQ
  BaseSerialInterface *_serial;
  AbstractUITask* _ui;

  ContactsIterator _iter;
  uint32_t _iter_filter_since;
  uint32_t _most_recent_lastmod;
  uint32_t _active_ble_pin;
  bool _iter_started;

  // #1283 (D1): a refused contact must be re-offered, not skipped. The iterator
  // cannot rewind -- hasNext() has already advanced by the time the write fails --
  // so the contact is held here and retried on the next idle pass. caplogDrain()
  // solves the same problem by not advancing its read offset (#718); an iterator
  // needs this instead.
  //
  // #1299 measured why it works: the refusal clears in under a millisecond
  // (avail=146-149 against need=151, since_ok=0-1ms, consec=1), so the very next
  // pass succeeds. The data was never gone -- it was being thrown away.
  // The held copy can be one pass (~1 ms) stale if a CMD_ that edits the contact
  // store lands in between. That is the SAME eventual-consistency the streams
  // already document and accept -- blockListDrain's comment says a concurrent
  // REMOVE mid-stream shifts indices and "the app re-syncs if it matters", and
  // hasNext() hands out a by-value snapshot regardless. This extends that window by
  // one pass; it does not introduce it.
  ContactInfo _pending_contact;
  bool _has_pending_contact;          // a held contact has ALREADY passed the filter
  uint32_t _contacts_last_progress;   // millis() of the last ACCEPTED contact frame

  // #1316: ALWAYS compiled, unlike the #1299 counters below. An abandonment report that
  // says only "stalled 15000ms" cannot tell us whether the client died one contact in or
  // three hundred, which is the difference between a client bug and a transport bug. The
  // diag counters answer that but are compiled out of every shipping image, and this
  // report has to work on a STOCK build -- that is the whole point of the task. Eight
  // bytes to make the one line we get from a tester's device actually diagnostic.
  // Initialized here, not only at CONTACTS_START: an abort can only fire inside a stream
  // that started, so reaching the report with these unset should be impossible -- and
  // "should be impossible" is not a reason to let a diagnostic print uninitialized memory.
  uint32_t _contacts_sent = 0;        // contact frames ACCEPTED by the transport this stream
  uint32_t _contacts_declared = 0;    // the count promised in the CONTACTS_START frame
  // ...and the ones deliberately skipped. Without this, a `since`-filtered sync that
  // succeeded COMPLETELY reports delivered=330/350 and reads as a partial failure, because
  // `declared` is getNumContacts() -- the total -- while a filtered sync only ever sends the
  // changed ones. Adversarial review found that false-failure pair. The three numbers now
  // reconcile: delivered + filtered should equal declared on a healthy stream.
  uint32_t _contacts_filtered = 0;

  // The denominator an abandonment report should use: what this sync was ever going to
  // send. `_contacts_declared` is getNumContacts(), the whole address book, which is what
  // the START frame promises the client -- but a `since`-filtered sync only sends the
  // changed ones, so comparing against it makes a COMPLETE sync look like a partial one.
  uint32_t contactsEligible() const {
    return _contacts_declared >= _contacts_filtered ? _contacts_declared - _contacts_filtered
                                                    : 0;
  }

  // #1316: rate-limit state for the abandonment report. A MEMBER, not a function-local
  // static: a static is one shared instance across every MyMesh that might ever exist, so
  // two instances would silence each other's reports. Nothing creates two today; that is a
  // reason to make it cheap to keep correct, not a reason to rely on it.
  offband::AbandonLimiter _abandon_limiter;

#if defined(OFFBAND_CONTACT_STREAM_COUNTERS) && OFFBAND_CONTACT_STREAM_COUNTERS
  // #1299 DIAGNOSE instrumentation. COUNTERS ONLY -- this changes no behavior, and
  // must not: the run exists to characterize the SHIPPED defect in #1283, so a build
  // that also fixes something measures nothing. Compiled out of every other image.
  //
  // The point is to DISCRIMINATE, not to confirm the refusal theory. yielded vs
  // declared isolates the iterator; filtered isolates the `since` path; refused
  // isolates the write. If all of them balance and the client still sees fewer
  // contacts, the cause is below writeFrame() or in the client, and the reading on
  // #1299 is the wrong tree.
  uint32_t _cs_declared;      // the count promised at CONTACTS_START
  uint32_t _cs_yielded;       // _iter.hasNext() returned true
  uint32_t _cs_filtered;      // yielded, but lastmod <= _iter_filter_since
  // NOTE, since D1: `attempted` counts send ATTEMPTS, so a retried contact counts
  // more than once and `attempted >= yielded`. Before D1 they were equal and
  // `declared - sent == refused` held exactly; now `refused` is a retry count, not
  // a loss count, and a healthy stream should end with sent == declared.
  uint32_t _cs_attempted;     // writeContactRespFrame called for the stream
  uint32_t _cs_refused;       // ...and writeFrame() returned 0
  uint32_t _cs_end_refused;   // the END_OF_CONTACTS frame was refused
  void csReset(uint32_t declared);
  void csReport();
#endif

  // #1316: the single report for "a stream gave up and data was lost". ALWAYS compiled.
  //
  // Every stall-abort used to report through MESH_DEBUG_PRINTLN, which tests
  // g_meshLogEnabled first -- and capture is OFF by default on a shipping build (#428).
  // So on every image a tester actually runs, abandoning a stream was SILENT. That
  // silence is why #1283 survived six observed occurrences across two diagnosis rounds:
  // the firmware dropped contacts and said nothing, and the owner found it by counting
  // what arrived on the host.
  //
  // Goes to crashLogf, which reaches the retained ring (survives a reset, present on a
  // stock build) and the UART mirror where one is built -- and which, since #1087,
  // suppresses its own Serial echo when the console carries the framed protocol, so it
  // cannot corrupt the stream it is reporting on.
  //
  // `unit` names what `progress` counts (" items", " bytes", " slot"); the four streams
  // measure different things and a bare number sends the reader back to the source.
  // `total` of 0 means "no meaningful total for this stream"; it is then omitted rather
  // than printed as a guess.
  void reportStreamAbandoned(const char* stream, const char* phase, uint32_t stalled_ms,
                             uint32_t progress, const char* unit, uint32_t total);
  bool _cli_rescue;
  bool send_unscoped;   // force un-scoped flood (instead of using send_scope)
  char cli_command[80];
#ifdef OFFBAND_OBSERVER
  // Strycher/LoRa#325: USB-serial observer-CLI line accumulator.
  // Initialized in the MyMesh constructor body (matching the convention
  // of the other members here), NOT via in-class initializers.
  char _obs_cli_buf[128];
  uint8_t _obs_cli_len;
  bool _obs_cli_redact;   // true once "set wifi.pwd " seen -> stop echoing the PSK
  // F8 (#169): streaming state for multi-frame config responses (OCFG_BROKERS /
  // OCFG_VIEW). offbandStreamDrain() emits ONE frame per idle main-loop pass
  // (mirroring ContactsIterator) so it never floods the 4-deep BLE send queue.
  enum : uint8_t { OB_STREAM_NONE = 0, OB_STREAM_BROKERS, OB_STREAM_VIEW };
  uint8_t _ob_stream;     // OB_STREAM_* ; constructor-init to NONE
  int     _ob_slot;       // BROKERS: slot whose rendered lines are in _ob_buf
  size_t  _ob_off;        // next line (BROKERS) / chunk (VIEW) offset into _ob_buf
  uint32_t _ob_last_progress;  // #1283 (D1): last ACCEPTED frame, for the stall abort
  char    _ob_buf[1024];  // current slot's "key=value\n" lines, or the VIEW text
#endif
  // #241: in-flight block-LIST streaming (ALWAYS compiled, unlike the observer
  // config stream above). blockListDrain() emits one key frame per idle main-loop
  // pass so the LIST never bursts the companion send queue (which drops when full,
  // #169). Framing: START=[..,0xFF,count] -> [..,idx,key]* -> END=[..,0xFE].
  bool    _blk_listing;
  uint8_t _blk_list_i;
  uint32_t _blk_last_progress;   // #1283 (D1): last ACCEPTED frame, for the stall abort
  // #396: in-flight serial-capture (caplog) download. Capture is frozen (auto-
  // stopped) at START so the ring is stable; caplogDrain() streams one CHUNK
  // frame per idle pass from _caplog_off until the buffer is exhausted.
  bool    _caplog_streaming;
  size_t  _caplog_off;
  bool    _caplog_resume;   // capture-enabled state to restore when the download ends
  // #718: millis() of the last CHUNK the transport actually accepted. A transport may
  // now REFUSE a frame it cannot deliver whole, and the drain re-offers the same bytes
  // rather than skipping them -- so a host that stops draining would otherwise leave
  // _caplog_streaming true forever, which also blocks ENABLE/DISABLE/ERASE (the guard
  // in handleCmdFrame). Timed on PROGRESS, not total duration, so a legitimately slow
  // but advancing download is never cut off.
  uint32_t _caplog_last_progress;
  uint8_t app_target_ver;
  uint8_t *sign_data;
  uint32_t sign_data_len;
  unsigned long dirty_contacts_expiry;

  TransportKey send_scope;

  uint8_t cmd_frame[MAX_FRAME_SIZE + 1];
  uint8_t out_frame[MAX_FRAME_SIZE + 1];
  CayenneLPP telemetry;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];

    bool isChannelMsg() const;
  };
  int offline_queue_len;
  Frame offline_queue[OFFLINE_QUEUE_SIZE];

  struct AckTableEntry {
    unsigned long msg_sent;
    uint32_t ack;
    ContactInfo* contact;
  };
  #define EXPECTED_ACK_TABLE_SIZE 8
  AckTableEntry expected_ack_table[EXPECTED_ACK_TABLE_SIZE]; // circular table
  int next_ack_idx;
#ifdef OFFBAND_GUI
  // #1464: the stored own message each expected ACK belongs to, slot for slot.
  uint32_t _gui_ack_seq[EXPECTED_ACK_TABLE_SIZE] = {};
  // #1464: own channel sends, watched for repeats ("heard xN").
  offband::HeardRepeats<16, MAX_HASH_SIZE> _gui_repeats;
  int guiThreadFor(const ContactInfo& contact);
  int guiThreadForChannel(uint8_t idx);
#endif

#if UI_HAS_CARDKB
  // #1227: DMs typed on the badge. Their ACKs are kept apart from expected_ack_table so
  // the phone is never told about a message it didn't send.
  offband::BadgeSendTracker<kBadgeDmSlots, kBadgeDmAttempts, MAX_TEXT_LEN, PUB_KEY_SIZE> _badge_dms;
  void badgeSendTick();
  BadgeMsgStore _badge_store;   // #1229
  offband::HeardRepeats<16, MAX_HASH_SIZE> _badge_repeats;   // #1232: recent channel sends
  // #1233: notices the clock being set under this run's stamps (messages, contacts heard,
  // the advert table), and moves them with it.
  void badgeClockCheck();
  offband::ClockJump _badge_clock;
  bool _badge_clock_set_by_phone = false;
#endif

  #define ADVERT_PATH_TABLE_SIZE   16
  AdvertPath advert_paths[ADVERT_PATH_TABLE_SIZE]; // circular table
};

extern MyMesh the_mesh;
