// #2218: the GUI's lock screen shows nothing private. The real GuiShell
// (Screens.cpp and friends) runs against LVGL 8.3.11 with the device's
// lv_conf.h, on a 240x320 display whose flush draws into a 1-bit frame as the
// e-paper port does (LvglPort.cpp).
//
// env:native_gui_lock builds it as the T-Deck Pro's e-paper GUI
// (GUI_PANEL_EINK): the lock screen sits on the glass while the panel sleeps,
// so it draws only the node's name and how to unlock. env:native_gui_lock_lit
// builds it as a lit panel's GUI (Heltec V4 TFT, R8), where the lock screen
// keeps its clock. On both, no message text, sender or draft reaches it.

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <lvgl.h>

#define private public
#define protected public
#include "Screens.h"
#undef private
#undef protected

#include <helpers/gui/EinkFrame.h>
#include <helpers/gui/GuiLayout.h>
#include <helpers/gui/GuiTheme.h>
#include <helpers/gui/MsgStore.h>

using namespace offband::gui;

extern "C" const lv_img_dsc_t ob_logo_64;

namespace {

const char* const kNode = "Bench-7";
const char* const kHint = "Press USR or swipe up to unlock";
// What must never reach the lock screen.
const char* const kSecretText = "PRIVATE-TEXT-7731";
const char* const kSecretSender = "Wren-Secret";
const char* const kDraft = "DRAFT-WORDS";

// --------------------------------------------------------------- the panel

uint8_t g_frame[240 * 320 / 8];

void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  for (lv_coord_t y = area->y1; y <= area->y2; y++) {
    for (lv_coord_t x = area->x1; x <= area->x2; x++) {
      const uint32_t i = uint32_t(x) / 8 + uint32_t(y) * (240 / 8);
      const uint8_t bit = uint8_t(1 << (7 - x % 8));
      if (einkInk(lv_color_brightness(*px++))) g_frame[i] &= uint8_t(~bit);
      else g_frame[i] |= bit;
    }
  }
  lv_disp_flush_ready(drv);
}

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
  uint32_t clock = 1763400000;   // 2025-11-17
  int dm = -1;

  void fill() {
    msgs.begin(200, 16);
    const uint8_t pub[16] = {1};
    const int ch = msgs.thread(ThreadKind::Channel, pub, sizeof pub, "#Public");
    msgs.addIncoming(ch, clock - 600, kSecretSender, kSecretText, strlen(kSecretText), 2, 20);
    const uint8_t wren[32] = {7};
    dm = msgs.thread(ThreadKind::Direct, wren, sizeof wren, kSecretSender);
    msgs.addIncoming(dm, clock - 300, kSecretSender, kSecretText, strlen(kSecretText), 1, 20);
  }

  const MsgStore& store() override { return msgs; }
  NotifyMode threadMode(int) override { return NotifyMode::All; }
  void threadOpened(int) override {}   // left unread: the lock can't show it either way
  void routeMeta(int, char* out, size_t n) override { snprintf(out, n, "1 hop"); }
  int zone() override { return 0; }
  void setZone(int) override {}
  int gpsZone() override { return 0; }
  bool gpsLiveFix() override { return false; }
  uint32_t utcNow() override { return clock; }
  void sendAdvert() override {}
  void loraPopover() override {}
  size_t composeBudget(int) override { return 140; }
  const char* cannedText(int) override { return ""; }
  bool sendText(int, const char*, size_t) override { return true; }
  uint8_t dmAttempt(uint32_t) override { return 0; }
  void resend(uint32_t) override {}
  void setThreadMode(int, NotifyMode) override {}
  const char* nodeName() override { return kNode; }
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
  void deviceInfo(DeviceInfo* out) override { *out = DeviceInfo{}; }
  void setBleOn(bool) override {}
  bool setBlePin(uint32_t) override { return true; }
  bool setFullMv(uint16_t) override { return true; }
  void learnFullAgain() override {}
  bool setLogLevel(int) override { return true; }
  bool advertNow() override { return true; }
  bool syncTimeFromGps() override { return false; }
  void reboot() override {}
  uint16_t deleteAllPaths() override { return 0; }
  int channelSlots(ChannelSlot*, int) override { return 0; }
  int addChannel(int, const char*, const uint8_t*) override { return -1; }
  void newChannelKey(uint8_t* out) override { memset(out, 0, kChannelKeyLen); }
  bool channelOf(int, uint8_t* key, bool* p) override {
    memset(key, 0, kChannelKeyLen);
    *p = false;
    return true;
  }
  bool leaveChannel(int) override { return false; }
  bool channelHeld(const uint8_t*) override { return true; }
};

