#include "LvglPort.h"

#include <Arduino.h>
#include <MeshLog.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "Chsc6xTouch.h"
#include "GuiPanel.h"

namespace offband {
namespace gui {

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static GuiPanel* s_panel = nullptr;
static size_t s_buf_bytes = 0;

static bool s_flush_inflight = false;

// A flush is finished when the SPI driver says the pixel write is done. It's
// asked from task context (LVGL's wait callback and the UI loop), never from
// the SPI interrupt: LVGL lives in flash, and the interrupt can fire while a
// SPIFFS write has the flash cache disabled.
static void ackFlush(lv_disp_drv_t* drv) {
  if (s_flush_inflight && s_panel->panel().pollDone()) {
    s_flush_inflight = false;
    lv_disp_flush_ready(drv);
  }
}

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  const size_t len = size_t(area->x2 - area->x1 + 1) * size_t(area->y2 - area->y1 + 1) *
                     sizeof(lv_color_t);
  St7789Dma& panel = s_panel->panel();
  panel.setWindow(area->x1, area->y1, area->x2, area->y2);
  if (panel.writePixelsAsync(reinterpret_cast<const uint8_t*>(px), len)) {
    s_flush_inflight = true;
  } else {
    lv_disp_flush_ready(drv);  // nothing went out; don't stall LVGL on it
  }
}

#ifdef OFFBAND_GUI_PERF
// Diag builds: a pixel write that never finishes hangs the refresh until the
// task watchdog fires. Say so once, so the log names the cause (#1445).
static uint32_t s_wait_since = 0;
static bool s_stall_reported = false;

static void noteWait(bool still_waiting) {
  if (!still_waiting) {
    s_wait_since = 0;
    return;
  }
  if (!s_wait_since) s_wait_since = millis();
  if (!s_stall_reported && millis() - s_wait_since > 2000) {
    s_stall_reported = true;
    mesh_log_line(MLOG_BOOT, "[gui] STALL: panel pixel write not finished after 2 s\n");
  }
}
#endif

// LVGL calls this while it waits for a buffer to come back.
static void waitCb(lv_disp_drv_t* drv) {
  ackFlush(drv);
#ifdef OFFBAND_GUI_PERF
  noteWait(s_flush_inflight);
#endif
}

static uint32_t s_frames = 0;
static uint32_t s_frame_ms_total = 0;
static uint32_t s_frame_ms_max = 0;

// Called by LVGL after each refresh with how long it took.
static void monitorCb(lv_disp_drv_t* drv, uint32_t time_ms, uint32_t px) {
  (void)drv;
  s_frames++;
  s_frame_ms_total += time_ms;
  if (time_ms > s_frame_ms_max) s_frame_ms_max = time_ms;
#ifdef OFFBAND_GUI_PERF
  // #1534: a slow frame, with its size, separates a big redraw from a stall.
  if (time_ms > 150) mesh_log_line(MLOG_BOOT, "[gui] slow frame: %lums, %lu px\n", (unsigned long)time_ms, (unsigned long)px);
#else
  (void)px;
#endif
}

void lvglTakeFrameStats(uint32_t* frames, uint32_t* avg_ms, uint32_t* max_ms) {
  *frames = s_frames;
  *avg_ms = s_frames ? s_frame_ms_total / s_frames : 0;
  *max_ms = s_frame_ms_max;
  s_frames = 0;
  s_frame_ms_total = 0;
  s_frame_ms_max = 0;
}

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
  s_disp_drv.monitor_cb = monitorCb;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);
  return true;
}

static lv_indev_drv_t s_indev_drv;
static Chsc6xTouch* s_touch = nullptr;
static TouchMapping s_mapping = {};
static lv_point_t s_last = {0, 0};
static bool s_pressed = false;
static bool s_any_touch = false;

// Consecutive failed reads before a held press is reported released: one
// glitch frame must not drop a drag, but a bus that stays down must not leave
// a press stuck. 3 reads at LVGL's 30 ms input period is about 90 ms.
static constexpr uint8_t kReleaseAfterFailedReads = 3;
static uint8_t s_failed_reads = 0;

static void touchReadCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  (void)drv;
  TouchPoint p{};
  if (s_touch && s_touch->read(&p)) {
    s_failed_reads = 0;
    s_pressed = p.pressed;
    if (p.pressed) {
      uint16_t x = p.x, y = p.y;
      mapTouch(s_mapping, &x, &y);
      s_last.x = lv_coord_t(x);
      s_last.y = lv_coord_t(y);
      s_any_touch = true;
    }
  } else if (s_failed_reads < kReleaseAfterFailedReads && ++s_failed_reads == kReleaseAfterFailedReads) {
    s_pressed = false;
  }
  data->point = s_last;
  data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lvglAttachTouch(Chsc6xTouch& touch, const TouchMapping& mapping) {
  s_touch = &touch;
  s_mapping = mapping;
  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = touchReadCb;
  lv_indev_drv_register(&s_indev_drv);
}

bool lvglLastTouch(uint16_t* x, uint16_t* y) {
  if (!s_any_touch) return false;
  *x = uint16_t(s_last.x);
  *y = uint16_t(s_last.y);
  return true;
}

void lvglLoop() {
  ackFlush(&s_disp_drv);
  lv_timer_handler();
}

size_t lvglDrawBufBytes() { return s_buf_bytes; }

}  // namespace gui
}  // namespace offband
