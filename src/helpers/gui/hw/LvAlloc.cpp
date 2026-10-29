// #1738 (Epic V #1893): LVGL's allocator on the ESP32 GUI builds. The policy
// and its reasons are in LvAlloc.h.
#include "../LvAlloc.h"

// lv_conf.h selects these on the same condition.
#if defined(ARDUINO_ARCH_ESP32) && !defined(OFFBAND_NATIVE_LVGL)
#include <esp_heap_caps.h>

static constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
static constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

extern "C" void* offband_lv_alloc(size_t size) {
  return offband::gui::allocPreferring(
      size, [](size_t n) { return heap_caps_malloc(n, kPsram); },
      [](size_t n) { return heap_caps_malloc(n, kInternal); });
}

// heap_caps_free takes a block from either heap.
extern "C" void offband_lv_free(void* p) { heap_caps_free(p); }

extern "C" void* offband_lv_realloc(void* p, size_t size) {
  return offband::gui::reallocPreferring(
      p, size, [](void* q, size_t n) { return heap_caps_realloc(q, n, kPsram); },
      [](void* q, size_t n) { return heap_caps_realloc(q, n, kInternal); });
}
#endif
