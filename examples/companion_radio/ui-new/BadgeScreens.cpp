// #1230: the badge's inbox and thread screens. See BadgeScreens.h.

#if UI_HAS_CARDKB

#include "BadgeScreens.h"

#include <stdio.h>
#include <string.h>
#include "BadgeUi.h"
#include "UITask.h"
#include "target.h"

using namespace badgeui;
using Store = MyMesh::BadgeMsgStore;
using offband::BadgeSend;

// #1244: NodePrefs ships a raw byte and BadgeLayout names the size it stands for. This
// is the only place that sees both, so it is where they are held together.
static_assert(DEFAULT_UI_TEXT_SIZE == badgeui::kDefaultTextSize,
              "NodePrefs ships a different text size than BadgeLayout's default names");

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

namespace {

// #1237, #1238: the badge's faces. Body carries the lists, threads and titles, and steps
// with the owner's Text size setting. Meta carries the Status screen's lines, a selected
// message's detail and the footers, and stays the smallest face at every step, where its
// extra room counts for more than its size costs.
//
// Each screen takes them at the top of its draw, so a change shows on the next frame.
const Face& uiBody() { return bodyFaceFor(the_mesh.getNodePrefs()->ui_text_size); }
const Face& uiMeta() { return detailFace(); }

// The rows under a title bar, in the body face.
int uiListRows() { return rowsFor(uiBody()) - 1; }

// #1233: when `t` happened, as message rows show it: today's clock time once a time
// zone is set and the phone or GPS has set the clock (the design's "12:04"), else an age.
void whenOf(uint32_t t, char* out, size_t n) {
  formatWhen(the_mesh.getNodePrefs()->ui_tz, t, rtc_clock.getCurrentTime(), the_mesh.badgeClockTrusted(), out, n);
}

int batteryPct(uint16_t mv) {
  const int pct = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

bool printable(uint8_t key) { return key >= 32 && key < 127; }

bool before(uint32_t now_ms, uint32_t until_ms) { return until_ms != 0 && (int32_t)(now_ms - until_ms) < 0; }

// One row in the Settings grammar (design 3a): the label from the left, the value flush
// right, the whole row lit when selected.
void listRow(DisplayDriver& d, int row, const char* label, const char* value, bool selected) {
  const Face& kBody = uiBody();
  char left[28];
  snprintf(left, sizeof(left), " %s", label);
  if (selected) fillRow(d, kBody, row);
  line(d, kBody, row, left, selected);
  if (value[0] != 0) lineRight(d, kBody, row, value, selected);
}

// #1235: the GPS while it's on, else null. MicroNMEA keeps its last fix after the GPS
// is turned off, which would otherwise read as current.
LocationProvider* liveGps(UITask* task) {
#if ENV_INCLUDE_GPS == 1
  if (task->getGPSState()) return sensors.getLocationProvider();
#else
  (void)task;
#endif
  return nullptr;
}

// #1235: whether a GPS module answered the sensor manager's check. A build that can't
// tell counts as having one, so it never claims the module is missing.
bool gpsModuleFound() {
#if ENV_INCLUDE_GPS == 1
  return sensors.gpsDetected();
#else
  return true;
#endif
}

}  // namespace

// ---- Inbox ------------------------------------------------------------------------

int InboxScreen::items(Item* out, int max) const {
  Store& store = the_mesh.badgeStore();
  uint8_t order[MyMesh::kBadgeConvos];
  const int n = store.ordered(order, MyMesh::kBadgeConvos);
  int k = 0;
  for (int i = 0; i < n && k < max; i++) out[k++] = {(int16_t)order[i], -1};
  for (int s = 0; s < MAX_GROUP_CHANNELS && k < max; s++) {
    ChannelDetails ch;
    if (the_mesh.getChannel(s, ch) && ch.name[0] != 0 && store.find(Store::Channel, ch.channel.secret) < 0) {
      out[k++] = {-1, (int16_t)s};
    }
  }
  return k;
}

// Who a row is: a conversation's kind and key, or for a channel the store hasn't seen
// yet, its secret, which is the key the store will give it.
bool InboxScreen::identity(const Item& item, uint8_t& kind, uint8_t* key) {
  if (item.convo >= 0) {
    const Store::Convo* v = the_mesh.badgeStore().convoAt(item.convo);
    if (v == nullptr) return false;
    kind = v->kind;
    memcpy(key, v->key, PUB_KEY_SIZE);
    return true;
  }
  ChannelDetails ch;
  if (!the_mesh.getChannel(item.slot, ch)) return false;
  kind = Store::Channel;
  memcpy(key, ch.channel.secret, PUB_KEY_SIZE);
  return true;
}

// Rows reorder as messages arrive; the selection stays on its conversation. If that's
// gone, it stays at the same position.
void InboxScreen::follow(const Item* list, int count) {
  uint8_t kind, key[PUB_KEY_SIZE];
  for (int i = 0; i < count; i++) {
    if (identity(list[i], kind, key) && kind == _sel_kind && memcmp(key, _sel_key, PUB_KEY_SIZE) == 0) {
      _sel = i;
      return;
    }
  }
  if (_sel >= count) _sel = count - 1;
  if (_sel < 0) _sel = 0;
  if (count > 0) identity(list[_sel], _sel_kind, _sel_key);
}

void InboxScreen::select(const Item* list, int count, int sel) {
  if (count == 0) return;
  _sel = sel < 0 ? 0 : (sel >= count ? count - 1 : sel);
  identity(list[_sel], _sel_kind, _sel_key);
  _marquee_from = millis();
}

void InboxScreen::open(const Item& item, char first_key) {
  int c = item.convo;
  if (c < 0) {   // a channel with nothing in it yet
    ChannelDetails ch;
    if (!the_mesh.getChannel(item.slot, ch)) return;
    c = the_mesh.badgeStore().convo(Store::Channel, ch.channel.secret, ch.name);
    if (c < 0) {
      _task->showAlert("Inbox full", 1000);
      return;
    }
  }
  _task->gotoThread(c, first_key);
}

void InboxScreen::flash() {
  Store& store = the_mesh.badgeStore();
  uint32_t newest = 0;
  _flash_convo = -1;
  for (int i = 0; i < MyMesh::kBadgeConvos; i++) {
    const Store::Convo* v = store.convoAt(i);
    if (v != nullptr && v->last_seq > newest) {
      newest = v->last_seq;
      _flash_convo = i;
    }
  }
  _flash_until = millis() + 1000;
}

// One inbox row: name on the left; the unread count and age on the right (design 1a,
// 1c). Selected or flashing, it inverts. A selected row too long to show whole
// becomes one string that scrolls (design 2a). Returns whether it is scrolling.
bool InboxScreen::drawItem(DisplayDriver& d, int row, const Item& item, bool selected, uint32_t now_ms) {
  const Face& kBody = uiBody();
  Store& store = the_mesh.badgeStore();
  const char* name = "";
  char sigil = '#';
  bool pinned = false, muted = false, active = false;
  uint16_t unread = 0;
  uint32_t last_time = 0;
  ChannelDetails ch;
  if (item.convo >= 0) {
    const Store::Convo* v = store.convoAt(item.convo);
    if (v == nullptr) return false;
    name = v->name;
    sigil = (v->kind == Store::Channel) ? '#' : '@';
    pinned = v->pinned;
    muted = v->muted;
    unread = v->unread;
    active = v->last_seq != 0;
    last_time = v->last_time;
  } else if (the_mesh.getChannel(item.slot, ch)) {
    name = ch.name;
  }

  char shown[32], left[40], count[5] = "", age[6] = "";
  d.translateUTF8ToBlocks(shown, name, sizeof(shown));
  // #1237: the pinned mark is drawn, not printed, so the name starts clear of it.
  snprintf(left, sizeof(left), "%s%c%s", pinned ? "  " : " ", sigil, shown);
  if (unread > 99) snprintf(count, sizeof(count), "99+");
  else if (unread > 0) snprintf(count, sizeof(count), "%u", (unsigned)unread);
  if (muted) snprintf(age, sizeof(age), "muted");
  else if (active) whenOf(last_time, age, sizeof(age));

  const int y = rowY(kBody, row);
  const bool inverted = selected || (item.convo == _flash_convo && before(now_ms, _flash_until));
  const int age_x = kScreenPx - kEdgePx - textPx(kBody, age);
  const int count_end = age[0] != 0 ? age_x - 3 : kScreenPx - kEdgePx;
  const int right_x = count[0] != 0 ? count_end - textPx(kBody, count) - 2 : (age[0] != 0 ? age_x : kScreenPx);
  if (inverted) fillRow(d, kBody, row);

  if (selected && textPx(kBody, left) + 4 > right_x) {
    char all[64];
    snprintf(all, sizeof(all), "%s   %s%s%s", left, count, count[0] != 0 ? " new   " : "", age);
    const int width = textPx(kBody, all);
    textAt(d, kBody, -marqueeOffset(width, kScreenPx, now_ms - _marquee_from), y, all, true);
    if (pinned) markPin(d, 0, y);
    return width > kScreenPx;
  }

  left[fitPx(kBody, left, right_x - 4)] = 0;   // a gap between name and count
  textAt(d, kBody, 0, y, left, inverted);
  if (pinned) markPin(d, 0, y);
  if (count[0] != 0) countBox(d, kBody, count_end, y, count, inverted);
  if (age[0] != 0) textAt(d, kBody, age_x, y, age, inverted);   // a muted row says "muted" here
  return false;
}

int InboxScreen::render(DisplayDriver& d) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  Store& store = the_mesh.badgeStore();
  Item list[kMaxItems];
  const int count = items(list, kMaxItems);
  follow(list, count);

