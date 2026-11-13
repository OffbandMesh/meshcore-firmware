#pragma once

// The T-Deck Pro keyboard (Epic C #2098), as plain values the GUI can act
// on. Arduino-free so the native tests cover it; the chip itself is driven by
// hw/Tca8418Keyboard.
//
// The keys are a 4x10 matrix on a TI TCA8418 at I2C 0x34. The chip keeps a
// 10-event FIFO; each event is one byte, bit 7 set for a press, bits 6:0 the
// key number. A matrix key is row * 10 + column + 1 (1-80); 97-114 are GPI
// events; 0 means the FIFO is empty (TI SCPS215G, 8.3.1 and 8.6.2.4).
//
// Our own code, from that datasheet. The key positions were cross-checked
// against two independent sources that agree, LilyGo's factory keymap and
// its column reversal (examples/factory2/peri_keypad.cpp) and another
// project's key table; no code is taken from either. C2 (#2150) confirms
// each position with a bench press of every key.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

constexpr uint8_t kTca8418Addr = 0x34;
constexpr uint8_t kProKeyRows = 4;
constexpr uint8_t kProKeyCols = 10;
constexpr uint8_t kTca8418FifoDepth = 10;

// TCA8418 registers and bits (SCPS215G Table 9 and 8.6.2.1-8.6.2.11).
namespace tca8418 {
constexpr uint8_t kRegCfg = 0x01;
constexpr uint8_t kRegIntStat = 0x02;
constexpr uint8_t kRegKeyLckEc = 0x03;
constexpr uint8_t kRegKeyEventA = 0x04;   // reading it pops the FIFO
constexpr uint8_t kRegKpGpio1 = 0x1D;     // rows 0-7
constexpr uint8_t kRegKpGpio2 = 0x1E;     // columns 0-7
constexpr uint8_t kRegKpGpio3 = 0x1F;     // columns 8-9, bits 0-1

constexpr uint8_t kCfgKeIen = 0x01;        // INT on a key event
constexpr uint8_t kCfgOvrFlowIen = 0x08;   // INT on an overflow
constexpr uint8_t kCfgIntCfg = 0x10;       // INT releases 50 us on a clear, then re-asserts if events wait
constexpr uint8_t kCfgOvrFlowM = 0x20;     // overflow: the newest event pushes the oldest out

constexpr uint8_t kIntK = 0x01;
constexpr uint8_t kIntGpi = 0x02;
constexpr uint8_t kIntKLck = 0x04;
constexpr uint8_t kIntOvrFlow = 0x08;
constexpr uint8_t kIntCad = 0x10;
constexpr uint8_t kIntAll = kIntK | kIntGpi | kIntKLck | kIntOvrFlow | kIntCad;

constexpr uint8_t kEventCountMask = 0x0F;  // KEY_LCK_EC[3:0]

// The overflow flag is set only with both OVR_FLOW_M and OVR_FLOW_IEN on
// (SCPS215G 8.6.4, the overflow erratum), so both are.
constexpr uint8_t kCfgValue = kCfgKeIen | kCfgOvrFlowIen | kCfgIntCfg | kCfgOvrFlowM;
}  // namespace tca8418

// The KP_GPIO values that put `rows` rows and `cols` columns in the matrix.
struct MatrixSelect {
  uint8_t rows;   // KP_GPIO1
  uint8_t cols_lo;  // KP_GPIO2
  uint8_t cols_hi;  // KP_GPIO3
};
MatrixSelect tca8418MatrixSelect(uint8_t rows, uint8_t cols);

// One FIFO byte as a matrix position. False for an empty FIFO (0), a GPI
// event, or a key outside the 4x10 matrix; *row, *col and *pressed are then
// left alone. `col` is the chip's column, which runs right to left across the
// keycaps: the leftmost key is column 9.
bool tca8418Decode(uint8_t raw, uint8_t* row, uint8_t* col, bool* pressed);

// What each key does.
enum class ProKey : uint8_t { None, Char, Enter, Backspace, Space, Shift, Alt, Sym, Mic };

struct ProKeyCap {
  ProKey kind;
  char plain;   // the letter or symbol it types, for Char
  char alt;     // what it types under Alt or Sym, as the keycap prints it; 0 for none
};

// The key at a chip position. {None} outside the matrix or where no switch is.
ProKeyCap proKeyAt(uint8_t row, uint8_t col);

// The column as the keycaps run, 0 at the left: the chip numbers them the
// other way.
inline uint8_t proKeyPhysicalCol(uint8_t col) { return uint8_t(kProKeyCols - 1 - col); }

// Backspace repeats while held: after kRepeatDelayMs, then every
// kRepeatEveryMs (owner's ruling 4, plan §9). No other key repeats.
constexpr uint32_t kProRepeatDelayMs = 500;
constexpr uint32_t kProRepeatEveryMs = 100;

// One byte for the GUI, as classifyKey() takes it: printable ASCII, 0x0D
// for Enter, 0x08 for Backspace. `repeat` marks a held Backspace's repeat,
// which must never become a Back once the field is empty.
struct ProKeyOut {
  uint8_t byte;
  bool repeat;
};

// Shift, Alt and Sym, and Backspace repeat. Each modifier works two ways:
// held while another key is pressed, it applies to that key; tapped on its
// own, it applies to the next key that types, and a second tap cancels it.
// Shift gives the capital; Alt or Sym gives the symbol the keycap prints
// (both reach the same layer, since each key prints one symbol). A key with
// no symbol types its plain value under Alt or Sym.
class ProKeyState {
public:
  // A press or release at chip position (row, col). Returns true and fills
  // *out when it types something.
  bool event(uint8_t row, uint8_t col, bool pressed, uint32_t now, ProKeyOut* out);
  // A held Backspace's next repeat, when it is due. At most one per call,
  // so a loop held up by a refresh doesn't come back to a burst.
  bool repeat(uint32_t now, ProKeyOut* out);
  // Forgets everything held or latched. For an overflow, where a release may
  // have been lost, and for a chip that stopped answering.
  void reset();

  bool shiftActive() const { return _shift_keys_held || _shift_latched; }
  bool altActive() const { return _alt_keys_held || _alt_latched; }

private:
  // Two Shift keys, and Alt and Sym: each pair is counted, so letting go of
  // one while the other is down neither ends the hold nor latches.
  uint8_t _shift_keys_held = 0;
  bool _shift_latched = false, _shift_used = false;
  uint8_t _alt_keys_held = 0;
  bool _alt_latched = false, _alt_used = false;
  bool _bsp_held = false;
  uint32_t _bsp_next = 0;
};

// Raw FIFO bytes taken in the panel's BUSY wait, kept until the UI loop
// reads them. A full queue drops the newest and counts it, and push()
// returns false, so the caller can let go of anything held: the dropped
// byte may have been a release.
class ProKeyQueue {
public:
  static constexpr uint8_t kCap = 32;
  bool push(uint8_t raw);
  bool pop(uint8_t* raw);
  uint8_t size() const { return _n; }
  uint32_t dropped() const { return _dropped; }

private:
  uint8_t _buf[kCap] = {};
  uint8_t _head = 0, _n = 0;
  uint32_t _dropped = 0;
};

}  // namespace gui
}  // namespace offband
