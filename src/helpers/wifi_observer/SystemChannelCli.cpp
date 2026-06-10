// src/helpers/wifi_observer/SystemChannelCli.cpp
//
// Plan 3 Task 10 (Strycher/LoRa#272). See SystemChannelCli.h for the
// channel-derivation + intercept + rate-limit contract.
//
// File layout:
//   1. Pure-logic functions (always compiled; host-testable):
//        deriveSystemChannelPsk
//        deriveSystemChannelName
//        systemChannelRateAllows
//      These depend ONLY on cstring + cstdint + the SHA-256 helper
//      that the host test stubs out.
//   2. I/O wrapper functions (#ifdef ARDUINO):
//        systemChannelInit, systemChannelInterceptMsg,
//        systemChannelAllowSet, systemChannelPostStatus,
//        systemChannelSetPostFunction, systemChannelDrain.
//      These touch BaseChatMesh (slot read/write) + Serial + the
//      MyMesh-registered post function.
//
// Why a post-callback queue instead of calling MyMesh directly:
//   systemChannelPostStatus() is invoked from WifiBootstrap and
//   other places that may run before BLE is ready and that should
//   not reach into MyMesh internals. A function-pointer registered
//   by MyMesh at startInterface() time, plus a tiny ring buffer
//   for messages that arrive before registration, keeps the
//   dependency direction one-way (this file knows nothing about
//   MyMesh) while still letting status posts survive boot ordering.

#include "SystemChannelCli.h"
#include "CliPassthrough.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Mesh.h>          // mesh::Identity, mesh::GroupChannel
  #include <Utils.h>         // mesh::Utils::sha256
  #include <Identity.h>      // PUB_KEY_SIZE
  #include <helpers/BaseChatMesh.h>
  #include <helpers/ChannelDetails.h>
#endif