  // The design: a fresh badge shows what the radio is doing, not an apology.
  if (count == 0) return _task->renderStatusAs(d, " Messages", 0);

  // Rows under the title: seven, or six while the cycle's breadcrumb takes the first.
  // When more follow, the last is cut in half: the design's way of saying so without
  // spending a row.
  const bool crumb = _task->breadcrumbShown();
  const int first_row = crumb ? 2 : 1;
  const int rows = kListRows - (crumb ? 1 : 0);
  int top = listTop(_sel, count, rows);
  const bool more_below = count - top > rows;
  if (more_below) top = listTop(_sel, count, rows - 1);

  char right[20] = "";
  if (crumb) {
    cycleTitle(right, sizeof(right), 0);
  } else {
    const unsigned unread = store.totalUnread();
    if (unread > 99) snprintf(right, sizeof(right), "99+ new");
    else if (unread > 0) snprintf(right, sizeof(right), "%u new", unread);
  }
  fillRow(d, kBody, 0);
  line(d, kBody, 0, " Messages", true);
  // The battery sits at the edge, then the scroll marks, then the count.
  int right_edge = kScreenPx - 14;
  battery(d, kScreenPx - 12, 1, batteryPct(_task->getBattMilliVolts()));
  if (!crumb && more_below) {
    markDown(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  if (!crumb && top > 0) {
    markUp(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  if (right[0] != 0) textAt(d, kBody, right_edge - textPx(kBody, right), 0, right, true);
  if (crumb) breadcrumb(d, kBody, 1, 0);

  const uint32_t now_ms = millis();
  bool scrolling = false;
  for (int r = 0; r < rows && top + r < count; r++) {
    scrolling |= drawItem(d, first_row + r, list[top + r], top + r == _sel, now_ms);
  }
  if (more_below) {
    dark(d);
    d.fillRect(0, kScreenRowsPx - 4, kScreenPx, 4);   // the bottom half of the last row
    lit(d);
  }
  if (scrolling) return 80;
  if (crumb) return 250;   // redraw when the breadcrumb goes
  if (before(now_ms, _flash_until)) return (int)(_flash_until - now_ms) + 10;
  return 1000;
}

bool InboxScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  Item list[kMaxItems];
  const int count = items(list, kMaxItems);
  follow(list, count);

  if (!_task->inputFromKeyboard()) {   // SW1: a tap moves along the cycle (design 1f)
    if (key == KEY_NEXT) _task->cycle(1);
    else if (key == KEY_PREV) _task->cycle(-1);
    else if (key == KEY_ENTER && count > 0) open(list[_sel], 0);
    return true;
  }
  switch (key) {
    case KEY_UP:
      if (_sel > 0) select(list, count, _sel - 1);
      return true;
    case KEY_DOWN:
      if (_sel + 1 < count) select(list, count, _sel + 1);
      return true;
    case KEY_ENTER:
      if (count > 0) open(list[_sel], 0);
      return true;
    case KEY_LEFT:    // the keyboard's way round the same cycle
      _task->cycle(-1);
      return true;
    case KEY_RIGHT:
      _task->cycle(1);
      return true;
#if defined(QCC_BADGE_SELFTEST)
    case KEY_TAB:     // #1207: the diag key test
      _task->gotoKeyTest();
      return true;
#endif
    default:
      break;
  }
  if (printable(key) && count > 0) {   // typing starts a reply to the selected row
    open(list[_sel], (char)key);
    return true;
  }
  return false;
}

// ---- Thread -----------------------------------------------------------------------

void ThreadScreen::begin(int convo) {
  Store& store = the_mesh.badgeStore();
  const Store::Convo* v = store.convoAt(convo);
  if (v == nullptr) return;
  const bool same = _convo >= 0 && v->kind == _kind && memcmp(v->key, _key, PUB_KEY_SIZE) == 0;
  _convo = convo;
  _kind = v->kind;
  memcpy(_key, v->key, PUB_KEY_SIZE);
  _unread_at_entry = v->unread;
  _entered_at = millis();
  _sel_seq = 0;
  _note_until = 0;
  if (!same) {
    _line.start(v->kind == Store::Channel ? compose::budget(true, strlen(the_mesh.getNodeName()), MAX_TEXT_LEN)
                                          : MAX_TEXT_LEN);
  }
  store.setOpen(convo);
}

void ThreadScreen::note(const char* text) {
  _note = text;
  _note_until = millis() + 2000;
}

void ThreadScreen::leave() {
  the_mesh.badgeStore().setOpen(-1);
  _task->gotoHomeScreen();
}

void ThreadScreen::send() {
  switch (the_mesh.uiSendTo(_convo, _line.text())) {
    case MyMesh::UiSend::Sent:    _line.clear(); break;
    case MyMesh::UiSend::Gone:    note(" no longer here"); break;
    case MyMesh::UiSend::NotSent: note(" not sent"); break;
    case MyMesh::UiSend::Busy:    note(" busy, try again"); break;
  }
}

bool ThreadScreen::failedSelected() const {
  const Store::Msg* m = the_mesh.badgeStore().msg(_sel_seq);
  return m != nullptr && m->outgoing && m->status == BadgeSend::Failed;
}

// Enter on a failed DM sends it again.
void ThreadScreen::resend() {
  if (!failedSelected()) return;
  if (the_mesh.uiResend(_sel_seq)) {
    _sel_seq = 0;
  } else {
    note(" not sent");
  }
}

void ThreadScreen::selectOlder() {
  const int n = the_mesh.badgeStore().thread(_convo, _seqs, kShow);
  if (n == 0) return;
  int i = n;
  for (int k = 0; k < n; k++) {
    if (_seqs[k] == _sel_seq) i = k;
  }
  if (i > 0) _sel_seq = _seqs[i - 1];
  _entered_at = millis() - 2000;   // the name bar gives way
}

void ThreadScreen::selectNewer() {
  if (_sel_seq == 0) return;
  const int n = the_mesh.badgeStore().thread(_convo, _seqs, kShow);
  for (int k = 0; k < n; k++) {
    if (_seqs[k] == _sel_seq) {
      _sel_seq = (k + 1 < n) ? _seqs[k + 1] : 0;   // past the newest: nothing selected
      return;
    }
  }
  _sel_seq = 0;
}

void ThreadScreen::drawRow(DisplayDriver& d, int screen_row, const Row& r, bool channel) {
  const Face& kBody = uiBody();
  const Face& kMeta = uiMeta();
  const int y = rowY(kBody, screen_row);
  const Store::Msg* m = the_mesh.badgeStore().msg(_seqs[r.msg]);
  if (m == nullptr) return;
  if (r.caret) textAt(d, kBody, 0, y, ">");

  if (r.kind == RowKind::Meta) {   // design 1a "Message selected": one line of detail
    char meta[32], age[6];
    whenOf(m->time, age, sizeof(age));
    if (m->outgoing) {
      switch (m->status) {
        case BadgeSend::Sending:   snprintf(meta, sizeof(meta), "sending"); break;
        case BadgeSend::Delivered:   // a DM's ACK, or a repeater heard passing a channel send on
          snprintf(meta, sizeof(meta), "%s %s", channel ? "repeated" : "delivered", age);
          break;
        case BadgeSend::Failed:    snprintf(meta, sizeof(meta), "failed  Enter=resend"); break;
        default:                   snprintf(meta, sizeof(meta), "sent %s", age); break;
      }
      // #1237: the detail line is the meta face, which leaves room for all of it.
      textAt(d, kMeta, kScreenPx - kEdgePx - textPx(kMeta, meta), y + 1, meta);
    } else {
      // Radio hops, as the design counts them: 1 is heard directly. A direct-routed
      // packet (0xFF) doesn't say how many.
      char hops[8];
      if (m->hops == 0xFF) snprintf(hops, sizeof(hops), "direct");
      else snprintf(hops, sizeof(hops), "%uhop%s", (unsigned)m->hops + 1, m->hops == 0 ? "" : "s");
      if (m->rssi != 0) snprintf(meta, sizeof(meta), "%s %s %d", age, hops, (int)m->rssi);
      else snprintf(meta, sizeof(meta), "%s %s", age, hops);
      textAt(d, kMeta, (channel ? kTagPx + kGapPx : 0) + 2, y + 1, meta);
    }
    return;
  }

  const MsgView& v = _views[r.msg];
  if (r.first && !v.outgoing && channel) {
    const int x = r.caret ? charPx(kBody, '>') + 1 : 0;
    lit(d);
    d.fillRect(x, y, textPx(kBody, _tag[r.msg]) + 2, kBody.row_px);
    textAt(d, kBody, x + 1, y, _tag[r.msg], true);
  }
  char text[64];
  const size_t len = r.span.len < sizeof(text) - 1 ? r.span.len : sizeof(text) - 1;
  memcpy(text, v.text + r.span.start, len);
  text[len] = 0;
  textAt(d, kBody, r.x_px, y, text);
  if (r.last && v.outgoing) {   // sending shows three dots; only a dead send shows a cross
    const int x = kScreenPx - kMarkPx;
    if (m->status == BadgeSend::Delivered) markTick(d, x, y);
    else if (m->status == BadgeSend::Sending) sendingDots(d, x, y);
    else if (m->status == BadgeSend::Failed) markCross(d, x, y);
  }
}

// The compose line under a dotted rule. The room left shows once typing starts.
void ThreadScreen::drawCompose(DisplayDriver& d, int row) {
  const Face& kBody = uiBody();
  const int y = rowY(kBody, row);
  dottedRule(d, y);
  if (before(millis(), _note_until)) {
    textAt(d, kBody, 0, y + 1, _note);
    return;
  }
  char buf[40];
  snprintf(buf, sizeof(buf), "> %s", _line.tail(kInlineChars));
  textAt(d, kBody, 0, y + 1, buf);
  if ((millis() / 500) % 2 == 0) {
    lit(d);
    d.fillRect(textPx(kBody, buf), y + 1, 1, kBody.row_px - 2);
  }
  if (_line.length() > 0) {
    char room[6];
    snprintf(room, sizeof(room), "%d", _line.remaining());
    textAt(d, kBody, kScreenPx - textPx(kBody, room) - kEdgePx, y + 1, room);
  }
}

// Past one line the editor takes the screen; its footer keeps the destination and the
// room left (design 1a, "Compose full").
void ThreadScreen::drawEditor(DisplayDriver& d, const char* name) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  const char* text = _line.text();
  const int indent = textPx(kBody, "> ");
  const int width = kScreenPx - kEdgePx - indent;
  Span spans[16];
  const int n = wrap(kBody, text, width, spans, 16);
  const int rows = kListRows;          // the rows above the footer
  const int first = n > rows ? n - rows : 0;
  int cursor_x = indent, cursor_y = 0;
  for (int i = first; i < n; i++) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%.*s", i == 0 ? "> " : "  ", (int)spans[i].len, text + spans[i].start);
    const int y = rowY(kBody, i - first);
    textAt(d, kBody, 0, y, buf);
    if (i == n - 1) {   // after the last character typed, trailing spaces included
      cursor_x = indent + textPx(kBody, text + spans[i].start);
      if (cursor_x > kScreenPx - 2) cursor_x = kScreenPx - 2;
      cursor_y = y;
    }
  }
  if ((millis() / 500) % 2 == 0) {
    lit(d);
    d.fillRect(cursor_x, cursor_y, 1, kBody.row_px - 1);
  }
  const int foot_y = rowY(kBody, rows);
  dottedRule(d, foot_y);
  char room[6], footer[28];
  snprintf(room, sizeof(room), "%d", _line.remaining());
  snprintf(footer, sizeof(footer), " %s", name);
  footer[fitPx(kBody, footer, kScreenPx - textPx(kBody, room) - 6)] = 0;
  textAt(d, kBody, 0, foot_y + 1, footer);
  textAt(d, kBody, kScreenPx - textPx(kBody, room) - kEdgePx, foot_y + 1, room);
}

int ThreadScreen::render(DisplayDriver& d) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  Store& store = the_mesh.badgeStore();
  const Store::Convo* v = store.convoAt(_convo);
  if (v == nullptr) {
    _task->gotoHomeScreen();
    return 100;
  }
  store.setOpen(_convo);   // on screen, so whatever arrives here is read

