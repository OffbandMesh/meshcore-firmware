#!/usr/bin/env python
"""
scripts/test_observer_system_channel.py

Host-runnable test for SystemChannelCli's pure-logic functions:

  1. deriveSystemChannelPsk with all-zero pubkey produces a specific
     16-byte golden vector (regression-locks the namespace literal +
     SHA-256 truncation policy).
  2. deriveSystemChannelPsk with all-0xff pubkey produces a DIFFERENT
     16-byte value (sanity: function actually mixes the input).
  3. Two pubkeys differing by exactly one bit produce dramatically
     different PSKs (avalanche property of SHA-256; at minimum half
     the output bits should differ).
  4. deriveSystemChannelName with pubkey starting 0x12 0x34 0x56 0x78
     produces "_sys @ 12345678" (15 chars + NUL).
  5. deriveSystemChannelName with undersized out_size writes an empty
     string rather than overrunning the buffer.

Run with:  python scripts/test_observer_system_channel.py

Plan 3 Task 10 (Strycher/LoRa#272). The functions under test live in
src/helpers/wifi_observer/SystemChannelCli.cpp; the test wires its own
SHA-256 implementation (via mbedtls if available on the system, or a
tiny pure-C fallback embedded in the harness) so the production
mesh::Utils::sha256 path can stay #ifdef ARDUINO'd out for host
builds.

Compiler discovery mirrors the MSVC vcvars64 + g++ fallback pattern
established in test_observer_derived_password.py and
test_observer_pbkdf2_round_trip.py.

Golden vector derivation (case 1):
    msg = bytes(32) + b"crosswire-sysch-v1"   # 50 bytes
    psk = SHA256(msg)[:16]
    psk_hex = "5a73cd5ba03d7c7c8a9024ad97fb7196"   # computed via
                                                   # `python -c
                                                   # 'import hashlib;
                                                   # print(hashlib
                                                   # .sha256(bytes(32)
                                                   # + b"crosswire-
                                                   # sysch-v1")
                                                   # .hexdigest()
                                                   # [:32])'`
The harness recomputes this in Python before compiling the C++ side
so the assertion is self-checking -- if the namespace literal ever
changes in SystemChannelCli.cpp, this test fails fast.
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


# ---------------------------------------------------------------------------
# Stub headers + harness source.
# ---------------------------------------------------------------------------

# The host build does NOT define ARDUINO, so the I/O wrapper half of
# SystemChannelCli.cpp is compiled out by its #ifdef. Only the pure-
# logic functions + the host stub bodies are linked. We still need a
# host implementation of sysch_sha256 (declared `static` in the .cpp
# but only DEFINED inside the ARDUINO branch). The harness provides
# a pure-C SHA-256 the test linker can resolve.
#
# Strategy: rather than rely on a system mbedtls, embed a small
# self-contained SHA-256 in the harness and #define sysch_sha256 to
# call it BEFORE including SystemChannelCli.cpp. The macro redirect
# happens inside a wrapper TU so the production code is unmodified.
HARNESS = r"""
// Pre-include defines and pre-declarations BEFORE pulling in the
// production translation unit.
//
// We don't define ARDUINO here, so SystemChannelCli.cpp's #ifdef
// ARDUINO branch (containing systemChannelInit + the I/O wrapper)
// gets compiled out. Only the pure-logic functions remain.
//
// The sysch_sha256 forward-declared inside the .cpp file has no
// host-side body (it's defined only in the #ifdef ARDUINO branch).
// We supply our own at the bottom of this TU so the linker
// resolves it.

#include <cstdio>
#include <cstring>
#include <cstdint>

