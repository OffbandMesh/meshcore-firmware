// LVGL 8.3.11 configuration for the Offband GUI (Feature #1313, decision 7).
// Only settings that differ from LVGL's defaults (lv_conf_internal.h) are set.
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
// The panel takes RGB565 big-endian; LVGL swaps bytes when it renders so the
// DMA transfer can send the buffer as-is.
#define LV_COLOR_16_SWAP 1

// LVGL objects come from the system heap, so the [mem] telemetry counts them.
// #1738: on the ESP32 builds they go to PSRAM first (LvAlloc.h says why), so
// they show in [mem]'s psram fields, and lv_int counts any that fell back.
#define LV_MEM_CUSTOM 1
#if defined(ARDUINO_ARCH_ESP32) && !defined(OFFBAND_NATIVE_LVGL)
#define LV_MEM_CUSTOM_INCLUDE "LvAlloc.h"
#define LV_MEM_CUSTOM_ALLOC offband_lv_alloc
#define LV_MEM_CUSTOM_FREE offband_lv_free
#define LV_MEM_CUSTOM_REALLOC offband_lv_realloc
#else
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#endif

#ifdef OFFBAND_NATIVE_LVGL
// #1671: the native LVGL tests (env:native_lvgl) advance the tick themselves
// with lv_tick_inc(); every other setting here is the device's. On a device
// nothing would advance it and the GUI would freeze, so it's refused there.
#ifdef ARDUINO
#error "OFFBAND_NATIVE_LVGL is for the native tests only"
#endif
#define LV_TICK_CUSTOM 0
#else
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_DISP_DEF_REFR_PERIOD 16

// Placeholder fonts until the design's font table lands (A6, #1444).
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif
