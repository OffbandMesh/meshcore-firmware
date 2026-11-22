// #2222: two bench findings on the T-Deck Pro GUI, run against the real
// GuiShell (Screens.cpp and friends), LVGL 8.3.11 and the device's lv_conf.h,
// on a 240x320 portrait display.
//
// 1. A Bluetooth PIN set in Settings › Connectivity is handed on at once, and
//    the PIN row shows it. (That it then reaches the Bluetooth stack is
//    test_multiserial and test_ble_pin; that a phone pairs with it is the
//    bench check on tdeck-pro-1.)
// 2. "+" in Chats opens Add Channel, with the Offband client's four choices in
//    its words and order, each leading to its step.
// 3. An open conversation follows new messages while it's at the newest, and
//    holds its place while the reader is scrolled up (bench, 2026-11-22).
//
// env:native_gui_shell builds it as the T-Deck Pro's e-paper GUI, with its
// fonts; env:native_gui_shell_lit as a lit panel's (Heltec V4 TFT, R8).

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <lvgl.h>

#define private public
#define protected public
#include "Screens.h"
#undef private
#undef protected

#include <helpers/ChannelKeys.h>
#include <helpers/gui/EinkFrame.h>
#include <helpers/gui/GuiLayout.h>
#include <helpers/gui/hw/GuiFonts.h>
#include <helpers/gui/GuiTheme.h>
#include <helpers/gui/MsgStore.h>

using namespace offband::gui;

namespace {

const char* const kChoices[4] = {"Create a Private Channel", "Join a Private Channel", "Join the Public Channel",
                                 "Join a Hashtag Channel"};

// --------------------------------------------------------------- the panel

void flushCb(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t*) { lv_disp_flush_ready(drv); }

void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  data->point.x = 0;
  data->point.y = 0;
  data->state = LV_INDEV_STATE_RELEASED;
}

// --------------------------------------------------------------- the model

class FakeModel : public ShellModel {
public:
  MsgStore msgs;
  GuiPrefs prefs;
  // Bluetooth: the PIN set, and the one in use (as MyMesh::setBlePin).
  uint32_t pin_stored = 0, pin_active = 731554;
  std::vector<uint32_t> pins_set;
  // Channels: eight slots, Public in slot 0 unless a test clears it.
  ChannelSlot slots[8] = {};
  struct Added {
    int slot;
    std::string name;
    uint8_t key[kChannelKeyLen];
  };
  std::vector<Added> added;
  int new_keys = 0;

  FakeModel() {
    msgs.begin(200, 16);
    slots[0].used = true;
    memcpy(slots[0].key, offband::kPublicChannelKey, kChannelKeyLen);
  }

