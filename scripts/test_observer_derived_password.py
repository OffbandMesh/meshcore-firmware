#!/usr/bin/env python
"""
scripts/test_observer_derived_password.py

Host-runnable test for WebAuth's derived initial password logic.
Compiles WebAuth.cpp host-side against stubs for Arduino / Preferences /
esp_random / mbedtls and asserts the 6 fixture cases from Plan 3 Task 5:

  1. Forward derivation     -> "1234567d4e8a"
  2. Reverse equivalence    -> "7d4e8a123456" verifies as initial password
  3. Forward verifies       -> "1234567d4e8a" verifies as initial password
  4. Off-by-one rejected    -> "1234567d4e8b" rejected (last hex char +1)
  5. Whitespace rejected    -> "123 456 7d4e8a" rejected (length mismatch)
  6. Case mismatch rejected -> "1234567D4E8A" rejected (hex side uppercase)

Fixture: BLE_PIN_CODE=123456, pubkey first three bytes = 0x7D 0x4E 0x8A
(producing the lowercase hex prefix "7d4e8a").

Run with:  python scripts/test_observer_derived_password.py

Plan 3 Task 5 (Strycher/LoRa#272). The WebAuth implementation under test
lives at src/helpers/wifi_observer/WebAuth.cpp; the production path
(everything inside `#ifdef ARDUINO`) is compiled here, with two stub
seams the plan calls out:
  - "IdentityStore" seam: webAuthInit() takes a borrowed pubkey pointer,
    so the test passes a pointer to a fixture byte array. No real
    IdentityStore involved.
  - "BLE PIN" seam: getBlePin() reads the BLE_PIN_CODE macro. The
    harness defines BLE_PIN_CODE=123456 before #include'ing WebAuth.cpp.

The PBKDF2 / mbedtls stored-hash path is never exercised: with no
"password_hash" key in the Preferences mock, webAuthHasUserPassword()
returns false and the verify path goes straight to the initial-password
branch. Mock implementations of mbedtls_md_*, mbedtls_pkcs5_*, and
mbedtls_base64_* are provided as unreachable stubs purely so the
translation unit links; only mbedtls_ct_memcmp gets real semantics
(it's the constant-time compare used on the initial-password branch).

Compiler discovery mirrors the MSVC vcvars64 + g++ fallback pattern
established in test_observer_nvs_round_trip.py and
test_observer_cli_allowlist.py.
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

# Preferences + String mock. Strict subset of the ESP32 surface that
# WebAuth.cpp actually touches: begin/end, getString, isKey, getUChar,
# putString, putUChar. Backed by a per-namespace std::map<string,string>.
# For Task 5 the table stays empty (no password_hash key) so the verify
# path's webAuthHasUserPassword() returns false and we ride the initial-
# password branch -- which is exactly what this test exercises.
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
 private:
    static inline std::map<std::string,
                           std::map<std::string, std::string>> kvs_;
    std::string ns_;
};
"""

# Arduino.h stub: just enough Serial surface for WebAuth's diagnostic
# Serial.println / Serial.printf calls. Routes to stdout so we can see
# any warnings during the test run if something goes sideways.
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

# esp_random stub. WebAuth uses esp_random() only in webAuthSetPassword
# (salt generation), which the Task 5 fixture does not invoke. The stub
# returns a constant; if anything ever calls into it, behavior is still
# deterministic.
ESP_RANDOM_MOCK = r"""
#pragma once
#include <cstdint>
static inline uint32_t esp_random() { return 0u; }
"""

# mbedtls/md.h stub. Used only in runPbkdf2(), which the Task 5 fixture
# does not invoke (the stored-hash branch is skipped because
# webAuthHasUserPassword() returns false). Stubs are sized just for the
# translation unit to link.
MBEDTLS_MD_MOCK = r"""
#pragma once
#include <cstddef>
typedef int mbedtls_md_type_t;
#define MBEDTLS_MD_SHA256 6
struct mbedtls_md_info_t {};
struct mbedtls_md_context_t { int dummy; };
static inline const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t) {
    static mbedtls_md_info_t info;
    return &info;
}
static inline void mbedtls_md_init(mbedtls_md_context_t*) {}
static inline void mbedtls_md_free(mbedtls_md_context_t*) {}
static inline int mbedtls_md_setup(mbedtls_md_context_t*,
                                   const mbedtls_md_info_t*, int) { return 0; }
"""