  const bool channel = v->kind == Store::Channel;
  char shown[32], name[36];
  d.translateUTF8ToBlocks(shown, v->name, sizeof(shown));
  snprintf(name, sizeof(name), "%c%s", channel ? '#' : '@', shown);

  if ((int)_line.length() > kInlineChars) {
    drawEditor(d, name);
    return 500;   // the cursor blinks
  }

  const uint32_t now = millis();
  const bool entry = now - _entered_at < 2000;

  const int n = store.thread(_convo, _seqs, kShow);
  int sel = -1;
  for (int i = 0; i < n; i++) {
    const Store::Msg* m = store.msg(_seqs[i]);
    d.translateUTF8ToBlocks(_txt[i], m->text, sizeof(_txt[i]));
    char tag[Store::kSenderLen];
    d.translateUTF8ToBlocks(tag, m->sender, sizeof(tag));
    snprintf(_tag[i], sizeof(_tag[i]), "%.5s", tag);
    _views[i] = {m->outgoing, _tag[i], _txt[i]};
    if (_seqs[i] == _sel_seq) sel = i;
  }
  if (sel < 0) _sel_seq = 0;   // it was evicted

  // The thread's rows, newest at the bottom: under the name bar for the first 2 s,
  // then above the compose line.
  const int nrows = layoutThread(kBody, _views, n, channel, sel, _rows, kMaxRows);
  const int visible = kListRows;
  const int top = threadTop(_rows, nrows, visible, sel);
  const int count = nrows - top < visible ? nrows - top : visible;
  const int first = (entry ? 1 : 0) + (visible - count);

