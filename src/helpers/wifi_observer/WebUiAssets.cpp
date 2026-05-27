// src/helpers/wifi_observer/WebUiAssets.cpp
//
// PROGMEM-resident SPA bundle (Plan 3 Task 9). Replaces the Task 7
// placeholders with the real one-file HTML + one-file CSS + one-file JS
// application that drives the 15 REST endpoints from Task 8.
//
// Architecture: vanilla JS, no framework, no build chain. hashchange
// router, fetch() helpers, periodic Status refresh, toast-style errors.
// Dark theme default + prefers-color-scheme light variant. No external
// CDN / font / script references -- the device is the only origin.
//
// Embedding: each asset lives in a single C++ raw-string literal
// (R"=====(...)====="). The terminator sequence )===== must NOT appear
// in the body content -- pre-commit grep confirms it does not.
//
// Strycher/LoRa#272 (Plan 3 Task 9).

#include "WebUiAssets.h"

namespace crosswire {
namespace WebUiAssets {

// NOTE: PROGMEM placement is a no-op on ESP32 (single linear address
// space; rodata is already in flash), but we keep the attribute for
// signal -- it documents intent and matches how the Task 7 placeholders
// declared the symbols.
//
// Linker dead-strip note: when WebServer is not yet wired into main.cpp
// (the current state until Plan 3 Task 11), --gc-sections eliminates
// the entire web-server call chain, and these assets along with it --
// nothing references them post-DCE. GCC 8.4's __attribute__((used))
// only protects against the COMPILER dropping, not the linker, and
// __attribute__((retain)) (which does the latter) requires GCC 11+.
// The bytes are correctly compiled into WebUiAssets.cpp.o (see
// xtensa-esp32s3-elf-size -A on that object); they will land in
// firmware.bin once Task 11 wires webServerStart into the WifiObserver
// lifecycle. Build SIZE reports during Plan 3 therefore understate the
// SPA cost until Task 11; the per-object rodata sizes are the truth.

// ===========================================================================
// index.html -- SPA shell + login overlay + change-password overlay
// ===========================================================================
const char index_html[] PROGMEM = R"=====(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Crosswire Observer</title>
<link rel="stylesheet" href="/app.css">
</head><body>

<div id="toast-container" aria-live="polite"></div>

<div id="login-overlay" class="overlay">
  <form id="login-form" class="card">
    <h2>Crosswire Observer</h2>
    <p class="muted">Sign in to continue.</p>
    <label>Password
      <input type="password" id="login-password" autocomplete="current-password" required>
    </label>
    <button type="submit">Sign in</button>
    <p class="muted small" id="login-hint">
      Initial password is printed in serial logs at boot (look for "Initial password").
    </p>
  </form>
</div>

<div id="pwchange-overlay" class="overlay hidden">
  <form id="pwchange-form" class="card">
    <h2>Change password</h2>
    <p class="muted">
      You must set a new password before continuing. Minimum 8 characters.
      Must differ from the derived initial password.
    </p>
    <label>New password
      <input type="password" id="pw-new" autocomplete="new-password" minlength="8" required>
    </label>
    <label>Confirm new password
      <input type="password" id="pw-confirm" autocomplete="new-password" minlength="8" required>
    </label>
    <button type="submit">Set password</button>
  </form>
</div>

<div id="app" class="hidden">
  <header>
    <div class="brand">
      <strong>Crosswire</strong>
      <span class="muted small" id="hdr-identity">loading...</span>
    </div>
    <nav id="nav">
      <a href="#status">Status</a>
      <a href="#brokers">Brokers</a>
      <a href="#wifi">WiFi</a>
      <a href="#ble" id="nav-ble" class="hidden">BLE</a>
      <a href="#cli">CLI</a>
      <a href="#security">Security</a>
      <a href="#advanced">Advanced</a>
      <a href="#" id="nav-logout" class="logout">Sign out</a>
    </nav>
  </header>

  <main>
    <section id="page-status" class="page">
      <h1>Status</h1>
      <div id="status-grid" class="grid">Loading...</div>
      <p class="muted small">Refreshes every 5 seconds while this page is visible.</p>
    </section>

    <section id="page-brokers" class="page hidden">
      <h1>MQTT Brokers</h1>
      <p class="muted">Per-slot broker configuration. Passwords and JWT tokens
        show <code>***</code> when set; leave as <code>***</code> to keep the
        existing secret.</p>
      <div id="brokers-list">Loading...</div>
    </section>

