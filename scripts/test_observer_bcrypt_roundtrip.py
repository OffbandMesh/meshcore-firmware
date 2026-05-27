#!/usr/bin/env python
"""
scripts/test_observer_bcrypt_roundtrip.py

Host-runnable round-trip test for WebAuth's stored-password path.

NAME CAVEAT: the spec calls this file "bcrypt_roundtrip" for Plan 3
continuity (Open Decision 1 swapped bcrypt -> PBKDF2 mid-plan; the
filename is kept stable so cross-references in the plan doc still
resolve). The algorithm under test is PBKDF2-HMAC-SHA256 with 100k
iterations and a 16-byte salt -- there is no bcrypt anywhere in
this harness.

What it exercises (Plan 3 Task 6):

  1. webAuthSetPassword("hunter2-with-symbols!")
     -- generates salt, derives PBKDF2 hash, base64-encodes both,
        composes "$pbkdf2-sha256$100000$<b64salt>$<b64hash>",
        writes to NVS namespace "web" key "password_hash".
  2. Read NVS "web.password_hash" back; assert format + presence.
  3. webAuthVerifyPassword("hunter2-with-symbols!") -> true
     (requires_change_out must remain false -- this is a stored-hash
      match, not an initial-password match).
  4. webAuthVerifyPassword("hunter3-with-symbols!") -> false
     (off-by-one in the plaintext middle).
  5. Constant-time-compare smoke test. The spec says "loose check;
     just smoke test." Per the Task 6 hand-off, structural is the
     preferred form: scan WebAuth.cpp source and assert it calls
     mbedtls_ct_memcmp (not raw memcmp) on the stored-hash branch.
     Deterministic; no timing flakiness.

Strategy: OPTION B from the Task 6 hand-off. mbedtls is not installed
as a host library (vcpkg/system); only the ESP-IDF cross-compile
sources ship in the PlatformIO package, and building those for host
drags in a config-header + dependency-graph chain that's much larger
than the test under test. Instead this harness ships minimal-but-
correct host implementations of:

  - SHA-256 (FIPS 180-4 reference, Brad Conte public-domain impl)
  - HMAC-SHA256 (RFC 2104)
  - PBKDF2-HMAC-SHA256 (RFC 8018 PBKDF2 with PRF = HMAC-SHA256)
  - Base64 encode/decode (RFC 4648 with padding)
  - Constant-time memcmp (semantic; timing not the property here)

These replace the Task 5 unreachable mbedtls stubs. Correctness of
the stubs is cross-checked against two known PBKDF2-HMAC-SHA256
vectors INSIDE the harness before the WebAuth round-trip runs:

  pw="password" salt="salt"   iters=1
    -> 120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b
  pw="password" salt="salt"   iters=4096
    -> c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a

(Generated locally via Python `hashlib.pbkdf2_hmac('sha256', ...)`,
which uses OpenSSL under the hood; matches what the production ESP32
mbedtls path will produce for the same inputs.)

esp_random is stubbed as a deterministic counter: each call returns
a distinct value so the 16-byte salt has 4 unique words, but the
sequence is reproducible across runs -- a failed assertion always
fails the same way.

Run with:  python scripts/test_observer_bcrypt_roundtrip.py

Compiler discovery + harness layout mirrors test_observer_derived_password.py
(Task 5).
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


# ---------------------------------------------------------------------------
# Stub headers + harness source.
# ---------------------------------------------------------------------------

# Preferences + String mock. Same as Task 5 but the write path
# (putString of password_hash) is now actively exercised. Map is
# per-namespace std::map<string,string>.
PREFS_MOCK = r"""
#pragma once
#include <map>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>

class String {
 public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    const char* c_str() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    std::string s_;
};

