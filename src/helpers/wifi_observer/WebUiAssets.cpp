// src/helpers/wifi_observer/WebUiAssets.cpp
//
// PLACEHOLDER bundle (Plan 3 Task 7). Task 9 replaces these byte arrays
// with the real built UI (HTML login page, SPA JS, SPA CSS). The
// placeholder content is intentionally minimal -- enough to confirm
// the route is wired and the browser receives the right Content-Type,
// nothing more.
//
// Strycher/LoRa#272.

#include "WebUiAssets.h"

namespace crosswire {
namespace WebUiAssets {

// NOTE: PROGMEM placement is a no-op on ESP32 (single linear address
// space; rodata is already in flash), but we keep the attribute for
// signal -- it documents intent and aligns with how Task 9 will declare
// the real bundles.

const char index_html[] PROGMEM =
    "<!doctype html>\n"
    "<html><head><meta charset=\"utf-8\">"
    "<title>Crosswire Observer (placeholder)</title>"
    "<link rel=\"stylesheet\" href=\"/app.css\">"
    "</head><body>"
    "<h1>Crosswire Observer</h1>"
    "<p>Task 9 of Plan 3 replaces this page with the real login UI.</p>"
    "<script src=\"/app.js\"></script>"
    "</body></html>\n";
const size_t index_html_len = sizeof(index_html) - 1;  // exclude trailing NUL
const char  index_html_content_type[] = "text/html; charset=utf-8";

const char app_js[] PROGMEM =
    "// Crosswire Observer SPA (placeholder; Task 9 replaces)\n"
    "console.log('Crosswire Observer placeholder JS loaded.');\n";
const size_t app_js_len = sizeof(app_js) - 1;
const char  app_js_content_type[] = "application/javascript; charset=utf-8";

const char app_css[] PROGMEM =
    "/* Crosswire Observer styles (placeholder; Task 9 replaces) */\n"
    "body { font-family: sans-serif; margin: 2rem; }\n";
const size_t app_css_len = sizeof(app_css) - 1;
const char  app_css_content_type[] = "text/css; charset=utf-8";

}  // namespace WebUiAssets
}  // namespace crosswire