    <section id="page-wifi" class="page hidden">
      <h1>WiFi</h1>
      <div id="wifi-current" class="card">Loading...</div>
      <h2>Connect to a network</h2>
      <form id="wifi-form" class="card">
        <label>SSID
          <input type="text" id="wifi-ssid" autocomplete="off" required>
        </label>
        <label>Passphrase (leave blank for open networks)
          <input type="password" id="wifi-psk" autocomplete="off">
        </label>
        <button type="submit">Save and reboot</button>
      </form>
      <h2>Scan</h2>
      <div id="wifi-scan-controls">
        <button type="button" id="wifi-scan-btn">Rescan</button>
        <span class="muted small" id="wifi-scan-status"></span>
      </div>
      <div id="wifi-scan-list"></div>
    </section>

    <section id="page-ble" class="page hidden">
      <h1>BLE Companion</h1>
      <div id="ble-info" class="card">Loading...</div>
      <form id="ble-form" class="card">
        <p>Resetting the PIN generates a new random 6-digit code. The
          device must reboot before the new PIN takes effect.</p>
        <button type="submit" class="danger">Reset PIN</button>
      </form>
    </section>

    <section id="page-cli" class="page hidden">
      <h1>CLI passthrough</h1>
      <p class="muted">Runs an allowlisted Console command. Type a command
        and press Enter or click Run.</p>
      <form id="cli-form">
        <input type="text" id="cli-line" autocomplete="off" spellcheck="false"
               placeholder="e.g. ver">
        <button type="submit">Run</button>
      </form>
      <pre id="cli-output" class="output"></pre>
    </section>

    <section id="page-security" class="page hidden">
      <h1>Security</h1>
      <div class="card">
        <h2>Change password</h2>
        <form id="sec-pw-form">
          <label>New password
            <input type="password" id="sec-pw-new" autocomplete="new-password" minlength="8" required>
          </label>
          <label>Confirm new password
            <input type="password" id="sec-pw-confirm" autocomplete="new-password" minlength="8" required>
          </label>
          <button type="submit">Change password</button>
        </form>
      </div>
      <div class="card">
        <h2>Regenerate TLS certificate</h2>
        <p>Generates a new self-signed EC P-256 certificate and reboots
          the device. Your browser will show a new certificate warning on
          next connect (different fingerprint).</p>
        <button type="button" id="sec-regen-btn" class="danger">Regenerate certificate</button>
      </div>
    </section>

    <section id="page-advanced" class="page hidden">
      <h1>Advanced</h1>
      <div class="card">
        <h2>OTA firmware upload</h2>
        <p class="muted">Not implemented in this firmware. Reserved for a
          later release (Plan 4).</p>
        <button type="button" id="adv-ota-btn" disabled>Upload firmware</button>
      </div>
      <div class="card danger-card">
        <h2>Factory reset</h2>
        <p>Erases all observer NVS namespaces, the saved WiFi credentials,
          the broker configurations, and the TLS certificate. The device
          reboots and comes back up with a derived initial password.</p>
        <button type="button" id="adv-reset-btn" class="danger">Factory reset</button>
      </div>
    </section>
  </main>
</div>

<script src="/app.js"></script>
</body></html>
)=====";
const size_t index_html_len = sizeof(index_html) - 1;
const char  index_html_content_type[] = "text/html; charset=utf-8";

// ===========================================================================
// app.css -- dark default + light variant via prefers-color-scheme.
// CSS Grid layout; mobile-responsive via media queries.
// ===========================================================================
const char app_css[] PROGMEM = R"=====(:root {
  --bg: #0f1115;
  --bg-elev: #181b22;
  --bg-card: #1f232c;
  --fg: #e6e8ec;
  --fg-muted: #9aa0a8;
  --accent: #4a9eff;
  --accent-fg: #ffffff;
  --danger: #e35d5d;
  --danger-bg: #3a1f22;
  --border: #2a2f3a;
  --ok: #5dd39e;
  --warn: #e7b557;
  --shadow: 0 6px 24px rgba(0,0,0,0.45);
  --radius: 8px;
  --gap: 1rem;
  --maxw: 960px;
}
@media (prefers-color-scheme: light) {
  :root {
    --bg: #f3f4f7;
    --bg-elev: #ffffff;
    --bg-card: #ffffff;
    --fg: #1c1f24;
    --fg-muted: #5a6068;
    --accent: #2563d6;
    --accent-fg: #ffffff;
    --danger: #c0392b;
    --danger-bg: #fbeae8;
    --border: #d8dbe0;
    --shadow: 0 4px 16px rgba(0,0,0,0.10);
  }
}
* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; }
body {
  font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif;
  background: var(--bg);
  color: var(--fg);
  line-height: 1.45;
  min-height: 100vh;
}
.hidden { display: none !important; }
.muted { color: var(--fg-muted); }
.small { font-size: 0.85rem; }
code, pre {
  font-family: ui-monospace, SFMono-Regular, Consolas, Menlo, monospace;
  font-size: 0.9em;
}
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
button {
  background: var(--accent);
  color: var(--accent-fg);
  border: none;
  border-radius: var(--radius);
  padding: 0.55rem 1rem;
  font-size: 0.95rem;
  cursor: pointer;
  font-weight: 600;
}
button:hover:not(:disabled) { filter: brightness(1.1); }
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.danger {
  background: var(--danger);
  color: #fff;
}
input[type=text], input[type=password] {
  width: 100%;
  padding: 0.55rem 0.7rem;
  background: var(--bg);
  color: var(--fg);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  font-size: 0.95rem;
  font-family: inherit;
}
input:focus { outline: 2px solid var(--accent); outline-offset: 1px; }
label {
  display: block;
  margin: 0.6rem 0 0.4rem;
  font-size: 0.9rem;
  color: var(--fg-muted);
}
label input { margin-top: 0.3rem; }