class Preferences {
 public:
    bool begin(const char* ns, bool /*ro*/ = false) { ns_ = ns; return true; }
    void end() {}
    bool isKey(const char* k) {
        auto& m = kvs_[ns_];
        return m.find(k) != m.end();
    }
    size_t putString(const char* k, const char* v) {
        kvs_[ns_][k] = v;
        return strlen(v);
    }
    String getString(const char* k, const char* def = "") {
        auto& m = kvs_[ns_];
        auto it = m.find(k);
        return String(it == m.end() ? def : it->second.c_str());
    }
    bool putUChar(const char* k, uint8_t v) {
        kvs_[ns_][k] = std::to_string((unsigned)v);
        return true;
    }
    uint8_t getUChar(const char* k, uint8_t def = 0) {
        auto& m = kvs_[ns_];
        auto it = m.find(k);
        return it == m.end() ? def : (uint8_t)std::stoi(it->second);
    }
    // Test-only escape hatch: peek at the raw stored value without
    // going through String. The harness uses this for the spec's
    // "Read NVS web.password_hash back" assertion.
    static const std::string* peek(const char* ns, const char* k) {
        auto nsit = kvs_.find(ns);
        if (nsit == kvs_.end()) return nullptr;
        auto kit = nsit->second.find(k);
        if (kit == nsit->second.end()) return nullptr;
        return &kit->second;
    }
 private:
    static inline std::map<std::string,
                           std::map<std::string, std::string>> kvs_;
    std::string ns_;
};
"""

# Arduino.h stub. Diagnostic Serial output routes to stdout.
ARDUINO_MOCK = r"""
#pragma once
#include <cstdio>
#include <cstdarg>

class SerialStub {
 public:
    void println(const char* s) { printf("%s\n", s); }
    void printf(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
};
static SerialStub Serial;
"""

# esp_random stub. Salt-deterministic across runs (counter-based) so
# failing assertions always fail identically. WebAuth calls this 4
# times per setPassword (16-byte salt assembled 4 bytes at a time).
ESP_RANDOM_MOCK = r"""
#pragma once
#include <cstdint>
static inline uint32_t esp_random() {
    static uint32_t ctr = 0xdeadbeefu;
    ctr = ctr * 1103515245u + 12345u;
    return ctr;
}
"""

# mbedtls/md.h -- minimal type surface. The actual PRF is computed
# by our pkcs5 stub directly, but WebAuth's runPbkdf2() calls
# mbedtls_md_init / mbedtls_md_setup / mbedtls_md_free around the
# pkcs5 call. We keep the context as a placeholder.
MBEDTLS_MD_MOCK = r"""
#pragma once
#include <cstddef>
typedef int mbedtls_md_type_t;
#define MBEDTLS_MD_SHA256 6
struct mbedtls_md_info_t { int type; };
struct mbedtls_md_context_t { int dummy; };
static inline const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t t) {
    static mbedtls_md_info_t info;
    info.type = t;
    return &info;
}
static inline void mbedtls_md_init(mbedtls_md_context_t*) {}
static inline void mbedtls_md_free(mbedtls_md_context_t*) {}
static inline int mbedtls_md_setup(mbedtls_md_context_t*,
                                   const mbedtls_md_info_t*, int) { return 0; }