# mbedtls/pkcs5.h stub. PBKDF2 entry point never gets called from the
# Task 5 fixture path; returning success with a zeroed output is fine.
MBEDTLS_PKCS5_MOCK = r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "mbedtls/md.h"
static inline int mbedtls_pkcs5_pbkdf2_hmac(mbedtls_md_context_t*,
                                            const unsigned char*, size_t,
                                            const unsigned char*, size_t,
                                            unsigned int,
                                            uint32_t key_len,
                                            unsigned char* out) {
    if (out && key_len) memset(out, 0, key_len);
    return 0;
}
"""

# mbedtls/base64.h stub. Same rationale: only walked in the stored-hash
# write and load paths, neither of which fires for the Task 5 fixture.
MBEDTLS_BASE64_MOCK = r"""
#pragma once
#include <cstddef>
static inline int mbedtls_base64_encode(unsigned char*, size_t, size_t* olen,
                                        const unsigned char*, size_t) {
    if (olen) *olen = 0;
    return 0;
}
static inline int mbedtls_base64_decode(unsigned char*, size_t, size_t* olen,
                                        const unsigned char*, size_t) {
    if (olen) *olen = 0;
    return 0;
}
"""

# mbedtls/constant_time.h stub. UNLIKE the others, this one IS exercised
# by the Task 5 path -- the initial-password branch uses
# mbedtls_ct_memcmp() to compare the candidate against both derivation
# orders. Implement real memcmp semantics (return 0 on equality, non-0
# otherwise). Constant-time-ness is not under test here; correctness of
# the verdict is.
MBEDTLS_CT_MOCK = r"""
#pragma once
#include <cstddef>
#include <cstring>
static inline int mbedtls_ct_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}
"""

# Driver harness. Defines BLE_PIN_CODE before pulling in WebAuth.cpp so
# the production `#ifdef BLE_PIN_CODE` guard inside getBlePin() picks
# the active branch. Identity pubkey is a 32-byte fixture whose first
# three bytes are 0x7D 0x4E 0x8A -> hex prefix "7d4e8a".
HARNESS = r"""
// Pre-include defines that WebAuth.cpp's #ifdef chains depend on.
#define ARDUINO 1
#define BLE_PIN_CODE 123456

// Compile WebAuth.cpp directly. Doing it via #include (rather than
// linking a separate TU) lets us keep all the stub plumbing local to
// this harness without leaking it into other host tests.
#include "src/helpers/wifi_observer/WebAuth.cpp"

#include <cstdio>
#include <cstring>

// Fixture pubkey: 32 bytes, first three are 0x7D 0x4E 0x8A so the
// lowercase-hex prefix WebAuth derives ("7d4e8a") matches the plan
// spec. Remaining 29 bytes are arbitrary -- WebAuth only reads the
// first three.
static const uint8_t kFixturePubkey[32] = {
    0x7d, 0x4e, 0x8a,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
    0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
};

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

