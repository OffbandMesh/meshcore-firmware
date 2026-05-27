// src/helpers/wifi_observer/WebAuth.cpp
#include "WebAuth.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Preferences.h>
  #include <esp_random.h>
  #include <mbedtls/md.h>
  #include <mbedtls/pkcs5.h>
  #include <mbedtls/base64.h>
  #include <mbedtls/constant_time.h>
#endif

namespace crosswire {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// NVS namespace + keys. Namespace is "web" (well under 15-char limit).
static const char* kNvsWeb              = "web";
static const char* kKeyPasswordHash     = "password_hash";
static const char* kKeyAllowInitial     = "allow_initial";   // u8; 1 = allow

// PBKDF2 tuning. 100k iters of HMAC-SHA256 is the controller-resolved
// hardness for this build; well within ESP32-S3 single-login latency
// budget (login is rare; brute-force defense is the point).
static constexpr unsigned int kPbkdf2Iterations = 100000;
static constexpr size_t       kSaltLen          = 16;
static constexpr size_t       kHashLen          = 32;   // SHA-256

// Derived-initial-password geometry. PIN is the BLE_PIN_CODE macro,
// formatted as 6 zero-padded decimal digits. Pubkey-prefix is the
// first 6 lowercase-hex chars of the 32-byte raw identity pubkey.
// Combined derived password is 12 chars + NUL (forward or reverse).
static constexpr size_t kDerivedPasswordLen = 12;   // 6 + 6

// Storage format prefix for the PBKDF2 hash. Used to sanity-check the
// stored blob before attempting to parse it.
static const char* kStoredPrefix = "$pbkdf2-sha256$";

#ifdef ARDUINO

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// Borrowed pointer to MyMesh's self_id.pub_key. Set by webAuthInit().
// nullptr until init is called -- derive functions fail gracefully in
// that window (e.g., if a request lands before WifiObserver completes
// startup).
static const uint8_t* s_pubkey_bytes = nullptr;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Format the BLE pairing PIN as 6 zero-padded decimal digits.
// Wrapped so a future runtime/NVS-sourced PIN can be swapped in
// without touching every call site. Returns false if the BLE_PIN_CODE
// macro is not defined for this env (e.g., a future repeater env that
// disables BLE entirely).
static bool getBlePin(char* out, size_t out_size) {
#ifdef BLE_PIN_CODE
    if (out == nullptr || out_size < 7) return false;
    snprintf(out, out_size, "%06u", (unsigned)BLE_PIN_CODE);
    return true;
#else
    (void)out;
    (void)out_size;
    Serial.println("[WebAuth] WARN: BLE_PIN_CODE not defined for this env");
    return false;
#endif
}

// First 6 lowercase-hex chars of the raw identity pubkey, NUL-terminated.
// Returns false if the pubkey hasn't been registered via webAuthInit().
static bool getPubkeyPrefix(char* out, size_t out_size) {
    if (out == nullptr || out_size < 7) return false;
    if (s_pubkey_bytes == nullptr) {
        Serial.println("[WebAuth] WARN: pubkey not registered (webAuthInit not called)");
        return false;
    }
    static const char kHex[] = "0123456789abcdef";
    // 6 hex chars = first 3 bytes of pubkey.
    for (int i = 0; i < 3; ++i) {
        out[i * 2]     = kHex[(s_pubkey_bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[s_pubkey_bytes[i] & 0xF];
    }
    out[6] = '\0';
    return true;
}

// Run PBKDF2-HMAC-SHA256 over (password, salt) producing kHashLen bytes
// in out. Returns true on success. Internal mbedtls scaffolding is
// freed before return on every path.
//
// `iters` defaults to kPbkdf2Iterations for fresh hashes (the write
// path); the verify path passes the iteration count parsed out of
// the stored blob so the same helper covers both directions and the
// mbedtls API surface lives in exactly one place.
static bool runPbkdf2(const char* password, size_t password_len,
                      const uint8_t* salt, size_t salt_len,
                      uint8_t* out, size_t out_len,
                      uint32_t iters = kPbkdf2Iterations) {
    if (out_len < kHashLen) return false;
    const mbedtls_md_info_t* md_info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr) return false;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    bool ok = false;
    do {
        if (mbedtls_md_setup(&ctx, md_info, /*hmac=*/1) != 0) break;
        int rc = mbedtls_pkcs5_pbkdf2_hmac(
            &ctx,
            (const unsigned char*)password, password_len,
            salt, salt_len,
            iters,
            (uint32_t)kHashLen,
            out);
        if (rc != 0) break;
        ok = true;
    } while (0);
    mbedtls_md_free(&ctx);
    return ok;
}

// Encode `src` (len bytes) as base64 without trailing NUL accounting
// in the size response. Returns true on success. `out_size` must be
// large enough; for sizing call with dlen=0 first (mbedtls convention).
static bool b64Encode(const uint8_t* src, size_t src_len,
                      char* out, size_t out_size, size_t* written) {
    size_t olen = 0;
    int rc = mbedtls_base64_encode((unsigned char*)out, out_size, &olen,
                                   src, src_len);
    if (rc != 0) return false;
    if (written) *written = olen;
    return true;
}

static bool b64Decode(const char* src, size_t src_len,
                      uint8_t* out, size_t out_size, size_t* written) {
    size_t olen = 0;
    int rc = mbedtls_base64_decode(out, out_size, &olen,
                                   (const unsigned char*)src, src_len);
    if (rc != 0) return false;
    if (written) *written = olen;
    return true;
}

// Load + parse the stored hash blob from NVS. On success:
//   - iters_out = iteration count from the stored blob
//   - salt_out  = decoded salt (must be kSaltLen)
//   - hash_out  = decoded expected hash (must be kHashLen)
// Returns false if the key is absent, the format is wrong, or the
// decoded sizes do not match expectations.
static bool loadStoredHash(unsigned int* iters_out,
                           uint8_t* salt_out, size_t salt_cap,
                           uint8_t* hash_out, size_t hash_cap) {
    Preferences p;
    if (!p.begin(kNvsWeb, /*readOnly=*/true)) return false;
    String stored = p.getString(kKeyPasswordHash, "");
    p.end();
    if (stored.length() == 0) return false;

    // Expect format: $pbkdf2-sha256$<iters>$<b64salt>$<b64hash>
    if (strncmp(stored.c_str(), kStoredPrefix, strlen(kStoredPrefix)) != 0) {
        Serial.println("[WebAuth] WARN: stored hash has wrong prefix");
        return false;
    }

    // Walk the $-separated fields. We have 4 leading $ delimiters
    // (empty, "pbkdf2-sha256", "<iters>", "<b64salt>", "<b64hash>").
    const char* s = stored.c_str();
    // Find positions of the 4 '$' after the leading one.
    const char* p1 = strchr(s + 1, '$');             // before "pbkdf2-sha256"
    if (!p1) return false;
    const char* p2 = strchr(p1 + 1, '$');            // before iters
    if (!p2) return false;
    const char* p3 = strchr(p2 + 1, '$');            // before b64salt
    if (!p3) return false;
    const char* p4 = strchr(p3 + 1, '$');            // before b64hash
    if (!p4) return false;
    // p2..p3 = iters, p3..p4 = b64salt, p4..end = b64hash
    const char* iters_begin = p2 + 1;
    size_t iters_len = (size_t)(p3 - iters_begin);
    const char* salt_begin  = p3 + 1;
    size_t salt_b64_len = (size_t)(p4 - salt_begin);
    const char* hash_begin  = p4 + 1;
    size_t hash_b64_len = strlen(hash_begin);

    if (iters_len == 0 || iters_len > 10) return false;
    char iters_buf[11];
    memcpy(iters_buf, iters_begin, iters_len);
    iters_buf[iters_len] = '\0';
    long iters_l = strtol(iters_buf, nullptr, 10);
    if (iters_l <= 0 || iters_l > 10000000) return false;
    *iters_out = (unsigned int)iters_l;

    size_t salt_decoded = 0;
    if (!b64Decode(salt_begin, salt_b64_len, salt_out, salt_cap, &salt_decoded)) {
        Serial.println("[WebAuth] WARN: salt base64 decode failed");
        return false;
    }
    if (salt_decoded != kSaltLen) {
        Serial.printf("[WebAuth] WARN: salt decoded len=%u (expected %u)\n",
                      (unsigned)salt_decoded, (unsigned)kSaltLen);
        return false;
    }

    size_t hash_decoded = 0;
    if (!b64Decode(hash_begin, hash_b64_len, hash_out, hash_cap, &hash_decoded)) {
        Serial.println("[WebAuth] WARN: hash base64 decode failed");
        return false;
    }
    if (hash_decoded != kHashLen) {
        Serial.printf("[WebAuth] WARN: hash decoded len=%u (expected %u)\n",
                      (unsigned)hash_decoded, (unsigned)kHashLen);
        return false;
    }
    return true;
}

// Constant-time compare for two equal-length char buffers. Wraps
// mbedtls_ct_memcmp so call sites don't have to repeat the length
// guard. Returns true on equality.
static bool ctEquals(const char* a, const char* b, size_t len) {
    return mbedtls_ct_memcmp(a, b, len) == 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void webAuthInit(const uint8_t* pubkey_32_bytes) {
    s_pubkey_bytes = pubkey_32_bytes;
    if (s_pubkey_bytes == nullptr) {
        Serial.println("[WebAuth] WARN: init called with null pubkey");
    } else {
        Serial.println("[WebAuth] init: pubkey registered");
    }
}

bool webAuthHasUserPassword() {
    Preferences p;
    if (!p.begin(kNvsWeb, /*readOnly=*/true)) return false;
    bool has = p.isKey(kKeyPasswordHash);
    p.end();
    return has;
}

bool webAuthAllowInitialPassword() {
    Preferences p;
    if (!p.begin(kNvsWeb, /*readOnly=*/true)) return false;
    uint8_t v = p.getUChar(kKeyAllowInitial, 0);
    p.end();
    return v != 0;
}

bool webAuthDeriveInitialPassword(char* out, size_t out_len) {
    if (out == nullptr || out_len < (kDerivedPasswordLen + 1)) return false;
    char pin[7];
    char pk6[7];
    if (!getBlePin(pin, sizeof(pin)))      return false;
    if (!getPubkeyPrefix(pk6, sizeof(pk6))) return false;
    // Forward order: <pin><pk6>
    snprintf(out, out_len, "%s%s", pin, pk6);
    return true;
}

bool webAuthVerifyPassword(const char* candidate, bool* requires_change_out) {
    if (requires_change_out) *requires_change_out = false;
    if (candidate == nullptr || candidate[0] == '\0') return false;
    size_t candidate_len = strlen(candidate);

    // ---- Stored-hash path ----
    // If the user has set their own password, the stored hash is the
    // primary credential. Initial-password fallback ONLY applies if
    // (a) no stored hash exists yet, OR
    // (b) recovery override (web.allow_initial) is set.
    bool has_stored = webAuthHasUserPassword();
    if (has_stored) {
        unsigned int iters = 0;
        uint8_t salt[kSaltLen];
        uint8_t expected[kHashLen];
        if (loadStoredHash(&iters, salt, sizeof(salt),
                           expected, sizeof(expected))) {
            // Honor the iteration count stored in the blob (forward-compat
            // if the constant ever increases); we currently always write
            // kPbkdf2Iterations.
            uint8_t derived[kHashLen];
            if (runPbkdf2(candidate, candidate_len,
                          salt, kSaltLen,
                          derived, sizeof(derived),
                          (uint32_t)iters) &&
                mbedtls_ct_memcmp(derived, expected, kHashLen) == 0) {
                // Stored-hash match. NOT an initial-password login,
                // so requires_change_out stays false.
                return true;
            }
        } else {
            // Stored-hash key was present (has_stored==true) but the blob
            // would not parse. Without this breadcrumb, operators debugging
            // "why did the initial password suddenly start working again?"
            // see only a silent slide into the recovery-override / initial-
            // password branch below.
            Serial.println("[WebAuth] WARN: stored hash present but unparseable; falling through to initial-password gating");
        }
        // Stored hash present but candidate did not match (or stored blob
        // failed to parse). Fall through ONLY if recovery override is set.
        if (!webAuthAllowInitialPassword()) return false;
    }

    // ---- Initial-password path ----
    char derived_forward[kDerivedPasswordLen + 1];
    if (!webAuthDeriveInitialPassword(derived_forward,
                                       sizeof(derived_forward))) {
        return false;
    }
    // Build reverse order: <pk6><pin>. Reuse the same components.
    char pin[7];
    char pk6[7];
    if (!getBlePin(pin, sizeof(pin)))      return false;
    if (!getPubkeyPrefix(pk6, sizeof(pk6))) return false;
    char derived_reverse[kDerivedPasswordLen + 1];
    snprintf(derived_reverse, sizeof(derived_reverse), "%s%s", pk6, pin);

    // Length check first (length is a public constant, so this is not
    // a timing leak). Then constant-time compare against BOTH orders.
    // We OR the two CT results -- avoid short-circuit "matched forward,
    // skip reverse" so total compare time is independent of which side
    // matched.
    bool match_forward = false;
    bool match_reverse = false;
    if (candidate_len == kDerivedPasswordLen) {
        match_forward = ctEquals(candidate, derived_forward,
                                 kDerivedPasswordLen);
        match_reverse = ctEquals(candidate, derived_reverse,
                                 kDerivedPasswordLen);
    }
    if (match_forward || match_reverse) {
        if (requires_change_out) *requires_change_out = true;
        return true;
    }
    return false;
}

bool webAuthSetPassword(const char* new_plaintext) {
    if (new_plaintext == nullptr || new_plaintext[0] == '\0') {
        Serial.println("[WebAuth] WARN: setPassword refused empty plaintext");
        return false;
    }

    // Fresh 16-byte salt from esp_random (HW RNG on ESP32-S3).
    uint8_t salt[kSaltLen];
    for (size_t i = 0; i < kSaltLen; i += 4) {
        uint32_t r = esp_random();
        for (int b = 0; b < 4 && (i + (size_t)b) < kSaltLen; ++b) {
            salt[i + b] = (uint8_t)(r >> (b * 8));
        }
    }

    uint8_t hash[kHashLen];
    if (!runPbkdf2(new_plaintext, strlen(new_plaintext),
                   salt, kSaltLen, hash, sizeof(hash))) {
        Serial.println("[WebAuth] WARN: PBKDF2 derivation failed");
        return false;
    }

    // Base64 the salt + hash. Sizes: 16 bytes -> 24 chars (with padding);
    // 32 bytes -> 44 chars. Add slack.
    char salt_b64[32] = {0};
    char hash_b64[64] = {0};
    size_t salt_b64_len = 0;
    size_t hash_b64_len = 0;
    if (!b64Encode(salt, kSaltLen, salt_b64, sizeof(salt_b64), &salt_b64_len)) {
        Serial.println("[WebAuth] WARN: salt base64 encode failed");
        return false;
    }
    if (!b64Encode(hash, kHashLen, hash_b64, sizeof(hash_b64), &hash_b64_len)) {
        Serial.println("[WebAuth] WARN: hash base64 encode failed");
        return false;
    }

    // Compose $pbkdf2-sha256$<iters>$<b64salt>$<b64hash>
    // Max length: 15 (prefix) + 10 (iters) + 1 ($) + 24 (salt) + 1 ($) +
    // 44 (hash) = ~95 + NUL. Allocate generously.
    char composed[160];
    int n = snprintf(composed, sizeof(composed),
                     "%s%u$%s$%s",
                     kStoredPrefix,
                     kPbkdf2Iterations,
                     salt_b64,
                     hash_b64);
    if (n < 0 || (size_t)n >= sizeof(composed)) {
        Serial.println("[WebAuth] WARN: composed stored hash truncated");
        return false;
    }

    Preferences p;
    if (!p.begin(kNvsWeb, /*readOnly=*/false)) {
        Serial.println("[WebAuth] WARN: NVS open (rw) failed");
        return false;
    }
    bool ok = (p.putString(kKeyPasswordHash, composed) == (size_t)n);
    // Clearing allow_initial: write 0 (we don't use remove() because
    // the read path defaults to 0 if absent and we want the write to
    // be observable in NVS introspection too).
    p.putUChar(kKeyAllowInitial, 0);
    p.end();

    if (!ok) {
        Serial.println("[WebAuth] WARN: NVS write of password_hash failed");
        return false;
    }
    Serial.println("[WebAuth] password updated; allow_initial cleared");
    return true;
}

#else  // !ARDUINO

// Host-build stubs so this header is harmless if included transitively
// by a future native unit test. Behaviors are intentionally degenerate;
// host-side PBKDF2 round-trip is a separate Task 6 deliverable that
// exercises the math without NVS.

void webAuthInit(const uint8_t*) {}
bool webAuthHasUserPassword() { return false; }
bool webAuthAllowInitialPassword() { return false; }
bool webAuthVerifyPassword(const char*, bool* requires_change_out) {
    if (requires_change_out) *requires_change_out = false;
    return false;
}
bool webAuthSetPassword(const char*) { return false; }
bool webAuthDeriveInitialPassword(char* out, size_t out_len) {
    if (out != nullptr && out_len > 0) out[0] = '\0';
    return false;
}

#endif  // ARDUINO

}  // namespace crosswire
