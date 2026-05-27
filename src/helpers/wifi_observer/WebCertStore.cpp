// src/helpers/wifi_observer/WebCertStore.cpp
#include "WebCertStore.h"

#include <string.h>
#include <stdlib.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <LittleFS.h>
  #include <mbedtls/pk.h>
  #include <mbedtls/x509_crt.h>
  #include <mbedtls/x509_csr.h>
  #include <mbedtls/ecp.h>
  #include <mbedtls/rsa.h>
  #include <mbedtls/entropy.h>
  #include <mbedtls/ctr_drbg.h>
  #include <mbedtls/bignum.h>
#endif

namespace crosswire {

static char* s_cert_pem = nullptr;
static char* s_key_pem  = nullptr;

#ifdef ARDUINO

static const char* kCertPath = "/web_cert.pem";
static const char* kKeyPath  = "/web_key.pem";

// Load a PEM file from LittleFS into a newly-allocated NUL-terminated
// C string. Returns nullptr on failure. Caller frees.
static char* loadPem(const char* path) {
    if (!LittleFS.exists(path)) return nullptr;
    File f = LittleFS.open(path, "r");
    if (!f) return nullptr;
    size_t sz = f.size();
    char* buf = (char*)malloc(sz + 1);
    if (!buf) { f.close(); return nullptr; }
    size_t got = f.read((uint8_t*)buf, sz);
    f.close();
    if (got != sz) { free(buf); return nullptr; }
    buf[got] = '\0';
    return buf;
}

static bool savePem(const char* path, const char* pem) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t n = strlen(pem);
    bool ok = (f.write((const uint8_t*)pem, n) == n);
    f.close();
    if (!ok) {
        LittleFS.remove(path);  // don't leave a truncated file behind
    }
    return ok;
}

// Generate a self-signed cert + key. Returns true on success and
// writes the PEM-encoded results into cert_pem_out / key_pem_out
// (caller frees with free()).
//
// API note: arduino-esp32 bundles mbed TLS 2.28 (LTS), not 3.x. Two
// drifts from the original plan text:
//   1. mbedtls_x509write_crt_set_serial_raw(ctx, bytes, len)  is 3.x;
//      2.x uses mbedtls_x509write_crt_set_serial(ctx, mbedtls_mpi*).
//      We build a tiny mpi holding 1 for the serial.
//   2. mbedtls_x509write_crt_pem / mbedtls_pk_write_key_pem return 0
//      on success in 2.x (not the PEM length, as 3.x does). The PEM
//      string is NUL-terminated in the output buffer; we strlen() it
//      to size the heap copy.
static bool generateSelfSigned(TlsKeyType key_type,
                               char** cert_pem_out,
                               char** key_pem_out) {
    mbedtls_pk_context        key;
    mbedtls_entropy_context   entropy;
    mbedtls_ctr_drbg_context  ctr_drbg;
    mbedtls_x509write_cert    crt;
    mbedtls_mpi               serial;
    bool ok = false;

    mbedtls_pk_init(&key);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_mpi_init(&serial);

    *cert_pem_out = nullptr;
    *key_pem_out  = nullptr;

    const char* pers = "crosswire-cert";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                              &entropy, (const uint8_t*)pers, strlen(pers)) != 0) {
        goto cleanup;
    }

    // Key generation
    if (key_type == TlsKeyType::EcP256) {
        if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) goto cleanup;
        if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                                 mbedtls_ctr_drbg_random, &ctr_drbg) != 0) goto cleanup;
    } else {
        if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0) goto cleanup;
        if (mbedtls_rsa_gen_key(mbedtls_pk_rsa(key),
                                 mbedtls_ctr_drbg_random, &ctr_drbg,
                                 2048, 65537) != 0) goto cleanup;
    }

    // Cert: CN=Crosswire-Observer, valid for ~30 years (epoch limits).
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    if (mbedtls_x509write_crt_set_subject_name(&crt, "CN=Crosswire-Observer") != 0) goto cleanup;
    if (mbedtls_x509write_crt_set_issuer_name(&crt,  "CN=Crosswire-Observer") != 0) goto cleanup;
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

    if (mbedtls_mpi_lset(&serial, 1) != 0) goto cleanup;
    if (mbedtls_x509write_crt_set_serial(&crt, &serial) != 0) goto cleanup;

    if (mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "20560101000000") != 0) goto cleanup;

    // PEM encode (2.x returns 0 on success; PEM is NUL-terminated in buf).
    {
        uint8_t crt_buf[2048];
        memset(crt_buf, 0, sizeof(crt_buf));
        int rc = mbedtls_x509write_crt_pem(&crt, crt_buf, sizeof(crt_buf),
                                           mbedtls_ctr_drbg_random, &ctr_drbg);
        if (rc != 0) goto cleanup;
        size_t crt_len = strlen((char*)crt_buf);
        *cert_pem_out = (char*)malloc(crt_len + 1);
        if (!*cert_pem_out) goto cleanup;
        memcpy(*cert_pem_out, crt_buf, crt_len);
        (*cert_pem_out)[crt_len] = '\0';
    }
    {
        uint8_t key_buf[2048];
        memset(key_buf, 0, sizeof(key_buf));
        int rc = mbedtls_pk_write_key_pem(&key, key_buf, sizeof(key_buf));
        if (rc != 0) {
            free(*cert_pem_out);
            *cert_pem_out = nullptr;
            goto cleanup;
        }
        size_t key_len = strlen((char*)key_buf);
        *key_pem_out = (char*)malloc(key_len + 1);
        if (!*key_pem_out) {
            free(*cert_pem_out);
            *cert_pem_out = nullptr;
            goto cleanup;
        }
        memcpy(*key_pem_out, key_buf, key_len);
        (*key_pem_out)[key_len] = '\0';
    }

    ok = true;