/* Overlays (login + forced pw change) */
.overlay {
  position: fixed;
  inset: 0;
  background: var(--bg);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 1rem;
  z-index: 100;
}
.overlay .card {
  width: 100%;
  max-width: 380px;
}

/* Card surface */
.card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 1.2rem 1.3rem;
  margin: 0 0 1rem;
  box-shadow: var(--shadow);
}
.card h2 { margin-top: 0; }
.danger-card { border-color: var(--danger); }

/* App shell */
header {
  background: var(--bg-elev);
  border-bottom: 1px solid var(--border);
  padding: 0.6rem 1rem;
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 0.5rem 1.5rem;
}
.brand { display: flex; flex-direction: column; line-height: 1.2; }
.brand strong { font-size: 1.05rem; }
nav#nav {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem 1rem;
  margin-left: auto;
  align-items: center;
}
nav#nav a {
  color: var(--fg-muted);
  padding: 0.3rem 0.5rem;
  border-radius: 6px;
  font-size: 0.95rem;
}
nav#nav a:hover { background: var(--bg-card); text-decoration: none; }
nav#nav a.active { color: var(--fg); background: var(--bg-card); }
nav#nav a.logout { color: var(--danger); }
main {
  max-width: var(--maxw);
  margin: 0 auto;
  padding: 1.5rem 1rem 3rem;
}
.page h1 { margin-top: 0; }

/* Status grid */
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
  gap: 0.7rem;
}
.grid .tile {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 0.7rem 0.9rem;
}
.grid .tile .k {
  display: block;
  font-size: 0.75rem;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: var(--fg-muted);
  margin-bottom: 0.2rem;
}
.grid .tile .v {
  font-size: 1.05rem;
  font-weight: 600;
  word-break: break-word;
}
.grid .tile .v.mono { font-family: ui-monospace, monospace; font-size: 0.95rem; }
.dot-ok { color: var(--ok); }
.dot-warn { color: var(--warn); }
.dot-bad { color: var(--danger); }

/* WiFi scan list */
#wifi-scan-list {
  display: grid;
  grid-template-columns: 1fr;
  gap: 0.4rem;
  margin-top: 0.6rem;
}
.scan-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.5rem 0.8rem;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  cursor: pointer;
  font-size: 0.95rem;
}
.scan-row:hover { border-color: var(--accent); }
.scan-row .ssid { font-weight: 600; }
.scan-row .meta { color: var(--fg-muted); font-size: 0.85rem; }
#wifi-scan-controls { display: flex; gap: 0.7rem; align-items: center; }

/* CLI */
#cli-form { display: flex; gap: 0.5rem; }
#cli-form input { flex: 1; }
.output {
  background: var(--bg-elev);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 0.8rem 1rem;
  max-height: 50vh;
  overflow: auto;
  white-space: pre-wrap;
  margin-top: 0.7rem;
}

/* Brokers list */
.broker {
  margin-bottom: 1.2rem;
}
.broker summary {
  cursor: pointer;
  padding: 0.6rem 0.9rem;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  font-weight: 600;
  list-style: none;
  display: flex;
  align-items: center;
  gap: 0.6rem;
}
.broker summary::-webkit-details-marker { display: none; }
.broker summary .badge {
  font-size: 0.75rem;
  padding: 0.1rem 0.45rem;
  border-radius: 4px;
  background: var(--bg);
  color: var(--fg-muted);
  border: 1px solid var(--border);
}
.broker summary .badge.on { color: var(--ok); border-color: var(--ok); }
.broker[open] summary { border-bottom-left-radius: 0; border-bottom-right-radius: 0; }
.broker .body {
  border: 1px solid var(--border);
  border-top: none;
  border-bottom-left-radius: var(--radius);
  border-bottom-right-radius: var(--radius);
  padding: 1rem 1.1rem;
  background: var(--bg-card);
}
.broker .row2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 0.5rem 1rem;
}
@media (max-width: 540px) {
  .broker .row2 { grid-template-columns: 1fr; }
}

