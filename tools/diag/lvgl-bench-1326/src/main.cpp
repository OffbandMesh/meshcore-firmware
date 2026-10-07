// #1326 D4: how fast can LVGL draw a message list on the Heltec V4 TFT, and how
// much of each frame is CPU (render) vs the 40 MHz SPI transfer (flush)?
//
// One run measures 80, 160 and 240 MHz by switching the CPU clock at runtime;
// the APB clock (and so the SPI clock) stays at 80 MHz across all three, so the
// flush share should hold steady and only the render share should move.
//
// Output: one "[lvgl]" line per measurement on the USB console, repeated every
// cycle so a capture started late still gets a full set.
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

static SPIClass s_spi(HSPI);
static Adafruit_ST7789 s_tft(&s_spi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_obj_t* s_list = nullptr;

static uint32_t s_flush_us = 0;   // SPI time accumulated across one measured refresh

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  uint32_t t0 = micros();
  uint32_t w = a->x2 - a->x1 + 1;
  uint32_t h = a->y2 - a->y1 + 1;
  s_tft.startWrite();
  s_tft.setAddrWindow(a->x1, a->y1, w, h);
  s_tft.writePixels(reinterpret_cast<uint16_t*>(px), w * h, true, false);
  s_tft.endWrite();
  s_flush_us += micros() - t0;
  lv_disp_flush_ready(drv);
}

static void logHeap(const char* tag) {
  Serial.printf("[lvgl] heap %s int free=%u largest=%u psram free=%u\n", tag,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// A screen shaped like the design's channel view: a title bar over a scrolling
// column of two-line message bubbles.
static void buildUi() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

  lv_obj_t* bar = lv_obj_create(scr);
  lv_obj_set_size(bar, LV_PCT(100), 32);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x1F6FEB), 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* title = lv_label_create(bar);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "#public");
  lv_obj_center(title);

  s_list = lv_obj_create(scr);
  lv_obj_set_size(s_list, LV_PCT(100), s_tft.height() - 32);
  lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(s_list, 0, 0);
  lv_obj_set_style_bg_color(s_list, lv_color_hex(0x101418), 0);
  lv_obj_set_style_pad_all(s_list, 6, 0);
  lv_obj_set_style_pad_row(s_list, 6, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

  for (int i = 0; i < 40; i++) {
    lv_obj_t* b = lv_obj_create(s_list);
    lv_obj_set_width(b, LV_PCT(85));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_pad_all(b, 6, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex((i & 1) ? 0x2D333B : 0x0E4429), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    if (i & 1) lv_obj_set_style_align(b, LV_ALIGN_RIGHT_MID, 0);

    lv_obj_t* who = lv_label_create(b);
    lv_obj_set_style_text_color(who, lv_color_hex(0x7EE787), 0);
    lv_label_set_text_fmt(who, "Node-%02d  12:%02d", i, i);

    lv_obj_t* msg = lv_label_create(b);
    lv_obj_set_width(msg, LV_PCT(100));
    lv_obj_set_style_text_color(msg, lv_color_hex(0xE6EDF3), 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_label_set_text(msg, "Heard you 2 hops out, SNR 6.5. Anyone on the ridge tonight?");
  }
}

static void measure(uint32_t mhz) {
  setCpuFrequencyMhz(mhz);
  delay(100);

  // Full-screen redraw: everything invalidated, one synchronous refresh.
  const int kFull = 10;
  uint32_t full_us = 0, full_flush_us = 0;
  for (int i = 0; i < kFull; i++) {
    lv_obj_invalidate(lv_scr_act());
    s_flush_us = 0;
    uint32_t t0 = micros();
    lv_refr_now(NULL);
    full_us += micros() - t0;
    full_flush_us += s_flush_us;
  }

  // List scroll: 6 px per frame, down then back up, each frame refreshed
  // synchronously so frame time = render + flush with no idle in between.
  const int kSteps = 100;
  uint32_t scr_us = 0, scr_flush_us = 0, scr_max_us = 0;
  for (int i = 0; i < kSteps; i++) {
    int dy = (i < kSteps / 2) ? -6 : 6;
    lv_obj_scroll_by(s_list, 0, dy, LV_ANIM_OFF);
    s_flush_us = 0;
    uint32_t t0 = micros();
    lv_refr_now(NULL);
    uint32_t dt = micros() - t0;
    scr_us += dt;
    scr_flush_us += s_flush_us;
    if (dt > scr_max_us) scr_max_us = dt;
  }

  float full_ms = full_us / 1000.0f / kFull;
  float full_flush_ms = full_flush_us / 1000.0f / kFull;
  float scr_ms = scr_us / 1000.0f / kSteps;
  float scr_flush_ms = scr_flush_us / 1000.0f / kSteps;
  Serial.printf("[lvgl] mhz=%u full=%.1fms (render %.1f flush %.1f) "
                "scroll=%.1fms/frame (render %.1f flush %.1f) max=%.1fms fps=%.1f "
                "buf_lines=%d spi=%uMHz %ux%u\n",
                (unsigned)getCpuFrequencyMhz(),
                full_ms, full_ms - full_flush_ms, full_flush_ms,
                scr_ms, scr_ms - scr_flush_ms, scr_flush_ms,
                scr_max_us / 1000.0f, scr_ms > 0 ? 1000.0f / scr_ms : 0.0f,
                BUF_LINES, (unsigned)(TFT_SPI_HZ / 1000000UL),
                (unsigned)s_tft.width(), (unsigned)s_tft.height());
}

void setup() {
  Serial.begin(115200);
  delay(3000);   // give the capture time to attach after a reset
  Serial.println("[lvgl] bench #1326 start");

  pinMode(PIN_VEXT_EN, OUTPUT);
  digitalWrite(PIN_VEXT_EN, HIGH);
  delay(50);
  pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
  digitalWrite(PIN_TFT_LEDA_CTL, HIGH);

  s_spi.begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, PIN_TFT_CS);
  s_tft.init(240, 320);
  s_tft.setRotation(TFT_ROTATION);
  s_tft.setSPISpeed(TFT_SPI_HZ);
  s_tft.fillScreen(ST77XX_BLACK);

  logHeap("before-lvgl");
  lv_init();

  const uint32_t buf_px = (uint32_t)s_tft.width() * BUF_LINES;
  lv_color_t* buf = static_cast<lv_color_t*>(
      heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  if (!buf) {
    Serial.println("[lvgl] draw buffer alloc FAILED");
    return;
  }
  lv_disp_draw_buf_init(&s_draw_buf, buf, NULL, buf_px);
  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = s_tft.width();
  s_disp_drv.ver_res = s_tft.height();
  s_disp_drv.flush_cb = flushCb;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  buildUi();
  lv_refr_now(NULL);
  logHeap("after-ui");
}

void loop() {
  if (!s_list) { delay(1000); return; }
  static const uint32_t kMhz[] = {80, 160, 240};
  for (uint32_t mhz : kMhz) measure(mhz);
  setCpuFrequencyMhz(80);
  logHeap("cycle-end");

  uint32_t until = millis() + 20000;
  while ((int32_t)(until - millis()) > 0) {
    lv_timer_handler();
    delay(5);
  }
}