  if (entry) {
    char left[40], right[16] = "";
    snprintf(left, sizeof(left), " %s", name);
    if (_unread_at_entry > 0) snprintf(right, sizeof(right), "%u unread", (unsigned)_unread_at_entry);
    bar(d, kBody, 0, left, right);
  }
  for (int r = 0; r < count; r++) drawRow(d, first + r, _rows[top + r], channel);
  if (n == 0) line(d, kBody, 3, " nothing here yet");
  if (!entry) drawCompose(d, kListRows);
  return entry ? (int)(2000 - (now - _entered_at)) + 10 : 500;
}

bool ThreadScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  if (!_task->inputFromKeyboard()) {   // SW1: a click leaves. A hold does nothing here:
    if (key == KEY_NEXT) leave();      // the design keeps it for the supporter card, and
    return true;                       // a stray one mustn't send a half-typed message
  }
  switch (key) {
    case KEY_UP:
      selectOlder();
      return true;
    case KEY_DOWN:
      selectNewer();
      return true;
    case KEY_ENTER:   // a selected failed DM goes first; the draft stays for later
      if (failedSelected()) resend();
      else if (_line.length() > 0) send();
      return true;
    case KEY_CANCEL:   // Esc drops a selection first, then leaves; the draft stays
      if (_sel_seq != 0) _sel_seq = 0; else leave();
      return true;
    default:
      break;
  }
  if (_line.apply(key)) {
    _sel_seq = 0;
    _note_until = 0;
    _entered_at = millis() - 2000;
  }
  return true;
}

// ---- Contacts (#1231) -----------------------------------------------------------------

// Chat contacts, most recently heard first. Only the newest kMax are kept; each by its
// whole key, since the phone can move contacts between slots at any time.
void ContactsScreen::reload() {
  _count = 0;
  _total = 0;
  const int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo c;
    // The first MAX_ANON_CONTACTS slots of the table hold transient anonymous peers.
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) || c.type != ADV_TYPE_CHAT) continue;
    _total++;
    int pos = _count;
    while (pos > 0 && _list[pos - 1].heard < c.lastmod) pos--;
    if (pos >= kMax) continue;
    for (int k = (_count < kMax ? _count : kMax - 1); k > pos; k--) _list[k] = _list[k - 1];
    _list[pos].heard = c.lastmod;
    memcpy(_list[pos].key, c.id.pub_key, PUB_KEY_SIZE);
    if (_count < kMax) _count++;
  }
  for (int i = 0; i < _count; i++) {   // the selection stays with its contact
    if (memcmp(_list[i].key, _sel_key, PUB_KEY_SIZE) == 0) {
      _sel = i;
      _loaded_at = millis();
      return;
    }
  }
  select(_sel);
  _loaded_at = millis();
}

void ContactsScreen::select(int sel) {
  if (_count == 0) {
    _sel = 0;
    return;
  }
  _sel = sel < 0 ? 0 : (sel >= _count ? _count - 1 : sel);
  memcpy(_sel_key, _list[_sel].key, PUB_KEY_SIZE);
}

void ContactsScreen::open(char first_key) {
  if (_count == 0) return;
  const ContactInfo* c = the_mesh.lookupContactByPubKey(_list[_sel].key, PUB_KEY_SIZE);
  if (c == nullptr) return;   // deleted since the last reload
  const int convo = the_mesh.badgeStore().convo(Store::Contact, c->id.pub_key, c->name);
  if (convo < 0) {
    _task->showAlert("Inbox full", 1000);
    return;
  }
  _task->gotoThread(convo, first_key);
}

