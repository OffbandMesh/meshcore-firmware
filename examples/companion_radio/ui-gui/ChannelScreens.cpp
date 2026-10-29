// Adding a channel on the device (Epic R #1743, R2 #1749). Design of record:
// epic-r-channels-mockups.html, owner-approved 2026-10-21. The radio is a
// standalone device; joining a channel must not need a phone.
//
// One step at a time under the Chat tab. The list steps (the kinds, a private
// channel's key choice, a key shown to share) take the focus group like any
// list; the typing steps share the keyboard and its button cursor with
// Compose. Back steps back; from the kinds it returns to Chats.

#include <string.h>

#include <MeshLog.h>
#include <helpers/ChannelKeys.h>
#include <helpers/ChannelSlotGuard.h>   // #1896: Public's slot

#include "Screens.h"
#include "ShellWidgets.h"

using namespace offband::gui;
using namespace shell;

namespace {

constexpr int kMaxSlots = 64;   // more than any board's MAX_GROUP_CHANNELS

// Glyphs the subset fonts carry (fonts/README.md): the mockups' shapes,
// drawn from what's in the build.
const char* const kGlyphPublic = "\xF0\x9F\x8C\x90";    // 🌐
const char* const kGlyphPrivate = "\xF0\x9F\x94\x92";   // 🔒
const char* const kGlyphNewKey = "\xE2\x9C\xA8";        // ✨
const char* const kGlyphEnterKey = "\xE2\x9C\x8F";      // ✏

enum ChanRow : int { RowPublic = 0, RowHashtag = 1, RowPrivate = 2, RowNewKey = 3, RowEnterKey = 4 };

}  // namespace

// "+" in Chats (R1 #1748, mockup 1): what to start. A sheet like a channel's
// menu, with no thread behind it.
void GuiShell::openStartSheet() {
  closeSheet();
  _sheet = Sheet::Start;
  _sheet_obj = sheetBackdrop();
  lv_obj_add_event_cb(
      _sheet_obj,
      [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
          static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeSheet(); });
        }
      },
      LV_EVENT_CLICKED, this);
  lv_obj_t* sheet = sheetPanel(_sheet_obj);
  lv_obj_t* tl = label(sheet, FontRole::Body, _t.ink, "Start a chat");
  lv_obj_set_style_pad_left(tl, 12, 0);
  lv_obj_set_style_pad_top(tl, 10, 0);
  lv_obj_set_style_pad_bottom(tl, 6, 0);

  static const char* const kName[2] = {"Message a node", "Add a channel"};
  static const char* const kSub[2] = {"Pick someone in Nodes", "Public, hashtag or private"};
  _sheet_count = 2;
  for (uint8_t i = 0; i < _sheet_count; i++) {
    lv_obj_t* row = box(sheet, LV_PCT(100), landscape() ? 30 : 38);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t* nl = label(row, FontRole::Body, _t.ink, kName[i]);
    lv_obj_set_width(nl, LV_PCT(100));
    oneLine(nl, FontRole::Body);
    if (_lay.hints) {
      lv_obj_t* sl = label(row, FontRole::Label, _t.dim, kSub[i]);
      lv_obj_set_width(sl, LV_PCT(100));
      oneLine(sl, FontRole::Label);
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(intptr_t(i)));
    lv_obj_add_event_cb(
        row,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_sheet_hot = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))));
          self->later([](GuiShell* g) { g->sheetPick(g->_sheet_hot); });
        },
        LV_EVENT_CLICKED, this);
    _sheet_rows[i] = row;
  }
  sheetHot(0);
}

// A private channel's key, from its menu (R3 #1750, mockup 7): the same
// screen a new key is shown on; Done or Back returns to the chat.
void GuiShell::showChannelKey() {
  const Thread* th = _model->store().threadAt(_thread);
  bool priv = false;
  if (!th || !_model->channelOf(_thread, _chan_key, &priv) || !priv) return;
  textf(_chan_name, sizeof _chan_name, "%s", th->name);
  _chan_thread = _thread;
  _tab = Tab::Chat;
  chanGo(ChanStep::ShowKey);
}

