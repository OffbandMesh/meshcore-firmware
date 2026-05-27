// src/helpers/wifi_observer/WebUiAssets.h
//
// PROGMEM-resident static assets for the web UI (login page, JS, CSS).
// Task 9 of Plan 3 produces the real bundled UI; this header is created
// in Task 7 with placeholder symbols so WebServer.cpp can register the
// `GET /`, `GET /app.js`, `GET /app.css` routes and link cleanly.
//
// Lifetime: PROGMEM is rodata in the flash image; pointers are valid
// for the lifetime of the process. The `_len` constants are exact byte
// counts (not including a trailing NUL).
//
// Strycher/LoRa#272 (Plan 3 Task 7 placeholders; Task 9 replaces).

#pragma once
#include <stddef.h>

#ifdef ARDUINO
  #include <pgmspace.h>
#else
  #define PROGMEM
#endif

namespace crosswire {
namespace WebUiAssets {

// Login + SPA shell. Served by GET /.
extern const char  index_html[]    PROGMEM;
extern const size_t index_html_len;
extern const char  index_html_content_type[];  // "text/html; charset=utf-8"

// SPA JS bundle. Served by GET /app.js.
extern const char  app_js[]        PROGMEM;
extern const size_t app_js_len;
extern const char  app_js_content_type[];      // "application/javascript; charset=utf-8"

// SPA CSS bundle. Served by GET /app.css.
extern const char  app_css[]       PROGMEM;
extern const size_t app_css_len;
extern const char  app_css_content_type[];     // "text/css; charset=utf-8"

}  // namespace WebUiAssets
}  // namespace crosswire
