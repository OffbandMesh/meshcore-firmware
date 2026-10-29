// #1738 (Epic V #1893): LVGL's allocator on the ESP32 GUI builds. The policy
// and its reasons are in LvAlloc.h.
#include "../LvAlloc.h"

// lv_conf.h selects these on the same condition.
#if defined(ARDUINO_ARCH_ESP32) && !defined(OFFBAND_NATIVE_LVGL)
#include <esp_heap_caps.h>
#include "lv_conf.h"

// Built only where lv_conf.h should route LVGL here, so a config that stops
// doing so (LV_MEM_CUSTOM 0, or plain malloc again) fails the build instead of
// quietly putting LVGL back in internal RAM.
static_assert(LV_MEM_CUSTOM == 1, "LVGL must use the custom allocator");
static_assert(&LV_MEM_CUSTOM_ALLOC == &offband_lv_alloc, "lv_conf.h must route LVGL to offband_lv_alloc");
static_assert(&LV_MEM_CUSTOM_FREE == &offband_lv_free, "lv_conf.h must route LVGL to offband_lv_free");
static_assert(&LV_MEM_CUSTOM_REALLOC == &offband_lv_realloc, "lv_conf.h must route LVGL to offband_lv_realloc");

static constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
static constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

// LVGL runs on the main loop only, so a plain counter is enough.
static unsigned s_fallbacks = 0;

extern "C" void* offband_lv_alloc(size_t size) {
  return offband::gui::allocPreferring(
      size, [](size_t n) { return heap_caps_malloc(n, kPsram); },
      [](size_t n) { s_fallbacks++; return heap_caps_malloc(n, kInternal); });
}

// heap_caps_free takes a block from either heap.
extern "C" void offband_lv_free(void* p) { heap_caps_free(p); }

extern "C" void* offband_lv_realloc(void* p, size_t size) {
  return offband::gui::reallocPreferring(
      p, size, [](void* q, size_t n) { return heap_caps_realloc(q, n, kPsram); },
      [](void* q, size_t n) { s_fallbacks++; return heap_caps_realloc(q, n, kInternal); });
}

extern "C" unsigned offband_lv_fallbacks(void) { return s_fallbacks; }
#endif