// Leave, confirmed (R3 #1750, mockup 9): what goes and what stays; Cancel
// first and the default, Leave red on the right.
void GuiShell::openLeaveSheet() {
  const Thread* th = _model->store().threadAt(_thread);
  uint8_t key[kChannelKeyLen];
  bool priv = false;
  if (!th || !_model->channelOf(_thread, key, &priv)) return;
  closeSheet();
  _sheet = Sheet::Leave;
  _leave_thread = _thread;   // what Leave acts on is what the sheet named
  _sheet_obj = sheetBackdrop();
  lv_obj_add_event_cb(
      _sheet_obj,
      [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
          static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->closeSheet(); });
        }
      },
      LV_EVENT_CLICKED, this);
  lv_obj_t* sheet = sheetPanel(_sheet_obj);
  char title[64], raw[64];
  textf(raw, sizeof raw, "Leave %s?", th->name);
  displayText(raw, strlen(raw), title, sizeof title);
  lv_obj_t* tl = label(sheet, FontRole::Body, _t.ink, title);
  lv_obj_set_style_pad_left(tl, 12, 0);
  lv_obj_set_style_pad_top(tl, 10, 0);
  lv_obj_t* sl = label(sheet, FontRole::Label, _t.dim, "");
  userText(sl, priv ? "This radio stops receiving it and the chat is removed. Its key is forgotten: to come back "
                      "you'll need it again."
                    : "This radio stops receiving it and the chat is removed. You can join it again any time.");
  lv_obj_set_width(sl, LV_PCT(100));
  lv_label_set_long_mode(sl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_hor(sl, 12, 0);
  lv_obj_set_style_pad_bottom(sl, 6, 0);

  lv_obj_t* br = box(sheet, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(br, 12, 0);
  lv_obj_set_style_pad_top(br, 6, 0);
  lv_obj_set_flex_flow(br, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(br, 6, 0);
  for (uint8_t i = 0; i < 2; i++) {
    const bool go = i == 1;
    lv_obj_t* b = box(br, 0, 32);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, _look.radius, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, c(go ? _t.bad : _t.line), 0);
    lv_obj_center(label(b, FontRole::Body, go ? _t.bad : _t.ink, go ? "Leave" : "Cancel"));
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(b, reinterpret_cast<void*>(intptr_t(i)));
    lv_obj_add_event_cb(
        b,
        [](lv_event_t* e) {
          GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
          self->_sheet_hot = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))));
          self->later([](GuiShell* g) { g->sheetPick(g->_sheet_hot); });
        },
        LV_EVENT_CLICKED, this);
    _sheet_rows[i] = b;
  }
  _sheet_count = 2;
  sheetHot(0);   // Cancel: a held button never leaves by accident
}

void GuiShell::openAddChannel() {
  closeSheet();
  _thread = -1;
  _tab = Tab::Chat;
  chanGo(ChanStep::Kinds);
}

void GuiShell::closeAddChannel() {
  _chan = ChanStep::None;
  _hot = -1;
  rebuild();
}

void GuiShell::chanGo(ChanStep step) {
  _chan = step;
  _hot = -1;
  _chan_already = false;
  switch (step) {
    case ChanStep::Hashtag:
      _chan_buf.start(kChannelNameMax - 1);   // the '#' is the 31st
      _page.select(KeyPage::Letters);
      _shift = ShiftState();
      break;
    case ChanStep::PrivName:
      _chan_buf.start(kChannelNameMax);
      _page.select(KeyPage::Letters);
      _shift = ShiftState();
      _shift.autoCap("", 0);
      break;
    case ChanStep::KeyEntry:
      _chan_buf.start(kChannelKeyHexLen);
      _page.select(KeyPage::Hex);
      break;
    default:
      break;
  }
  rebuild();
}

void GuiShell::chanBack() {
  switch (_chan) {
    case ChanStep::Hashtag:
    case ChanStep::PrivName:
      chanGo(ChanStep::Kinds);
      break;
    case ChanStep::PrivKey:
      chanGo(ChanStep::PrivName);
      _chan_buf.insert(_chan_name, strlen(_chan_name));   // the name, as typed
      refreshChanField();
      break;
    case ChanStep::KeyEntry:
      chanGo(ChanStep::PrivKey);
      break;
    case ChanStep::ShowKey:
      chanOpenThread(_chan_thread);   // the channel exists: Back is Done
      break;
    default:
      closeAddChannel();
      break;
  }
}

