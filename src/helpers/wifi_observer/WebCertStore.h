// src/helpers/wifi_observer/WebCertStore.h
//
// Manages the device's self-signed TLS certificate for the local web
// UI. Cert + key are generated once at first WiFi-up, persisted to
// LittleFS, and reloaded on every subsequent boot.
//
// Per spec ("Web authentication" section): self-signed is deliberate
// for v1. Browser warning on first visit is the cost of zero-config
// setup; corporate-hardening iteration adds user-uploaded CA-signed
// certs.

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace crosswire {

enum class TlsKeyType : uint8_t {
    EcP256 = 0,  // ECDSA P-256: ~50ms gen, ~250-byte cert
    Rsa2048 = 1, // RSA 2048: ~5-8s gen, ~1100-byte cert; broader compat
};

// Load existing cert+key from LittleFS. If missing or invalid,
// generate fresh ones using the configured key type and persist.
// Returns true on success; populates cert_pem / key_pem with
// pointers to internal buffers.
//
// LIFETIME: the returned pointers are valid until EITHER
// webCertStoreShutdown() OR webCertStoreRegenerate() is called.
// Both operations free the buffers. The caller (or web-server lifecycle
// owner) is responsible for ensuring no TLS handler is mid-request
// when regenerate or shutdown fires -- this module does NOT take any
// lock; it assumes single-task ownership of the pointers via the
// WifiObserver loop-task discipline (Plan 1).
bool webCertStoreBegin(TlsKeyType key_type,
                       const char** cert_pem_out,
                       const char** key_pem_out);

// Force a regenerate (e.g., user pressed "regenerate cert" in
// Settings page). Persists new pair to LittleFS.
//
// INVALIDATES any pointers previously returned by webCertStoreBegin.
// Caller MUST quiesce any TLS handler holding those pointers first
// (e.g., stop the web server, regenerate, restart the web server).
bool webCertStoreRegenerate(TlsKeyType key_type);

void webCertStoreShutdown();

}  // namespace crosswire