  const MsgStore& store() override { return msgs; }
  NotifyMode threadMode(int) override { return NotifyMode::All; }
  void threadOpened(int) override {}
  void routeMeta(int, char* out, size_t n) override { snprintf(out, n, "1 hop"); }
  int zone() override { return 0; }
  void setZone(int) override {}
  int gpsZone() override { return 0; }
  bool gpsLiveFix() override { return false; }
  uint32_t utcNow() override { return 1763400000; }
  void sendAdvert() override {}
  void loraPopover() override {}
  size_t composeBudget(int) override { return 140; }
  const char* cannedText(int) override { return ""; }
  bool sendText(int, const char*, size_t) override { return true; }
  uint8_t dmAttempt(uint32_t) override { return 0; }
  void resend(uint32_t) override {}
  void setThreadMode(int, NotifyMode) override {}
  const char* nodeName() override { return "Bench-7"; }
  const NodeRow* nodeRows(size_t* total) override {
    *total = 0;
    return nullptr;
  }
  uint8_t radioSf() override { return 10; }
  bool nodeDetail(const uint8_t*, NodeDetail*) override { return false; }
  void detailOpened(const uint8_t*) override {}
  bool startNodeAct(const uint8_t*, NodeAct, const char*) override { return false; }
  void nodeActState(NodeAct, ActView* out) override { *out = ActView{}; }
  const char* savedPassword(const uint8_t*) override { return nullptr; }
  int openDm(const uint8_t*) override { return -1; }
  bool ownPos(GeoPos*) override { return false; }
  RadioValues radioValues() override { return RadioValues{}; }
  int8_t txMax() override { return 22; }
  bool saveRadio(const RadioValues&) override { return true; }
  void setupSummary(SetPage, char* out, size_t n) override { snprintf(out, n, "On"); }
  GuiPrefs& guiPrefs() override { return prefs; }
  void prefsChanged() override {}
  void savePrefsNow() override {}
  uint16_t screenOffSecs() override { return 60; }
  void setScreenOffSecs(uint16_t) override {}
  bool buzzerOn() override { return false; }
  void setBuzzerOn(bool) override {}
  NodeSettings nodeSettings() override { return NodeSettings{}; }
  void setNodeSettings(const NodeSettings&) override {}
  void radioStats(RadioStats* out) override { *out = RadioStats{}; }
  size_t sensorReadings(SensorReading*, size_t) override { return 0; }
  int sensorCount() override { return 0; }
  const char* sensorName(uint8_t) override { return ""; }
  bool gpsLines(char*, size_t, char*, size_t) override { return false; }
  const uint8_t* selfKey() override {
    static uint8_t k[32] = {1, 2, 3};
    return k;
  }
  void deviceInfo(DeviceInfo* out) override {
    *out = DeviceInfo{};
    out->ble_on = true;
    out->pin_stored = pin_stored;
    out->pin_active = pin_active;
  }
  void setBleOn(bool) override {}
  bool setBlePin(uint32_t pin) override {
    pins_set.push_back(pin);
    pin_stored = pin;
    if (pin) pin_active = pin;   // in use at once (#2222)
    return true;
  }
  bool setFullMv(uint16_t) override { return true; }
  void learnFullAgain() override {}
  bool setLogLevel(int) override { return true; }
  bool advertNow() override { return true; }
  bool syncTimeFromGps() override { return false; }
  void reboot() override {}
  uint16_t deleteAllPaths() override { return 0; }
  int channelSlots(ChannelSlot* out, int max) override {
    const int n = max < 8 ? max : 8;
    for (int i = 0; i < n; i++) out[i] = slots[i];
    return n;
  }
  int addChannel(int slot, const char* name, const uint8_t* key) override {
    Added a{slot, name, {}};
    memcpy(a.key, key, kChannelKeyLen);
    added.push_back(a);
    slots[slot].used = true;
    memcpy(slots[slot].key, key, kChannelKeyLen);
    return msgs.thread(ThreadKind::Channel, key, kChannelKeyLen, name);
  }
  void newChannelKey(uint8_t* out) override {
    new_keys++;
    memset(out, 0xAB, kChannelKeyLen);
  }
  bool channelOf(int, uint8_t* key, bool* p) override {
    memset(key, 0, kChannelKeyLen);
    *p = false;
    return true;
  }
  bool leaveChannel(int) override { return false; }
  bool channelHeld(const uint8_t*) override { return true; }
};

// --------------------------------------------------------------- the rig

void pump(int passes = 4) {
  for (int i = 0; i < passes; i++) {
    lv_tick_inc(5);
    lv_timer_handler();
  }
}

void setupRig() {
  static bool done = false;
  if (done) return;
  done = true;
  lv_init();
  static lv_disp_draw_buf_t buf;
  static lv_color_t b1[240 * 32], b2[240 * 32];
  lv_disp_draw_buf_init(&buf, b1, b2, 240 * 32);
  static lv_disp_drv_t disp;
  lv_disp_drv_init(&disp);
  disp.hor_res = 240;
  disp.ver_res = 320;
  disp.flush_cb = flushCb;
  disp.draw_buf = &buf;
  lv_disp_drv_register(&disp);
  static lv_indev_drv_t indev;
  lv_indev_drv_init(&indev);
  indev.type = LV_INDEV_TYPE_POINTER;
  indev.read_cb = readCb;
  lv_indev_drv_register(&indev);
}

void collectLabels(lv_obj_t* o, std::vector<lv_obj_t*>* out) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_obj_check_type(o, &lv_label_class)) out->push_back(o);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) collectLabels(lv_obj_get_child(o, int32_t(i)), out);
}

std::vector<lv_obj_t*> labels() {
  std::vector<lv_obj_t*> out;
  collectLabels(lv_scr_act(), &out);
  collectLabels(lv_layer_top(), &out);
  return out;
}

// The shown label whose text is exactly `text`, or null.
lv_obj_t* labelOf(const char* text) {
  for (lv_obj_t* l : labels())
    if (strcmp(lv_label_get_text(l), text) == 0) return l;
  return nullptr;
}

