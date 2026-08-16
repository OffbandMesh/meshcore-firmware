#pragma once
#include <cstdint> // For uint8_t, uint32_t
#include <helpers/ConfigSerializer.h>

#define TELEM_MODE_DENY            0
#define TELEM_MODE_ALLOW_FLAGS     1     // use contact.flags
#define TELEM_MODE_ALLOW_ALL       2

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1

class NodePrefs : public ConfigSerializer {  // persisted to file
public:
  float airtime_factor = 0;
  char node_name[32];
  double node_lat = 0, node_lon = 0;
  float freq = 0;
  uint8_t sf = 0;
  uint8_t cr = 0;
  uint8_t multi_acks = 0;
  uint8_t manual_add_contacts = 0;
  float bw = 0;
  int8_t tx_power_dbm = 0;
  uint8_t telemetry_mode_base = 0;
  uint8_t telemetry_mode_loc = 0;
  uint8_t telemetry_mode_env = 0;
  float rx_delay_base = 0;
  uint32_t ble_pin = 0;
  uint8_t  advert_loc_policy = 0;
  uint8_t  buzzer_quiet = 0;
  uint8_t  vibe_quiet = 0;
  uint8_t  gps_enabled = 0;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval = 0;     // GPS read interval in seconds
  uint8_t autoadd_config = 0;    // bitmask for auto-add contacts config
  uint8_t rx_boosted_gain = 0; // SX126x RX boosted gain mode (0=power saving, 1=boosted)
  uint8_t _client_repeat = 0;  // DEPRECATED -> use repeat.disable_fwd
  uint8_t path_hash_mode = 0;    // which path mode to use when sending
  uint8_t autoadd_max_hops = 0;  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops (max 64)
  char default_scope_name[31];
  uint8_t default_scope_key[16];

private:
  class RadioPrefs : public ConfigSerializer {  // COPIED from CommonCLI (for now)
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("freq", _parent->freq);
      def("bw", _parent->bw);
      def("sf", _parent->sf);
      def("cr", _parent->cr);
      //def("cad", _parent->cad_enabled);
      //def("int_thr", _parent->interference_threshold);
      def("rxgain", _parent->rx_boosted_gain);
      // Upstream maps this to rx_boosted_gain (no radio_fem_rxgain member exists
      // in their companion class) -- the same copy/paste bug as CommonCLI.h,
      // fixed upstream in PR #3137. Offband carries the real field (#298).
      def("fem_rxgain", _parent->radio_fem_rxgain);
      def("tx", _parent->tx_power_dbm);
      def("af", _parent->airtime_factor);
      def("rxdelay", _parent->rx_delay_base);
      //def("f_txdelay", _parent->tx_delay_factor);   currently hard-coded
      //def("d_txdelay", _parent->direct_tx_delay_factor);  currently hard-coded
      //def("agc_int", _parent->agc_reset_interval);
      def("hash_mode", _parent->path_hash_mode);
      def("multi_ack", _parent->multi_acks);
    }
  public:
    RadioPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RadioPrefs radio;

  class GPSPrefs : public ConfigSerializer {  // COPIED from CommonCLI (for now)
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->gps_enabled); // boolean
      def("int", _parent->gps_interval);   // interval in seconds
      def("adv_loc", _parent->advert_loc_policy);
    }
  public:
    GPSPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  GPSPrefs gps;

  class RepeatPrefs : public ConfigSerializer {  // COPIED from CommonCLI (for now)
  public:
    uint8_t disable_fwd = 1;
  protected:
    void structure() override {
      def("disable", disable_fwd);
      //def("f_max", flood_max);
      //def("f_max_uns", flood_max_unscoped);
      //def("f_max_adv", flood_max_advert);
      //def("loop", loop_detect);
    }
  };
  RepeatPrefs repeat;

  class CompanionPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("auto_max", _parent->autoadd_max_hops);  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops (max 64)
      def("defs_nm", _parent->default_scope_name, sizeof(_parent->default_scope_name));
      def("defs_key", (void *) _parent->default_scope_key, sizeof(_parent->default_scope_key));
      def("pin", _parent->ble_pin);
      def("buzz_q", _parent->buzzer_quiet);
      def("vibe_q", _parent->vibe_quiet);
      def("auto_add", _parent->autoadd_config);    // bitmask for auto-add contacts config
      def("man_add", _parent->manual_add_contacts);
      def("tel_base", _parent->telemetry_mode_base);
      def("tel_loc", _parent->telemetry_mode_loc);
      def("tel_env", _parent->telemetry_mode_env);
    }
  public:
    CompanionPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  CompanionPrefs companion;

