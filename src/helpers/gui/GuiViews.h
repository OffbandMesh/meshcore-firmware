#pragma once

// What the core screens show (#1428, PLAN.md §3 Epic D): the status bar, Home,
// Lock, Chats, Conversation and Compose, as text and states the LVGL code puts
// on screen. Design of record: ObScreen.dc.html and the GUI spec, plus the
// owner-approved gap mockups (epic-d-gap-mockups.html, 2026-10-16).
//
// Pure: no Arduino, no LVGL, no mesh. The caller passes times and readings in.
//
// Every function that writes text writes a NUL-terminated string, does nothing
// when n is 0, and cuts to fit only at a whole UTF-8 character. Store-based
// functions treat a store that isn't ready as empty.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "GuiGps.h"
#include "GuiPrefs.h"
#include "MsgStore.h"

namespace offband {
namespace gui {

// snprintf by the rules above: does nothing when n is 0, and a cut lands on
// a whole UTF-8 character. For every view module's text.
void textf(char* out, size_t n, const char* f, ...);
void vtextf(char* out, size_t n, const char* f, va_list ap);

// Text from the mesh (names, messages) as the screens show it: the
// characters that only steer how an emoji is drawn are dropped, since the
// fonts' black-and-white emoji have no glyph for them and LVGL would draw a
// box: variation selectors (U+FE0E/FE0F), the zero-width joiner (U+200D),
// skin-tone modifiers (U+1F3FB-1F3FF), the keycap mark (U+20E3) and tag
// characters (U+E0020-E007F). A joined emoji shows as its parts. Reads
// `len` bytes; writes a NUL-terminated string, cut at a whole character.
void displayText(const char* in, size_t len, char* out, size_t n);

// Colour roles. Every role has a second cue on screen (bar count, glyph,
// fill), so Mono HC and e-ink lose nothing.
enum class Tone : uint8_t { Ok, Warn, Bad, Dim, Signal };

// ------------------------------------------------------------- status bar

// Battery: ok above 30%, warn 15-30%, bad below 15% (spec v0.2).
Tone batteryTone(uint8_t percent);

// Bluetooth: shown only when enabled; signal colour with a phone connected,
// dim while advertising.
enum class BleIcon : uint8_t { Hidden, Advertising, Connected };
BleIcon bleIcon(bool enabled, bool connected);

// GPS: shown only when a module is present and enabled. Seeking blinks at
// 1 Hz; no fix for kGpsTimeoutSecs turns it red.
enum class GpsIcon : uint8_t { Hidden, Fix3D, Fix2D, Seeking, TimedOut };
constexpr uint32_t kGpsTimeoutSecs = 600;
// secs_without_fix: since the fix was lost, or since the GPS started if it
// never had one.
GpsIcon gpsIcon(bool present, FixType fix, uint32_t secs_without_fix);
// The seeking blink: lit for the first half of each second.
bool gpsBlinkLit(uint32_t now_ms);

// The unread count left of the icons; hidden at 0.
inline bool showUnread(uint32_t unread) { return unread > 0; }

// ------------------------------------------------ LoRa long-press popover

// The level word for a bar count (spec "Signal level scale").
const char* signalWord(uint8_t bars);
// "LoRa · last 8 packets", "LoRa · last packet", or "No packets yet".
void loraTitle(size_t packets, char* out, size_t n);
// "+6.5 dB" / "-3.0 dB".
void formatSnr(float snr_db, char* out, size_t n);
// "-92 dBm".
void formatDbm(int16_t dbm, char* out, size_t n);
// The margin over the SF's demodulation floor: "14.0 dB".
void formatMargin(float snr_db, uint8_t sf, char* out, size_t n);
constexpr uint32_t kPopoverCloseMs = 5000;

// ------------------------------------------------------------------- Home

void formatFreq(float mhz, char* out, size_t n);                        // "910.525"
void radioSub(uint8_t sf, float bw_khz, uint8_t cr, char* out, size_t n);  // "SF7 · BW62.5 · CR5"
void heardSub(uint16_t repeaters, uint16_t rooms, uint16_t chat, char* out, size_t n);
const char* gpsValue(bool present, FixType fix);                        // "3D", "2D", "No fix", "Off"
void gpsSub(uint8_t sats, bool clock_synced, char* out, size_t n);      // "9 sats · clock synced"
void formatUptime(uint32_t secs, char* out, size_t n);                  // "42m", "5h 12m", "3d 4h"
void batterySub(uint16_t millivolts, uint32_t uptime_secs, char* out, size_t n);  // "4.02 V · 3d 4h up"
void formatAirtime(uint16_t permille, char* out, size_t n);             // "1.2%"
// The names of threads with unread messages, newest first: two, then "+N".
// "Newest" here and in the lists below is arrival order on this device, not
// the sender's timestamp, which can be far off.
void unreadSub(const MsgStore& store, char* out, size_t n);

// Send advert: the button reads "Advert sent ✓" (or "Not sent, try again")
// for kAdvertFeedbackMs, and presses meanwhile are ignored.
constexpr uint32_t kAdvertFeedbackMs = 2000;
class AdvertButton {
public:
  enum class State : uint8_t { Idle, Sent, NotSent };
  // True when this press should send an advert.
  bool press(uint32_t now_ms);
  void result(bool sent, uint32_t now_ms);
  State state(uint32_t now_ms) const;

private:
  State _state = State::Idle;
  bool _pending = false;
  uint32_t _at = 0;
};

// ------------------------------------------------------------------- Lock

// "21:07", or "--:--" before the clock is set. zone: a TimeZones.h index.
void formatClock(int zone, uint32_t utc, char* out, size_t n);
// "Fri 16 Oct"; empty before the clock is set.
void formatDate(int zone, uint32_t utc, char* out, size_t n);
// The newest unread message of each thread with unread, newest first, at
// most `max`. Each item stands for its whole thread ("#Public · 3 new"), so
// *more = unread messages in threads not shown: the "+N more" line. A thread
// whose unread messages were all evicted shows no item but counts in *more.
size_t lockItems(const MsgStore& store, uint32_t* out, size_t max, uint32_t* more);
// A lock item's label: "#Public · 3 new", "Wren · direct" (one unread DM),
// "Wren · 2 new".
void lockLabel(const Thread& t, char* out, size_t n);
// A lock item's text: "Sender: text" on channels and rooms, the text alone
// in a DM.
void lockText(const MsgRecord& m, ThreadKind kind, char* out, size_t n);

// The Home header's key line: "a3f1 7e0c … 9c42" (first 4 bytes, last 2).
void keySummary(const uint8_t* pub_key, char* out, size_t n);

// ------------------------------------------------- Chats and Conversation

// Live threads, unread ones first, then by latest activity. Returns the count.
size_t chatOrder(const MsgStore& store, uint16_t* out, size_t max);
// The second row of a chat: "Sender: text" on channels and rooms, "You: text"
// for an own message, the text alone in a DM; "No messages yet" when empty.
void chatPreview(const MsgStore& store, int thread, char* out, size_t n);
// The status at the end of an own message's meta line.
// DM: "sending…", "sending… try 2/3", "✓ delivered", "✕ not delivered · hold
// to resend". Channel: "heard ×N", empty before any echo.
void deliveryMeta(const MsgRecord& m, bool channel, uint8_t attempt, uint8_t max_attempts,
                  char* out, size_t n);
// The route on a received message's meta line, as drawn: "direct" when it
// came straight from the sender (0 hops, or a direct route, 0xFF), else
// "1 hop", "2 hops".
void incomingMeta(uint8_t hops, char* out, size_t n);
// "{from} · {time} · {meta}", skipping empty parts.
void metaLine(const char* from, const char* time, const char* meta, char* out, size_t n);

// ------------------------------------------------- channel notifications

// Whether a channel message sounds the alert under the channel's mode (spec
// v0.3 "Channel notifications"): All always; Mentions only when it mentions
// this node; Off never (it still counts as unread).
bool channelAlerts(NotifyMode mode, bool mentions_self);

// ---------------------------------------------------------------- Compose

// The draft, in bytes of UTF-8, stopping at the budget: kMsgTextMax for a
// DM; less the "<name>: " prefix on a channel (compose::budget).
class ComposeBuffer {
public:
  void start(size_t budget);
  // Appends one UTF-8 character; false (and nothing changes) if it doesn't fit.
  bool insert(const char* utf8, size_t len);
  // Removes the last whole character; false when empty.
  bool backspace();
  const char* text() const { return _buf; }
  size_t length() const { return _len; }
  size_t budget() const { return _budget; }
  // "14/160".
  void counter(char* out, size_t n) const;
  // Amber from 10 bytes short of the budget: 150 of 160 (owner, 2026-10-16).
  Tone counterTone() const;

private:
  char _buf[kMsgTextMax + 1] = {};
  size_t _len = 0;
  size_t _budget = kMsgTextMax;
};

// Shift: one tap = the next letter capital; a double tap (kMultiClickMs) =
// caps lock; a tap from either clears it. A draft's start and the start of a
// sentence ("." "!" "?" then a space) turn it on for one letter.
enum class Shift : uint8_t { Off, Once, Lock };
class ShiftState {
public:
  void tap(uint32_t now_ms);
  void typedLetter();   // Once -> Off
  void autoCap(const char* text, size_t len);
  Shift state() const { return _s; }

private:
  Shift _s = Shift::Off;
  uint32_t _last_tap = 0;
  bool _tapped = false;
};

// Keyboard pages: ?123 -> Symbols, =\< -> Symbols2, ABC -> Letters. After a
// symbol and then a space, it returns to Letters.
enum class KeyPage : uint8_t { Letters, Symbols, Symbols2 };
class PageState {
public:
  void select(KeyPage p) { _p = p; _typed_symbol = false; }
  void typed(char c);
  KeyPage page() const { return _p; }

private:
  KeyPage _p = KeyPage::Letters;
  bool _typed_symbol = false;
};

// Key geometry at 240 px, so a touch snaps to the nearest key centre, gaps
// included. Four rows of 32 px keys, 3 px apart.
struct KeyRect {
  int16_t x, y, w, h;
  const char* label;   // "q", "⇧", "⌫", "?123", "=\\<", "ABC", "space", "Send"
};
constexpr int16_t kKeyH = 32;
constexpr int16_t kKeyRowGap = 3;
// A page laid out in a keyboard area `width` wide whose top is at y0.
size_t pageLayout(KeyPage page, int16_t width, int16_t y0, KeyRect* out, size_t max);
inline size_t lettersLayout(int16_t width, int16_t y0, KeyRect* out, size_t max) {
  return pageLayout(KeyPage::Letters, width, y0, out, max);
}
// A key that types its label (a letter, digit or symbol), as opposed to a
// control key (shift, backspace, page switches, space, Send).
bool typesLabel(const char* label);
// Index of the key whose centre is nearest (x, y); -1 if none.
int nearestKey(const KeyRect* keys, size_t count, int16_t x, int16_t y);

// A DM sent from the device: up to kDmAttempts tries, each waiting for the
// ACK as long as the send estimated, then failed (owner-approved mockup 6).
// The phone app retries its own sends; this covers the device's.
constexpr uint8_t kDmAttempts = 3;
class DmRetry {
public:
  enum class Step : uint8_t { Wait, Resend, Fail, Done };
  void start(uint32_t seq, uint32_t now_ms, uint32_t timeout_ms);
  // Each pass. delivered: the store already says Delivered.
  Step poll(uint32_t now_ms, bool delivered);
  // After acting on a Resend: the next try is out, with its own timeout.
  void resent(uint32_t now_ms, uint32_t timeout_ms);
  bool active() const { return _seq != 0; }
  uint32_t seq() const { return _seq; }
  uint8_t attempt() const { return _attempt; }   // 1-based: the try in flight

private:
  uint32_t _seq = 0;
  uint32_t _deadline = 0;
  uint8_t _attempt = 0;
};

}  // namespace gui
}  // namespace offband
