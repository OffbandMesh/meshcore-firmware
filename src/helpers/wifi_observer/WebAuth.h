// src/helpers/wifi_observer/WebAuth.h
//
// Web authentication backend.
//
// Companion variant: initial password is DERIVED from
//   <6-digit BLE pairing PIN> + <first 6 hex chars of identity pubkey>
// accepted in EITHER order (per spec). The first successful login
// immediately forces a password change; from then on only the bcrypt
// hash of the new password is accepted.
//
// (Future repeater variant: reuses LoRa admin password; same WebAuth
// surface, different verifyCandidate() body. Out of Plan 3 scope.)
//
// PUBKEY ACCESS: WebAuth does not reach into MeshCore's self-identity
// storage. Task 11 (WifiObserver wire-up) calls webAuthInit() with a
// pointer to the 32-byte raw pubkey held by MyMesh's self_id. The
// pointer is borrowed; WebAuth does not own or free it. Until init
// is called, webAuthDeriveInitialPassword() returns false.
//
// THREAD-SAFETY: same single-task ownership discipline as the other
// wifi_observer modules. All entry points are intended to be called
// from the WifiObserver loop task (the same task that runs the web
// server handler). No internal locking; do not call from other tasks
// or ISRs. NVS access uses a fresh Preferences instance per call so
// scopes do not overlap.

#pragma once
#include <stdint.h>
#include <stddef.h>

namespace crosswire {

// One-shot init: hand WebAuth a borrowed pointer to the 32-byte raw
// identity pubkey. Caller (Task 11) guarantees the pointer outlives
// WebAuth -- it is the self_id.pub_key bytes held by MyMesh, which
// live for the duration of the process.
void webAuthInit(const uint8_t* pubkey_32_bytes);

// True if NVS has web.password_hash set (i.e., user has already
// changed the initial password). False on factory / first boot.
bool webAuthHasUserPassword();

// Allow-initial-password override (set via `set web.allow_initial on`
// serial CLI; useful for recovery if user forgot their changed
// password). Cleared automatically after next successful login.
bool webAuthAllowInitialPassword();

// Verify a user-submitted password. Returns true if matches either
// (a) the PBKDF2 hash in NVS, or (b) the derived initial password
// and the device is currently in initial-password-acceptable state.
// Sets *requires_change_out=true if the verification succeeded via
// the initial-password path (caller MUST force password change).
bool webAuthVerifyPassword(const char* candidate, bool* requires_change_out);

// Set + persist the user's PBKDF2 hash. Clears
// allow_initial_password automatically.
bool webAuthSetPassword(const char* new_plaintext);

// Builds the derived initial password into out. Format:
//   <6-digit pin> + <first 6 hex chars of pubkey>
// (Returns the "forward" order; the verify path also tries reverse.)
// Returns false if BLE PIN or pubkey is unavailable.
bool webAuthDeriveInitialPassword(char* out, size_t out_len);

}  // namespace crosswire
