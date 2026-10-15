#include "LvglPort.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "GuiPanel.h"

namespace offband {
namespace gui {

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static GuiPanel* s_panel = nullptr;
static size_t s_buf_bytes = 0;

// Set by the SPI interrupt when a pixel transfer ends. The interrupt must not
// call into LVGL: LVGL lives in flash, and the interrupt can fire while a
// SPIFFS write has the flash cache disabled. LVGL is told from task context.
static volatile bool s_flush_done = false;

static void IRAM_ATTR onTransferDone(void* arg) {
  (void)arg;
  s_flush_done = true;
}

static void ackFlush(lv_disp_drv_t* drv) {
  if (s_flush_done) {
    s_flush_done = false;
    lv_disp_flush_ready(drv);
  }
}

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  const size_t len = size_t(area->x2 - area->x1 + 1) * size_t(area->y2 - area->y1 + 1) *
                     sizeof(lv_color_t);
  St7789Dma& panel = s_panel->panel();
  panel.setWindow(area->x1, area->y1, area->x2, area->y2);
  if (!panel.writePixelsAsync(reinterpret_cast<const uint8_t*>(px), len)) {
    lv_disp_flush_ready(drv);  // nothing went out; don't stall LVGL on it
  }
}

// LVGL calls this while it waits for a buffer to come back.
static void waitCb(lv_disp_drv_t* drv) { ackFlush(drv); }

bool lvglBegin(GuiPanel& panel) {
  s_panel = &panel;
  const uint32_t px = uint32_t(panel.width()) * kDrawBufLines;
  const size_t bytes = px * sizeof(lv_color_t);

  void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!b1 || !b2) {
    heap_caps_free(b1);
    heap_caps_free(b2);
    return false;
  }
  if (!panel.begin(bytes)) {
    heap_caps_free(b1);
    heap_caps_free(b2);
    return false;
  }
  s_buf_bytes = 2 * bytes;

  lv_init();
  lv_disp_draw_buf_init(&s_draw_buf, b1, b2, px);
  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = panel.width();
  s_disp_drv.ver_res = panel.height();
  s_disp_drv.flush_cb = flushCb;
  s_disp_drv.wait_cb = waitCb;
  s_disp_drv.draw_buf = &s_draw_buf;
  panel.panel().setDoneCallback(onTransferDone, nullptr);
  lv_disp_drv_register(&s_disp_drv);
  return true;
}

void lvglLoop() {
  ackFlush(&s_disp_drv);
  lv_timer_handler();
}

size_t lvglDrawBufBytes() { return s_buf_bytes; }

}  // namespace gui
}  // namespace offband