int ContactsScreen::render(DisplayDriver& d) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  if (millis() - _loaded_at > 5000) reload();
  const bool crumb = _task->breadcrumbShown();
  const int first_row = crumb ? 2 : 1;
  const int rows = kListRows - (crumb ? 1 : 0);
  int top = listTop(_sel, _count, rows);
  const bool more_below = _count - top > rows;
  if (more_below) top = listTop(_sel, _count, rows - 1);

  char right[16];
  if (crumb) cycleTitle(right, sizeof(right), 1);
  else snprintf(right, sizeof(right), "%d", _total);
  bar(d, kBody, 0, " Contacts", "");
  int right_edge = kScreenPx - kEdgePx;
  if (!crumb && more_below) {
    markDown(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  if (!crumb && top > 0) {
    markUp(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  textAt(d, kBody, right_edge - textPx(kBody, right), 0, right, true);
  if (crumb) breadcrumb(d, kBody, 1, 1);

  if (_count == 0) {
    line(d, kBody, first_row + 1, " nobody heard yet");
    return crumb ? 250 : 5000;
  }
  const uint32_t now = rtc_clock.getCurrentTime();
  for (int r = 0; r < rows && top + r < _count; r++) {
    const Entry& e = _list[top + r];
    const ContactInfo* c = the_mesh.lookupContactByPubKey(e.key, PUB_KEY_SIZE);
    const int row = first_row + r;
    const bool selected = (top + r == _sel);
    char shown[32], left[34], age[6], info[16];
    d.translateUTF8ToBlocks(shown, c != nullptr ? c->name : "?", sizeof(shown));
    snprintf(left, sizeof(left), " @%s", shown);
    const uint32_t secs = now > e.heard ? now - e.heard : 0;
    formatAge(secs, age, sizeof(age));
    const bool stale = secs >= kStaleSecs;
    if (stale) {
      // The design: quiet nodes read "stale" rather than disappear. It also dims them,
      // which a 1-bit panel can only do by knocking out pixels, and that left the text
      // unreadable on the owner's badge, so the word does it alone.
      if (strcmp(age, "old") == 0) snprintf(info, sizeof(info), "stale");
      else snprintf(info, sizeof(info), "stale %s", age);
    } else if (c != nullptr && c->out_path_len != OUT_PATH_UNKNOWN) {
      snprintf(info, sizeof(info), "%uhop %s", (unsigned)(c->out_path_len & 63) + 1, age);
    } else {
      snprintf(info, sizeof(info), "%s", age);
    }
    const int info_x = kScreenPx - kEdgePx - textPx(kBody, info);
    left[fitPx(kBody, left, info_x - 4)] = 0;
    if (selected) fillRow(d, kBody, row);
    textAt(d, kBody, 0, rowY(kBody, row), left, selected);
    textAt(d, kBody, info_x, rowY(kBody, row), info, selected);
  }
  if (more_below) {
    dark(d);
    d.fillRect(0, kScreenRowsPx - 4, kScreenPx, 4);
    lit(d);
  }
  return crumb ? 250 : 1000;
}

bool ContactsScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  if (!_task->inputFromKeyboard()) {   // SW1: a tap moves along the cycle
    if (key == KEY_NEXT) _task->cycle(1);
    else if (key == KEY_PREV) _task->cycle(-1);
    else if (key == KEY_ENTER) open(0);
    return true;
  }
  switch (key) {
    case KEY_UP:
      select(_sel - 1);
      return true;
    case KEY_DOWN:
      select(_sel + 1);
      return true;
    case KEY_ENTER:
      open(0);
      return true;
    case KEY_LEFT:
      _task->cycle(-1);
      return true;
    case KEY_RIGHT:
      _task->cycle(1);
      return true;
    default:
      break;
  }
  if (printable(key)) {   // typing starts a DM to the selected contact
    open((char)key);
    return true;
  }
  return false;
}

// ---- Nearby (#1234) -------------------------------------------------------------------

namespace {

// The marker before a node's name: a person, a repeater, a room, or anything else.
char nearbyMarker(const NearbyNode& n) {
  if (!n.contact) return '*';
  switch (n.type) {
    case ADV_TYPE_CHAT:     return '@';
    case ADV_TYPE_REPEATER: return '^';
    case ADV_TYPE_ROOM:     return '&';
    default:                return '*';
  }
}

}  // namespace

// Contacts heard in the last hour, then the advert table's nodes, which include the
// ones that aren't contacts. The same node from both is one row.
void NearbyScreen::reload() {
  _list.clear();
  const uint32_t now = rtc_clock.getCurrentTime();
  const int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo c;
    // The first MAX_ANON_CONTACTS slots of the table hold transient anonymous peers.
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
    if (now < c.lastmod || now - c.lastmod >= kWindowSecs) continue;
    NearbyNode node = {};
    node.heard = c.lastmod;
    memcpy(node.key, c.id.pub_key, PUB_KEY_SIZE);
    node.key_len = PUB_KEY_SIZE;
    node.type = c.type;
    node.hops = (c.out_path_len != OUT_PATH_UNKNOWN) ? (c.out_path_len & 63) : 0xFF;
    node.contact = true;
    snprintf(node.name, sizeof(node.name), "%s", c.name);
    _list.offer(node);
  }
  the_mesh.uiEachHeard([&](const AdvertPath& p) {
    if (now < p.recv_timestamp || now - p.recv_timestamp >= kWindowSecs) return;
    NearbyNode node = {};
    node.heard = p.recv_timestamp;
    memcpy(node.key, p.pubkey_prefix, sizeof(p.pubkey_prefix));
    node.key_len = sizeof(p.pubkey_prefix);
    node.hops = p.path_len & 63;
    snprintf(node.name, sizeof(node.name), "%s", p.name);
    _list.offer(node);
  });
  for (int i = 0; i < _list.count(); i++) {   // the selection stays with its node
    const NearbyNode& node = _list.at(i);
    const int k = node.key_len < _sel_key_len ? node.key_len : _sel_key_len;
    if (k > 0 && memcmp(node.key, _sel_key, (size_t)k) == 0) {
      _sel = i;
      _loaded_at = millis();
      return;
    }
  }
  select(_sel);
  _loaded_at = millis();
}

void NearbyScreen::select(int sel) {
  const int count = _list.count();
  if (count == 0) {
    _sel = 0;
    return;
  }
  _sel = sel < 0 ? 0 : (sel >= count ? count - 1 : sel);
  memcpy(_sel_key, _list.at(_sel).key, PUB_KEY_SIZE);
  _sel_key_len = _list.at(_sel).key_len;
}

// Enter on a person opens the DM thread; there's nothing to write to anyone else.
void NearbyScreen::open(char first_key) {
  if (_list.count() == 0) return;
  const NearbyNode& n = _list.at(_sel);
  if (!n.contact || n.type != ADV_TYPE_CHAT) return;
  const ContactInfo* c = the_mesh.lookupContactByPubKey(n.key, PUB_KEY_SIZE);
  if (c == nullptr) return;
  const int convo = the_mesh.badgeStore().convo(Store::Contact, c->id.pub_key, c->name);
  if (convo < 0) {
    _task->showAlert("Inbox full", 1000);
    return;
  }
  _task->gotoThread(convo, first_key);
}

int NearbyScreen::render(DisplayDriver& d) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  if (millis() - _loaded_at > 5000) reload();
  const int count = _list.count();
  const bool crumb = _task->breadcrumbShown();
  const int first_row = crumb ? 2 : 1;
  const int rows = kListRows - (crumb ? 1 : 0);
  int top = listTop(_sel, count, rows);
  const bool more_below = count - top > rows;
  if (more_below) top = listTop(_sel, count, rows - 1);

  char right[16];
  if (crumb) cycleTitle(right, sizeof(right), 2);
  else snprintf(right, sizeof(right), "%d", count);
  bar(d, kBody, 0, " Nearby", "");
  int right_edge = kScreenPx - kEdgePx;
  if (!crumb && more_below) {
    markDown(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  if (!crumb && top > 0) {
    markUp(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  textAt(d, kBody, right_edge - textPx(kBody, right), 0, right, true);
  if (crumb) breadcrumb(d, kBody, 1, 2);

  if (count == 0) {
    line(d, kBody, first_row + 1, " nobody heard lately");
    return crumb ? 250 : 5000;
  }
  const uint32_t now = rtc_clock.getCurrentTime();
  for (int r = 0; r < rows && top + r < count; r++) {
    const NearbyNode& n = _list.at(top + r);
    const int row = first_row + r;
    const bool selected = (top + r == _sel);
    char shown[24], left[28], age[6], info[16];
    d.translateUTF8ToBlocks(shown, n.name, sizeof(shown));
    snprintf(left, sizeof(left), " %c%s", nearbyMarker(n), shown);
    formatAge(now > n.heard ? now - n.heard : 0, age, sizeof(age));
    // Radio hops, as everywhere on the badge: 1 is heard directly.
    if (n.hops != 0xFF) snprintf(info, sizeof(info), "%uhop %s", (unsigned)n.hops + 1, age);
    else snprintf(info, sizeof(info), "%s", age);
    const int info_x = kScreenPx - kEdgePx - textPx(kBody, info);
    left[fitPx(kBody, left, info_x - 4)] = 0;
    if (selected) fillRow(d, kBody, row);
    textAt(d, kBody, 0, rowY(kBody, row), left, selected);
    textAt(d, kBody, info_x, rowY(kBody, row), info, selected);
  }
  if (more_below) {
    dark(d);
    d.fillRect(0, kScreenRowsPx - 4, kScreenPx, 4);
    lit(d);
  }
  return crumb ? 250 : 1000;
}

bool NearbyScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  if (!_task->inputFromKeyboard()) {   // SW1: a tap moves along the cycle
    if (key == KEY_NEXT) _task->cycle(1);
    else if (key == KEY_PREV) _task->cycle(-1);
    else if (key == KEY_ENTER) open(0);
    return true;
  }
  switch (key) {
    case KEY_UP:
      select(_sel - 1);
      return true;
    case KEY_DOWN:
      select(_sel + 1);
      return true;
    case KEY_ENTER:
      open(0);
      return true;
    case KEY_LEFT:
      _task->cycle(-1);
      return true;
    case KEY_RIGHT:
      _task->cycle(1);
      return true;
    default:
      break;
  }
  if (printable(key)) {   // typing starts a DM to the selected person
    open((char)key);
    return true;
  }
  return false;
}

// ---- Status (#1231) -------------------------------------------------------------------

namespace {

// "tx 41 rx 219", with counts past 9999 in thousands so the row fits.
void counts(char* out, size_t n, uint32_t tx, uint32_t rx) {
  char a[8], b[8];
  if (tx > 9999) snprintf(a, sizeof(a), "%luk", (unsigned long)(tx / 1000)); else snprintf(a, sizeof(a), "%lu", (unsigned long)tx);
  if (rx > 9999) snprintf(b, sizeof(b), "%luk", (unsigned long)(rx / 1000)); else snprintf(b, sizeof(b), "%lu", (unsigned long)rx);
  snprintf(out, n, "tx %s rx %s", a, b);
}

}  // namespace

int StatusScreen::drawAs(DisplayDriver& d, const char* title, int pos) {
  const Face& kBody = uiBody();
  const Face& kMeta = uiMeta();
  const bool crumb = _task->breadcrumbShown() && _task->cyclePos() == pos;
  char right[8];
  cycleTitle(right, sizeof(right), pos);
  // Status carries the brand (design 1a); as the empty inbox it carries the inbox's
  // battery instead.
  const bool as_status = (pos == kCycleStops - 1);
  bar(d, kBody, 0, title, crumb ? right : (as_status ? "OFFBAND" : ""));
  if (!crumb && !as_status) battery(d, kScreenPx - 12, 1, batteryPct(_task->getBattMilliVolts()));
  // #1237: the lines are the meta face. They're values, and it fits them whole.
  int y = kBody.row_px;
  if (crumb) {
    breadcrumb(d, kBody, 1, pos);
    y += kBody.row_px;
  }
  y += 1;

  char buf[48], shown[32];
  d.translateUTF8ToBlocks(shown, the_mesh.getNodeName(), sizeof(shown));
  snprintf(buf, sizeof(buf), " @%s", shown);
  textAt(d, kMeta, 0, y, buf);
  y += kMeta.row_px;

  // Uptime as h:mm:ss, and as days and hours once that would not fit.
  const unsigned long up = millis() / 1000;
  const uint8_t* id = the_mesh.self_id.pub_key;
  if (up < 100UL * 3600UL) {
    snprintf(buf, sizeof(buf), " node %02X%02X up %lu:%02lu:%02lu", id[0], id[1], up / 3600, up / 60 % 60, up % 60);
  } else {
    snprintf(buf, sizeof(buf), " node %02X%02X up %lud %luh", id[0], id[1], up / 86400, up / 3600 % 24);
  }
  textAt(d, kMeta, 0, y, buf);
  y += kMeta.row_px;
  if (!crumb) y += 2;   // the design's breathing room, given up to the breadcrumb

  // Nodes heard in the last hour, and how far the farthest is in radio hops. The scan
  // copies every contact, so it runs every 10 s rather than on every redraw.
  if (_stats_at == 0 || millis() - _stats_at >= 10000) {
    const uint32_t now = rtc_clock.getCurrentTime();
    _heard = 0;
    _farthest = 0;
    const int n = the_mesh.getNumContacts();
    for (int i = 0; i < n; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
      if (now < c.lastmod || now - c.lastmod >= 3600) continue;
      _heard++;
      if (c.out_path_len != OUT_PATH_UNKNOWN && (int)(c.out_path_len & 63) + 1 > _farthest) _farthest = (c.out_path_len & 63) + 1;
    }
    _stats_at = millis() | 1;
  }
  if (_heard == 0) snprintf(buf, sizeof(buf), " no nodes this hour");
  else if (_farthest > 0) snprintf(buf, sizeof(buf), " %d node%s  %d hop%s", _heard, _heard == 1 ? "" : "s", _farthest, _farthest == 1 ? "" : "s");
  else snprintf(buf, sizeof(buf), " %d node%s", _heard, _heard == 1 ? "" : "s");
  textAt(d, kMeta, 0, y, buf);
  y += kMeta.row_px;

  const NodePrefs* prefs = the_mesh.getNodePrefs();
  snprintf(buf, sizeof(buf), " %.3f MHz SF%d", (double)prefs->freq, (int)prefs->sf);
  textAt(d, kMeta, 0, y, buf);
  snprintf(buf, sizeof(buf), "BW%.1f ", (double)prefs->bw);
  textAt(d, kMeta, kScreenPx - kEdgePx - textPx(kMeta, buf), y, buf);
  y += kMeta.row_px;

  snprintf(buf, sizeof(buf), " batt %d%%", batteryPct(_task->getBattMilliVolts()));
  textAt(d, kMeta, 0, y, buf);
  char tx_rx[20];
  counts(tx_rx, sizeof(tx_rx), radio_driver.getPacketsSent(), radio_driver.getPacketsRecv());
  textAt(d, kMeta, kScreenPx - kEdgePx - textPx(kMeta, tx_rx), y, tx_rx);

  // The footer: the Bluetooth pairing PIN while a phone could pair, which used to be
  // Home's job on this board.
  const int foot_y = kScreenRowsPx - kMeta.row_px;
  dottedRule(d, foot_y - 1);
  if (_task->hasConnection()) snprintf(buf, sizeof(buf), " phone connected");
  else if (!_task->isBluetoothEnabled()) snprintf(buf, sizeof(buf), " bluetooth off");
  else if (the_mesh.getBLEPin() != 0) snprintf(buf, sizeof(buf), " BT pin %06lu", (unsigned long)the_mesh.getBLEPin());
  else snprintf(buf, sizeof(buf), " quiet on the mesh");
  textAt(d, kMeta, 0, foot_y, buf);
  return crumb ? 250 : 1000;
}

bool StatusScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  if (!_task->inputFromKeyboard()) {   // SW1: a tap moves along the cycle; a hold opens Settings
    if (key == KEY_NEXT) _task->cycle(1);
    else if (key == KEY_PREV) _task->cycle(-1);
    else if (key == KEY_ENTER) _task->gotoSettings();
    return true;
  }
  switch (key) {
    case KEY_ENTER:   // #1233: Settings, which holds what the device pages did
      _task->gotoSettings();
      return true;
    case KEY_LEFT:
      _task->cycle(-1);
      return true;
    case KEY_RIGHT:
      _task->cycle(1);
      return true;
    default:
      return false;   // Esc: UITask backs out to Messages
  }
}

// ---- Settings (#1233) ------------------------------------------------------------------

void SettingsScreen::begin() {
  _gate = false;
  _shutdown_pending = false;
  _sent_until = 0;
  if (!shown(_sel)) _sel = 0;
}

bool SettingsScreen::shown(int row) {
#if ENV_INCLUDE_GPS == 1
  return row >= 0 && row < kRows;
#else
  return row >= 0 && row < kRows && row != Gps;
#endif
}

void SettingsScreen::step(int dir) {
  int next = _sel;
  do {
    next = (next + dir + kRows) % kRows;
  } while (!shown(next));
  _sel = next;
}

void SettingsScreen::act() {
  if (_sel != TextSize) saveSizeIfPending();   // #1238: anything else may leave this screen
  switch (_sel) {
    case Bluetooth:
      if (_task->isBluetoothEnabled()) _task->disableBluetooth(); else _task->enableBluetooth();
      break;
    case TextSize: {   // #1238: large, medium, small, and round again
      NodePrefs* prefs = the_mesh.getNodePrefs();
      prefs->ui_text_size = (uint8_t)((prefs->ui_text_size + 1) % kTextSteps);
      // Saved once the cycling settles: trying the three sizes shouldn't be three
      // writes to flash. The screen changes at once either way.
      _size_at = millis() | 1;
      _task->notify(UIEventType::ack);
      break;
    }
    case TimeZone:
      _task->gotoZones();
      break;
    case Gps:
      _task->gotoGps();
      break;
    case AdvertZeroHop:
    case AdvertFlood:
      if (the_mesh.uiAdvert(_sel == AdvertFlood)) {
        _task->notify(UIEventType::ack);
        _sent_row = _sel;
        _sent_until = millis() + 2000;
      } else {
        _task->showAlert("Advert failed", 1000);
      }
      break;
    case Hibernate:
      _gate = true;
      break;
    case DevicePages:
      _task->gotoTools();
      break;
  }
}

// The design's gate: what happens, then Enter to go ahead or Esc to back out.
int SettingsScreen::drawGate(DisplayDriver& d) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  bar(d, kBody, 0, " Hibernate", "");
  line(d, kBody, 1, " radio and screen off");
  line(d, kBody, 2, " until the next reset");
  line(d, kBody, 4, " messages on the badge");
  line(d, kBody, 5, " are cleared");
  fillRow(d, kBody, kListRows);
  line(d, kBody, kListRows, " Enter sleep", true);
  lineRight(d, kBody, kListRows, "Esc no", true);
  return 1000;
}

int SettingsScreen::render(DisplayDriver& d) {
  if (_gate) return drawGate(d);
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  // #1238: the rows that show, so the list can scroll. At the large text size there are
  // more of them than the screen holds.
  uint8_t list[kRows];
  int count = 0, sel = 0;
  for (int r = 0; r < kRows; r++) {
    if (!shown(r)) continue;
    if (r == _sel) sel = count;
    list[count++] = (uint8_t)r;
  }
  const int top = listTop(sel, count, kListRows);
  const bool more_below = count - top > kListRows;

  bar(d, kBody, 0, " Settings", "");
  int right_edge = kScreenPx - kEdgePx;
  if (more_below) {
    markDown(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  if (top > 0) markUp(d, right_edge - 6, 0, true);

  const NodePrefs* prefs = the_mesh.getNodePrefs();
  const bool sent = before(millis(), _sent_until);
  for (int i = 0; i < kListRows && top + i < count; i++) {
    const int r = list[top + i];
    const char* label = "";
    char value[16] = "";   // "No GPS Module" is the longest
    switch (r) {
      case Bluetooth:     label = "Bluetooth"; snprintf(value, sizeof(value), "%s", _task->isBluetoothEnabled() ? "on" : "off"); break;
      case TextSize:      label = "Text size"; snprintf(value, sizeof(value), "%s", textSizeName(prefs->ui_text_size)); break;
      case TimeZone:      label = "Time zone"; snprintf(value, sizeof(value), "%s", offband::tz::zone(prefs->ui_tz).name); break;
      case Gps:           label = "GPS"; GpsScreen::summary(_task, value, sizeof(value)); break;
      case AdvertZeroHop: label = "Advert zero-hop"; snprintf(value, sizeof(value), "%s", sent && _sent_row == r ? "sent" : "send"); break;
      case AdvertFlood:   label = "Advert flood"; snprintf(value, sizeof(value), "%s", sent && _sent_row == r ? "sent" : "send"); break;
      case Hibernate:     label = "Hibernate"; break;
      case DevicePages:   label = "Device pages"; break;
    }
    listRow(d, 1 + i, label, value, r == _sel);
  }
  return sent ? 250 : 1000;
}

bool SettingsScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  const bool from_button = !_task->inputFromKeyboard();
  if (_gate) {
    if (key == KEY_ENTER) {
      // A hold on SW1 is still down: wait for its release, as the old hibernate page
      // did. A key from the keyboard can go now.
      if (from_button) _shutdown_pending = true; else _task->shutdown();
    } else if (key == KEY_CANCEL || (from_button && key == KEY_NEXT)) {
      _gate = false;
    }
    return true;
  }
  if (from_button) {   // SW1 walks the list; a hold picks
    if (key == KEY_NEXT) step(1);
    else if (key == KEY_PREV) step(-1);
    else if (key == KEY_ENTER) act();
    return true;
  }
  switch (key) {
    case KEY_UP:    step(-1); return true;
    case KEY_DOWN:  step(1); return true;
    case KEY_ENTER: act(); return true;
    default:
      saveSizeIfPending();   // #1238: Esc leaves for Status, so keep the size
      return false;
  }
}

void SettingsScreen::saveSizeIfPending() {
  if (_size_at == 0) return;
  _size_at = 0;
  the_mesh.savePrefs();
}

void SettingsScreen::poll() {
  if (_shutdown_pending && !_task->isButtonPressed()) _task->shutdown();
  if (_size_at != 0 && millis() - _size_at > kSizeSettleMs) saveSizeIfPending();   // #1238
}

// ---- Time zone picker (#1233) ----------------------------------------------------------

void ZonePickerScreen::begin() {
  _suggested = GpsScreen::suggestedZone(_task);
  const int current = the_mesh.getNodePrefs()->ui_tz;
  _sel = offband::tz::isSet(current) ? current : (offband::tz::isSet(_suggested) ? _suggested : offband::tz::kUtc);
}

int ZonePickerScreen::render(DisplayDriver& d) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  namespace tz = offband::tz;
  const int count = tz::kZoneCount;
  int top = listTop(_sel, count, kListRows);
  const bool more_below = count - top > kListRows;
  if (more_below) top = listTop(_sel, count, kListRows - 1);
  bar(d, kBody, 0, " Time zone", "");
  int right_edge = kScreenPx - kEdgePx;
  if (more_below) {
    markDown(d, right_edge - 6, 0, true);
    right_edge -= 7;
  }
  if (top > 0) markUp(d, right_edge - 6, 0, true);

  const int current = the_mesh.getNodePrefs()->ui_tz;
  const uint32_t now = rtc_clock.getCurrentTime();
  for (int r = 0; r < kListRows && top + r < count; r++) {
    const int z = top + r;
    const int row = 1 + r;
    const bool selected = (z == _sel);
    char left[16], right[16], offset[8];
    snprintf(left, sizeof(left), " %s", tz::zone(z).name);
    if (!tz::isSet(z)) {
      snprintf(right, sizeof(right), "ages");
    } else {
      tz::formatOffset(tz::offsetMinutes(z, now), offset, sizeof(offset));
      const char* mark = (z == current) ? "now " : (z == _suggested ? "gps " : "");
      snprintf(right, sizeof(right), "%s%s", mark, offset);
    }
    if (selected) fillRow(d, kBody, row);
    line(d, kBody, row, left, selected);
    lineRight(d, kBody, row, right, selected);
  }
  if (more_below) {
    dark(d);
    d.fillRect(0, kScreenRowsPx - 4, kScreenPx, 4);
    lit(d);
  }
  return 1000;
}

bool ZonePickerScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  const int count = offband::tz::kZoneCount;
  const bool from_button = !_task->inputFromKeyboard();
  if ((from_button && key == KEY_NEXT) || key == KEY_DOWN) {
    _sel = (_sel + 1) % count;
    return true;
  }
  if ((from_button && key == KEY_PREV) || key == KEY_UP) {
    _sel = (_sel + count - 1) % count;
    return true;
  }
  if (key == KEY_ENTER) {
    NodePrefs* prefs = the_mesh.getNodePrefs();
    if (prefs->ui_tz != (uint8_t)_sel) {   // a flash write only for a change
      prefs->ui_tz = (uint8_t)_sel;
      the_mesh.savePrefs();
    }
    _task->gotoSettings();
    return true;
  }
  return false;   // Esc: back to Settings, unchanged
}