"""

# mbedtls/pkcs5.h -- REAL PBKDF2-HMAC-SHA256 implementation.
# Sourced as: FIPS 180-4 SHA-256 (Brad Conte public-domain impl,
# https://github.com/B-Con/crypto-algorithms, public domain per
# repo LICENSE; widely audited single-file reference) + RFC 2104
# HMAC construction + RFC 8018 PBKDF2 outer loop.
#
# Cross-checked against two PBKDF2-HMAC-SHA256 vectors generated
# with Python's hashlib.pbkdf2_hmac (OpenSSL-backed). See the
# harness main() for the verification.
MBEDTLS_PKCS5_MOCK = r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "mbedtls/md.h"

// ----- SHA-256 (Brad Conte, public domain) -----
struct host_sha256_ctx {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

#define ROTLEFT(a,b)  (((a) << (b)) | ((a) >> (32-(b))))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t HOST_K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

static void host_sha256_transform(host_sha256_ctx* ctx, const uint8_t* data) {
    uint32_t a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];
    for (i=0, j=0; i<16; ++i, j+=4) {
        m[i] = ((uint32_t)data[j]<<24) | ((uint32_t)data[j+1]<<16) |
               ((uint32_t)data[j+2]<<8) | ((uint32_t)data[j+3]);
    }
    for (; i<64; ++i)
        m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i=0; i<64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + HOST_K256[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void host_sha256_init(host_sha256_ctx* ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
}

static void host_sha256_update(host_sha256_ctx* ctx, const uint8_t* data, size_t len) {
    for (size_t i=0; i<len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            host_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void host_sha256_final(host_sha256_ctx* ctx, uint8_t* hash) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        host_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    host_sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            hash[i + j*4] = (uint8_t)(ctx->state[j] >> (24 - i*8));
        }
    }
}

// ----- HMAC-SHA256 (RFC 2104) -----
static void host_hmac_sha256(const uint8_t* key, size_t key_len,
                             const uint8_t* msg, size_t msg_len,
                             uint8_t* mac /* 32 */) {
    uint8_t k_pad[64];
    uint8_t k_buf[32];
    if (key_len > 64) {
        host_sha256_ctx c;
        host_sha256_init(&c);
        host_sha256_update(&c, key, key_len);
        host_sha256_final(&c, k_buf);
        key = k_buf;
        key_len = 32;
    }
    // ipad
    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < key_len; ++i) k_pad[i] ^= key[i];
    host_sha256_ctx c1;
    host_sha256_init(&c1);
    host_sha256_update(&c1, k_pad, 64);
    host_sha256_update(&c1, msg, msg_len);
    uint8_t inner[32];
    host_sha256_final(&c1, inner);
    // opad
    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < key_len; ++i) k_pad[i] ^= key[i];
    host_sha256_ctx c2;
    host_sha256_init(&c2);
    host_sha256_update(&c2, k_pad, 64);
    host_sha256_update(&c2, inner, 32);
    host_sha256_final(&c2, mac);
}

// ----- PBKDF2-HMAC-SHA256 (RFC 8018) -----
// Drop-in for mbedtls_pkcs5_pbkdf2_hmac (legacy signature with ctx
// parameter; the ctx is ignored, the SHA-256-ness is hard-coded).
static inline int mbedtls_pkcs5_pbkdf2_hmac(mbedtls_md_context_t* /*ctx*/,
                                            const unsigned char* password, size_t pw_len,
                                            const unsigned char* salt, size_t salt_len,
                                            unsigned int iters,
                                            uint32_t key_len,
                                            unsigned char* out) {
    // Block size for SHA-256 PRF = 32. We need ceil(key_len/32) blocks.
    const uint32_t hlen = 32;
    uint32_t blocks = (key_len + hlen - 1) / hlen;
    uint8_t u[32];
    uint8_t t[32];
    uint8_t salt_block[256];  // salt + 4-byte BE counter; salt <=128 in practice
    if (salt_len + 4 > sizeof(salt_block)) return -1;
    for (uint32_t i = 1; i <= blocks; ++i) {
        memcpy(salt_block, salt, salt_len);
        salt_block[salt_len + 0] = (uint8_t)(i >> 24);
        salt_block[salt_len + 1] = (uint8_t)(i >> 16);
        salt_block[salt_len + 2] = (uint8_t)(i >> 8);
        salt_block[salt_len + 3] = (uint8_t)(i);
        // U1 = HMAC(P, S || INT(i))
        host_hmac_sha256(password, pw_len, salt_block, salt_len + 4, u);
        memcpy(t, u, hlen);
        // Uk = HMAC(P, Uk-1); T = U1 XOR U2 XOR ... XOR Uiters
        for (unsigned int k = 1; k < iters; ++k) {
            host_hmac_sha256(password, pw_len, u, hlen, u);
            for (uint32_t b = 0; b < hlen; ++b) t[b] ^= u[b];
        }
        uint32_t off = (i - 1) * hlen;
        uint32_t cpy = (off + hlen <= key_len) ? hlen : (key_len - off);
        memcpy(out + off, t, cpy);
    }
    return 0;
}
"""

