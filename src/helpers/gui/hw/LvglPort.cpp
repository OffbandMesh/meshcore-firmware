#include "LvglPort.h"

#include <Arduino.h>
#include <MeshLog.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "../GuiLayout.h"
#ifdef GUI_PANEL_EINK
  #include "../EinkFrame.h"
#endif
#include "Chsc6xTouch.h"
#include "GuiPanel.h"

namespace offband {
namespace gui {

static lv_disp_draw_buf_t s_draw_buf;
static uint16_t s_rotation = 0;   // Epic H: 0, 90 or 270
static lv_disp_drv_t s_disp_drv;
static GuiPanel* s_panel = nullptr;
static size_t s_buf_bytes = 0;

static bool s_flush_inflight = false;

#ifdef GUI_PANEL_EINK
// E-paper (#2009): LVGL draws into the panel's frame buffer, which takes no
// time, and the pacer decides when the glass refreshes, which takes most of
// a second.
static EinkPacer s_pacer;

static void ackFlush(lv_disp_drv_t* drv) { (void)drv; }

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  for (lv_coord_t y = area->y1; y <= area->y2; y++) {
    for (lv_coord_t x = area->x1; x <= area->x2; x++) {
      s_panel->setInk(x, y, einkInk(lv_color_brightness(*px++)));
    }
  }
  s_pacer.mark(area->x1, area->y1, area->x2, area->y2);
  lv_disp_flush_ready(drv);
}

// Sends what LVGL has drawn when the pacer says it's time. Nothing goes out
// while the panel sleeps; it's kept for the wake.
static void pushIfDue() {
  if (!s_panel->isOn()) return;
  if (s_panel->takeNeedFull()) s_pacer.requestFull();
  const uint32_t now = millis();
  const EinkPush kind = s_pacer.due(now);
  if (kind == EinkPush::None) return;
  const EinkArea& a = s_pacer.dirty();
  s_panel->push(kind == EinkPush::Full, a.x1, a.y1, a.x2, a.y2);
  s_pacer.pushed(kind, millis());   // after the push: the gap is free loop time
}
#else
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
#endif  // GUI_PANEL_EINK

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
static TouchSource* s_touch = nullptr;
static TouchMapping s_mapping = {};
static lv_point_t s_last = {0, 0};
static bool s_any_touch = false;
// A failed read keeps a held press for a few polls, then lets it go
// (TouchFrame.h, kReleaseAfterFailedReads).
static FailedReadGuard s_guard;

static void touchReadCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  (void)drv;
  TouchPoint p{};
  const bool ok = s_touch && s_touch->read(&p);
#ifdef OFFBAND_GUI_PERF
  const bool press_edge = ok && p.pressed && !s_guard.pressed();
#endif
  s_guard.update(ok, p.pressed);
  if (ok) {
    if (p.pressed) {
      uint16_t x = p.x, y = p.y;
      mapTouch(s_mapping, &x, &y);   // to portrait panel coordinates
      rotateTouch(s_rotation, &x, &y);   // to where LVGL draws now (Epic H)
#ifdef OFFBAND_GUI_PERF
      // Diag builds: the first presses, raw and as LVGL gets them, so a bench
      // tap settles any mapping question (#1730).
      static uint8_t s_logged = 0;
      if (press_edge && s_logged < 30) {
        s_logged++;
        mesh_log_line(MLOG_BOOT, "[gui] touch raw=%u,%u -> %u,%u rot=%u\n", unsigned(p.x), unsigned(p.y), unsigned(x),
                      unsigned(y), unsigned(s_rotation));
      }
#endif
      s_last.x = lv_coord_t(x);
      s_last.y = lv_coord_t(y);
      s_any_touch = true;
    }
  }
  data->point = s_last;
  data->state = s_guard.pressed() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lvglAttachTouch(TouchSource& touch, const TouchMapping& mapping) {
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
#ifdef GUI_PANEL_EINK
  if (s_panel) pushIfDue();
#endif
}

// Epic H: the panel turned by MADCTL, LVGL told the new resolution, and
// touches mapped the same way. The draw buffers are sized in pixels, so they
// serve either orientation.
void lvglSetRotation(uint16_t rotation, uint8_t portrait_madctl) {
  if (!s_panel) return;
  const Layout l = layoutFor(rotation);
#ifdef GUI_PANEL_EINK
  // No MADCTL: GxEPD2 turns the frame buffer. The whole screen is redrawn
  // in the new orientation, so it goes out as one full refresh.
  (void)portrait_madctl;
  s_panel->setRotation(l.rotation);
  s_pacer.requestFull();
#else
  // Let the last frame finish first; a frame takes tens of ms, so 200 ms
  // means the transfer is stuck, and turning anyway beats hanging the loop.
  const uint32_t t0 = millis();
  while (s_flush_inflight && millis() - t0 < 200) ackFlush(&s_disp_drv);
  s_panel->panel().setMadctl(madctlFor(l.rotation, portrait_madctl));
#endif
  s_rotation = l.rotation;
  // A touch held across the turn is in the old coordinates: drop it.
  s_guard.release();
  s_last = {0, 0};
  s_disp_drv.hor_res = l.w;
  s_disp_drv.ver_res = l.h;
  lv_disp_t* d = lv_disp_get_default();
  if (d) {
    lv_disp_drv_update(d, &s_disp_drv);
    lv_obj_invalidate(lv_scr_act());
  }
}

size_t lvglDrawBufBytes() { return s_buf_bytes; }

}  // namespace gui
}  // namespace offband
