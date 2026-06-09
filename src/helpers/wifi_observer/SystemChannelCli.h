// src/helpers/wifi_observer/SystemChannelCli.h
//
// BLE-side first-contact CLI exposed as a locked group channel at slot
// kSystemChannelSlot (the LAST slot, MAX_GROUP_CHANNELS-1). On first boot,
// this module writes a deterministic name + PSK into that slot, derived
// from the device
// identity public key. The slot is "locked": MyMesh refuses
// CMD_SET_CHANNEL targeting it so the user cannot accidentally
// delete it from their MeshCore Companion / Open Web app.
//
// The user opens this channel in their already-paired MeshCore
// app (it auto-appears at the top of the channel list because the
// channel name is "_sys @ <8hex>" -- leading underscore sorts
// alphabetically above named user channels). They type CLI
// commands as channel messages and get replies pushed back as
// channel messages from the same slot.
//
// CLI commands route through the same allowlist as CliPassthrough
// (Plan 3 Task 4): `get *` / `set *` with the deny-prefix filter
// (no shell escapes, no fs.* / flash.* / reboot / format). The
// first commands the user needs are:
//   set wifi.ssid <ssid>
//   set wifi.pwd  <psk>
//   get wifi.status
// Implemented in ObserverCli as wifi.* handlers (Plan 3 Task 10).
//
// SystemChannelCli also serves as a one-way device->user
// notification channel: posts the welcome message on first boot
// with no creds, posts WiFi-connected status with IP + mDNS
// hostname on STA connect. Posts are rate-limited to one message
// per 5 seconds to avoid spamming the user's chat view.
//
// Plan 3 Task 10 (Strycher/LoRa#272).

#pragma once
#include <stddef.h>
#include <stdint.h>

// Forward declarations of MeshCore types. Including <Mesh.h> here
// would drag Arduino + RadioLib into every translation unit that
// merely needs the rate-limit / pure-logic surface. The .cpp
// includes the real headers under #ifdef ARDUINO.
namespace mesh {
class Identity;
}  // namespace mesh

