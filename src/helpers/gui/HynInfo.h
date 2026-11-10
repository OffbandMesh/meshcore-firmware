#pragma once

// Hynitron touch controllers' info blocks (#2108). The T-Deck Pro V1.1
// shipped with a CST328 or a CST3530, both at I2C 0x1A, in two different
// command families (plan row 2):
//
// - cst66xx (36xx, 35xx, 66xx, 68xx, 148E): 4-byte big-endian register
//   addresses. After the normal-mode writes, 50 bytes read from 0xD0030000.
//   Bytes 2 and 3 are 0xCA when the block is real; the chip type is bytes 0-3,
//   little-endian.
// - cst3xx (328, 128, 140, 148, 340, 348): 2-byte big-endian addresses. After
//   a write of 0xD101, 28 bytes read from 0xD1F4. The chip type is bytes 18-19,
//   little-endian. The block has no marker, so a read is believed only when its
//   chip type and resolution are plausible.
//
// Layouts from the register use in LilyGo's V1.1 sources; nothing is taken
// from them (they carry no license). Arduino-free so the native tests cover it.

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

enum class HynFamily : uint8_t { None, Cst66xx, Cst3xx };

struct HynInfo {
  HynFamily family = HynFamily::None;
  uint32_t chip_type = 0;
  uint32_t fw_ver = 0;
  uint32_t project_id = 0;
  uint16_t res_x = 0, res_y = 0;
};

constexpr uint8_t kHynAddr = 0x1A;
constexpr size_t kCst66xxInfoLen = 50;
constexpr size_t kCst3xxInfoLen = 28;

inline const char* hynFamilyName(HynFamily f) {
  return f == HynFamily::Cst66xx ? "cst66xx" : f == HynFamily::Cst3xx ? "cst3xx" : "none";
}

namespace detail {
inline uint32_t le32(const uint8_t* b) {
  return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}
inline uint16_t le16(const uint8_t* b) { return uint16_t(b[0] | (b[1] << 8)); }
// The T-Deck Pro's panel is 240x320. Below 16 or past 4096 on either axis is
// bus noise, not a touch panel.
inline bool plausibleRes(uint16_t x, uint16_t y) { return x >= 16 && y >= 16 && x <= 4096 && y <= 4096; }
}  // namespace detail

// True when `b` is a real cst66xx info block; fills `out` then.
inline bool parseCst66xxInfo(const uint8_t* b, size_t len, HynInfo* out) {
  if (!b || len < kCst66xxInfoLen || b[2] != 0xCA || b[3] != 0xCA) return false;
  HynInfo i;
  i.family = HynFamily::Cst66xx;
  i.chip_type = detail::le32(b + 0);
  i.res_x = detail::le16(b + 28);
  i.res_y = detail::le16(b + 30);
  i.fw_ver = detail::le32(b + 32);
  i.project_id = detail::le32(b + 36);
  if (out) *out = i;
  return true;
}

// True when `b` is a plausible cst3xx info block; fills `out` then.
inline bool parseCst3xxInfo(const uint8_t* b, size_t len, HynInfo* out) {
  if (!b || len < kCst3xxInfoLen) return false;
  HynInfo i;
  i.family = HynFamily::Cst3xx;
  i.chip_type = detail::le16(b + 18);
  i.res_x = detail::le16(b + 4);
  i.res_y = detail::le16(b + 6);
  i.project_id = detail::le16(b + 16);
  i.fw_ver = detail::le32(b + 20);
  if (i.chip_type == 0 || i.chip_type == 0xFFFF || !detail::plausibleRes(i.res_x, i.res_y)) return false;
  if (out) *out = i;
  return true;
}

}  // namespace gui
}  // namespace offband