namespace crosswire {

// ---------------------------------------------------------------------------
// Pure logic. Always compiled.
// ---------------------------------------------------------------------------

// Namespace literal: bound into the derivation so the same pubkey
// hashed for a different purpose (e.g., device_id hex display)
// can never accidentally produce the same PSK. The "-v1" suffix
// leaves room for a future PSK derivation change without breaking
// devices that already booted with the v1 derivation.
static const char kPskNamespace[] = "crosswire-sysch-v1";

// SHA-256 entry-point. On ARDUINO we use MeshCore's mesh::Utils
// (which the firmware already pulls in via BaseChatMesh, so no
// new code size). On the host test we provide a tiny pure-C
// implementation via a stub header (see scripts/
// test_observer_system_channel.py).
static void sysch_sha256(uint8_t* out_hash, size_t out_len,
                         const uint8_t* msg, size_t msg_len);

#ifdef ARDUINO
static void sysch_sha256(uint8_t* out_hash, size_t out_len,
                         const uint8_t* msg, size_t msg_len) {
    mesh::Utils::sha256(out_hash, out_len, msg, (int)msg_len);
}
#endif

void deriveSystemChannelPsk(const uint8_t pubkey[32], uint8_t out_psk[16]) {
    // Concat pubkey || namespace literal (without the trailing
    // NUL: 32 + 18 = 50 bytes) into a small stack buffer, then
    // SHA-256 the whole thing and take the first 16 bytes.
    // sha256(out, out_len, msg, msg_len) writes out_len bytes;
    // we ask for exactly 16 since that's what we keep.
    constexpr size_t kNsLen = sizeof(kPskNamespace) - 1;  // strip NUL
    uint8_t buf[32 + kNsLen];
    memcpy(buf, pubkey, 32);
    memcpy(buf + 32, kPskNamespace, kNsLen);
    sysch_sha256(out_psk, 16, buf, sizeof(buf));
}

void deriveSystemChannelName(const uint8_t pubkey[32], char* out_name,
                             size_t out_size) {
    // Format: "_sys @ XXXXXXXX" -- 7 char prefix + 8 hex chars +
    // NUL = 16 bytes. Channel name field in MeshCore is 32 bytes,
    // so this fits with room to spare. Lowercase hex by
    // convention (matches MeshCore's default node-name format and
    // the WebAuth derived-password derivation -- so we stay
    // consistent across surfaces).
    if (out_name == nullptr || out_size == 0) return;
    if (out_size < 16) {
        // Not enough room for the full label. Write a truncated
        // prefix-only or empty string rather than corrupting
        // memory; callers that need the full string must size
        // properly.
        out_name[0] = '\0';
        return;
    }
    static const char H[] = "0123456789abcdef";
    // snprintf-equivalent without dragging in the formatter for
    // such a small task. Fixed layout, fixed width.
    out_name[0] = '_'; out_name[1] = 's'; out_name[2] = 'y';
    out_name[3] = 's'; out_name[4] = ' '; out_name[5] = '@';
    out_name[6] = ' ';
    for (int i = 0; i < 4; ++i) {
        out_name[7 + i * 2]     = H[(pubkey[i] >> 4) & 0x0F];
        out_name[7 + i * 2 + 1] = H[pubkey[i] & 0x0F];
    }
    out_name[15] = '\0';
}

bool systemChannelRateAllows(uint32_t last_post_ms, uint32_t now_ms,
                             uint32_t min_interval_ms) {
    // First post is always allowed: a "last_post_ms == 0" sentinel
    // distinguishes "never posted" from "posted at boot=0", which
    // is a millisecond window we accept as the cost of a simple
    // initial-state convention. millis() rolls back to 0 only
    // every ~49 days; the worst case is one dropped status at the
    // moment of wrap. Caller responsibility to assign 0 only to
    // mean "never posted."
    if (last_post_ms == 0) return true;
    // Unsigned subtraction is wrap-around safe: (now - last) is
    // the elapsed window in milliseconds even across a 32-bit
    // overflow, as long as last_post_ms is updated within the
    // half-rollover window (~25 days), which it always is in
    // practice.
    uint32_t elapsed = now_ms - last_post_ms;
    return elapsed >= min_interval_ms;
}

// ---------------------------------------------------------------------------
// Internal state for the I/O wrapper. Always present (zero-sized on
// host) so the test harness can compile without #ifdef noise.
// ---------------------------------------------------------------------------

namespace {

struct PendingPost {
    char text[kSystemChannelMaxTextLen];
    size_t len;
};

static PendingPost s_queue[kSystemChannelQueueDepth];
static uint8_t s_queue_head = 0;  // next slot to write
static uint8_t s_queue_tail = 0;  // next slot to read
static uint8_t s_queue_count = 0;
static uint32_t s_last_post_ms = 0;
static SystemChannelPostFn s_post_fn = nullptr;

// Enqueue without rate-limiting (caller already gated). If the
// queue is full, the OLDEST entry is dropped to make room: under
// memory pressure we'd rather show the latest status than the
// historical one. Returns true if the message was queued.
bool enqueue(const char* text, size_t text_len) {
    if (text == nullptr || text_len == 0) return false;
    if (text_len >= kSystemChannelMaxTextLen) {
        text_len = kSystemChannelMaxTextLen - 1;
    }
    if (s_queue_count == kSystemChannelQueueDepth) {
        // Drop oldest: bump tail.
        s_queue_tail = (s_queue_tail + 1) % kSystemChannelQueueDepth;
        --s_queue_count;
    }
    PendingPost& p = s_queue[s_queue_head];
    memcpy(p.text, text, text_len);
    p.text[text_len] = '\0';
    p.len = text_len;
    s_queue_head = (s_queue_head + 1) % kSystemChannelQueueDepth;
    ++s_queue_count;
    return true;
}

}  // namespace

void systemChannelSetPostFunction(SystemChannelPostFn fn) {
    s_post_fn = fn;
}

void systemChannelDrain() {
    if (s_post_fn == nullptr) return;
    while (s_queue_count > 0) {
        const PendingPost& p = s_queue[s_queue_tail];
        s_post_fn(p.text, p.len);
        s_queue_tail = (s_queue_tail + 1) % kSystemChannelQueueDepth;
        --s_queue_count;
    }
}

// ---------------------------------------------------------------------------
// I/O wrapper. ARDUINO-only -- the host test never executes these
// (they touch BaseChatMesh + millis() + Serial), but they're
// always *declared* so the test can stub-link.
// ---------------------------------------------------------------------------

#ifdef ARDUINO

}  // namespace crosswire

// External reference to the global MyMesh instance defined in
// examples/companion_radio/MyMesh.cpp at translation-unit scope
// (NOT inside any namespace). We don't include MyMesh.h here
// (would create a circular-ish dep + drag DataStore etc. into
// the wifi_observer module). All we need is BaseChatMesh's
// channel API which the cast below reaches through.
extern class MyMesh the_mesh;