// Adds the channel, or finds the slot already holding its key; returns the
// chat's thread, or -1 if there was no room or the write failed.
int GuiShell::chanJoin(const char* name, const uint8_t key[kChannelKeyLen]) {
  ChannelSlot slots[kMaxSlots];
  const int n = _model->channelSlots(slots, kMaxSlots);
  int slot = findChannelByKey(slots, n, key);
  // #1896: Public goes into its own slot, displacing what's there (owner, 2026-10-28).
  if (slot < 0 && memcmp(key, offband::kPublicChannelKey, kChannelKeyLen) == 0) slot = offband::kPublicSlot;
  if (slot < 0) slot = firstFreeChannelSlot(slots, n);
  if (slot < 0) return -1;
  const int t = _model->addChannel(slot, name, key);
  if (t < 0) {
    // #1865: the slot changed under us, or the write was refused; nothing was joined.
    mesh_log_line(MLOG_BOOT, "[gui] channel join failed: slot %d\n", slot);
    return -1;
  }
  mesh_log_line(MLOG_BOOT, "[gui] channel joined: slot %d thread %d\n", slot, t);
  return t;
}

void GuiShell::chanOpenThread(int thread) {
  _chan = ChanStep::None;
  _hot = -1;
  if (thread >= 0) {
    openThread(thread);
  } else {
    rebuild();
  }
}

// The confirm key (Join / Done), or Enter on a keyboard.
void GuiShell::chanSubmit() {
  if (_chan == ChanStep::Hashtag) {
    if (namesPublic(_chan_buf.text())) {
      // #1896: "public" is the real Public, never a #public hashtag channel.
      later([](GuiShell* g) { g->chanOpenThread(g->chanJoin("Public", offband::kPublicChannelKey)); });
      return;
    }
    if (!hashtagName(_chan_buf.text(), _chan_name, sizeof _chan_name)) return;   // empty: nothing to join
    offband::hashtagChannelKey(_chan_name, _chan_key);
    // After this event: opening the chat rebuilds, deleting the key that sent it.
    later([](GuiShell* g) { g->chanOpenThread(g->chanJoin(g->_chan_name, g->_chan_key)); });
    return;
  }
  if (_chan == ChanStep::PrivName) {
    if (_chan_buf.length() == 0) return;
    textf(_chan_name, sizeof _chan_name, "%s", _chan_buf.text());
    later([](GuiShell* g) { g->chanGo(ChanStep::PrivKey); });
    return;
  }
  if (_chan == ChanStep::KeyEntry) {
    uint8_t key[kChannelKeyLen];
    if (parseChannelKey(_chan_buf.text(), key) != kChannelKeyHexLen) return;   // not 32 yet
    memcpy(_chan_key, key, sizeof _chan_key);
    later([](GuiShell* g) { g->chanOpenThread(g->chanJoin(g->_chan_name, g->_chan_key)); });
  }
}

// A row on a list step, by what it is (ChanRow).
void GuiShell::chanPick(int row) {
  ChannelSlot slots[kMaxSlots];
  const int n = _model->channelSlots(slots, kMaxSlots);
  const bool full = firstFreeChannelSlot(slots, n) < 0;
  switch (row) {
    case RowPublic:
      // #1896: never refused for a full table; Public takes its own slot.
      chanOpenThread(chanJoin("Public", offband::kPublicChannelKey));
      return;
    case RowHashtag:
      if (!full) chanGo(ChanStep::Hashtag);
      return;
    case RowPrivate:
      if (!full) chanGo(ChanStep::PrivName);
      return;
    case RowNewKey:
      _model->newChannelKey(_chan_key);
      _chan_thread = chanJoin(_chan_name, _chan_key);
      chanGo(ChanStep::ShowKey);
      return;
    case RowEnterKey:
      chanGo(ChanStep::KeyEntry);
      return;
    default:
      return;
  }
}

// Buttons. The typing steps walk the key cursor as Compose does; the list
// steps leave Next / Prev / Open to the focus group. Back is ours everywhere.
bool GuiShell::chanAction(Action a) {
  if (a == Action::Back) {
    chanBack();
    return true;
  }
  if (!chanTyping()) return false;
  switch (a) {
    case Action::Next:
      stepKey(1);
      return true;
    case Action::Prev:
      stepKey(-1);
      return true;
    case Action::Open:
      if (_hot >= 0) pressKey(_hot);
      return true;
    default:
      return false;
  }
}

