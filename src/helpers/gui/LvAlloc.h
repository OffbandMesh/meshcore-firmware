#pragma once
// #1738 (Epic V #1893): where LVGL's objects live.
//
// The ESP32-S3 GUI builds serve every malloc of 4 KB or less from internal RAM
// (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096), and nearly every LVGL object,
// style and label is smaller than that. Through plain malloc the whole object
// tree landed in internal RAM: free internal RAM fell from ~120 KB before the
// GUI to 21-48 KB, in fragments, with PSRAM almost unused. That left no room
// for Wi-Fi beside BLE (#1891). LVGL now asks for PSRAM first and falls back
// to internal RAM only when PSRAM can't serve the request, or the board has
// none. The draw buffers don't come through here: LvglPort takes them as
// DMA-capable internal RAM.
//
// lv_conf.h includes this from LVGL's C sources, so the C half stays C.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The ESP32 implementation is in hw/LvAlloc.cpp.
void* offband_lv_alloc(size_t size);
void offband_lv_free(void* p);
void* offband_lv_realloc(void* p, size_t size);
// How many LVGL allocations PSRAM couldn't serve, so went to internal RAM:
// the [mem] line prints it, since that fallback is otherwise silent.
unsigned offband_lv_fallbacks(void);

#ifdef __cplusplus
}

namespace offband {
namespace gui {

// The policy, apart from the heap it runs on so the host tests can drive it:
// the preferred heap first, the fallback only if that fails.
template <class Prefer, class Fallback>
void* allocPreferring(size_t size, Prefer prefer, Fallback fallback) {
  void* p = prefer(size);
  return p ? p : fallback(size);
}

// A realloc moves the block to the preferred heap when it can, and falls back
// in place. Size 0 frees the block, and no fallback is tried: a null return
// there is the answer, not a failure.
template <class Prefer, class Fallback>
void* reallocPreferring(void* p, size_t size, Prefer prefer, Fallback fallback) {
  void* q = prefer(p, size);
  if (q || size == 0) return q;
  return fallback(p, size);
}

}  // namespace gui
}  // namespace offband
#endif