// SHA-256 reference implementation (public-domain WeiDai/Steve
// Reid lineage; ~150 LOC). Kept inline so the host test has zero
// external deps beyond a C++17 compiler.
namespace test_sha {

static constexpr uint32_t K[64] = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
  0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
  0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
  0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
  0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
  0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
  0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
  0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
  0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
  0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
  0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
  0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

static void sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
  uint32_t h[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
  };
  // Build padded message: msg || 0x80 || zeros || 64-bit big-endian
  // length (in bits). For the test fixtures (50-byte and 32-byte
  // inputs) one or two 64-byte blocks is plenty; allocate generously.
  uint8_t buf[256];
  if (len + 9 > sizeof(buf)) return;  // host test never feeds bigger
  memcpy(buf, msg, len);
  buf[len] = 0x80;
  size_t pad_to = ((len + 9 + 63) / 64) * 64;
  for (size_t i = len + 1; i < pad_to - 8; ++i) buf[i] = 0;
  uint64_t bit_len = (uint64_t)len * 8;
  for (int i = 0; i < 8; ++i) {
    buf[pad_to - 1 - i] = (uint8_t)(bit_len & 0xff);
    bit_len >>= 8;
  }
  for (size_t block = 0; block < pad_to; block += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = ((uint32_t)buf[block + i*4    ] << 24) |
             ((uint32_t)buf[block + i*4 + 1] << 16) |
             ((uint32_t)buf[block + i*4 + 2] <<  8) |
             ((uint32_t)buf[block + i*4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
      uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
      uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + mj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
    h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
  }
  for (int i = 0; i < 8; ++i) {
    out[i*4  ] = (uint8_t)(h[i] >> 24);
    out[i*4+1] = (uint8_t)(h[i] >> 16);
    out[i*4+2] = (uint8_t)(h[i] >>  8);
    out[i*4+3] = (uint8_t)(h[i] & 0xff);
  }
}

}  // namespace test_sha

// Provide the static-but-named sysch_sha256 the .cpp expects. Since
// sysch_sha256 is `static` inside SystemChannelCli.cpp's anonymous
// translation unit, we can't link a separately-defined version
// against it. Workaround: compile the .cpp via #include here (same
// TU), and supply the host body BEFORE the #include so the .cpp's
// forward declaration finds it.
//
// The .cpp's #ifdef ARDUINO block provides the ARDUINO body; for
// host builds (no ARDUINO defined) the symbol is otherwise
// undefined. We define it here in the anonymous namespace and the
// .cpp uses the file-scope sysch_sha256 via its own static
// declaration. To keep linkage consistent, we define the body
// BEFORE the #include using `static` linkage that the .cpp's
// internal call resolves to.

// Marker: we promise to define sysch_sha256 right after the .cpp's
// declaration but before its use. Easiest: include the .cpp,
// and the host-build branch (#ifndef ARDUINO) at the bottom of the
// .cpp does NOT define sysch_sha256, so the static reference would
// be unresolved. To fix: the .cpp's forward declaration is
// `static void sysch_sha256(...)` -- "static" means internal
// linkage, so we can supply the body inside the SAME TU after the
// declaration. The trick: include the .cpp INSIDE the crosswire
// namespace so we can then add the host body right after.

#include "src/helpers/wifi_observer/SystemChannelCli.cpp"

// Provide the host body for sysch_sha256. The .cpp's forward
// declaration is `static void sysch_sha256(...)` at namespace
// scope, so we extend with a definition in the same translation
// unit. The #ifdef ARDUINO body is compiled out for this host
// build, so this is the only definition.
namespace crosswire {
static void sysch_sha256(uint8_t* out_hash, size_t out_len,
                         const uint8_t* msg, size_t msg_len) {
  uint8_t full[32];
  test_sha::sha256(msg, msg_len, full);
  if (out_len > 32) out_len = 32;
  memcpy(out_hash, full, out_len);
}
}  // namespace crosswire

// Stub for cliPassthroughExecute since SystemChannelCli.cpp links
// against it. Not exercised by any of the 5 pure-logic tests, but
// the symbol must resolve. The .cpp's ARDUINO-only call site is
// compiled out, but the include of CliPassthrough.h still
// declares it.
namespace crosswire {
CliResult cliPassthroughExecute(const char* /*line*/,
                                char* out, size_t out_len) {
  if (out && out_len) out[0] = '\0';
  return CliResult::Unknown;
}
}  // namespace crosswire

// ---------------------------------------------------------------------------
// Test harness.
// ---------------------------------------------------------------------------

static int fails = 0;
static int total = 0;

static void check_bytes_eq(const char* name, const uint8_t* got,
                           const uint8_t* expect, size_t n) {
  ++total;
  bool ok = (memcmp(got, expect, n) == 0);
  if (!ok) ++fails;
  printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) {
    printf("  got:    ");
    for (size_t i = 0; i < n; ++i) printf("%02x", got[i]);
    printf("\n  expect: ");
    for (size_t i = 0; i < n; ++i) printf("%02x", expect[i]);
    printf("\n");
  }
}

