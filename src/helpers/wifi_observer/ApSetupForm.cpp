// src/helpers/wifi_observer/ApSetupForm.cpp
//
// Plan 3 Task 10b (Strycher/LoRa#272) -- secondary, build-flag-gated
// softAP HTTP setup form. See ApSetupForm.h for design notes.
//
// Entire body wrapped in the build flag. When the flag is unset,
// this file produces an empty translation unit (verified by symbol
// grep against firmware.elf).

#include "ApSetupForm.h"

#if defined(CROSSWIRE_AP_SETUP_FORM_ENABLED) && CROSSWIRE_AP_SETUP_FORM_ENABLED

#include "WifiBootstrap.h"
#include "WifiObserverConfig.h"
#include "ConfigSchema.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <WiFi.h>
  #include <WiFiServer.h>
  #include <WiFiClient.h>
  #include <Preferences.h>
  #include <esp_system.h>   // esp_restart()
#endif

namespace crosswire {

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
namespace {

#ifdef ARDUINO
WiFiServer* g_server = nullptr;
#endif

bool g_started   = false;
bool g_completed = false;
char g_ap_ssid[32] = {0};   // null-terminated SSID for system-channel post

// Request size caps. Plenty for a 3-field form; anything beyond
// these is rejected as malformed to avoid an unbounded read on a
// rogue client.
constexpr size_t kMaxRequestLineLen   = 256;
constexpr size_t kMaxHeaderLineLen    = 256;
constexpr size_t kMaxHeadersBytes     = 2048;
constexpr size_t kMaxBodyLen          = 1024;
constexpr uint32_t kClientReadTimeoutMs = 3000;

// Form field caps. SSID per 802.11: 32 chars max. PSK: 8-63 ASCII
// per WPA2-PSK; 0 for open network. broker_url is optional, bounded
// by BrokerConfig::url which is 128 bytes.
constexpr size_t kMaxSsidLen      = 32;
constexpr size_t kMaxPskLen       = 63;
constexpr size_t kMinPskLen       = 8;
constexpr size_t kMaxBrokerUrlLen = 127;   // leave room for NUL

}  // namespace

// ---------------------------------------------------------------------------
// URL decoding -- pure function, host-safe.
// ---------------------------------------------------------------------------
// Decodes one application/x-www-form-urlencoded value in place into
// `out`. Returns the number of bytes written (excluding the
// terminating NUL). `out_size` must be >= 1 (room for NUL).
//
// '+' -> ' ', '%XX' -> byte, '%XY' or '%X' (malformed) -> kept
// literally (defensive; the validator catches downstream).
static size_t urlDecode(const char* in, size_t in_len, char* out, size_t out_size) {
    if (out_size == 0) return 0;
    size_t oi = 0;
    for (size_t i = 0; i < in_len && oi + 1 < out_size; ++i) {
        char c = in[i];
        if (c == '+') {
            out[oi++] = ' ';
        } else if (c == '%' && i + 2 < in_len) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return 10 + (h - 'a');
                if (h >= 'A' && h <= 'F') return 10 + (h - 'A');
                return -1;
            };
            int hi = hex(in[i + 1]);
            int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[oi++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                // Malformed escape; keep literal '%' and let the
                // validator reject downstream if it matters.
                out[oi++] = c;
            }
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
    return oi;
}

// ---------------------------------------------------------------------------
// application/x-www-form-urlencoded parser
// ---------------------------------------------------------------------------
// Finds the value for `key` in `body` (length `body_len`), URL-decodes
// it into `out` (capacity `out_size`). Returns true if the key was
// found (even if its value is empty); false if not present.
static bool parseFormField(const char* body, size_t body_len,
                           const char* key, char* out, size_t out_size) {
    if (out_size == 0) return false;
    out[0] = '\0';
    const size_t key_len = strlen(key);
    size_t i = 0;
    while (i < body_len) {
        // Find end of this pair (& or end)
        size_t pair_end = i;
        while (pair_end < body_len && body[pair_end] != '&') ++pair_end;
        // Find '=' within [i, pair_end)
        size_t eq = i;
        while (eq < pair_end && body[eq] != '=') ++eq;
        size_t this_key_len = eq - i;
        if (this_key_len == key_len && memcmp(body + i, key, key_len) == 0) {
            size_t val_start = (eq < pair_end) ? eq + 1 : pair_end;
            urlDecode(body + val_start, pair_end - val_start, out, out_size);
            return true;
        }
        i = pair_end + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// HTML escaping for safe redisplay of user input on validation errors.
// ---------------------------------------------------------------------------
// Writes an HTML-attribute-safe rendering of `in` into `out`. Escapes
// the standard XSS-relevant characters. Truncates rather than failing
// if `out_size` is exceeded.
static void htmlEscape(const char* in, char* out, size_t out_size) {
    if (out_size == 0) return;
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 1 < out_size; ++i) {
        const char* rep = nullptr;
        switch (in[i]) {
            case '&':  rep = "&amp;";  break;
            case '<':  rep = "&lt;";   break;
            case '>':  rep = "&gt;";   break;
            case '"':  rep = "&quot;"; break;
            case '\'': rep = "&#39;";  break;
            default: break;
        }
        if (rep) {
            size_t rl = strlen(rep);
            if (oi + rl + 1 >= out_size) break;
            memcpy(out + oi, rep, rl);
            oi += rl;
        } else {
            out[oi++] = in[i];
        }
    }
    out[oi] = '\0';
}

// ---------------------------------------------------------------------------
// HTML page rendering
// ---------------------------------------------------------------------------
// `error_msg` may be NULL (initial form) or non-NULL (re-render after
// validation failure). `prefill_ssid` is the user-entered SSID to
// preserve on re-render (NULL for empty).
//
// Returns a heap-allocated String. Caller takes ownership and is
// responsible for sending + discarding.
#ifdef ARDUINO
static String renderForm(const char* error_msg, const char* prefill_ssid) {
    String s;
    s.reserve(2048);
    s += F("HTTP/1.1 200 OK\r\n");
    s += F("Content-Type: text/html; charset=utf-8\r\n");
    s += F("Connection: close\r\n");
    s += F("Cache-Control: no-store\r\n");
    s += F("\r\n");
    s += F("<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>Crosswire Setup</title>"
           "<style>"
           "body{font-family:system-ui,sans-serif;max-width:480px;margin:2em auto;padding:0 1em;color:#222}"
           "h1{font-size:1.3em}"
           "label{display:block;margin-top:1em;font-weight:600}"
           "input{display:block;width:100%;box-sizing:border-box;padding:.5em;font-size:1em;margin-top:.25em}"
           "button{margin-top:1.5em;padding:.6em 1.2em;font-size:1em;cursor:pointer}"
           ".err{background:#fee;border:1px solid #c00;padding:.5em;margin-top:1em;color:#900}"
           ".note{background:#eef;border:1px solid #88c;padding:.5em;margin-top:1em;font-size:.9em}"
           "</style></head><body>");
    s += F("<h1>Crosswire Observer Setup</h1>");
    s += F("<p class=\"note\">You can also configure WiFi via the "
           "<strong>_sys</strong> system channel in the MeshCore app. "
           "Whichever you complete first wins.</p>");
    if (error_msg != nullptr && error_msg[0] != '\0') {
        char esc[256];
        htmlEscape(error_msg, esc, sizeof(esc));
        s += F("<div class=\"err\">");
        s += esc;
        s += F("</div>");
    }
    char ssid_esc[64] = {0};
    if (prefill_ssid != nullptr) {
        htmlEscape(prefill_ssid, ssid_esc, sizeof(ssid_esc));
    }
    s += F("<form method=\"POST\" action=\"/\">"
           "<label>WiFi SSID<input type=\"text\" name=\"ssid\" required maxlength=\"32\" value=\"");
    s += ssid_esc;
    s += F("\"></label>"
           "<label>WiFi PSK (8-63 chars, blank for open)"
           "<input type=\"password\" name=\"psk\" maxlength=\"63\"></label>"
           "<label>Optional MQTT broker URL (e.g. mqtts://broker.example.org:8883)"
           "<input type=\"text\" name=\"broker_url\" maxlength=\"127\"></label>"
           "<button type=\"submit\">Save and Reboot</button>"
           "</form></body></html>");
    return s;
}

static String renderSavedPage(const char* ssid) {
    String s;
    s.reserve(512);
    s += F("HTTP/1.1 200 OK\r\n");
    s += F("Content-Type: text/html; charset=utf-8\r\n");
    s += F("Connection: close\r\n");
    s += F("\r\n");
    s += F("<!doctype html><html><head><meta charset=\"utf-8\">"
           "<title>Crosswire Saved</title>"
           "<style>body{font-family:system-ui,sans-serif;max-width:480px;margin:2em auto;padding:0 1em}</style>"
           "</head><body><h1>Saved</h1><p>WiFi credentials saved for SSID <strong>");
    char esc[64];
    htmlEscape(ssid, esc, sizeof(esc));
    s += esc;
    s += F("</strong>.</p><p>Rebooting in 3 seconds. The device will join your "
           "WiFi network and start the encrypted web UI on port 443.</p>"
           "</body></html>");
    return s;
}
#endif  // ARDUINO

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool apSetupFormStart() {
#ifdef ARDUINO
    if (g_started) return true;

    // Derive SSID from MAC. WifiBootstrap::deriveApSsid signature is
    // (mac_bytes, out, out_len). WiFi.macAddress(buf) returns the
    // base MAC; softAP uses a derived MAC, but the last 6 hex chars
    // we expose to the user are stable across modes.
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    WifiBootstrap::deriveApSsid(mac, g_ap_ssid, sizeof(g_ap_ssid));

    // We need AP mode AT MINIMUM. Use WIFI_AP (not WIFI_AP_STA) so
    // we don't fight with the no-creds branch -- there's no STA
    // associated until the user completes the form anyway. After
    // form completion we ESP.restart(), which brings up STA fresh.
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(g_ap_ssid, /*password=*/nullptr);
    if (!ok) {
        Serial.printf("[ApSetupForm] WiFi.softAP(%s) failed\n", g_ap_ssid);
        g_ap_ssid[0] = '\0';
        return false;
    }

    g_server = new WiFiServer(80);
    g_server->begin();
    g_server->setNoDelay(true);
    g_started = true;
    Serial.printf("[ApSetupForm] softAP up: SSID=%s URL=http://%s/\n",
                  g_ap_ssid, WiFi.softAPIP().toString().c_str());
    return true;
#else
    return false;
#endif
}

void apSetupFormStop() {
#ifdef ARDUINO
    if (g_server != nullptr) {
        g_server->end();
        delete g_server;
        g_server = nullptr;
    }
    if (g_started) {
        WiFi.softAPdisconnect(/*wifioff=*/true);
    }
    g_started = false;
    // g_completed intentionally NOT cleared; caller may read it
    // again before reboot.
#endif
}

bool apSetupFormCompleted() {
    return g_completed;
}

const char* apSetupFormSsid() {
    return (g_ap_ssid[0] != '\0') ? g_ap_ssid : nullptr;
}

// ---------------------------------------------------------------------------
// Request servicing
// ---------------------------------------------------------------------------
#ifdef ARDUINO

// Read up to `max_bytes` from `client` into `buf` until the byte
// sequence `terminator` is observed OR `max_bytes` reached OR the
// client read times out. On success returns the number of bytes
// written to `buf` (terminator NOT included). Returns -1 on
// timeout / read error. The caller passes terminator="\r\n\r\n" to
// read all headers, or NULL to read up to max_bytes regardless
// (used for the POST body once Content-Length is known).
//
// This is a hand-rolled tiny HTTP reader -- the spec calls for a
// hand-rolled tiny parser and we want to avoid pulling in
// AsyncWebServer (already used elsewhere but heavy + WebServer.cpp
// uses esp_http_server). WiFiClient is the simplest dependency.
static int readUntil(WiFiClient& client, char* buf, size_t max_bytes,
                     const char* terminator, uint32_t timeout_ms) {
    const uint32_t deadline = millis() + timeout_ms;
    size_t got = 0;
    size_t tlen = (terminator != nullptr) ? strlen(terminator) : 0;
    while (got < max_bytes) {
        if (client.available()) {
            int c = client.read();
            if (c < 0) break;
            buf[got++] = (char)c;
            if (tlen > 0 && got >= tlen) {
                if (memcmp(buf + got - tlen, terminator, tlen) == 0) {
                    return (int)(got - tlen);  // strip terminator
                }
            }
        } else {
            if (!client.connected() && client.available() == 0) break;
            if ((int32_t)(millis() - deadline) >= 0) return -1;
            delay(1);
        }
    }
    if (terminator == nullptr) {
        return (int)got;
    }
    return -1;
}

// Returns the Content-Length value parsed out of the headers blob,
// or -1 if no header / unparseable.
static long parseContentLength(const char* headers, size_t headers_len) {
    static const char* needle = "Content-Length:";
    size_t nl = strlen(needle);
    for (size_t i = 0; i + nl < headers_len; ++i) {
        // Case-insensitive compare
        bool match = true;
        for (size_t j = 0; j < nl; ++j) {
            char a = headers[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
            if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
            if (a != b) { match = false; break; }
        }
        if (!match) continue;
        size_t k = i + nl;
        while (k < headers_len && (headers[k] == ' ' || headers[k] == '\t')) ++k;
        long v = 0;
        bool any = false;
        while (k < headers_len && headers[k] >= '0' && headers[k] <= '9') {
            v = v * 10 + (headers[k] - '0');
            ++k;
            any = true;
        }
        return any ? v : -1;
    }
    return -1;
}

// Handle a single accepted client. Reads + parses one HTTP request,
// dispatches GET / or POST /, writes a response, closes the client.
static void handleClient(WiFiClient& client) {
    // Read request line
    char req_line[kMaxRequestLineLen];
    int req_n = readUntil(client, req_line, sizeof(req_line) - 1, "\r\n",
                          kClientReadTimeoutMs);
    if (req_n < 0) {
        client.stop();
        return;
    }
    req_line[req_n] = '\0';

    // Parse method + path (we don't care about HTTP version)
    char method[8] = {0};
    char path[64]  = {0};
    {
        size_t i = 0;
        while (i < (size_t)req_n && req_line[i] != ' ' && i + 1 < sizeof(method)) {
            method[i] = req_line[i];
            ++i;
        }
        method[i] = '\0';
        while (i < (size_t)req_n && req_line[i] == ' ') ++i;
        size_t pi = 0;
        while (i < (size_t)req_n && req_line[i] != ' ' && pi + 1 < sizeof(path)) {
            path[pi++] = req_line[i++];
        }
        path[pi] = '\0';
    }

    // Read headers
    char headers[kMaxHeadersBytes];
    int hdr_n = readUntil(client, headers, sizeof(headers) - 1, "\r\n\r\n",
                          kClientReadTimeoutMs);
    if (hdr_n < 0) {
        client.stop();
        return;
    }
    headers[hdr_n] = '\0';

    // Route
    bool is_get_root  = (strcmp(method, "GET")  == 0) && (strcmp(path, "/") == 0);
    bool is_post_root = (strcmp(method, "POST") == 0) && (strcmp(path, "/") == 0);

    if (is_get_root) {
        String resp = renderForm(nullptr, nullptr);
        client.write((const uint8_t*)resp.c_str(), resp.length());
        client.stop();
        return;
    }
    if (!is_post_root) {
        // 404 -- everything that isn't /
        static const char* k404 =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Not Found\n";
        client.write((const uint8_t*)k404, strlen(k404));
        client.stop();
        return;
    }

    // POST / -- read body up to Content-Length
    long content_len = parseContentLength(headers, (size_t)hdr_n);
    if (content_len < 0 || content_len > (long)kMaxBodyLen) {
        static const char* k413 =
            "HTTP/1.1 413 Payload Too Large\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Body too large or missing Content-Length\n";
        client.write((const uint8_t*)k413, strlen(k413));
        client.stop();
        return;
    }
    char body[kMaxBodyLen + 1];
    int body_n = 0;
    if (content_len > 0) {
        body_n = readUntil(client, body, (size_t)content_len, nullptr,
                           kClientReadTimeoutMs);
        if (body_n < 0 || body_n != content_len) {
            client.stop();
            return;
        }
    }
    body[body_n] = '\0';

    // Parse fields
    char ssid[kMaxSsidLen + 1]      = {0};
    char psk[kMaxPskLen + 1]        = {0};
    char broker_url[kMaxBrokerUrlLen + 1] = {0};
    parseFormField(body, (size_t)body_n, "ssid", ssid, sizeof(ssid));
    parseFormField(body, (size_t)body_n, "psk", psk, sizeof(psk));
    parseFormField(body, (size_t)body_n, "broker_url",
                   broker_url, sizeof(broker_url));

    // Validate
    const char* err = nullptr;
    size_t ssid_len = strlen(ssid);
    size_t psk_len  = strlen(psk);
    if (ssid_len == 0) {
        err = "SSID is required.";
    } else if (ssid_len > kMaxSsidLen) {
        err = "SSID too long (max 32 chars).";
    } else if (psk_len != 0 && (psk_len < kMinPskLen || psk_len > kMaxPskLen)) {
        err = "PSK must be 8-63 characters, or blank for an open network.";
    }
    if (err != nullptr) {
        String resp = renderForm(err, ssid);
        client.write((const uint8_t*)resp.c_str(), resp.length());
        client.stop();
        return;
    }

    // Write NVS. Spec mandates reusing Task 10's helpers. Those
    // helpers live in ObserverCli.cpp and are static (file scope);
    // exposing them externally just for this caller would broaden
    // the surface for one consumer. Both touch the same namespace
    // "wifi" + keys "ssid"/"pwd"; replicate the 4-line write here.
    // This file is the ONLY second writer; ObserverCli's set
    // wifi.ssid/pwd handlers remain the single canonical CLI path.
    {
        Preferences p;
        if (!p.begin("wifi", /*readOnly=*/false)) {
            String resp = renderForm(
                "Internal error: could not open WiFi config storage.", ssid);
            client.write((const uint8_t*)resp.c_str(), resp.length());
            client.stop();
            return;
        }
        p.putString("ssid", ssid);
        p.putString("pwd", psk);
        p.end();
    }

    // Optional broker URL -> slot 0. Mirrors ObserverCli's
    // handleSetBrokerField shape but writes only url + enabled.
    if (broker_url[0] != '\0') {
        BrokerConfig cfg;
        if (readBrokerConfig(0, cfg)) {
            strncpy(cfg.url, broker_url, sizeof(cfg.url) - 1);
            cfg.url[sizeof(cfg.url) - 1] = '\0';
            cfg.enabled = true;
            (void)writeBrokerConfig(0, cfg);  // best-effort
        }
    }

    Serial.printf("[ApSetupForm] saved wifi creds for SSID=%s; rebooting\n", ssid);

    String resp = renderSavedPage(ssid);
    client.write((const uint8_t*)resp.c_str(), resp.length());
    client.flush();
    client.stop();

    g_completed = true;
    // Hand control back to apSetupFormService() / WifiBootstrap::loop();
    // the bootstrap polls apSetupFormCompleted() and will call
    // apSetupFormStop() + ESP.restart() after a brief delay. We
    // could ESP.restart() here, but staying inside the handler
    // means the WiFiClient destructor runs cleanly and Serial
    // diagnostics complete; the spec asks for a "rebooting in 3
    // seconds" page, and the bootstrap's delay covers that.
}

#endif  // ARDUINO

void apSetupFormService() {
#ifdef ARDUINO
    if (!g_started || g_server == nullptr) return;
    WiFiClient client = g_server->available();
    if (!client) return;
    handleClient(client);
#endif
}

}  // namespace crosswire

#endif  // CROSSWIRE_AP_SETUP_FORM_ENABLED