// What each typing step will take. A hashtag can't hold a space (the name is
// the key); a key is hex only, so a stray letter can't make a wrong one.
bool GuiShell::chanAccepts(char ch) const {
  if (_chan == ChanStep::Hashtag) return hashtagAccepts(ch);
  if (_chan == ChanStep::KeyEntry) return channelKeyAccepts(ch);
  return true;
}

void GuiShell::refreshChanField() {
  if (!_chan_field) return;
  if (_chan == ChanStep::KeyEntry) {
    // Grouped in fours as it's typed (mockup 6).
    char shown[kChannelKeyHexLen + 8];
    size_t o = 0;
    const char* t = _chan_buf.text();
    for (size_t i = 0; t[i] && o + 2 < sizeof shown; i++) {
      if (i && i % 4 == 0) shown[o++] = ' ';
      const char ch = t[i];
      shown[o++] = (ch >= 'A' && ch <= 'F') ? char(ch - 'A' + 'a') : ch;
    }
    shown[o] = 0;
    lv_label_set_text(_chan_field, shown);
    if (_chan_count) {
      char cnt[12];
      textf(cnt, sizeof cnt, "%u/%u", unsigned(_chan_buf.length()), unsigned(kChannelKeyHexLen));
      lv_label_set_text(_chan_count, cnt);
    }
    return;
  }
  userText(_chan_field, _chan_buf.text());
  if (_chan == ChanStep::Hashtag && _chan_note) {
    // "You're already in #name": Join then opens that chat (mockup 4).
    char name[kChannelNameMax + 1];
    bool held = false;
    if (namesPublic(_chan_buf.text())) {
      // #1896: Join takes "public" to the real Public, so the note checks that.
      textf(name, sizeof name, "Public");
      ChannelSlot slots[kMaxSlots];
      const int n = _model->channelSlots(slots, kMaxSlots);
      held = findChannelByKey(slots, n, offband::kPublicChannelKey) >= 0;
    } else if (hashtagName(_chan_buf.text(), name, sizeof name)) {
      uint8_t key[kChannelKeyLen];
      offband::hashtagChannelKey(name, key);
      ChannelSlot slots[kMaxSlots];
      const int n = _model->channelSlots(slots, kMaxSlots);
      held = findChannelByKey(slots, n, key) >= 0;
    }
    if (held != _chan_already || held) {
      _chan_already = held;
      char note[64];
      if (held) textf(note, sizeof note, "You're already in %s", name);
      userText(_chan_note, held ? note : "Anyone who knows or guesses this name can read it.");
    }
  }
}

// ------------------------------------------------------------------ build