public:
  // ---- Offband-only members ----------------------------------------------
  // MUST be public: DataStore::loadPrefsInt() reads them directly, same as the
  // upstream members above. They sit after upstream's private: section, so the
  // access specifier has to be re-opened here.
  uint8_t radio_fem_rxgain = 1;   // #298: 1 = LNA on, 0 = bypass
  uint8_t caplog_enabled = 0;     // #428
  uint8_t caplog_level = 0;       // #428
  uint8_t notify_scope = 0;       // #510
  uint8_t button_actions[4] = {0, 0, 0, 0};  // #509
  uint8_t ui_led_enabled = 1;     // #542 B1
  uint8_t ui_display_mode = 0;    // #542 B1

private:
  // ---- Offband-only prefs -------------------------------------------------
  // No upstream counterpart -- carried across every base update, and they must
  // appear in structure() below or they will not survive the /prefs.json
  // migration. Groups mirror upstream's nesting convention (#197).
  class UIPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("led", _parent->ui_led_enabled);        // #542 B1
      def("disp", _parent->ui_display_mode);      // #542 B1
      def("notify", _parent->notify_scope);       // #510
      // #509 button-action matrix: 4 bytes, one per OFFBAND_UI_SEQ_*.
      def("btn", _parent->button_actions, sizeof(_parent->button_actions));
    }
  public:
    UIPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  UIPrefs ui;

  class LogPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("caplog_en", _parent->caplog_enabled);  // #428
      def("caplog_lvl", _parent->caplog_level);   // #428
    }
  public:
    LogPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  LogPrefs log;

protected:
  void structure() override {
    def("name", node_name, sizeof(node_name));
    //def("adv_int", advert_interval);
    //def("f_adv_int", flood_advert_interval);
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("gps", gps);
    def("repeat", repeat);
    def("comp", companion);
    def("ui", ui);      // Offband #509/#510/#542
    def("log", log);    // Offband #428
  }
public:
  NodePrefs() : radio(this), gps(this), companion(this), ui(this), log(this) {
    node_name[0] = 0;
    default_scope_name[0] = 0;
    memset(default_scope_key, 0, sizeof(default_scope_key));
  }
  // new accessor methods
  bool isRepeatEn() const { return repeat.disable_fwd == 0; }
  void setRepeatEn(bool en) { repeat.disable_fwd = en ? 0 : 1; }
};

// #509: button-sequence indices. Order is the press count, so the array index IS the
// sequence id on the wire.
#define OFFBAND_UI_SEQ_SINGLE  0
#define OFFBAND_UI_SEQ_DOUBLE  1
#define OFFBAND_UI_SEQ_TRIPLE  2
#define OFFBAND_UI_SEQ_QUAD    3
#define OFFBAND_UI_SEQ_COUNT   4

// #509: assignable actions. Values are wire values -- see the 0xC5 contract in
// OffbandConfigProtocol.h. Append only; never renumber.
#define OFFBAND_UI_ACTION_NONE          0
#define OFFBAND_UI_ACTION_ADVERT        1
#define OFFBAND_UI_ACTION_GPS_TOGGLE    2
#define OFFBAND_UI_ACTION_CYCLE_SCOPE   3
#define OFFBAND_UI_ACTION_BATTERY_BEEP  4
#define OFFBAND_UI_ACTION_COUNT         5

// #510: values for NodePrefs::notify_scope. Kept as an enum-like constant set rather
// than a bare magic number so the CLI, the button handler and the config command all
// agree. Order is the triple-press cycle order: ALL -> SELF -> NONE -> ALL.
#define NOTIFY_SCOPE_ALL    0
#define NOTIFY_SCOPE_SELF   1
#define NOTIFY_SCOPE_NONE   2
#define NOTIFY_SCOPE_COUNT  3