/* Toasts */
#toast-container {
  position: fixed;
  top: 0.8rem;
  right: 0.8rem;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  z-index: 200;
  pointer-events: none;
}
.toast {
  pointer-events: auto;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-left: 4px solid var(--accent);
  border-radius: var(--radius);
  padding: 0.65rem 1rem;
  min-width: 220px;
  max-width: 360px;
  box-shadow: var(--shadow);
  cursor: pointer;
  font-size: 0.9rem;
  animation: slidein 0.18s ease-out;
}
.toast.err  { border-left-color: var(--danger); }
.toast.ok   { border-left-color: var(--ok); }
.toast.warn { border-left-color: var(--warn); }
@keyframes slidein {
  from { transform: translateX(20px); opacity: 0; }
  to   { transform: translateX(0); opacity: 1; }
}

@media (max-width: 540px) {
  header { padding: 0.5rem 0.7rem; }
  nav#nav { width: 100%; margin-left: 0; }
  main { padding: 1rem 0.7rem 2rem; }
}
)=====";
const size_t app_css_len = sizeof(app_css) - 1;
const char  app_css_content_type[] = "text/css; charset=utf-8";

// ===========================================================================
// app.js -- vanilla JS SPA driver.
// Pattern: fetch() helpers (jget/jpost) read cw_csrf cookie + send as
// X-Csrf-Token on POST; auto-handle 401 (re-show login), 423 (force pw
// change overlay), 503 on /api/status (retry message). Hashchange router
// dispatches to per-page render functions. Periodic status refresh runs
// only while the Status page is visible. Errors surface as auto-dismiss
// toasts (top-right, 5s).
// ===========================================================================
const char app_js[] PROGMEM = R"=====((function(){
'use strict';

// ---------- DOM helpers ----------
var $ = function(sel){ return document.querySelector(sel); };
var $$ = function(sel){ return Array.prototype.slice.call(document.querySelectorAll(sel)); };
function el(tag, attrs, children){
  var n = document.createElement(tag);
  if (attrs) for (var k in attrs){
    if (k === 'class') n.className = attrs[k];
    else if (k === 'text') n.textContent = attrs[k];
    else if (k === 'html') n.innerHTML = attrs[k];
    else if (k.indexOf('on') === 0) n.addEventListener(k.slice(2), attrs[k]);
    else n.setAttribute(k, attrs[k]);
  }
  if (children) for (var i=0;i<children.length;i++){
    var c = children[i];
    if (c == null) continue;
    n.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
  }
  return n;
}

// ---------- Toast ----------
function toast(msg, kind){
  var c = $('#toast-container');
  if (!c) return;
  var t = el('div', { class: 'toast ' + (kind || ''), text: msg });
  var dismiss = function(){ if (t.parentNode) t.parentNode.removeChild(t); };
  t.addEventListener('click', dismiss);
  c.appendChild(t);
  setTimeout(dismiss, 5000);
}

// ---------- Cookie + CSRF ----------
function getCookie(name){
  var parts = document.cookie ? document.cookie.split(';') : [];
  for (var i=0;i<parts.length;i++){
    var p = parts[i].replace(/^\s+/, '');
    if (p.indexOf(name + '=') === 0) return p.slice(name.length + 1);
  }
  return '';
}

// ---------- Fetch wrappers ----------
function handleStatus(res){
  if (res.status === 401){
    showLogin();
    throw new Error('not_authenticated');
  }
  if (res.status === 423){
    showPwChange();
    throw new Error('password_change_required');
  }
  return res;
}

function jget(path){
  return fetch(path, { credentials: 'same-origin', cache: 'no-store' })
    .then(handleStatus)
    .then(function(res){
      if (!res.ok) return res.json().catch(function(){ return { error: 'http_' + res.status }; })
        .then(function(j){ throw new Error(j.error || ('http_' + res.status)); });
      return res.json();
    });
}

function jpost(path, body){
  var csrf = getCookie('cw_csrf');
  return fetch(path, {
    method: 'POST',
    credentials: 'same-origin',
    cache: 'no-store',
    headers: {
      'Content-Type': 'application/json',
      'X-Csrf-Token': csrf
    },
    body: body == null ? '{}' : JSON.stringify(body)
  }).then(handleStatus).then(function(res){
    return res.json().catch(function(){ return {}; }).then(function(j){
      if (!res.ok){
        var msg = j.error || ('http_' + res.status);
        if (j.required) msg += ' (' + j.required + ')';
        var e = new Error(msg);
        e.status = res.status;
        e.body = j;
        throw e;
      }
      return j;
    });
  });
}

// ---------- Overlay control ----------
function showLogin(){
  $('#login-overlay').classList.remove('hidden');
  $('#pwchange-overlay').classList.add('hidden');
  $('#app').classList.add('hidden');
  stopStatusTimer();
  var pw = $('#login-password'); if (pw) pw.value = '';
}
function hideLogin(){
  $('#login-overlay').classList.add('hidden');
  $('#app').classList.remove('hidden');
}
function showPwChange(){
  $('#pwchange-overlay').classList.remove('hidden');
  $('#login-overlay').classList.add('hidden');
  $('#app').classList.add('hidden');
  stopStatusTimer();
}
function hidePwChange(){
  $('#pwchange-overlay').classList.add('hidden');
  $('#app').classList.remove('hidden');
}