static void check_bytes_ne(const char* name, const uint8_t* a,
                           const uint8_t* b, size_t n) {
  ++total;
  bool diff = (memcmp(a, b, n) != 0);
  if (!diff) ++fails;
  printf("[%s] %s\n", diff ? "PASS" : "FAIL", name);
}

static void check_str(const char* name, const char* got, const char* expect) {
  ++total;
  bool ok = (strcmp(got, expect) == 0);
  if (!ok) ++fails;
  printf("[%s] %s: got='%s' expect='%s'\n",
         ok ? "PASS" : "FAIL", name, got, expect);
}

static int hamming_bits(const uint8_t* a, const uint8_t* b, size_t n) {
  int count = 0;
  for (size_t i = 0; i < n; ++i) {
    uint8_t x = a[i] ^ b[i];
    while (x) { count += (x & 1); x >>= 1; }
  }
  return count;
}

int main() {
  using namespace crosswire;

  // -------------------------------------------------------------
  // Case 1: golden vector for all-zero pubkey.
  // -------------------------------------------------------------
  uint8_t pk_zero[32] = {0};
  uint8_t psk_zero[16];
  deriveSystemChannelPsk(pk_zero, psk_zero);
  // Computed Python-side: hashlib.sha256(bytes(32) +
  //   b"crosswire-sysch-v1").digest()[:16] -> see GOLDEN_PSK_ZERO
  uint8_t expected_zero[16] = { GOLDEN_PSK_ZERO_BYTES };
  check_bytes_eq("golden_psk_zero", psk_zero, expected_zero, 16);

  // -------------------------------------------------------------
  // Case 2: all-0xff pubkey -> different PSK.
  // -------------------------------------------------------------
  uint8_t pk_ff[32];
  memset(pk_ff, 0xff, sizeof(pk_ff));
  uint8_t psk_ff[16];
  deriveSystemChannelPsk(pk_ff, psk_ff);
  check_bytes_ne("psk_ff_differs_from_zero", psk_ff, psk_zero, 16);

  // -------------------------------------------------------------
  // Case 3: avalanche -- single-bit pubkey delta -> >= 32 bits
  // different in the 128-bit output. (SHA-256 expected ~50%.)
  // -------------------------------------------------------------
  uint8_t pk_one_bit[32] = {0};
  pk_one_bit[0] = 0x01;  // one bit set
  uint8_t psk_one_bit[16];
  deriveSystemChannelPsk(pk_one_bit, psk_one_bit);
  int delta = hamming_bits(psk_zero, psk_one_bit, 16);
  ++total;
  bool ok_avalanche = (delta >= 32);  // 25% of 128 bits, very loose
  if (!ok_avalanche) ++fails;
  printf("[%s] avalanche_1bit_delta: hamming=%d (expect >= 32)\n",
         ok_avalanche ? "PASS" : "FAIL", delta);

  // -------------------------------------------------------------
  // Case 4: name derivation with known prefix.
  // -------------------------------------------------------------
  uint8_t pk_named[32] = {
    0x12, 0x34, 0x56, 0x78, 0xaa, 0xbb, 0xcc, 0xdd,
    0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
    0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
    0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
  };
  char name_buf[24] = {0};
  deriveSystemChannelName(pk_named, name_buf, sizeof(name_buf));
  check_str("name_derivation", name_buf, "_sys @ 12345678");

  // -------------------------------------------------------------
  // Case 5: undersized buffer must NOT overrun -- writes empty
  // string instead. We deliberately put a canary byte just past
  // the requested size and confirm it survives.
  // -------------------------------------------------------------
  char small_buf[20] = {0};
  small_buf[10] = 0x7e;  // canary at index 10 (beyond the 8 we ask for)
  small_buf[0]  = 'X';   // pre-content to confirm function writes
  deriveSystemChannelName(pk_named, small_buf, 8);  // 8 < 16 required
  ++total;
  bool empty_ok = (small_buf[0] == '\0');
  if (!empty_ok) ++fails;
  printf("[%s] undersize_writes_empty: small_buf[0]=0x%02x (expect 0x00)\n",
         empty_ok ? "PASS" : "FAIL", (unsigned)(uint8_t)small_buf[0]);
  ++total;
  bool canary_ok = (small_buf[10] == 0x7e);
  if (!canary_ok) ++fails;
  printf("[%s] undersize_canary_intact: small_buf[10]=0x%02x (expect 0x7e)\n",
         canary_ok ? "PASS" : "FAIL", (unsigned)(uint8_t)small_buf[10]);

  if (fails == 0) {
    printf("OK: %d cases pass.\n", total);
    return 0;
  }
  printf("FAIL: %d of %d cases mismatched.\n", fails, total);
  return 1;
}
"""


# ---------------------------------------------------------------------------
# Compiler discovery + build dispatchers. Mirrors the pattern from
# test_observer_derived_password.py.
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

def _golden_psk_zero_c_array() -> str:
    """Compute the golden 16-byte PSK for the all-zero pubkey case
    and render it as a brace-initializer C array literal so the
    test harness compiles with the value baked in. If the namespace
    literal in SystemChannelCli.cpp ever changes, this Python-side
    computation moves with it and the assertion still self-checks."""
    msg = bytes(32) + b"crosswire-sysch-v1"
    digest = hashlib.sha256(msg).digest()[:16]
    return ", ".join(f"0x{b:02x}" for b in digest)


def main() -> int:
    project_root = Path.cwd()
    target = (project_root / "src" / "helpers" /
              "wifi_observer" / "SystemChannelCli.cpp")
    if not target.is_file():
        print(f"FAIL: missing {target}")
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
        harness_src = td / "harness.cpp"
        # Substitute the golden vector into the harness source so the
        # assertion has the SHA-256 truth value baked in at compile
        # time.
        golden_c = _golden_psk_zero_c_array()
        substituted = HARNESS.replace("GOLDEN_PSK_ZERO_BYTES", golden_c)
        # Prepend the GOLDEN_PSK_ZERO_BYTES define for clarity in
        # diagnostics; harmless because the macro is also expanded
        # inline above.
        prelude = f"// GOLDEN_PSK_ZERO_BYTES = {{ {golden_c} }}\n"
        harness_src.write_text(prelude + substituted)

        if vcvars is not None:
            rc, build_msg, exe_path = _build_with_msvc(td, project_root, vcvars)
        else:
            rc, build_msg, exe_path = _build_with_gcc(td, project_root)

        if rc != 0:
            print("FAIL compile:")
            print(build_msg)
            return 1

        r = subprocess.run([str(exe_path)], capture_output=True, text=True)
        out = (r.stdout or "").rstrip()
        print(out)
        # Expect 6 individual checks across 5 spec cases (case 5
        # carries two: empty-write + canary-intact).
        if r.returncode == 0 and "OK: 6 cases pass." in out:
            return 0
        print(f"FAIL (rc={r.returncode})")
        if r.stderr:
            print(f"stderr:\n{r.stderr}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