// ---- GPS (#1235) -----------------------------------------------------------------------

void GpsScreen::summary(UITask* task, char* out, size_t n) {
  LocationProvider* gps = liveGps(task);
  const bool fix = gps != nullptr && gps->isValid();
  formatGpsState(gps != nullptr, gpsModuleFound(), fix, fix ? gps->satellitesCount() : 0, out, n);
}

int GpsScreen::suggestedZone(UITask* task) {
  LocationProvider* gps = liveGps(task);
  if (gps == nullptr || !gps->isValid()) return offband::tz::kNotSet;
  return offband::tz::suggest(gps->getLatitude() / 1000000.0, gps->getLongitude() / 1000000.0);
}

int GpsScreen::pending() const {
  const int zone = suggestedZone(_task);
  return (offband::tz::isSet(zone) && zone != the_mesh.getNodePrefs()->ui_tz) ? zone : offband::tz::kNotSet;
}

void GpsScreen::begin() {
  _use_shown = offband::tz::isSet(pending());   // as the first render will draw it
  _sel = _use_shown ? UseZone : Power;
}

void GpsScreen::act() {
  if (_sel == Power) {
    _task->toggleGPS();
    return;
  }
  // Checked again: the fix can go between a render and the key.
  const int zone = pending();
  if (offband::tz::isSet(zone)) {
    the_mesh.getNodePrefs()->ui_tz = (uint8_t)zone;
    the_mesh.savePrefs();
    _task->notify(UIEventType::ack);
  }
  _sel = Power;
}

