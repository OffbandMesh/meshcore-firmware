#include "TdeckProKeys.h"

namespace offband {
namespace gui {

MatrixSelect tca8418MatrixSelect(uint8_t rows, uint8_t cols) {
  if (rows > 8) rows = 8;
  if (cols > 10) cols = 10;
  MatrixSelect m;
  m.rows = uint8_t((1u << rows) - 1);
  m.cols_lo = uint8_t((1u << (cols > 8 ? 8 : cols)) - 1);
  m.cols_hi = uint8_t(cols > 8 ? (1u << (cols - 8)) - 1 : 0);
  return m;
}

bool tca8418Decode(uint8_t raw, uint8_t* row, uint8_t* col, bool* pressed) {
  const uint8_t key = raw & 0x7F;
  if (key < 1 || key > 80) return false;   // empty FIFO, or a GPI event (97-114)
  const uint8_t r = uint8_t((key - 1) / 10), c = uint8_t((key - 1) % 10);
  if (r >= kProKeyRows || c >= kProKeyCols) return false;
  if (row) *row = r;
  if (col) *col = c;
  if (pressed) *pressed = (raw & 0x80) != 0;
  return true;
}

namespace {

constexpr ProKeyCap C(char plain, char alt) { return ProKeyCap{ProKey::Char, plain, alt}; }
constexpr ProKeyCap K(ProKey k) { return ProKeyCap{k, 0, 0}; }
constexpr ProKeyCap kNone = {ProKey::None, 0, 0};

// By chip position: column 0 is the rightmost keycap. Each row's comment
// reads left to right as the keycaps do.
const ProKeyCap kMap[kProKeyRows][kProKeyCols] = {
    // q w e r t y u i o p
    {C('p', '@'), C('o', '+'), C('i', '-'), C('u', '_'), C('y', ')'), C('t', '('), C('r', '3'), C('e', '2'),
     C('w', '1'), C('q', '#')},
    // a s d f g h j k l Backspace
    {K(ProKey::Backspace), C('l', '"'), C('k', '\''), C('j', ';'), C('h', ':'), C('g', '/'), C('f', '6'),
     C('d', '5'), C('s', '4'), C('a', '*')},
    // Alt z x c v b n m $ Enter
    {K(ProKey::Enter), C('$', 0), C('m', '.'), C('n', ','), C('b', '!'), C('v', '?'), C('c', '9'), C('x', '8'),
     C('z', '7'), K(ProKey::Alt)},
    // Shift Mic Space Sym Shift, then no switch
    {K(ProKey::Shift), K(ProKey::Sym), K(ProKey::Space), ProKeyCap{ProKey::Mic, 0, '0'}, K(ProKey::Shift), kNone,
     kNone, kNone, kNone, kNone},
};

bool due(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }

// One modifier pair's press or release. A tap is a press and release of the
// pair with nothing typed in between; it toggles the latch.
void modifier(bool pressed, uint8_t* held, bool* used, bool* latched) {
  if (pressed) {
    if (*held == 0) *used = false;
    if (*held < 2) (*held)++;
  } else if (*held) {
    if (--*held == 0 && !*used) *latched = !*latched;
  }
}

}  // namespace

ProKeyCap proKeyAt(uint8_t row, uint8_t col) {
  if (row >= kProKeyRows || col >= kProKeyCols) return kNone;
  return kMap[row][col];
}

bool ProKeyState::event(uint8_t row, uint8_t col, bool pressed, uint32_t now, ProKeyOut* out) {
  const ProKeyCap cap = proKeyAt(row, col);
  switch (cap.kind) {
    case ProKey::None:
      return false;
    case ProKey::Shift:
      modifier(pressed, &_shift_keys_held, &_shift_used, &_shift_latched);
      return false;
    case ProKey::Alt:
    case ProKey::Sym:
      modifier(pressed, &_alt_keys_held, &_alt_used, &_alt_latched);
      return false;
    default:
      break;
  }

  if (!pressed) {
    if (cap.kind == ProKey::Backspace) _bsp_held = false;
    return false;
  }

  const bool alt = altActive(), shift = shiftActive();
  uint8_t b = 0;
  switch (cap.kind) {
    case ProKey::Char:
      if (alt && cap.alt) {
        b = uint8_t(cap.alt);
      } else if (shift && cap.plain >= 'a' && cap.plain <= 'z') {
        b = uint8_t(cap.plain - 'a' + 'A');
      } else {
        b = uint8_t(cap.plain);
      }
      break;
    case ProKey::Mic:
      if (!alt) return false;   // the microphone key types only its symbol; a latch waits for a key that types
      b = uint8_t(cap.alt);
      break;
    case ProKey::Space:
      b = ' ';
      break;
    case ProKey::Enter:
      b = 0x0D;
      break;
    case ProKey::Backspace:
      b = 0x08;
      _bsp_held = true;
      _bsp_next = now + kProRepeatDelayMs;
      break;
    default:
      return false;
  }
  if (_shift_keys_held) _shift_used = true;
  if (_alt_keys_held) _alt_used = true;
  _shift_latched = _alt_latched = false;   // a tapped modifier applies to one key
  if (out) *out = ProKeyOut{b, false};
  return true;
}

bool ProKeyState::repeat(uint32_t now, ProKeyOut* out) {
  if (!_bsp_held || !due(now, _bsp_next)) return false;
  _bsp_next = now + kProRepeatEveryMs;
  if (out) *out = ProKeyOut{0x08, true};
  return true;
}

void ProKeyState::reset() { *this = ProKeyState(); }

bool ProKeyQueue::push(uint8_t raw) {
  if (_n >= kCap) {
    _dropped++;
    return false;
  }
  _buf[uint8_t((_head + _n) % kCap)] = raw;
  _n++;
  return true;
}

bool ProKeyQueue::pop(uint8_t* raw) {
  if (!_n) return false;
  if (raw) *raw = _buf[_head];
  _head = uint8_t((_head + 1) % kCap);
  _n--;
  return true;
}

}  // namespace gui
}  // namespace offband