bool shows(const char* what) {
  for (lv_obj_t* l : labels())
    if (strstr(lv_label_get_text(l), what)) return true;
  return false;
}

// Whether a one-line label's text fits its width, so it isn't cut to "...".
bool fits(lv_obj_t* l) {
  lv_obj_update_layout(lv_scr_act());
  lv_point_t size;
  lv_txt_get_size(&size, lv_label_get_text(l), lv_obj_get_style_text_font(l, 0),
                  lv_obj_get_style_text_letter_space(l, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x <= lv_obj_get_content_width(l);
}

// A tap on what holds `text`: the nearest clickable object above its label.
bool tap(const char* text) {
  lv_obj_t* o = labelOf(text);
  while (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) o = lv_obj_get_parent(o);
  if (!o) return false;
  lv_event_send(o, LV_EVENT_CLICKED, nullptr);
  pump();
  return true;
}

}  // namespace

class Gui : public ::testing::Test {
protected:
  FakeModel* model = nullptr;
  GuiShell* shell = nullptr;

  void SetUp() override {
    setupRig();
    lv_obj_clean(lv_scr_act());
    lv_obj_clean(lv_layer_top());
    model = new FakeModel();
    shell = new GuiShell();
    shell->setLayout(layoutFor(0));
    shell->begin(themeTokens(kEinkTheme, kDefaultAccentRgb), Look(model->prefs.look), model);
    pump();
  }
  void TearDown() override {
    lv_obj_clean(lv_scr_act());
    lv_obj_clean(lv_layer_top());
    delete shell;
    delete model;
  }

  // Settings › Connectivity › PIN, its pad open.
  void openPinPad() {
    shell->showTab(Tab::Setup);
    shell->setOpen(SetPage::Conn);
    pump();
    ASSERT_TRUE(tap("PIN"));
    ASSERT_EQ(shell->_ss, GuiShell::SetSheet::Pad);
  }
  // The pad's digits, then Set: key i is kPadKeys[i] ("1".."9", ".", "0").
  void typePin(const char* digits) {
    for (const char* d = digits; *d; d++) shell->setSheetPress(*d == '0' ? 10 : *d - '1');
    shell->setSheetPress(14);   // Set
    pump();
  }

  void openAddChannel() {
    shell->showTab(Tab::Chat);
    pump();
    ASSERT_TRUE(tap("+")) << "Chats has no \"+\"";
  }
  void typeName(const char* name) {
    for (const char* p = name; *p; p++) shell->_chan_buf.insert(p, 1);   // one character a call
    shell->chanSubmit();
    pump();
  }

  // A channel with enough history to scroll, open in Chats. Every third
  // message is long enough to wrap to several lines.
  int openBusyChannel(int n = 14) {
    const uint8_t key[16] = {9, 9, 9};
    const int t = model->msgs.thread(ThreadKind::Channel, key, sizeof key, "#busy");
    for (int i = 0; i < n; i++) arrive(t, i);
    shell->showTab(Tab::Chat);
    shell->openThread(t);
    pump();
    return t;
  }
  void arrive(int t, int i) {
    char text[160];
    if (i % 3 == 2)
      snprintf(text, sizeof text, "msg %d: a longer one that wraps across several lines of the bubble, as a real "
               "channel's chatter does", i);
    else
      snprintf(text, sizeof text, "msg %d", i);
    model->msgs.addIncoming(t, 1763400000u + uint32_t(i), "Wren", text, strlen(text), 1, 20);
  }
  // As UITask does each pass: the shell sees the store changed.
  void newMessage(int t, int i) {
    arrive(t, i);
    shell->updateChats();
    pump();
  }
  lv_coord_t belowView() {
    lv_obj_update_layout(lv_scr_act());
    return lv_obj_get_scroll_bottom(shell->_msg_list);
  }
};

// ------------------------------------------------------------ Bluetooth PIN

TEST_F(Gui, ASetPinIsHandedOnAndShownAtOnce) {
  openPinPad();
  typePin("482913");
  ASSERT_EQ(model->pins_set.size(), 1u);
  EXPECT_EQ(model->pins_set[0], 482913u);
  EXPECT_EQ(shell->_ss, GuiShell::SetSheet::None) << "the pad stayed open after Set";
  EXPECT_TRUE(shows("482913")) << "the PIN row still shows the PIN from boot";
  EXPECT_FALSE(shows("731554"));
}

// The PIN is drawn once, in the clock's 46 px digits, and fits the screen.
// At 12 px the owner couldn't read it on the T-Deck Pro without magnifying it.
TEST_F(Gui, ThePinIsDrawnLargeOnce) {
  shell->showTab(Tab::Setup);
  shell->setOpen(SetPage::Conn);
  pump();
  int found = 0;
  lv_obj_t* big = nullptr;
  for (lv_obj_t* l : labels()) {
    if (strcmp(lv_label_get_text(l), "731554") == 0) {
      found++;
      big = l;
    }
  }
  ASSERT_EQ(found, 1) << "the PIN is drawn small as well as large, or not at all";
  EXPECT_EQ(lv_obj_get_style_text_font(big, 0), font(FontRole::Clock));
  // How tall a digit is drawn, against the 12 px row value it replaces.
  lv_font_glyph_dsc_t g, small;
  ASSERT_TRUE(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(big, 0), &g, '8', 0));
  ASSERT_TRUE(lv_font_get_glyph_dsc(font(FontRole::BodySmall), &small, '8', 0));
  RecordProperty("digit_px", g.box_h);
  RecordProperty("old_digit_px", small.box_h);
  EXPECT_GE(g.box_h, 3 * small.box_h) << "digits " << g.box_h << " px, against " << small.box_h << " px before";
  EXPECT_TRUE(fits(big)) << "six large digits don't fit a 240 px screen";
}