int GpsScreen::render(DisplayDriver& d) {
  const Face& kBody = uiBody();
  const int kListRows = uiListRows();
  char buf[24], part[24];
  summary(_task, part, sizeof(part));
  bar(d, kBody, 0, " GPS", part);

  LocationProvider* gps = liveGps(_task);
  if (gps == nullptr) {
    line(d, kBody, 1, " GPS is off");
  } else if (!gps->isValid()) {
    // With no module the title says "No GPS Module"; otherwise one is still looking.
    if (gpsModuleFound()) line(d, kBody, 1, " waiting for a fix");
  } else {
    formatPosition(gps->getLatitude(), gps->getLongitude(), part, sizeof(part));
    snprintf(buf, sizeof(buf), " %s", part);
    line(d, kBody, 1, buf);
    // The provider reports 0 for an altitude it hasn't got (no GGA yet). A real reading
    // of exactly 0.0 m hides too, which only sea level makes possible.
    const long alt_mm = gps->getAltitude();
    if (alt_mm != 0) {
      formatAltitude(alt_mm, part, sizeof(part));
      snprintf(buf, sizeof(buf), " alt %s", part);
      line(d, kBody, 2, buf);
    }
  }
  // The GPS's own clock, which can arrive before a position does.
  const long t = (gps != nullptr) ? gps->getTimestamp() : 0;
  if (t > 0) {
    formatUtcTime((uint32_t)t, part, sizeof(part));
    snprintf(buf, sizeof(buf), " %s UTC", part);
    line(d, kBody, 3, buf);
  }

  // The rows sit at the foot.
  const int zone = pending();
  _use_shown = offband::tz::isSet(zone);
  if (_use_shown) {
    snprintf(buf, sizeof(buf), "Use zone %s", offband::tz::zone(zone).name);
    listRow(d, kListRows - 1, buf, "", _sel == UseZone);
  } else {
    _sel = Power;   // the fix went, or the zone is in use now
  }
  listRow(d, kListRows, "GPS", _task->getGPSState() ? "on" : "off", _sel == Power);
  return 1000;
}

bool GpsScreen::handleInput(char c) {
  const uint8_t key = (uint8_t)c;
  const bool from_button = !_task->inputFromKeyboard();
  if (key == KEY_UP || key == KEY_DOWN || (from_button && (key == KEY_NEXT || key == KEY_PREV))) {
    // Two rows at most: a move goes to the other one, when it's on the screen.
    _sel = (_sel == Power && _use_shown) ? UseZone : Power;
    return true;
  }
  if (key == KEY_ENTER) {
    act();
    return true;
  }
  return from_button;   // Esc: back to Settings
}

#endif  // UI_HAS_CARDKB