int main() {
    using namespace crosswire;

    // Wire the borrowed-pubkey seam. From here on, WebAuth reads the
    // fixture's first 3 bytes when it needs the hex prefix.
    webAuthInit(kFixturePubkey);

    // Case 1: forward derivation matches "pin + pk6" = "1234567d4e8a".
    char forward[16] = {0};
    bool derived_ok = webAuthDeriveInitialPassword(forward, sizeof(forward));
    if (!derived_ok) {
        printf("[FAIL] derive returned false\n");
        ++fails;
        ++total;
    } else {
        check_str("forward_derivation", forward, "1234567d4e8a");
    }

    // Case 2: verify the reverse-order candidate "pk6 + pin" =
    // "7d4e8a123456" -- WebAuth accepts either order on the initial-
    // password path.
    bool require_change = false;
    bool ok_reverse = webAuthVerifyPassword("7d4e8a123456", &require_change);
    check("reverse_accepted", ok_reverse, true);
    check("reverse_requires_change", require_change, true);

    // Case 3: verify the forward-order candidate.
    require_change = false;
    bool ok_forward = webAuthVerifyPassword("1234567d4e8a", &require_change);
    check("forward_accepted", ok_forward, true);
    check("forward_requires_change", require_change, true);

    // Case 4: off-by-one on the trailing hex character is rejected.
    // "...8a" -> "...8b". Same length, same prefix, last byte differs.
    require_change = false;
    bool ok_offbyone = webAuthVerifyPassword("1234567d4e8b", &require_change);
    check("offbyone_rejected", ok_offbyone, false);
    check("offbyone_no_requires_change", require_change, false);

    // Case 5: whitespace-injected candidate is rejected. WebAuth length-
    // checks before the constant-time compare; "123 456 7d4e8a" is 14
    // chars vs the expected 12, so it falls through to "no match" and
    // does NOT request a password change.
    require_change = false;
    bool ok_whitespace = webAuthVerifyPassword("123 456 7d4e8a", &require_change);
    check("whitespace_rejected", ok_whitespace, false);
    check("whitespace_no_requires_change", require_change, false);

    // Case 6: case mismatch on the hex half is rejected. WebAuth emits
    // the prefix as lowercase ("7d4e8a"); a candidate with uppercase
    // hex ("1234567D4E8A") must NOT verify. Catches any accidental
    // case-insensitive comparison.
    require_change = false;
    bool ok_case = webAuthVerifyPassword("1234567D4E8A", &require_change);
    check("case_mismatch_rejected", ok_case, false);
    check("case_mismatch_no_requires_change", require_change, false);

    // Spec calls for 6 fixture cases; the harness emits 11 individual
    // assertions (1 string equality + 5 bool-plus-requires-change pairs)
    // because each verify-path case carries the "did we incorrectly
    // request a password change?" cross-check alongside its main accept
    // / reject verdict. Print both counts so the runner's expected-line
    // matcher stays stable even if we tighten or loosen this in future.
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
# test_observer_cli_allowlist.py and test_observer_nvs_round_trip.py.
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
    """Locate vcvars64.bat that sets up MSVC environment."""
    for root in _MSVC_ROOTS:
        vcvars = Path(root) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
        if vcvars.is_file():
            return vcvars
    return None


def _build_with_msvc(td: Path, project_root: Path, vcvars: Path) -> tuple[int, str, Path]:
    """Compile the harness with MSVC. Returns (rc, msg, exe_path)."""
    harness_src = td / "harness.cpp"
    exe_path = td / "harness.exe"
    batch_path = td / "build.bat"
    # cd into the temp dir so cl drops intermediate .obj files there
    # rather than the project root. /I {td} first puts our stub headers
    # ahead of any system mbedtls / Arduino install on the include path.
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
    """Compile the harness with g++. Returns (rc, stderr, exe_path)."""
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
    # Run from project root so the harness's `#include "src/helpers/..."`
    # resolves via the `/I project_root` include path.
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
        # Stub headers go in td and the harness's -I {td} puts them
        # ahead of any system Arduino / mbedtls install on the include
        # path. WebAuth.cpp #include's <Arduino.h>, <Preferences.h>,
        # <esp_random.h>, <mbedtls/md.h>, <mbedtls/pkcs5.h>,
        # <mbedtls/base64.h>, <mbedtls/constant_time.h> from inside its
        # #ifdef ARDUINO block, so all of those need to resolve.
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

        r = subprocess.run([str(exe_path)], capture_output=True, text=True)
        out = (r.stdout or "").rstrip()
        # Echo full per-case output for visibility.
        print(out)
        # Expect 11 individual checks across the 6 spec cases (see
        # comment in the harness for why 11 not 6).
        if r.returncode == 0 and "OK: 11 cases pass." in out:
            return 0
        print(f"FAIL (rc={r.returncode})")
        if r.stderr:
            print(f"stderr:\n{r.stderr}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
