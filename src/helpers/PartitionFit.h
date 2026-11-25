#pragma once

// #2297: two decisions for the 4 MB ESP32 boards that moved to offband_4MB.csv
// (#2295). Pure, no ESP-IDF, so the native suite tests them.
//
// An OTA writes only the app, so a unit updated over the air keeps whatever
// partition table it was last USB-flashed with. Nothing here ever repartitions
// or formats: that erases the node, and only a USB flash the owner chooses may.

#include <stddef.h>
#include <stdint.h>

namespace partition_fit {

// The upload is multipart: the request's Content-Length is the image plus the
// form framing (boundaries, part headers, the MD5 field). The framing is a few
// hundred bytes; this is a safe ceiling for it.
constexpr size_t kMultipartOverhead = 1024;

enum class OtaVerdict : uint8_t {
  Fits,       // the whole request is no bigger than the slot, so the image fits
  TooBig,     // even with the most framing, the image is bigger than the slot
  Uncertain,  // the last kMultipartOverhead bytes: Update's own size check decides
};

// content_length 0 means the length is unknown (chunked); nothing can be decided
// up front, so the updater's own size check stays the guard.
inline OtaVerdict otaImageFits(size_t content_length, size_t slot_size) {
  if (content_length == 0) return OtaVerdict::Uncertain;
  if (content_length <= slot_size) return OtaVerdict::Fits;
  if (content_length > slot_size + kMultipartOverhead) return OtaVerdict::TooBig;
  return OtaVerdict::Uncertain;
}

// The parts of a partition table that tell the old 4 MB tables from the new one.
struct Layout {
  uint32_t app_size;       // the running app slot
  uint32_t spiffs_offset;  // 0 when there is no SPIFFS partition
  uint32_t spiffs_size;
};

// True when the running table is not the one this build was made for: a unit
// still on its old table after an OTA. It is an exact comparison on purpose.
// "Smaller" catches one board and misses the other:
// * the C3's old default table has a SMALLER app slot (1,280 KB) but MORE SPIFFS
//   (1,408 KB) than offband_4MB.csv;
// * the C6's old min_spiffs.csv has a BIGGER app slot (1,920 KB) but less SPIFFS.
// expected.app_size 0 means the build sets no expectation (boards that never
// changed table), so it is never "old".
inline bool isOldTable(const Layout& running, const Layout& expected) {
  if (expected.app_size == 0) return false;
  return running.app_size != expected.app_size || running.spiffs_offset != expected.spiffs_offset ||
         running.spiffs_size != expected.spiffs_size;
}

}  // namespace partition_fit