// --------------------------------------------------------------- the rig

void pump(int passes = 3) {
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
  static lv_color_t b1[240 * 32], b2[240 * 32];   // as the device: two 32-line buffers
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

// Every label shown, on the screen and on the top layer (sheets, the SD
// notice, the popover). A hidden object hides what's inside it.
void collect(lv_obj_t* o, std::vector<std::string>* out) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_obj_check_type(o, &lv_label_class)) out->push_back(lv_label_get_text(o));
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) collect(lv_obj_get_child(o, int32_t(i)), out);
}

std::vector<std::string> shownText() {
  std::vector<std::string> out;
  collect(lv_scr_act(), &out);
  collect(lv_layer_top(), &out);
  return out;
}

bool shows(const char* what) {
  for (const std::string& s : shownText())
    if (s.find(what) != std::string::npos) return true;
  return false;
}

// The whole screen drawn into the frame, as a panel refresh would see it.
std::array<uint8_t, sizeof g_frame> drawFrame() {
  lv_obj_invalidate(lv_scr_act());
  lv_obj_invalidate(lv_layer_top());
  lv_refr_now(nullptr);
  std::array<uint8_t, sizeof g_frame> f;
  memcpy(f.data(), g_frame, sizeof g_frame);
  return f;
}

}  // namespace

// A fresh shell over a fresh store, portrait, as UITask sets it up.
class Lock : public ::testing::Test {
protected:
  FakeModel* model = nullptr;
  GuiShell* shell = nullptr;