// Landscape (Heltec V4 TFT, R8, T-Deck): the large PIN still leaves the
// Phone row on screen without scrolling.
TEST_F(Gui, ThePhoneRowStaysOnScreenInLandscape) {
  for (uint16_t rot : {uint16_t(90), uint16_t(270)}) {
    shell->setLayout(layoutFor(rot));
    lv_disp_set_rotation(nullptr, rot == 90 ? LV_DISP_ROT_90 : LV_DISP_ROT_270);
    shell->showTab(Tab::Setup);
    shell->setOpen(SetPage::Conn);
    pump();
    lv_obj_update_layout(lv_scr_act());
    ASSERT_EQ(lv_disp_get_ver_res(nullptr), 240) << "not landscape: the check below would pass trivially";
    lv_obj_t* phone = labelOf("Phone");
    ASSERT_NE(phone, nullptr);
    lv_area_t a;
    lv_obj_get_coords(phone, &a);
    EXPECT_LE(a.y2, lv_disp_get_ver_res(nullptr)) << "rotation " << rot << ": the Phone row is pushed off screen";
    EXPECT_NE(labelOf("731554"), nullptr);
  }
  lv_disp_set_rotation(nullptr, LV_DISP_ROT_NONE);
}

// Negative: with no PIN in use, nothing is drawn large; the row says None.
TEST_F(Gui, NoPinIsNotDrawnLarge) {
  model->pin_stored = model->pin_active = 0;
  shell->showTab(Tab::Setup);
  shell->setOpen(SetPage::Conn);
  pump();
  for (lv_obj_t* l : labels()) EXPECT_NE(lv_obj_get_style_text_font(l, 0), font(FontRole::Clock));
  EXPECT_TRUE(shows("None"));
}

// The pad said "Used from the next boot". With the PIN in use at once, it
// says nothing of a reboot.
TEST_F(Gui, ThePinPadDoesNotTalkOfAReboot) {
  openPinPad();
  EXPECT_FALSE(shows("next boot"));
  EXPECT_FALSE(shows("reboot"));
}

// Negative: five digits are refused; nothing is handed on and the pad stays.
TEST_F(Gui, AShortPinIsRefused) {
  openPinPad();
  typePin("48291");
  EXPECT_TRUE(model->pins_set.empty());
  EXPECT_EQ(shell->_ss, GuiShell::SetSheet::Pad) << "a refused PIN closed the pad as if set";
  EXPECT_EQ(model->pin_active, 731554u);
}

// An empty pad is "new each boot": 0, handed on.
TEST_F(Gui, AnEmptyPinIsNewEachBoot) {
  model->pin_stored = model->pin_active = 482913;
  openPinPad();
  typePin("");
  ASSERT_EQ(model->pins_set.size(), 1u);
  EXPECT_EQ(model->pins_set[0], 0u);
  EXPECT_TRUE(shows("New each boot"));
}