namespace crosswire {

bool systemChannelInit(const mesh::Identity& self_id) {
    // Derive what we WANT slot 40 to look like.
    char want_name[18] = {0};
    deriveSystemChannelName(self_id.pub_key, want_name, sizeof(want_name));
    uint8_t want_psk[16];
    deriveSystemChannelPsk(self_id.pub_key, want_psk);

    // Read what's currently in slot 40.
    ChannelDetails existing;
    BaseChatMesh* mesh_ptr = reinterpret_cast<BaseChatMesh*>(&the_mesh);
    bool have_existing = mesh_ptr->getChannel(kSystemChannelSlot, existing);

    // Idempotent check: matching name + matching first 16 bytes of
    // secret (we only ever use the 128-bit key path) AND the
    // upper 16 secret bytes are zero (256-bit keys would diff
    // here even if first 16 match). On match, return false so
    // the caller skips the NVS save and avoids flash wear at
    // every boot.
    if (have_existing) {
        bool name_match = (strncmp(existing.name, want_name, 16) == 0);
        bool psk_match  = (memcmp(existing.channel.secret, want_psk, 16) == 0);
        bool upper_zero = true;
        for (int i = 16; i < 32; ++i) {
            if (existing.channel.secret[i] != 0) { upper_zero = false; break; }
        }
        if (name_match && psk_match && upper_zero) {
            Serial.printf("[SystemChannelCli] slot %u already provisioned; no-op.\n", (unsigned)kSystemChannelSlot);
            return false;
        }
    }

    // Build the canonical ChannelDetails and write it.
    ChannelDetails want;
    memset(&want, 0, sizeof(want));
    // setChannel copies the name field byte-for-byte; we wrote a
    // NUL-terminated string of <= 32 bytes so a memcpy is safe.
    strncpy(want.name, want_name, sizeof(want.name) - 1);
    memcpy(want.channel.secret, want_psk, 16);
    // Upper 16 bytes already zero from memset; setChannel will
    // detect the zero tail and compute the 128-bit hash.
    if (!mesh_ptr->setChannel(kSystemChannelSlot, want)) {
        Serial.printf("[SystemChannelCli] setChannel(%u) failed -- slot >= "
                      "MAX_GROUP_CHANNELS (%u)?\n",
                      (unsigned)kSystemChannelSlot,
                      (unsigned)MAX_GROUP_CHANNELS);
        return false;
    }
    Serial.printf("[SystemChannelCli] slot %u provisioned: name='%s' "
                  "(caller should persist via saveChannels)\n",
                  (unsigned)kSystemChannelSlot, want_name);
    return true;
}

bool systemChannelInterceptMsg(const char* msg_text, size_t msg_len) {
    if (msg_text == nullptr) return false;

    // Build a NUL-terminated local copy: cmd_frame text is not
    // guaranteed terminated by upstream parsers. Truncate to a
    // safe length.
    char line[kSystemChannelMaxTextLen];
    size_t copy_len = (msg_len < sizeof(line) - 1) ? msg_len
                                                   : sizeof(line) - 1;
    memcpy(line, msg_text, copy_len);
    line[copy_len] = '\0';

    // Dispatch through the same allowlist + dispatcher as the web
    // Console. Reuses Plan 3 Task 4's logic, so the deny list and
    // verb coverage stay consistent across the two surfaces.
    char reply[kSystemChannelReplyBufLen];
    reply[0] = '\0';
    (void)cliPassthroughExecute(line, reply, sizeof(reply));

    // The reply goes back as channel message(s). A long reply (e.g.
    // `mqtt status` across 6 brokers) exceeds one 140-byte _sys frame,
    // so split on newlines and enqueue each line as its own frame
    // rather than truncating to one (#48 Item 3). No rate-limit gate --
    // a deliberate user-initiated command deserves an immediate reply;
    // the rate limiter applies only to unsolicited status posts.
    for (const char* p = reply; *p; ) {
        const char* nl = strchr(p, '\n');
        size_t seg = nl ? (size_t)(nl - p) : strlen(p);
        if (seg > 0) enqueue(p, seg);  // enqueue clamps to kSystemChannelMaxTextLen-1
        if (!nl) break;
        p = nl + 1;
    }
    return true;
}

bool systemChannelAllowSet() {
    return false;  // locked channel
}

void systemChannelPostStatus(const char* fmt, ...) {
    if (fmt == nullptr) return;
    uint32_t now = millis();
    if (!systemChannelRateAllows(s_last_post_ms, now,
                                 kSystemChannelStatusMinIntervalMs)) {
        Serial.printf("[SystemChannelCli] rate-limited; dropping status: %s\n",
                      fmt);
        return;
    }
    char buf[kSystemChannelMaxTextLen];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    enqueue(buf, (size_t)n);
    // Note: s_last_post_ms is the gate for the formatted post,
    // not the enqueue-flush. Sentinel value 0 means "never
    // posted"; if millis() happens to return 0 here (only at
    // boot=0), we coerce to 1 to keep the sentinel intact.
    s_last_post_ms = (now == 0) ? 1u : now;
}

#else  // !ARDUINO

// Host-side stubs so tests can link without dragging the Arduino
// surface. These never execute under the host test (the test
// only exercises the pure-logic functions), but their presence
// keeps the translation unit linkable if a test ever does
// reference them.
bool systemChannelInit(const mesh::Identity& /*self_id*/) { return false; }
bool systemChannelInterceptMsg(const char* /*msg_text*/, size_t /*msg_len*/) {
    return false;
}
bool systemChannelAllowSet() { return false; }
void systemChannelPostStatus(const char* /*fmt*/, ...) {}

#endif  // ARDUINO

}  // namespace crosswire