  void SetUp() override {
    setupRig();
    lv_obj_clean(lv_scr_act());
    lv_obj_clean(lv_layer_top());
    model = new FakeModel();
    model->fill();
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

  // The screens a timeout can catch the GUI on.
  enum class Start { Home, Chats, Conversation, Compose, ThreadMenu, Setup, Nodes, Map, SdNotice };
  void open(Start s) {
    switch (s) {
      case Start::Home: shell->showTab(Tab::Home); break;
      case Start::Chats: shell->showTab(Tab::Chat); break;
      case Start::Conversation:
        shell->showTab(Tab::Chat);
        shell->openThread(model->dm);
        break;
      case Start::Compose:
        shell->showTab(Tab::Chat);
        shell->openThread(model->dm);
        shell->openCompose(model->dm);
        for (const char* p = kDraft; *p; p++) shell->_draft.insert(p, 1);   // one character a call
        ASSERT_STREQ(shell->_draft.text(), kDraft);
        shell->rebuild();
        break;
      case Start::ThreadMenu:
        shell->showTab(Tab::Chat);
        shell->openThread(model->dm);
        shell->openSheet(GuiShell::Sheet::Menu);
        break;
      case Start::Setup: shell->showTab(Tab::Setup); break;
      case Start::Nodes: shell->showTab(Tab::Nodes); break;
      case Start::Map: shell->showTab(Tab::Map); break;
      case Start::SdNotice:
        shell->showTab(Tab::Home);
        shell->sdNotice(true, 3, 10);
        break;
    }
    pump();
  }
};

// The negative control: the screens below do show what the lock must not,
// so a lock that let them through would be caught.
TEST_F(Lock, TheScreensBelowShowWhatTheLockMustNot) {
  open(Start::Conversation);
  EXPECT_TRUE(shows(kSecretText));
  open(Start::Compose);
  EXPECT_TRUE(shows(kDraft));
  open(Start::SdNotice);
  EXPECT_TRUE(shows("Copying messages to the SD card"));
}

TEST_F(Lock, NothingFromAnyScreenReachesTheLock) {
  for (Start s : {Start::Home, Start::Chats, Start::Conversation, Start::Compose, Start::ThreadMenu, Start::Setup,
                  Start::Nodes, Start::Map, Start::SdNotice}) {
    SCOPED_TRACE(int(s));
    open(s);
    shell->showLock();
    pump();
    ASSERT_TRUE(shell->locked());
    EXPECT_FALSE(shows(kSecretText));
    EXPECT_FALSE(shows(kSecretSender));
    EXPECT_FALSE(shows(kDraft));
    EXPECT_FALSE(shows("#Public"));
    EXPECT_FALSE(shows("new"));                 // no "2 new", no "+1 more"
    EXPECT_FALSE(shows("No new messages"));     // nor that there are none
    EXPECT_FALSE(shows("Copying messages"));    // the top layer is cleared too
    EXPECT_TRUE(shows(kHint));
    shell->showTab(shell->tab());   // unlock for the next start
    pump();
  }
}

// Pixel for pixel: the lock screen is the same whatever it covered, so
// nothing of the screen below shows through anywhere on the glass.
TEST_F(Lock, TheLockFrameIsTheSameFromEveryScreen) {
  open(Start::Home);
  shell->showLock();
  pump();
  const auto ref = drawFrame();
  shell->showTab(Tab::Home);
  pump();
  for (Start s : {Start::Chats, Start::Conversation, Start::Compose, Start::ThreadMenu, Start::Setup, Start::Nodes,
                  Start::Map, Start::SdNotice}) {
    SCOPED_TRACE(int(s));
    open(s);
    EXPECT_NE(drawFrame(), ref);   // the control: the screen below differs
    shell->showLock();
    pump();
    EXPECT_EQ(drawFrame(), ref);
    shell->showTab(shell->tab());
    pump();
  }
}

#ifdef GUI_PANEL_EINK
// E-paper: the lock sits on the glass while the panel sleeps, so it draws
// only what doesn't change: the node's name and how to unlock.
TEST_F(Lock, EpaperShowsOnlyTheLockupTheNameAndTheHint) {
  open(Start::Conversation);
  shell->showLock();
  pump();
  const std::vector<std::string> want = {"offband", kNode, kHint};
  EXPECT_EQ(shownText(), want);
  EXPECT_EQ(shell->_lock_clock, nullptr);
  EXPECT_EQ(shell->_lock_sub, nullptr);
  EXPECT_EQ(shell->_lock_zone, nullptr);
}

// The second-by-second update has nothing to change on e-paper.
TEST_F(Lock, EpaperIgnoresTheClockUpdate) {
  shell->showLock();
  pump();
  LockData d{};
  snprintf(d.clock, sizeof d.clock, "9:07 PM");
  snprintf(d.sub, sizeof d.sub, "Fri 16 Oct · %s · 80%%", kNode);
  snprintf(d.zone, sizeof d.zone, "Eastern");
  shell->updateLock(d);
  pump();
  EXPECT_FALSE(shows("9:07"));
  EXPECT_FALSE(shows("80%"));
  EXPECT_FALSE(shows("Eastern"));
}
#else
// A lit panel keeps its clock: it ticks while the screen is lit.
TEST_F(Lock, ALitPanelKeepsTheClock) {
  shell->showLock();
  pump();
  ASSERT_NE(shell->_lock_clock, nullptr);
  LockData d{};
  snprintf(d.clock, sizeof d.clock, "9:07 PM");
  snprintf(d.sub, sizeof d.sub, "Fri 16 Oct · %s · 80%%", kNode);
  snprintf(d.zone, sizeof d.zone, "Eastern");
  shell->updateLock(d);
  pump();
  EXPECT_TRUE(shows("9:07"));
  EXPECT_TRUE(shows("PM"));
  EXPECT_TRUE(shows("Eastern"));
  EXPECT_FALSE(shows(kSecretText));
}

// Landscape (Heltec V4 TFT, R8, T-Deck): the lockup over the clock still
// clears the unlock hint and stays on screen.
TEST_F(Lock, TheLockupAndClockFitInLandscape) {
  for (uint16_t rot : {uint16_t(90), uint16_t(270)}) {
    shell->setLayout(layoutFor(rot));
    lv_disp_set_rotation(nullptr, rot == 90 ? LV_DISP_ROT_90 : LV_DISP_ROT_270);
    shell->_locked = false;
    shell->showLock();
    pump();
    lv_obj_update_layout(lv_scr_act());
    ASSERT_EQ(lv_disp_get_ver_res(nullptr), 240) << "not landscape";
    lv_obj_t* hint = nullptr;
    lv_obj_t* col = nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(lv_scr_act()); i++) {
      lv_obj_t* o = lv_obj_get_child(lv_scr_act(), int32_t(i));
      if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), kHint) == 0) hint = o;
      else if (lv_obj_get_child_cnt(o) > 2) col = o;   // the centered column
    }
    ASSERT_NE(hint, nullptr);
    ASSERT_NE(col, nullptr);
    lv_area_t h, c;
    lv_obj_get_coords(hint, &h);
    lv_obj_get_coords(col, &c);
    EXPECT_GE(c.y1, 0) << "rotation " << rot << ": the lockup runs off the top";
    EXPECT_LT(c.y2, h.y1) << "rotation " << rot << ": the clock column runs into the unlock hint";
  }
  lv_disp_set_rotation(nullptr, LV_DISP_ROT_NONE);
}
#endif