// ---------- Boot ----------
function boot(){
  jget('/api/status').then(function(){
    hideLogin();
    hidePwChange();
    detectBle();
    route();
  }).catch(function(err){
    if (err.message === 'not_authenticated') return;
    if (err.message === 'password_change_required') return;
    // 503 status_not_ready: try once more after 2s, otherwise show app
    if (String(err.message).indexOf('status_not_ready') >= 0){
      toast('Status not ready yet, retrying...', 'warn');
      setTimeout(boot, 2000);
      return;
    }
    toast('Failed to load: ' + err.message, 'err');
    // Most likely we lack a session cookie; show login.
    showLogin();
  });
}

// ---------- Router ----------
var PAGES = ['status','brokers','wifi','ble','cli','security','advanced'];
function route(){
  var hash = (location.hash || '#status').slice(1);
  if (PAGES.indexOf(hash) < 0) hash = 'status';
  $$('.page').forEach(function(p){ p.classList.add('hidden'); });
  var sec = $('#page-' + hash);
  if (sec) sec.classList.remove('hidden');
  $$('#nav a').forEach(function(a){
    a.classList.toggle('active', a.getAttribute('href') === '#' + hash);
  });
  if (hash === 'status')   renderStatus();
  if (hash === 'brokers')  renderBrokers();
  if (hash === 'wifi')     renderWifi();
  if (hash === 'ble')      renderBle();
  if (hash === 'cli')      focusCli();
  if (hash === 'security') resetSecurityForms();
  if (hash !== 'status') stopStatusTimer();
}
window.addEventListener('hashchange', route);

// ---------- BLE feature detect ----------
// /api/ble is only present in companion-observer builds. We probe once
// at boot; if it returns 200 we reveal the nav link, otherwise we hide it.
function detectBle(){
  fetch('/api/ble', { credentials: 'same-origin', cache: 'no-store' })
    .then(function(res){
      if (res.status === 200) $('#nav-ble').classList.remove('hidden');
      // 404 / other: stays hidden (default).
    }).catch(function(){ /* swallow */ });
}

// ---------- Status page ----------
var statusTimer = null;
function startStatusTimer(){
  stopStatusTimer();
  statusTimer = setInterval(renderStatus, 5000);
}
function stopStatusTimer(){
  if (statusTimer) { clearInterval(statusTimer); statusTimer = null; }
}
function tile(k, v, kls){
  return el('div', { class: 'tile' }, [
    el('span', { class: 'k', text: k }),
    el('span', { class: 'v ' + (kls || ''), text: (v == null ? '-' : String(v)) })
  ]);
}
function fmtUptime(secs){
  secs = Number(secs) || 0;
  var d = Math.floor(secs / 86400); secs -= d * 86400;
  var h = Math.floor(secs / 3600);  secs -= h * 3600;
  var m = Math.floor(secs / 60);    secs -= m * 60;
  var parts = [];
  if (d) parts.push(d + 'd');
  if (h || d) parts.push(h + 'h');
  if (m || h || d) parts.push(m + 'm');
  parts.push(Math.floor(secs) + 's');
  return parts.join(' ');
}
function renderStatus(){
  jget('/api/status').then(function(s){
    var grid = $('#status-grid');
    grid.innerHTML = '';
    var st = s.stats || {};
    grid.appendChild(tile('Status', s.status,
      s.status === 'online' ? 'dot-ok' : 'dot-warn'));
    grid.appendChild(tile('Origin', s.origin || s.origin_id || '-'));
    grid.appendChild(tile('Model', s.model || '-'));
    grid.appendChild(tile('Firmware', s.firmware_version || '-'));
    grid.appendChild(tile('Battery (mV)', st.battery_mv));
    grid.appendChild(tile('Uptime', fmtUptime(st.uptime_secs)));
    grid.appendChild(tile('Errors', st.errors));
    grid.appendChild(tile('Queue len', st.queue_len));
    grid.appendChild(tile('Noise floor', st.noise_floor));
    grid.appendChild(tile('TX air (s)', st.tx_air_secs));
    grid.appendChild(tile('RX air (s)', st.rx_air_secs));
    grid.appendChild(tile('Recv errors', st.recv_errors));
    grid.appendChild(tile('Radio', s.radio || '-', 'mono'));
    grid.appendChild(tile('Repeat', s.repeat || '-'));
    grid.appendChild(tile('Client', s.client_version || '-'));
    grid.appendChild(tile('Timestamp', s.timestamp || '-', 'mono'));
    // Header identity
    var hdr = $('#hdr-identity');
    if (hdr) hdr.textContent = (s.origin || s.origin_id || '') +
      (s.firmware_version ? ' / ' + s.firmware_version : '');
    startStatusTimer();
  }).catch(function(err){
    if (String(err.message).indexOf('status_not_ready') >= 0){
      $('#status-grid').textContent = 'Status not ready yet. Retrying...';
      stopStatusTimer();
      setTimeout(renderStatus, 10000);
      return;
    }
    toast('Status fetch failed: ' + err.message, 'err');
  });
}