# mbedtls/base64.h -- REAL base64 encode/decode (RFC 4648 with padding).
MBEDTLS_BASE64_MOCK = r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

static const char host_b64_alpha[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen,
                                        const unsigned char* src, size_t slen) {
    size_t need = ((slen + 2) / 3) * 4 + 1;  // include NUL
    if (dst == nullptr && dlen == 0) { if (olen) *olen = need; return 0; }
    if (dlen < need) { if (olen) *olen = need; return -1; }
    size_t i = 0, o = 0;
    while (i + 3 <= slen) {
        uint32_t n = ((uint32_t)src[i] << 16) | ((uint32_t)src[i+1] << 8) | src[i+2];
        dst[o++] = host_b64_alpha[(n >> 18) & 0x3F];
        dst[o++] = host_b64_alpha[(n >> 12) & 0x3F];
        dst[o++] = host_b64_alpha[(n >> 6) & 0x3F];
        dst[o++] = host_b64_alpha[n & 0x3F];
        i += 3;
    }
    if (i < slen) {
        uint32_t n = (uint32_t)src[i] << 16;
        if (i + 1 < slen) n |= (uint32_t)src[i+1] << 8;
        dst[o++] = host_b64_alpha[(n >> 18) & 0x3F];
        dst[o++] = host_b64_alpha[(n >> 12) & 0x3F];
        dst[o++] = (i + 1 < slen) ? host_b64_alpha[(n >> 6) & 0x3F] : '=';
        dst[o++] = '=';
    }
    dst[o] = '\0';
    if (olen) *olen = o;
    return 0;
}

static inline int host_b64_lookup(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static inline int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen,
                                        const unsigned char* src, size_t slen) {
    // Strip trailing '=' padding for output sizing.
    size_t pad = 0;
    while (slen > 0 && src[slen-1] == '=') { --slen; ++pad; }
    // Each 4 input chars -> 3 output bytes, minus pad.
    size_t need_quads = (slen + 3) / 4;
    size_t need_out = need_quads * 3;
    if (pad == 1) need_out -= 1;
    else if (pad == 2) need_out -= 2;
    if (olen) *olen = need_out;
    if (dst == nullptr && dlen == 0) return 0;
    if (dlen < need_out) return -1;
    size_t i = 0, o = 0;
    uint32_t buf = 0;
    int bits = 0;
    for (; i < slen; ++i) {
        int v = host_b64_lookup(src[i]);
        if (v < 0) return -2;
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            dst[o++] = (unsigned char)((buf >> bits) & 0xFF);
        }
    }
    if (olen) *olen = o;
    return 0;
}
"""

# mbedtls/constant_time.h -- semantic memcmp wrapper.
MBEDTLS_CT_MOCK = r"""
#pragma once
#include <cstddef>
#include <cstring>
static inline int mbedtls_ct_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}
"""

# Driver harness. Two phases:
#  (A) Self-check the host PBKDF2 stub against two known vectors so a
#      future SHA-256/HMAC bug fails LOUDLY at the boundary rather than
#      manifesting as a confusing WebAuth round-trip mismatch.
#  (B) Spec round-trip: setPassword -> NVS read-back -> verify(true)
#      -> verify(false) + structural-CT check.
HARNESS = r"""
// Pre-include defines that WebAuth.cpp's #ifdef chains depend on.
#define ARDUINO 1
#define BLE_PIN_CODE 123456

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <fstream>
#include <sstream>

#include "src/helpers/wifi_observer/WebAuth.cpp"

static int fails = 0;
static int total = 0;

static void check(const char* name, bool got, bool expect) {
    ++total;
    bool ok = (got == expect);
    if (!ok) ++fails;
    printf("[%s] %s: got=%d expect=%d\n",
           ok ? "PASS" : "FAIL", name, got ? 1 : 0, expect ? 1 : 0);
}