// ------------------------------------------------------------ Add Channel

TEST_F(Gui, PlusOpensAddChannelWithTheClientsChoicesInOrder) {
  model->slots[0].used = false;   // Public not held: all four are listed
  openAddChannel();
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::Kinds);
  EXPECT_TRUE(shows("Add Channel"));
  lv_coord_t last_y = -1;
  for (const char* c : kChoices) {
    lv_obj_t* l = labelOf(c);
    ASSERT_NE(l, nullptr) << "missing: " << c;
    lv_obj_update_layout(lv_scr_act());
    lv_area_t a;
    lv_obj_get_coords(l, &a);
    EXPECT_GT(a.y1, last_y) << c << " is out of the client's order";
    last_y = a.y1;
    EXPECT_TRUE(fits(l)) << c << " is cut short on a 240 px screen";
  }
}

// Negative: the old in-between sheet is gone.
TEST_F(Gui, PlusNoLongerAsksWhatToStart) {
  openAddChannel();
  EXPECT_EQ(shell->_sheet, GuiShell::Sheet::None);
  EXPECT_FALSE(shows("Start a chat"));
  EXPECT_FALSE(shows("Message a node"));
}

// With no chats, the empty Chats button opens it too.
TEST_F(Gui, TheEmptyChatsButtonOpensAddChannel) {
  shell->showTab(Tab::Chat);
  pump();
  ASSERT_TRUE(tap("+ Add Channel"));
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::Kinds);
}

TEST_F(Gui, CreateAPrivateChannelMakesAKeyAndShowsIt) {
  openAddChannel();
  ASSERT_TRUE(tap("Create a Private Channel"));
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::PrivName);
  EXPECT_TRUE(shows("Create a Private Channel"));
  typeName("Ops");
  EXPECT_EQ(model->new_keys, 1);
  ASSERT_EQ(model->added.size(), 1u);
  EXPECT_EQ(model->added[0].name, "Ops");
  EXPECT_EQ(model->added[0].key[0], 0xAB);
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::ShowKey);
}

// Negative: the last slot goes while the name is typed (a phone added a
// channel). No key is shown for a channel that wasn't saved; the choices say
// the slots are full.
TEST_F(Gui, CreateWithNoSlotLeftShowsNoKey) {
  openAddChannel();
  ASSERT_TRUE(tap("Create a Private Channel"));
  for (auto& s : model->slots) s.used = true;
  typeName("Ops");
  EXPECT_TRUE(model->added.empty());
  EXPECT_NE(shell->_chan, GuiShell::ChanStep::ShowKey);
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::Kinds);
  EXPECT_TRUE(shows("slots are in use"));
}

TEST_F(Gui, JoinAPrivateChannelAsksForItsKey) {
  openAddChannel();
  ASSERT_TRUE(tap("Join a Private Channel"));
  EXPECT_TRUE(shows("Join a Private Channel"));
  typeName("Ops");
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::KeyEntry);
  EXPECT_EQ(model->new_keys, 0) << "Join made a key instead of asking for one";
  EXPECT_TRUE(model->added.empty());
  for (const char* p = "00112233445566778899aabbccddeeff"; *p; p++) shell->_chan_buf.insert(p, 1);
  shell->chanSubmit();
  pump();
  ASSERT_EQ(model->added.size(), 1u);
  EXPECT_EQ(model->added[0].name, "Ops");
  EXPECT_EQ(model->added[0].key[15], 0xFF);
}

// Back from the key returns to the name, as typed.
TEST_F(Gui, BackFromTheKeyKeepsTheName) {
  openAddChannel();
  ASSERT_TRUE(tap("Join a Private Channel"));
  typeName("Ops");
  shell->chanBack();
  pump();
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::PrivName);
  EXPECT_STREQ(shell->_chan_buf.text(), "Ops");
}

// The same with a name that isn't all ASCII.
TEST_F(Gui, BackFromTheKeyKeepsANameWithAccents) {
  openAddChannel();
  ASSERT_TRUE(tap("Join a Private Channel"));
  const char* name = "Caf\xC3\xA9 \xE2\x9C\xA8";   // "Café ✨"
  for (const char* p = name; *p;) {
    const uint8_t b = uint8_t(*p);
    const size_t k = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
    ASSERT_TRUE(shell->_chan_buf.insert(p, k));
    p += k;
  }
  shell->chanSubmit();
  pump();
  shell->chanBack();
  pump();
  EXPECT_STREQ(shell->_chan_buf.text(), name);
}