namespace crosswire {

// Reserved _sys slot = the LAST channel slot (MAX_GROUP_CHANNELS - 1).
// Observer envs set MAX_GROUP_CHANNELS via -D (Phase-1 strip #42: =11 ->
// user slots 0-9 + system slot 10; pre-strip was 41 -> slot 40). Deriving
// keeps this in lockstep with the env's -D; the #else covers pure-logic
// host builds compiled without the -D (matches the stripped observer value).
#ifdef MAX_GROUP_CHANNELS
constexpr uint8_t kSystemChannelSlot = MAX_GROUP_CHANNELS - 1;
#else
constexpr uint8_t kSystemChannelSlot = 10;
#endif

// Rate-limit window for systemChannelPostStatus(), in
// milliseconds. Status messages dropped silently when called
// faster than this; a Serial diagnostic line is emitted instead.
constexpr uint32_t kSystemChannelStatusMinIntervalMs = 5000;

// Internal queue depth for pending status messages between
// systemChannelPostStatus() (any context, including pre-boot
// WifiBootstrap) and the MyMesh-side drain. 4 slots is enough
// for the burst at "STA connected" + the welcome message + a
// couple of safety-margin entries; over-queue drops oldest.
constexpr uint8_t kSystemChannelQueueDepth = 4;

// Maximum payload length per status message. Chosen to fit
// comfortably in the v3 channel-msg-recv frame (MAX_FRAME_SIZE
// upstream is 184) after the header overhead.
constexpr size_t kSystemChannelMaxTextLen = 140;

// ---------------------------------------------------------------------------
// Lifecycle (called from main.cpp wiring; ARDUINO-only impl).
// ---------------------------------------------------------------------------

// Idempotent. Compares slot 40's current name+PSK against the
// derived values; only writes via BaseChatMesh::setChannel when
// they differ. Returns TRUE if the in-memory slot was changed
// (caller should persist via its own DataStore wrapper -- this
// module deliberately doesn't reach into MyMesh internals).
// FALSE means slot 40 already matched the derived values; no
// write was performed (avoids flash wear on every boot).
//
// Caller passes the local identity, which is the stable source
// for the derivation. MyMesh::begin() is the right place to
// invoke this -- it's where channels are loaded from DataStore
// and where the saveChannels() private wrapper is reachable.
bool systemChannelInit(const mesh::Identity& self_id);

// Type of the function pointer MyMesh registers so SystemChannelCli
// can push a message into the BLE-side offline queue. The function
// must build a RESP_CODE_CHANNEL_MSG_RECV_V3 frame for slot
// kSystemChannelSlot, append it to the offline queue, and (if
// connected) emit a PUSH_CODE_MSG_WAITING tickle. See the
// implementation hook in MyMesh.cpp.
using SystemChannelPostFn = void (*)(const char* text, size_t text_len);

// Register the MyMesh-side poster. Called once from MyMesh
// startInterface() / begin(). NULL clears the registration (the
// queue then accumulates until a poster is registered again).
void systemChannelSetPostFunction(SystemChannelPostFn fn);

// Drain any queued status messages by invoking the registered
// post function for each. Called from MyMesh::loop(). Safe to
// call before a poster is registered (no-op).
void systemChannelDrain();

// ---------------------------------------------------------------------------
// Intercepts (called from MyMesh::handleCmdFrame() under
// CROSSWIRE_OBSERVER_BLE_COMPANION).
// ---------------------------------------------------------------------------

// Handle a CMD_SEND_CHANNEL_TXT_MSG whose channel_idx is
// kSystemChannelSlot. The text payload is dispatched through the
// CliPassthrough allowlist; the reply (or "denied: not in
// allowlist") is enqueued for posting back on the same channel.
// Returns true if the message was accepted for processing
// (caller should writeOKFrame), false if the dispatch could not
// even be queued (caller should writeErrFrame).
bool systemChannelInterceptMsg(const char* msg_text, size_t msg_len);

// Locked channel: caller must writeErrFrame and not mutate the
// channel slot. Returns false unconditionally; kept as a function
// for symmetry + future extensibility (e.g., maintenance mode
// override).
bool systemChannelAllowSet();

// ---------------------------------------------------------------------------
// One-way device->user notifications.
// ---------------------------------------------------------------------------

// Format-and-enqueue a status message. Honors the
// kSystemChannelStatusMinIntervalMs rate limiter; messages that
// arrive sooner are dropped with a Serial diagnostic. Safe to
// call from anywhere on the main thread (no Mesh / BLE state
// touched here -- the message is pushed onto an internal queue
// drained from MyMesh::loop()).
//
// CW_PRINTF_FMT: gcc/clang format-string checker attribute;
// MSVC's host build compiles without it (no equivalent for free
// functions). Production targets (gcc, ESP-IDF, Arduino) all
// have GCC/Clang so the warning still fires there.
#if defined(__GNUC__) || defined(__clang__)
  #define CW_SYSCH_PRINTF_FMT __attribute__((format(printf, 1, 2)))
#else
  #define CW_SYSCH_PRINTF_FMT
#endif
void systemChannelPostStatus(const char* fmt, ...) CW_SYSCH_PRINTF_FMT;

// ---------------------------------------------------------------------------
// PURE LOGIC -- host-testable, no Arduino/MeshCore deps.
// ---------------------------------------------------------------------------

// PSK derivation: SHA-256(pubkey[0..32] || "crosswire-sysch-v1")
// take first 16 bytes. Namespace literal prevents key confusion
// with other places we might hash the same pubkey (e.g.,
// device_id rendering).
void deriveSystemChannelPsk(const uint8_t pubkey[32], uint8_t out_psk[16]);

// Visible name derivation: "_sys @ <8hex>" where 8hex is the
// first 4 bytes of the pubkey rendered as lowercase hex. The
// leading underscore makes the channel sort at the TOP of
// alphabetical lists across MeshCore clients (the prevention of
// "device buried under 200+ entries"). Truncates safely if
// out_size < 16 (writes prefix-only or empty).
//
// out_size should be >= 17 for the full "_sys @ 12345678\0".
void deriveSystemChannelName(const uint8_t pubkey[32], char* out_name,
                             size_t out_size);

// Returns true if at least one millisecond timestamp argument is
// outside the rate-limit window. Implemented as a pure function
// so the rate-limit policy is host-testable without an Arduino
// millis() fake.
//
// last_post_ms: timestamp of the previous accepted post, or 0
//   if no post has been made yet.
// now_ms:       current millis() reading.
// min_interval_ms: window width (kSystemChannelStatusMinIntervalMs
//   in production; smaller in tests).
//
// Special case: when last_post_ms == 0 (never posted), always
// allows. Wrap-around safe under unsigned arithmetic.
bool systemChannelRateAllows(uint32_t last_post_ms, uint32_t now_ms,
                             uint32_t min_interval_ms);

}  // namespace crosswire
