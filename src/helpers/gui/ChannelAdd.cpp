#include "ChannelAdd.h"

#include <string.h>

namespace offband {
namespace gui {

bool hashtagName(const char* typed, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = 0;
  if (!typed) return false;
  while (*typed == '#' || *typed == ' ') typed++;
  size_t len = strlen(typed);
  while (len && typed[len - 1] == ' ') len--;   // a trailing space is dropped too
  if (len == 0 || len + 1 > kChannelNameMax || len + 2 > n) return false;
  out[0] = '#';
  for (size_t i = 0; i < len; i++) {
    char c = typed[i];
    if (c == ' ') {
      out[0] = 0;
      return false;
    }
    out[i + 1] = c;
  }
  out[len + 1] = 0;
  return true;
}

bool namesPublic(const char* typed) {
  if (!typed) return false;
  while (*typed == '#' || *typed == ' ') typed++;
  size_t len = strlen(typed);
  while (len && typed[len - 1] == ' ') len--;
  static const char kPublic[] = "public";
  if (len != sizeof kPublic - 1) return false;
  for (size_t i = 0; i < len; i++) {
    char c = typed[i];
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (c != kPublic[i]) return false;
  }
  return true;
}

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size_t parseChannelKey(const char* text, uint8_t out[kChannelKeyLen]) {
  if (!text) return 0;
  uint8_t key[kChannelKeyLen] = {};
  size_t digits = 0;
  for (const char* p = text; *p; p++) {
    const int v = hexValue(*p);
    if (v < 0) {
      if (*p == ' ' || *p == '-' || *p == ':') continue;
      return kChannelKeyBad;   // a typo isn't skipped: it would silently change the key
    }
    if (digits == kChannelKeyHexLen) return kChannelKeyBad;   // too long: not a key
    key[digits / 2] = uint8_t(key[digits / 2] | (digits % 2 ? v : v << 4));
    digits++;
  }
  if (digits == kChannelKeyHexLen && out) memcpy(out, key, kChannelKeyLen);
  return digits;
}

bool hashtagAccepts(char c) { return c != ' ' && c != '#' && c != 0; }

bool channelKeyAccepts(char c) { return hexValue(c) >= 0; }

void formatChannelKey(const uint8_t key[kChannelKeyLen], char* out, size_t n) {
  static const char kHex[] = "0123456789abcdef";
  if (!out || n == 0) return;
  size_t o = 0;
  for (size_t i = 0; i < kChannelKeyLen; i++) {
    const bool space = i && i % 2 == 0;
    if (o + (space ? 3 : 2) >= n) break;   // and one left for the NUL
    if (space) out[o++] = ' ';
    out[o++] = kHex[key[i] >> 4];
    out[o++] = kHex[key[i] & 0x0F];
  }
  out[o] = 0;
}

int firstFreeChannelSlot(const ChannelSlot* slots, int count) {
  for (int i = 1; slots && i < count; i++) {   // 0 is Public's (#1896)
    if (!slots[i].used) return i;
  }
  return -1;
}

int findChannelByKey(const ChannelSlot* slots, int count, const uint8_t key[kChannelKeyLen]) {
  for (int i = 0; slots && key && i < count; i++) {
    if (slots[i].used && memcmp(slots[i].key, key, kChannelKeyLen) == 0) return i;
  }
  return -1;
}

int usedChannelSlots(const ChannelSlot* slots, int count) {
  int n = 0;
  for (int i = 0; slots && i < count; i++) n += slots[i].used ? 1 : 0;
  return n;
}

}  // namespace gui
}  // namespace offband