TEST_F(Gui, JoinThePublicChannelJoinsIt) {
  model->slots[0].used = false;
  openAddChannel();
  ASSERT_TRUE(tap("Join the Public Channel"));
  ASSERT_EQ(model->added.size(), 1u);
  EXPECT_EQ(memcmp(model->added[0].key, offband::kPublicChannelKey, kChannelKeyLen), 0);
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::None);
}

// Public already held: not offered again (mockup 3), the other three are.
TEST_F(Gui, PublicHeldIsNotOfferedAgain) {
  openAddChannel();
  EXPECT_EQ(labelOf("Join the Public Channel"), nullptr);
  EXPECT_NE(labelOf("Join a Hashtag Channel"), nullptr);
}

TEST_F(Gui, JoinAHashtagChannelAsksForTheName) {
  openAddChannel();
  ASSERT_TRUE(tap("Join a Hashtag Channel"));
  EXPECT_EQ(shell->_chan, GuiShell::ChanStep::Hashtag);
}

// Negative: with every slot used, the three that take a slot do nothing.
TEST_F(Gui, AFullTableRefusesTheChoicesThatNeedASlot) {
  for (auto& s : model->slots) s.used = true;
  openAddChannel();
  for (int i = 0; i < 4; i++) {
    if (i == 2) continue;   // Public: held here, so not listed
    shell->_chan = GuiShell::ChanStep::Kinds;
    shell->rebuild();
    pump();
    tap(kChoices[i]);
    EXPECT_EQ(shell->_chan, GuiShell::ChanStep::Kinds) << kChoices[i] << " opened with no free slot";
  }
  EXPECT_TRUE(model->added.empty());
}

// ------------------------------------------------------------ a conversation

// At the newest, each new message is shown as it arrives, short or long,
// message after message (owner, bench: "if I'm at newest, it should advance").
TEST_F(Gui, AtTheNewestEachNewMessageIsShown) {
  const int t = openBusyChannel();
  ASSERT_GT(lv_obj_get_scroll_top(shell->_msg_list), 0) << "the history doesn't scroll: the test proves nothing";
  ASSERT_LE(belowView(), 0) << "opening didn't start at the newest";
  for (int i = 14; i < 22; i++) {
    newMessage(t, i);
    EXPECT_LE(belowView(), 0) << "message " << i << " arrived below the view";
    char want[16];
    snprintf(want, sizeof want, "msg %d", i);
    EXPECT_TRUE(shows(want));
  }
}

// Scrolled up, the reader keeps their place as messages come in, and Newest
// offers the way back (owner: "it should stay where I've scrolled to").
TEST_F(Gui, ScrolledUpTheViewHoldsItsPlace) {
  const int t = openBusyChannel();
  lv_obj_scroll_to_y(shell->_msg_list, 0, LV_ANIM_OFF);
  pump();
  const lv_coord_t at = lv_obj_get_scroll_y(shell->_msg_list);
  for (int i = 14; i < 18; i++) {
    newMessage(t, i);
    EXPECT_EQ(lv_obj_get_scroll_y(shell->_msg_list), at) << "message " << i << " moved the reader";
  }
  EXPECT_GT(belowView(), 0);
  ASSERT_NE(shell->_jump_newest, nullptr);
  EXPECT_FALSE(lv_obj_has_flag(shell->_jump_newest, LV_OBJ_FLAG_HIDDEN)) << "no way back to the newest";
}

// Back at the newest (the Newest button), it follows again.
TEST_F(Gui, BackAtTheNewestItFollowsAgain) {
  const int t = openBusyChannel();
  lv_obj_scroll_to_y(shell->_msg_list, 0, LV_ANIM_OFF);
  pump();
  newMessage(t, 14);
  ASSERT_GT(belowView(), 0);
  lv_event_send(shell->_jump_newest, LV_EVENT_CLICKED, nullptr);
  pump();
  ASSERT_LE(belowView(), 0) << "Newest didn't reach the newest";
  for (int i = 15; i < 19; i++) {
    newMessage(t, i);
    EXPECT_LE(belowView(), 0) << "message " << i << " arrived below the view after Newest";
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