// ---------- Brokers page ----------
var TRANSPORT_NAMES = ['plain','tls','wss'];
var AUTH_NAMES = ['None','Basic','JWT'];
function renderBrokers(){
  var host = $('#brokers-list');
  host.textContent = 'Loading...';
  jget('/api/brokers').then(function(arr){
    host.innerHTML = '';
    if (!Array.isArray(arr) || arr.length === 0){
      host.appendChild(el('p', { class: 'muted', text: 'No brokers configured.' }));
      return;
    }
    arr.forEach(function(b){ host.appendChild(brokerCard(b)); });
  }).catch(function(err){
    host.textContent = '';
    toast('Brokers load failed: ' + err.message, 'err');
  });
}
function brokerCard(b){
  var details = el('details', { class: 'broker' });
  var sum = el('summary', null, [
    el('span', { text: 'Slot ' + b.slot }),
    el('span', { class: 'badge ' + (b.enabled ? 'on' : ''),
                 text: b.enabled ? 'enabled' : 'disabled' }),
    el('span', { class: 'muted small', text: b.url || '(no url)' })
  ]);
  details.appendChild(sum);
  var body = el('div', { class: 'body' });
  // Build form
  var form = el('form');
  function field(label, input){
    var l = el('label', { text: label });
    l.appendChild(input);
    return l;
  }
  var enabled = el('input', { type: 'checkbox' });
  enabled.checked = !!b.enabled;
  var enabledLabel = el('label', null, [enabled, document.createTextNode(' Enabled')]);
  form.appendChild(enabledLabel);

  var row1 = el('div', { class: 'row2' });
  var url = el('input', { type: 'text', value: b.url || '' });
  var port = el('input', { type: 'text', value: String(b.port || 1883) });
  row1.appendChild(field('URL', url));
  row1.appendChild(field('Port', port));
  form.appendChild(row1);

  var row2 = el('div', { class: 'row2' });
  var transport = el('select');
  TRANSPORT_NAMES.forEach(function(t, i){
    var o = el('option', { value: String(i), text: t });
    if (b.transport === i) o.selected = true;
    transport.appendChild(o);
  });
  var authType = el('select');
  AUTH_NAMES.forEach(function(t, i){
    var o = el('option', { value: String(i), text: t });
    if (b.auth_type === i) o.selected = true;
    authType.appendChild(o);
  });
  row2.appendChild(field('Transport', transport));
  row2.appendChild(field('Auth type', authType));
  form.appendChild(row2);

  var row3 = el('div', { class: 'row2' });
  var username = el('input', { type: 'text', value: b.username || '' });
  var password = el('input', { type: 'password', value: b.password || '' });
  row3.appendChild(field('Username', username));
  row3.appendChild(field('Password (leave *** to keep)', password));
  form.appendChild(row3);

  var row4 = el('div', { class: 'row2' });
  var jwt = el('input', { type: 'password', value: b.jwt_token || '' });
  var aud = el('input', { type: 'text', value: b.jwt_audience || '' });
  row4.appendChild(field('JWT token (leave *** to keep)', jwt));
  row4.appendChild(field('JWT audience', aud));
  form.appendChild(row4);

  var row5 = el('div', { class: 'row2' });
  var refresh = el('input', { type: 'text', value: String(b.jwt_refresh_sec || 0) });
  var ca = el('input', { type: 'text', value: b.ca_cert_name || '' });
  row5.appendChild(field('JWT refresh (sec)', refresh));
  row5.appendChild(field('CA cert name', ca));
  form.appendChild(row5);

  var row6 = el('div', { class: 'row2' });
  var prefix = el('input', { type: 'text', value: b.topic_prefix || '' });
  var iata = el('input', { type: 'text', value: b.iata_override || '' });
  row6.appendChild(field('Topic prefix', prefix));
  row6.appendChild(field('IATA override', iata));
  form.appendChild(row6);

  var saveBtn = el('button', { type: 'submit', text: 'Save slot ' + b.slot });
  form.appendChild(saveBtn);

  form.addEventListener('submit', function(e){
    e.preventDefault();
    var payload = {
      enabled: enabled.checked,
      url: url.value,
      port: parseInt(port.value, 10) || 0,
      transport: parseInt(transport.value, 10),
      auth_type: parseInt(authType.value, 10),
      username: username.value,
      password: password.value,
      jwt_token: jwt.value,
      jwt_audience: aud.value,
      jwt_refresh_sec: parseInt(refresh.value, 10) || 0,
      ca_cert_name: ca.value,
      topic_prefix: prefix.value,
      iata_override: iata.value
    };
    saveBtn.disabled = true;
    jpost('/api/brokers/' + b.slot, payload).then(function(r){
      toast('Slot ' + b.slot + ' saved' + (r.reloaded ? ' and reloaded' : ''), 'ok');
      renderBrokers();
    }).catch(function(err){
      toast('Save failed: ' + err.message, 'err');
      saveBtn.disabled = false;
    });
  });
  body.appendChild(form);
  details.appendChild(body);
  return details;
}