cleanup:
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_pk_free(&key);
    return ok;
}

bool webCertStoreBegin(TlsKeyType key_type,
                       const char** cert_pem_out,
                       const char** key_pem_out) {
    if (!LittleFS.begin(/*formatOnFail=*/true)) {
        Serial.println("[WebCertStore] LittleFS mount failed.");
        return false;
    }

    // Try to load existing.
    s_cert_pem = loadPem(kCertPath);
    s_key_pem  = loadPem(kKeyPath);
    if (s_cert_pem && s_key_pem) {
        Serial.println("[WebCertStore] Loaded persisted cert+key.");
        *cert_pem_out = s_cert_pem;
        *key_pem_out  = s_key_pem;
        return true;
    }
    // One side present but not the other -> drop the stray and regenerate.
    if (s_cert_pem) { free(s_cert_pem); s_cert_pem = nullptr; }
    if (s_key_pem)  { free(s_key_pem);  s_key_pem  = nullptr; }

    // Generate fresh.
    Serial.printf("[WebCertStore] Generating self-signed cert (key_type=%u)...\n",
                  (unsigned)key_type);
    uint32_t t0 = millis();
    if (!generateSelfSigned(key_type, &s_cert_pem, &s_key_pem)) {
        Serial.println("[WebCertStore] cert generation FAILED.");
        return false;
    }
    Serial.printf("[WebCertStore] generated in %u ms.\n", (unsigned)(millis() - t0));
    if (!savePem(kCertPath, s_cert_pem)) Serial.println("[WebCertStore] WARN: cert persist failed");
    if (!savePem(kKeyPath,  s_key_pem))  Serial.println("[WebCertStore] WARN: key persist failed");
    *cert_pem_out = s_cert_pem;
    *key_pem_out  = s_key_pem;
    return true;
}

bool webCertStoreRegenerate(TlsKeyType key_type) {
    if (s_cert_pem) { free(s_cert_pem); s_cert_pem = nullptr; }
    if (s_key_pem)  { free(s_key_pem);  s_key_pem  = nullptr; }
    if (!generateSelfSigned(key_type, &s_cert_pem, &s_key_pem)) return false;
    if (!savePem(kCertPath, s_cert_pem)) Serial.println("[WebCertStore] WARN: cert persist failed");
    if (!savePem(kKeyPath,  s_key_pem))  Serial.println("[WebCertStore] WARN: key persist failed");
    return true;
}

void webCertStoreShutdown() {
    if (s_cert_pem) { free(s_cert_pem); s_cert_pem = nullptr; }
    if (s_key_pem)  { free(s_key_pem);  s_key_pem  = nullptr; }
}

#else  // !ARDUINO

// Host-build stubs so the file is harmless if the env doesn't define
// ARDUINO (e.g., a future native unit test that includes this header
// transitively). Returning false makes it obvious in tests that no
// cert was produced.

bool webCertStoreBegin(TlsKeyType, const char**, const char**) { return false; }
bool webCertStoreRegenerate(TlsKeyType) { return false; }
void webCertStoreShutdown() {}

#endif  // ARDUINO

}  // namespace crosswire