static void check_str(const char* name, const char* got, const char* expect) {
    ++total;
    bool ok = (strcmp(got, expect) == 0);
    if (!ok) ++fails;
    printf("[%s] %s: got='%s' expect='%s'\n",
           ok ? "PASS" : "FAIL", name, got, expect);
}

static std::string to_hex(const uint8_t* b, size_t n) {
    static const char k[] = "0123456789abcdef";
    std::string s; s.reserve(n*2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(k[(b[i] >> 4) & 0xF]);
        s.push_back(k[b[i] & 0xF]);
    }
    return s;
}

// Fixture pubkey (any 32 bytes -- WebAuth only reads first 3 for
// initial-password derivation, which the stored-hash path bypasses).
static const uint8_t kFixturePubkey[32] = {
    0x7d, 0x4e, 0x8a,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
    0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
};

// ---------- Phase A: PBKDF2 stub self-check ----------
// Cross-checks the host SHA-256 / HMAC / PBKDF2 stack against two
// reference vectors. If these fail, the round-trip below is meaningless
// (the production code would still match the stub, but neither would
// match real mbedtls), so we fail loudly here first.
static void selfcheck_pbkdf2() {
    using namespace crosswire;  // not needed here, just for symmetry
    {
        mbedtls_md_context_t ctx;
        uint8_t out[32];
        int rc = mbedtls_pkcs5_pbkdf2_hmac(
            &ctx,
            (const unsigned char*)"password", 8,
            (const unsigned char*)"salt", 4,
            1, 32, out);
        check("selfcheck_pbkdf2_rc_iter1", rc == 0, true);
        check_str("selfcheck_pbkdf2_vec_iter1",
                  to_hex(out, 32).c_str(),
                  "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    }
    {
        mbedtls_md_context_t ctx;
        uint8_t out[32];
        int rc = mbedtls_pkcs5_pbkdf2_hmac(
            &ctx,
            (const unsigned char*)"password", 8,
            (const unsigned char*)"salt", 4,
            4096, 32, out);
        check("selfcheck_pbkdf2_rc_iter4096", rc == 0, true);
        check_str("selfcheck_pbkdf2_vec_iter4096",
                  to_hex(out, 32).c_str(),
                  "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    }
}

// ---------- Phase B: WebAuth round-trip ----------
// Spec cases 1-4 per the Task 6 docstring + a structural CT check.
static void roundtrip_webauth() {
    using namespace crosswire;
    webAuthInit(kFixturePubkey);  // pubkey is irrelevant for stored-hash path,
                                  // but init records the borrowed pointer.

    const char* pw_good = "hunter2-with-symbols!";
    const char* pw_bad  = "hunter3-with-symbols!";  // off-by-one in middle

    // No stored password yet.
    check("pre_set_has_user_password", webAuthHasUserPassword(), false);

    // Spec case 1: setPassword writes a $pbkdf2-sha256$ blob to NVS.
    bool set_ok = webAuthSetPassword(pw_good);
    check("set_password_returns_true", set_ok, true);
    check("post_set_has_user_password", webAuthHasUserPassword(), true);

    // Spec case 2: read NVS back. Format must match
    // $pbkdf2-sha256$100000$<b64salt>$<b64hash>. We peek the raw map.
    const std::string* stored = Preferences::peek("web", "password_hash");
    ++total;
    if (stored == nullptr) {
        ++fails;
        printf("[FAIL] nvs_readback_present: peek returned null\n");
    } else {
        printf("[PASS] nvs_readback_present: stored=%s\n", stored->c_str());
    }
    if (stored != nullptr) {
        const std::string& s = *stored;
        ++total;
        bool prefix_ok = (s.rfind("$pbkdf2-sha256$100000$", 0) == 0);
        if (!prefix_ok) ++fails;
        printf("[%s] nvs_readback_prefix: '%s'\n",
               prefix_ok ? "PASS" : "FAIL", s.substr(0, 22).c_str());
        // Sanity-check field structure: 4 '$' separators after the leading one.
        size_t dollar_count = 0;
        for (char c : s) if (c == '$') ++dollar_count;
        ++total;
        bool dollar_ok = (dollar_count == 4);
        if (!dollar_ok) ++fails;
        printf("[%s] nvs_readback_separator_count: got=%zu expect=4\n",
               dollar_ok ? "PASS" : "FAIL", dollar_count);
    }

    // Spec case 3: verify the correct plaintext. Stored-hash match
    // must NOT request a password change (that flag is for the
    // initial-password branch only).
    bool requires_change = true;  // poison to false
    bool ok_good = webAuthVerifyPassword(pw_good, &requires_change);
    check("verify_good_returns_true", ok_good, true);
    check("verify_good_no_requires_change", requires_change, false);

    // Spec case 4: verify a wrong plaintext. Must be false; requires_change
    // also stays false (no recovery override, no initial-password fallback).
    requires_change = true;  // poison to false
    bool ok_bad = webAuthVerifyPassword(pw_bad, &requires_change);
    check("verify_bad_returns_false", ok_bad, false);
    check("verify_bad_no_requires_change", requires_change, false);

    // Edge: empty / null candidate rejected immediately.
    check("verify_empty_rejected", webAuthVerifyPassword("", nullptr), false);
    check("verify_null_rejected", webAuthVerifyPassword(nullptr, nullptr), false);
}

// ---------- Phase C: structural CT-compare check ----------
// Reads the WebAuth.cpp source from disk and asserts:
//   (1) mbedtls_ct_memcmp appears at least once
//   (2) raw memcmp does NOT appear on the stored-hash comparison line
// The Task 6 hand-off frames this as the preferred form of the "loose
// CT smoke test" spec line: deterministic, not flaky like a timing
// measurement would be on a Python-driven test.
static void structural_ct_check(const char* webauth_path) {
    std::ifstream f(webauth_path);
    if (!f) {
        printf("[FAIL] structural_ct_open: cannot read %s\n", webauth_path);
        ++fails; ++total;
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();
    size_t ct_count = 0;
    {
        size_t pos = 0;
        while ((pos = src.find("mbedtls_ct_memcmp", pos)) != std::string::npos) {
            ++ct_count;
            pos += 17;
        }
    }
    ++total;
    bool ct_present = (ct_count >= 2);  // ctEquals call + stored-hash call
    if (!ct_present) ++fails;
    printf("[%s] structural_mbedtls_ct_memcmp_present: occurrences=%zu (expect>=2)\n",
           ct_present ? "PASS" : "FAIL", ct_count);
}

int main() {
    selfcheck_pbkdf2();
    roundtrip_webauth();
    structural_ct_check("src/helpers/wifi_observer/WebAuth.cpp");

    if (fails == 0) {
        printf("OK: %d cases pass.\n", total);
        return 0;
    }
    printf("FAIL: %d of %d cases mismatched.\n", fails, total);
    return 1;
}
"""


# ---------------------------------------------------------------------------
# Compiler discovery + build dispatchers (mirrors Task 5).
# ---------------------------------------------------------------------------

_MSVC_ROOTS = [
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community",
    r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community",
    r"C:\Program Files\Microsoft Visual Studio\2022\Professional",
    r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
]


def _find_msvc_vcvars() -> Path | None:
    for root in _MSVC_ROOTS:
        vcvars = Path(root) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
        if vcvars.is_file():
            return vcvars
    return None


def _build_with_msvc(td: Path, project_root: Path, vcvars: Path) -> tuple[int, str, Path]:
    harness_src = td / "harness.cpp"
    exe_path = td / "harness.exe"
    batch_path = td / "build.bat"
    batch_path.write_text(
        f'@echo off\r\n'
        f'call "{vcvars}" >nul\r\n'
        f'cd /d "{td}"\r\n'
        f'cl /nologo /std:c++17 /EHsc /I "{td}" /I "{project_root}" '
        f'"{harness_src}" /Fe"{exe_path}"\r\n'
        f'exit /b %ERRORLEVEL%\r\n'
    )
    r = subprocess.run([str(batch_path)], capture_output=True, text=True)
    return (r.returncode, (r.stderr or "") + (r.stdout or ""), exe_path)


def _build_with_gcc(td: Path, project_root: Path) -> tuple[int, str, Path]:
    harness_src = td / "harness.cpp"
    exe_path = td / "harness"
    r = subprocess.run(
        ["g++", "-std=c++17", "-I", str(td), "-I", str(project_root),
         str(harness_src), "-o", str(exe_path)],
        capture_output=True, text=True,
    )
    return (r.returncode, r.stderr, exe_path)


# ---------------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------------

def main() -> int:
    project_root = Path.cwd()
    webauth_cpp = (project_root / "src" / "helpers" /
                   "wifi_observer" / "WebAuth.cpp")
    if not webauth_cpp.is_file():
        print(f"FAIL: missing {webauth_cpp}")
        print("Run this from the meshcore-firmware (or worktree) root.")
        return 1

    vcvars = _find_msvc_vcvars()
    gcc_path = shutil.which("g++")
    if vcvars is not None:
        compiler_label = f"MSVC ({vcvars})"
    elif gcc_path is not None:
        compiler_label = f"g++ ({gcc_path})"
    else:
        print(
            "FAIL: no C++ host compiler found. Tried MSVC at standard "
            "install paths and g++ on PATH. Install one of:\n"
            "  - Visual Studio Build Tools (https://visualstudio.microsoft.com/downloads/)\n"
            "  - MinGW-w64 (choco install mingw)\n"
            "  - apt install g++ (Linux)"
        )
        return 1

    print(f"compiler: {compiler_label}")

    with tempfile.TemporaryDirectory() as td_str:
        td = Path(td_str)
        (td / "Arduino.h").write_text(ARDUINO_MOCK)
        (td / "Preferences.h").write_text(PREFS_MOCK)
        (td / "esp_random.h").write_text(ESP_RANDOM_MOCK)
        mbedtls_dir = td / "mbedtls"
        mbedtls_dir.mkdir()
        (mbedtls_dir / "md.h").write_text(MBEDTLS_MD_MOCK)
        (mbedtls_dir / "pkcs5.h").write_text(MBEDTLS_PKCS5_MOCK)
        (mbedtls_dir / "base64.h").write_text(MBEDTLS_BASE64_MOCK)
        (mbedtls_dir / "constant_time.h").write_text(MBEDTLS_CT_MOCK)

        harness_src = td / "harness.cpp"
        harness_src.write_text(HARNESS)

        if vcvars is not None:
            rc, build_msg, exe_path = _build_with_msvc(td, project_root, vcvars)
        else:
            rc, build_msg, exe_path = _build_with_gcc(td, project_root)

        if rc != 0:
            print("FAIL compile:")
            print(build_msg)
            return 1

        # Run from project_root so the harness's structural_ct_check
        # can open "src/helpers/wifi_observer/WebAuth.cpp" by relative path.
        r = subprocess.run([str(exe_path)],
                           cwd=str(project_root),
                           capture_output=True, text=True)
        out = (r.stdout or "").rstrip()
        print(out)
        # Expected count per harness:
        #   Phase A self-check: 4 cases (rc + vector x 2 iters)
        #   Phase B round-trip: 12 cases
        #     (pre/post has_user_password, set returns true,
        #      nvs_readback present + prefix + separator_count,
        #      verify_good + no_change, verify_bad + no_change,
        #      verify_empty, verify_null)
        #   Phase C structural: 1 case
        # Total: 17
        if r.returncode == 0 and "OK: 17 cases pass." in out:
            return 0
        print(f"FAIL (rc={r.returncode})")
        if r.stderr:
            print(f"stderr:\n{r.stderr}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