// ---------- WiFi page ----------
var scanPollTimer = null;
function renderWifi(){
  jget('/api/wifi').then(function(w){
    var host = $('#wifi-current');
    host.innerHTML = '';
    host.appendChild(el('p', null, [
      el('strong', { text: 'Saved SSID: ' }),
      document.createTextNode(w.saved_ssid || '(none)')
    ]));
    host.appendChild(el('p', null, [
      el('strong', { text: 'Connected: ' }),
      document.createTextNode(w.connected ? 'yes' : 'no')
    ]));
    if (w.connected){
      host.appendChild(el('p', null, [
        el('strong', { text: 'IP: ' }),
        document.createTextNode(w.ip || '-')
      ]));
      host.appendChild(el('p', null, [
        el('strong', { text: 'RSSI: ' }),
        document.createTextNode(String(w.rssi))
      ]));
    }
  }).catch(function(err){
    toast('WiFi load failed: ' + err.message, 'err');
  });
  pollScan();
}
function pollScan(){
  jget('/api/wifi/scan').then(function(s){
    var status = $('#wifi-scan-status');
    status.textContent = s.in_flight ? 'Scanning...' :
      (s.networks && s.networks.length ? (s.networks.length + ' found') : 'No results');
    var host = $('#wifi-scan-list');
    host.innerHTML = '';
    (s.networks || []).forEach(function(n){
      var row = el('div', { class: 'scan-row' }, [
        el('span', { class: 'ssid', text: n.ssid || '(hidden)' }),
        el('span', { class: 'meta',
          text: 'ch ' + n.channel + '  ' + n.rssi + ' dBm' +
                (n.encryption === 0 ? '  open' : '') })
      ]);
      row.addEventListener('click', function(){
        $('#wifi-ssid').value = n.ssid || '';
        $('#wifi-psk').focus();
      });
      host.appendChild(row);
    });
    if (s.in_flight){
      if (scanPollTimer) clearTimeout(scanPollTimer);
      scanPollTimer = setTimeout(pollScan, 2000);
    }
  }).catch(function(err){
    toast('Scan failed: ' + err.message, 'err');
  });
}

// ---------- BLE page ----------
function renderBle(){
  var host = $('#ble-info');
  host.textContent = 'Loading...';
  jget('/api/ble').then(function(b){
    host.innerHTML = '';
    host.appendChild(el('p', null, [
      el('strong', { text: 'Current PIN: ' }),
      el('code', { text: b.current_pin || '------' })
    ]));
    if (b.pin_overridden){
      host.appendChild(el('p', { class: 'muted small',
        text: 'PIN was overridden via web (takes effect after reboot).' }));
    }
    host.appendChild(el('p', null, [
      el('strong', { text: 'Public key: ' }),
      el('code', { text: b.pubkey_hex || '(unavailable)' })
    ]));
    host.appendChild(el('p', null, [
      el('strong', { text: 'Paired devices: ' }),
      document.createTextNode(b.paired_count < 0 ? 'unavailable' : String(b.paired_count))
    ]));
  }).catch(function(err){
    host.textContent = '';
    toast('BLE info failed: ' + err.message, 'err');
  });
}

// ---------- CLI page ----------
function focusCli(){
  var i = $('#cli-line');
  if (i) i.focus();
}

// ---------- Security helpers ----------
function resetSecurityForms(){
  $('#sec-pw-new').value = '';
  $('#sec-pw-confirm').value = '';
}