namespace {

// The step's header: back chevron and title, the whole strip a Back.
lv_obj_t* chanHeader(GuiShell* shell, lv_obj_t* scr, const ThemeTokens& t, int16_t w, int16_t y, const char* title,
                     void (*on_back)(lv_event_t*)) {
  lv_obj_t* h = box(scr, w, 28);
  lv_obj_set_pos(h, 0, y);
  lv_obj_set_style_pad_hor(h, 8, 0);
  lv_obj_set_flex_flow(h, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(h, 6, 0);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(h, 1, 0);
  lv_obj_set_style_border_color(h, c(t.line), 0);
  lv_obj_t* back = box(h, 10, 20);
  line(back, chevronPoints(), 3, t.sig);
  lv_obj_t* tl = label(h, FontRole::Body, t.ink);
  userText(tl, title);
  lv_obj_set_flex_grow(tl, 1);
  oneLine(tl, FontRole::Body);
  lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(h, on_back, LV_EVENT_CLICKED, shell);
  return h;
}

lv_obj_t* sectionLabel(lv_obj_t* scr, const ThemeTokens& t, int16_t x, int16_t y, const char* text) {
  lv_obj_t* l = label(scr, FontRole::Label, t.dim, text);
  lv_obj_set_pos(l, x, y);
  return l;
}

}  // namespace

void GuiShell::buildAddChannel(lv_obj_t* scr) {
  const int16_t w = _lay.content_w;
  const int16_t top = int16_t(kStatusH + 28);
  auto back = [](lv_event_t* e) {
    static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->chanBack(); });
  };

  // A list row (mockups 3 and 5): a tile, the name, a one-line explainer.
  auto row = [&](lv_obj_t* list, const char* glyph, const char* name, const char* sub, int id, bool dim) {
    lv_obj_t* r = box(list, LV_PCT(100), landscape() ? 32 : 40);
    lv_obj_set_style_pad_hor(r, 8, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, c(_t.line2), 0);
    lv_obj_t* tile = box(r, 22, 22);
    lv_obj_set_style_radius(tile, _look.radius, 0);
    lv_obj_set_style_bg_opa(tile, lv_opa_t(_t.wash >> 24), 0);
    lv_obj_set_style_bg_color(tile, c(_t.wash), 0);
    lv_obj_center(label(tile, FontRole::Body, dim ? _t.dim : _t.sig, glyph));
    lv_obj_t* col = box(r, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* nl = label(col, FontRole::Body, dim ? _t.dim : _t.ink, name);
    lv_obj_set_width(nl, LV_PCT(100));
    oneLine(nl, FontRole::Body);
    if (_lay.hints) {
      lv_obj_t* sl = label(col, FontRole::Label, _t.dim, sub);
      lv_obj_set_width(sl, LV_PCT(100));
      oneLine(sl, FontRole::Label);
    }
    label(r, FontRole::Body, _t.dim, "›");
    lv_obj_set_user_data(r, reinterpret_cast<void*>(intptr_t(id)));
    if (!dim) {
      focusable(r);
      lv_obj_add_event_cb(
          r,
          [](lv_event_t* e) {
            GuiShell* self = static_cast<GuiShell*>(lv_event_get_user_data(e));
            self->_chan_pick = int(intptr_t(lv_obj_get_user_data(lv_event_get_current_target(e))));
            self->later([](GuiShell* g) { g->chanPick(g->_chan_pick); });
          },
          LV_EVENT_CLICKED, this);
    }
    return r;
  };

  auto list = [&](int16_t y) {
    lv_obj_t* l = box(scr, w, LV_SIZE_CONTENT);
    lv_obj_set_pos(l, 0, y);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    return l;
  };

  // The field the typing steps type into, with what goes before the text.
  auto field = [&](int16_t y, const char* prefix, bool mono) {
    lv_obj_t* f = box(scr, w - 16, 30);
    lv_obj_set_pos(f, 8, y);
    lv_obj_set_style_pad_hor(f, 8, 0);
    lv_obj_set_style_border_width(f, 1, 0);
    lv_obj_set_style_border_color(f, c(_t.sig), 0);
    lv_obj_set_style_bg_opa(f, lv_opa_t(_t.wash >> 24), 0);
    lv_obj_set_style_bg_color(f, c(_t.wash), 0);
    lv_obj_set_style_radius(f, _look.radius, 0);
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (prefix) label(f, FontRole::Body, _t.dim, prefix);
    _chan_field = label(f, mono ? FontRole::Data : FontRole::Body, _t.ink);
    lv_obj_t* caret = box(f, 1, 14);
    lv_obj_set_style_bg_opa(caret, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(caret, c(_t.sig), 0);
    return f;
  };

  auto note = [&](int16_t y, uint32_t ink, const char* text) {
    lv_obj_t* n = label(scr, FontRole::BodySmall, ink, "");
    userText(n, text);
    lv_obj_set_width(n, w - 16);
    lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(n, 8, y);
    return n;
  };

  switch (_chan) {
    case ChanStep::Kinds: {
      chanHeader(this, scr, _t, w, kStatusH, "Add a channel", back);
      ChannelSlot slots[kMaxSlots];
      const int n = _model->channelSlots(slots, kMaxSlots);
      const int used = usedChannelSlots(slots, n);
      const bool full = firstFreeChannelSlot(slots, n) < 0;
      lv_obj_t* l = list(top);
      // Public only when this radio doesn't hold it (mockup 3).
      if (findChannelByKey(slots, n, offband::kPublicChannelKey) < 0) {
        // #1896: never dimmed for a full table; Public takes its own slot.
        row(l, kGlyphPublic, "Public", "The channel every MeshCore radio shares", RowPublic, false);
      }
      row(l, "#", "Hashtag channel", "Anyone who knows the name can read it", RowHashtag, full);
      row(l, kGlyphPrivate, "Private channel", "Only people with its key can read it", RowPrivate, full);
      char msg[64];
      if (full) textf(msg, sizeof msg, "All %d channel slots are in use. Leave one first.", n);
      else textf(msg, sizeof msg, "%d of %d channel slots used", used, n);
      lv_obj_update_layout(l);
      note(int16_t(top + lv_obj_get_height(l) + 8), full ? _t.warn : _t.dim, msg);
      return;
    }
    case ChanStep::Hashtag: {
      chanHeader(this, scr, _t, w, kStatusH, "Hashtag channel", back);
      sectionLabel(scr, _t, 10, int16_t(top + 6), "Name");
      field(int16_t(top + 18), "#", false);
      _chan_note = note(int16_t(top + 52), _t.dim, "Anyone who knows or guesses this name can read it.");
      buildKeyboard(scr);
      refreshChanField();
      return;
    }
    case ChanStep::PrivName: {
      chanHeader(this, scr, _t, w, kStatusH, "Private channel", back);
      sectionLabel(scr, _t, 10, int16_t(top + 6), "Name");
      field(int16_t(top + 18), nullptr, false);
      buildKeyboard(scr);
      refreshChanField();
      return;
    }
    case ChanStep::PrivKey: {
      chanHeader(this, scr, _t, w, kStatusH, "Private channel", back);
      sectionLabel(scr, _t, 10, int16_t(top + 6), "Name");
      field(int16_t(top + 18), nullptr, false);
      userText(_chan_field, _chan_name);
      _chan_field = nullptr;   // shown, not typed
      sectionLabel(scr, _t, 10, int16_t(top + 54), "Key");
      lv_obj_t* l = list(int16_t(top + 66));
      row(l, kGlyphNewKey, "Make a new key", "Then share it with the others", RowNewKey, false);
      row(l, kGlyphEnterKey, "Enter a key", "32 characters, 0\xE2\x80\x93" "9 and a\xE2\x80\x93" "f", RowEnterKey, false);
      return;
    }
    case ChanStep::KeyEntry: {
      chanHeader(this, scr, _t, w, kStatusH, _chan_name, back);
      sectionLabel(scr, _t, 10, int16_t(top + 6), "Key");
      _chan_count = label(scr, FontRole::Meta, _t.dim, "0/32");
      lv_obj_align(_chan_count, LV_ALIGN_TOP_RIGHT, -10, int16_t(top + 4));
      field(int16_t(top + 18), nullptr, true);
      note(int16_t(top + 52), _t.dim, "Copy it from the radio or app that made the channel.");
      buildKeyboard(scr);
      refreshChanField();
      return;
    }
    case ChanStep::ShowKey: {
      char title[64];
      textf(title, sizeof title, "%s \xC2\xB7 key", _chan_name);
      chanHeader(this, scr, _t, w, kStatusH, title, back);
      char hex[40];
      formatChannelKey(_chan_key, hex, sizeof hex);
      hex[19] = '\n';   // two lines of four groups
      lv_obj_t* kb = box(scr, w - 16, LV_SIZE_CONTENT);
      lv_obj_set_pos(kb, 8, int16_t(top + 8));
      lv_obj_set_style_pad_all(kb, 8, 0);
      lv_obj_set_style_border_width(kb, 1, 0);
      lv_obj_set_style_border_color(kb, c(_t.line), 0);
      lv_obj_set_style_radius(kb, _look.radius, 0);
      lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(kb, c(_t.bg2), 0);
      lv_obj_t* kl = label(kb, FontRole::Data, _t.ink, hex);
      lv_obj_set_style_text_align(kl, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_width(kl, LV_PCT(100));
      lv_obj_update_layout(kb);
      const int16_t below = int16_t(top + 8 + lv_obj_get_height(kb) + 8);
      lv_obj_t* wn = note(below, _t.warn, "Anyone with this key can read the channel. Share it only with the people who should.");
      lv_obj_update_layout(wn);
      lv_obj_t* done = box(scr, w - 16, 30);
      lv_obj_set_pos(done, 8, int16_t(below + lv_obj_get_height(wn) + 10));
      lv_obj_set_style_radius(done, _look.radius, 0);
      lv_obj_set_style_bg_opa(done, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(done, c(_t.sig), 0);
      lv_obj_center(label(done, FontRole::Body, _t.sigInk, "Done"));
      focusable(done);
      lv_obj_add_event_cb(
          done,
          [](lv_event_t* e) {
            static_cast<GuiShell*>(lv_event_get_user_data(e))->later([](GuiShell* g) { g->chanOpenThread(g->_chan_thread); });
          },
          LV_EVENT_CLICKED, this);
      lv_group_focus_obj(done);
      return;
    }
    default:
      return;
  }
}