// The Offband lockup is on the lock screen, on every panel: the mark over the
// wordmark, as at boot (owner, 2026-11-21).
TEST_F(Lock, TheLockShowsTheOffbandLockup) {
  open(Start::Home);
  shell->showLock();
  pump();
  EXPECT_TRUE(shows("offband"));
  int marks = 0;
  std::vector<lv_obj_t*> stack = {lv_scr_act()};
  while (!stack.empty()) {
    lv_obj_t* o = stack.back();
    stack.pop_back();
    if (lv_obj_check_type(o, &lv_img_class) && lv_img_get_src(o) == &ob_logo_64) marks++;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) stack.push_back(lv_obj_get_child(o, int32_t(i)));
  }
  EXPECT_EQ(marks, 1) << "the Offband mark is missing from the lock screen, or drawn twice";
}

// Negative control: the lockup belongs to the lock screen, not the screen it covers.
TEST_F(Lock, TheScreenBelowHasNoLockup) {
  open(Start::Conversation);
  EXPECT_FALSE(shows("offband"));
}

TEST_F(Lock, UnlockingReturnsToComposeWithTheDraft) {
  open(Start::Compose);
  shell->showLock();
  pump();
  EXPECT_FALSE(shows(kDraft));
  EXPECT_TRUE(shell->action(Action::Next));   // USR
  pump();
  EXPECT_FALSE(shell->locked());
  EXPECT_TRUE(shell->composing());
  EXPECT_STREQ(shell->_draft.text(), kDraft);
  EXPECT_TRUE(shows(kDraft));
}

TEST_F(Lock, UnlockingReturnsToTheOpenConversation) {
  open(Start::Conversation);
  shell->showLock();
  pump();
  EXPECT_TRUE(shell->action(Action::Open));   // USR held, or Enter
  pump();
  EXPECT_FALSE(shell->locked());
  EXPECT_EQ(shell->tab(), Tab::Chat);
  EXPECT_EQ(shell->_thread, model->dm);
}

// A tab switch queued by a tap just before the lock (later()) must not run
// after it and unlock the screen.
TEST_F(Lock, ASwitchQueuedBeforeTheLockDoesNotUndoIt) {
  open(Start::Home);
  shell->later([](GuiShell* s) { s->showTab(Tab::Setup); });
  shell->showLock();
  pump();
  EXPECT_TRUE(shell->locked());
  EXPECT_FALSE(shows("Setup"));
}

// The control for the test above: unlocked, the same queued switch runs.
TEST_F(Lock, AQueuedSwitchRunsWhenNothingLocks) {
  open(Start::Home);
  shell->later([](GuiShell* s) { s->showTab(Tab::Setup); });
  pump();
  EXPECT_EQ(shell->tab(), Tab::Setup);
}

// AUX (Prev, Back) doesn't unlock.
TEST_F(Lock, BackAndPrevDoNotUnlock) {
  shell->showLock();
  pump();
  EXPECT_FALSE(shell->action(Action::Back));
  EXPECT_FALSE(shell->action(Action::Prev));
  EXPECT_TRUE(shell->locked());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