// ---------- Event wiring ----------
function wire(){
  // Login
  $('#login-form').addEventListener('submit', function(e){
    e.preventDefault();
    var pw = $('#login-password').value;
    if (!pw) return;
    jpost('/api/login', { password: pw }).then(function(r){
      $('#login-password').value = '';
      if (r.password_change_required){
        showPwChange();
      } else {
        hideLogin();
        detectBle();
        route();
        toast('Signed in', 'ok');
      }
    }).catch(function(err){
      if (err.status === 429) toast('Too many attempts. Wait a minute.', 'err');
      else if (err.status === 401) toast('Wrong password', 'err');
      else toast('Sign in failed: ' + err.message, 'err');
    });
  });

  // Forced password change overlay
  $('#pwchange-form').addEventListener('submit', function(e){
    e.preventDefault();
    var a = $('#pw-new').value;
    var b = $('#pw-confirm').value;
    if (a !== b){ toast('Passwords do not match', 'err'); return; }
    jpost('/api/change_password', { new_password: a }).then(function(){
      $('#pw-new').value = '';
      $('#pw-confirm').value = '';
      hidePwChange();
      detectBle();
      route();
      toast('Password changed', 'ok');
    }).catch(function(err){
      toast('Change failed: ' + err.message, 'err');
    });
  });

  // Security page password change (same endpoint, different form)
  $('#sec-pw-form').addEventListener('submit', function(e){
    e.preventDefault();
    var a = $('#sec-pw-new').value;
    var b = $('#sec-pw-confirm').value;
    if (a !== b){ toast('Passwords do not match', 'err'); return; }
    jpost('/api/change_password', { new_password: a }).then(function(){
      resetSecurityForms();
      toast('Password changed', 'ok');
    }).catch(function(err){
      toast('Change failed: ' + err.message, 'err');
    });
  });

  // Regenerate cert
  $('#sec-regen-btn').addEventListener('click', function(){
    if (!confirm('Regenerate TLS certificate and reboot? The browser will show a new certificate warning.')) return;
    jpost('/api/regenerate_cert', {}).then(function(r){
      toast('Cert regenerated. Rebooting in ' + (r.reboot_in_ms/1000) + 's...', 'warn');
    }).catch(function(err){
      toast('Regenerate failed: ' + err.message, 'err');
    });
  });

  // Factory reset
  $('#adv-reset-btn').addEventListener('click', function(){
    if (!confirm('Factory reset will erase WiFi, brokers, password, and TLS cert. Continue?')) return;
    if (!confirm('Are you absolutely sure? This cannot be undone.')) return;
    jpost('/api/factory_reset', {}).then(function(r){
      toast('Factory reset scheduled. Rebooting in ' + (r.reboot_in_ms/1000) + 's...', 'warn');
    }).catch(function(err){
      toast('Reset failed: ' + err.message, 'err');
    });
  });

  // OTA stub
  var ota = $('#adv-ota-btn');
  if (ota){
    ota.addEventListener('click', function(){
      toast('OTA upload is not implemented in this firmware.', 'warn');
    });
  }

  // WiFi
  $('#wifi-form').addEventListener('submit', function(e){
    e.preventDefault();
    var ssid = $('#wifi-ssid').value;
    var psk = $('#wifi-psk').value;
    if (!ssid) return;
    if (!confirm('Save WiFi credentials and reboot to apply?')) return;
    jpost('/api/wifi', { ssid: ssid, psk: psk }).then(function(r){
      toast('WiFi saved. Rebooting in ' + (r.reboot_in_ms/1000) + 's...', 'warn');
      $('#wifi-psk').value = '';
    }).catch(function(err){
      toast('Save failed: ' + err.message, 'err');
    });
  });
  $('#wifi-scan-btn').addEventListener('click', pollScan);

  // BLE reset PIN
  $('#ble-form').addEventListener('submit', function(e){
    e.preventDefault();
    if (!confirm('Reset BLE PIN to a new random value? Reboot required to apply.')) return;
    jpost('/api/ble/reset_pin', {}).then(function(r){
      toast('New PIN: ' + r.new_pin + ' (reboot to apply)', 'ok');
      renderBle();
    }).catch(function(err){
      toast('Reset failed: ' + err.message, 'err');
    });
  });

  // CLI
  $('#cli-form').addEventListener('submit', function(e){
    e.preventDefault();
    var line = $('#cli-line').value.trim();
    if (!line) return;
    var out = $('#cli-output');
    out.textContent += '> ' + line + '\n';
    jpost('/api/cli', { line: line }).then(function(r){
      out.textContent += '[' + r.result + '] ' + (r.response || '') + '\n\n';
      out.scrollTop = out.scrollHeight;
      $('#cli-line').value = '';
      $('#cli-line').focus();
    }).catch(function(err){
      out.textContent += '! ' + err.message + '\n\n';
      out.scrollTop = out.scrollHeight;
    });
  });

  // Logout
  $('#nav-logout').addEventListener('click', function(e){
    e.preventDefault();
    jpost('/api/logout', {}).then(function(){
      showLogin();
      toast('Signed out', 'ok');
    }).catch(function(){
      showLogin();
    });
  });
}

document.addEventListener('DOMContentLoaded', function(){
  wire();
  boot();
});

})();
)=====";
const size_t app_js_len = sizeof(app_js) - 1;
const char  app_js_content_type[] = "application/javascript; charset=utf-8";

}  // namespace WebUiAssets
}  // namespace crosswire
