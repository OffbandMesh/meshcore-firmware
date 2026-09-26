#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// #1075: one place that names a reset, for the two decode tables that used to
// keep their own copies and drift apart:
//   - CrashLog's resetReasonString(), behind the ring banner and the [boot]
//     line's reset_reason= field: short tokens.
//   - ESP32Board::getResetReasonString(), behind "[boot] <role> up; reset=":
//     sentences.
//
// Both switched on esp_reset_reason_t and both stopped at ESP_RST_SDIO, so
// every newer code printed "UNKNOWN" -- including the two a board reports most
// often. A Photon-1W spent hours as a mystery for reporting 11, which means
// "reset by the USB peripheral", i.e. somebody opened the port (#1036, #1084).
//
// The codes are written as literals rather than the IDF enum names on purpose:
// ESP-IDF 4.4 (the RC32's toolchain) declares only 0..10, so naming 11..15
// there would not compile, while a table that knows them still reads a capture
// from an IDF 5 board correctly.
//
// Pure and Arduino-free: unit-tested natively in test/test_reset_reason.

namespace offband {
namespace reset {

struct Reason {
  uint8_t     code;
  const char* token;    // grep-able, for the machine-read lines
  const char* phrase;   // human, for the boot banner
};

// esp_reset_reason_t. 0..10 exist on every IDF we build; 11..15 were added in
// IDF 5.0 (USB, JTAG, EFUSE) and 5.1 (PWR_GLITCH, CPU_LOCKUP).
constexpr Reason kEspReasons[] = {
  { 0, "UNKNOWN",        "Unknown or first boot"},
  { 1, "POWERON",        "Power-on reset"},
  { 2, "EXT",            "External reset"},
  { 3, "SW",             "Software reset"},
  { 4, "PANIC",          "Panic / exception reset"},
  { 5, "INT_WDT",        "Interrupt watchdog reset"},
  { 6, "TASK_WDT",       "Task watchdog reset"},
  { 7, "WDT",            "Other watchdog reset"},
  { 8, "DEEPSLEEP",      "Wake from deep sleep"},
  { 9, "BROWNOUT",       "Brownout (low voltage)"},
  {10, "SDIO",           "SDIO reset"},
  {11, "USB_PERIPHERAL", "Reset by USB peripheral (host opened the port)"},
  {12, "JTAG",           "Reset by JTAG"},
  {13, "EFUSE_ERROR",    "eFuse error"},
  {14, "PWR_GLITCH",     "Power glitch"},
  {15, "CPU_LOCKUP",     "CPU lockup"},
};
constexpr size_t kEspReasonCount = sizeof(kEspReasons) / sizeof(kEspReasons[0]);

// The ROM's own reset code, which the chip records even when the IDF layer
// cannot name it. Values differ BY CHIP, so each family gets its own table:
// on the S3 and C3, 21 is a USB-UART reset and 19/20/22/23 exist; on the
// classic ESP32 none of those do, and 14 means something else entirely.
//
// Both tables are always compiled, and the build picks one below. That keeps
// the table for the other family visible to the native tests, which would
// otherwise never see whichever one this target does not use.
constexpr Reason kRomReasonsEsp32[] = {
  { 1, "POWERON_RESET",          "Power-on"},
  { 3, "SW_RESET",               "Software reset of the digital core"},
  { 4, "OWDT_RESET",             "Legacy watchdog"},
  { 5, "DEEPSLEEP_RESET",        "Deep-sleep wake"},
  { 6, "SDIO_RESET",             "SDIO"},
  { 7, "TG0WDT_SYS_RESET",       "Timer group 0 watchdog"},
  { 8, "TG1WDT_SYS_RESET",       "Timer group 1 watchdog"},
  { 9, "RTCWDT_SYS_RESET",       "RTC watchdog"},
  {10, "INTRUSION_RESET",        "Intrusion detector"},
  {11, "TGWDT_CPU_RESET",        "Timer group watchdog, CPU"},
  {12, "SW_CPU_RESET",           "Software reset of the CPU"},
  {13, "RTCWDT_CPU_RESET",       "RTC watchdog, CPU"},
  {14, "EXT_CPU_RESET",          "Reset by the other CPU"},
  {15, "RTCWDT_BROWN_OUT_RESET", "Brownout"},
  {16, "RTCWDT_RTC_RESET",       "RTC watchdog, core and RTC"},
};
constexpr size_t kRomReasonsEsp32Count =
    sizeof(kRomReasonsEsp32) / sizeof(kRomReasonsEsp32[0]);

// S3, C3 and the rest of the newer families.
constexpr Reason kRomReasonsNewer[] = {
  { 1, "POWERON_RESET",          "Power-on"},
  { 3, "RTC_SW_SYS_RESET",       "Software reset of the digital core"},
  { 5, "DEEPSLEEP_RESET",        "Deep-sleep wake"},
  { 7, "TG0WDT_SYS_RESET",       "Timer group 0 watchdog"},
  { 8, "TG1WDT_SYS_RESET",       "Timer group 1 watchdog"},
  { 9, "RTCWDT_SYS_RESET",       "RTC watchdog"},
  {10, "INTRUSION_RESET",        "Intrusion detector"},
  {11, "TG0WDT_CPU_RESET",       "Timer group 0 watchdog, CPU"},
  {12, "RTC_SW_CPU_RESET",       "Software reset of the CPU"},
  {13, "RTCWDT_CPU_RESET",       "RTC watchdog, CPU"},
  {15, "RTCWDT_BROWN_OUT_RESET", "Brownout"},
  {16, "RTCWDT_RTC_RESET",       "RTC watchdog, core and RTC"},
  {17, "TG1WDT_CPU_RESET",       "Timer group 1 watchdog, CPU"},
  {18, "SUPER_WDT_RESET",        "Super watchdog"},
  {19, "GLITCH_RTC_RESET",       "Glitch detector"},
  {20, "EFUSE_RESET",            "eFuse error"},
  {21, "USB_UART_CHIP_RESET",    "USB-serial host reset (port opened)"},
  {22, "USB_JTAG_CHIP_RESET",    "USB-JTAG host reset"},
  {23, "POWER_GLITCH_RESET",     "Power glitch"},
};
constexpr size_t kRomReasonsNewerCount =
    sizeof(kRomReasonsNewer) / sizeof(kRomReasonsNewer[0]);

// The table this build decodes against.
#if defined(CONFIG_IDF_TARGET_ESP32)
constexpr const Reason* kRomReasons = kRomReasonsEsp32;
constexpr size_t kRomReasonCount = kRomReasonsEsp32Count;
#else
constexpr const Reason* kRomReasons = kRomReasonsNewer;
constexpr size_t kRomReasonCount = kRomReasonsNewerCount;
#endif

inline const Reason* findEsp(uint32_t code) {
  for (size_t i = 0; i < kEspReasonCount; i++) {
    if (kEspReasons[i].code == code) return &kEspReasons[i];
  }
  return nullptr;
}

inline const Reason* findRom(uint32_t code) {
  for (size_t i = 0; i < kRomReasonCount; i++) {
    if (kRomReasons[i].code == code) return &kRomReasons[i];
  }
  return nullptr;
}

// A code with no entry keeps its number rather than becoming "UNKNOWN": the
// number is the one fact there is, and a caller prints it alongside.
inline const char* espToken(uint32_t code) {
  const Reason* r = findEsp(code);
  return r ? r->token : "UNRECOGNIZED";
}

inline const char* espPhrase(uint32_t code) {
  const Reason* r = findEsp(code);
  return r ? r->phrase : "Unrecognized reset code";
}

inline const char* romToken(uint32_t code) {
  const Reason* r = findRom(code);
  return r ? r->token : nullptr;   // nullptr: say nothing rather than guess
}

// Whether the IDF layer failed to name the reset, i.e. whether the ROM code is
// worth printing. Code 0 is ESP_RST_UNKNOWN, which is what IDF 4.4 reports for
// a USB-host reset on the RC32 -- the gap this exists to close.
inline bool espReasonIsUnknown(uint32_t code) { return code == 0; }

// Write the full description into the caller's buffer. Nothing here returns a
// pointer to shared state, so two of these in one printf are independent.
//
// A number is never dropped: an esp code with no entry keeps its own number,
// and a ROM code with no entry is printed as a number rather than vanishing --
// an unrecognized code is still the one fact there is.
//
// `rom_code` is the chip's ROM reset code; pass `have_rom=false` on a build
// with no ROM table. It is used only when the esp code is UNKNOWN.
inline void formatToken(uint32_t code, uint32_t rom_code, bool have_rom,
                        char* out, size_t cap) {
  if (!out || cap == 0) return;
  const Reason* r = findEsp(code);
  if (r == nullptr) {
    snprintf(out, cap, "UNRECOGNIZED(%lu)", (unsigned long)code);
    return;
  }
  if (!espReasonIsUnknown(code) || !have_rom) {
    snprintf(out, cap, "%s", r->token);
    return;
  }
  const Reason* rom = findRom(rom_code);
  if (rom != nullptr) snprintf(out, cap, "%s(rom:%s)", r->token, rom->token);
  else snprintf(out, cap, "%s(rom:%lu)", r->token, (unsigned long)rom_code);
}

inline void formatPhrase(uint32_t code, uint32_t rom_code, bool have_rom,
                         char* out, size_t cap) {
  if (!out || cap == 0) return;
  const Reason* r = findEsp(code);
  if (r == nullptr) {
    snprintf(out, cap, "Unrecognized reset code (%lu)", (unsigned long)code);
    return;
  }
  if (!espReasonIsUnknown(code) || !have_rom) {
    snprintf(out, cap, "%s", r->phrase);
    return;
  }
  const Reason* rom = findRom(rom_code);
  if (rom != nullptr) snprintf(out, cap, "%s, ROM says %s", r->phrase, rom->token);
  else snprintf(out, cap, "%s, ROM code %lu", r->phrase, (unsigned long)rom_code);
}

}  // namespace reset
}  // namespace offband
