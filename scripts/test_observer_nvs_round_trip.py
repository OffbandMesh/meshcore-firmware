#!/usr/bin/env python
"""
scripts/test_observer_nvs_round_trip.py

Host-runnable test for ConfigSchema. Compiles ConfigSchema.cpp
against an in-memory Preferences mock and asserts every BrokerConfig
field round-trips through writeBrokerConfig + readBrokerConfig. Also
verifies status_interval clamps to [10, 3600] regardless of input.

Run with:  python scripts/test_observer_nvs_round_trip.py

Plan 2 Task 2 (Strycher/LoRa#244). TDD anchor for ConfigSchema --
verifies the schema round-trips BEFORE any consumer of it exists.

Compiler discovery: tries MSVC (Visual Studio Build Tools cl.exe)
first via vcvars64.bat invocation, then falls back to g++ on PATH.
The C++ source itself is portable (no GNU or MSVC extensions); only
the build invocation differs.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


# ---------------------------------------------------------------------------
# Sources written into the temp dir at build time.
# ---------------------------------------------------------------------------

PREFS_MOCK = r"""
// Minimal Preferences mock matching the ESP32 API surface used by
// ConfigSchema.cpp. Backed by a per-namespace std::map<string,string>.
#pragma once
#include <map>
#include <string>
#include <cstring>
#include <cstdint>

class String {
 public:
    String() = default;
    String(const char* s) : s_(s ? s : "") {}
    const char* c_str() const { return s_.c_str(); }
    bool isEmpty() const { return s_.empty(); }
    operator std::string() const { return s_; }
    std::string s_;
};

class Preferences {
 public:
    bool begin(const char* ns, bool /*ro*/) { ns_ = ns; return true; }
    void end() {}
    bool clear() { kvs_[ns_].clear(); return true; }
    bool   putBool   (const char* k, bool v)        { kvs_[ns_][k] = v ? "1":"0"; return true; }
    bool   getBool   (const char* k, bool def)      { auto& m = kvs_[ns_]; auto it=m.find(k); return it==m.end()?def:(it->second=="1"); }
    size_t putString (const char* k, const char* v) { kvs_[ns_][k] = v; return strlen(v); }
    String getString (const char* k, const char* def) { auto& m = kvs_[ns_]; auto it=m.find(k); return String(it==m.end()?def:it->second.c_str()); }
    size_t putUShort (const char* k, uint16_t v)    { kvs_[ns_][k] = std::to_string(v); return 2; }
    uint16_t getUShort(const char* k, uint16_t def) { auto& m = kvs_[ns_]; auto it=m.find(k); return it==m.end()?def:(uint16_t)std::stoi(it->second); }
    size_t putUChar  (const char* k, uint8_t v)     { kvs_[ns_][k] = std::to_string(v); return 1; }
    uint8_t getUChar (const char* k, uint8_t def)   { auto& m = kvs_[ns_]; auto it=m.find(k); return it==m.end()?def:(uint8_t)std::stoi(it->second); }
    size_t putULong  (const char* k, uint32_t v)    { kvs_[ns_][k] = std::to_string(v); return 4; }
    uint32_t getULong(const char* k, uint32_t def)  { auto& m = kvs_[ns_]; auto it=m.find(k); return it==m.end()?def:(uint32_t)std::stoul(it->second); }
 private:
    static inline std::map<std::string, std::map<std::string,std::string>> kvs_;
    std::string ns_;
};
"""

HARNESS = r"""
// Define BOTH ARDUINO (so ConfigSchema.cpp picks the real branch) AND
// CROSSWIRE_OBSERVER (so the transitive include of WifiObserverConfig.h
// doesn't trip its "missing define" #error guard). Both defines must
// precede every include.
#define ARDUINO 1
#define CROSSWIRE_OBSERVER 1
#include "prefs_mock.h"
#include "src/helpers/wifi_observer/ConfigSchema.cpp"

#include <cstdio>
#include <cstring>

int main() {
    using namespace crosswire;

    // Fixture: realistic broker config in slot 2 (incl Plan 2 v2 fields).
    BrokerConfig wrote;
    wrote.enabled = true;
    strcpy(wrote.url, "mqtt.example.org");
    wrote.port = 8883;
    wrote.transport = BrokerTransport::Tls;
    wrote.auth_type = BrokerAuthType::Basic;
    strcpy(wrote.username, "observer-bench");
    strcpy(wrote.password, "p4ssw0rd-with-symbols!@#");
    strcpy(wrote.topic_prefix, "meshcore");
    strcpy(wrote.iata_override, "CMH");
    // Plan 2 v2 fields
    strcpy(wrote.jwt_audience, "https://test.example.org");
    wrote.jwt_refresh_sec = 1800;  // 30 min
    strcpy(wrote.ca_cert_name, "letsencrypt");

    if (!writeBrokerConfig(2, wrote)) { puts("FAIL write"); return 1; }

    BrokerConfig back;
    if (!readBrokerConfig(2, back)) { puts("FAIL read"); return 1; }

    #define CHK_STR(field)   if (strcmp(wrote.field, back.field) != 0) { printf("FAIL %s: '%s' != '%s'\n", #field, wrote.field, back.field); return 1; }
    #define CHK_INT(field)   if ((int)wrote.field != (int)back.field) { printf("FAIL %s: %d != %d\n", #field, (int)wrote.field, (int)back.field); return 1; }
    #define CHK_U32(field)   if ((uint32_t)wrote.field != (uint32_t)back.field) { printf("FAIL %s: %u != %u\n", #field, (uint32_t)wrote.field, (uint32_t)back.field); return 1; }

    CHK_INT(enabled); CHK_STR(url); CHK_INT(port); CHK_INT(transport);
    CHK_INT(auth_type); CHK_STR(username); CHK_STR(password);
    CHK_STR(topic_prefix); CHK_STR(iata_override);
    // Plan 2 v2 round-trip asserts
    CHK_STR(jwt_audience); CHK_U32(jwt_refresh_sec); CHK_STR(ca_cert_name);

    // Also verify global keys.
    writeGlobalIata("CMH");
    char iata[8];
    if (!readGlobalIata(iata, sizeof(iata)) || strcmp(iata, "CMH") != 0) {
        printf("FAIL global iata: '%s'\n", iata);
        return 1;
    }

    writeStatusIntervalSec(60);
    if (readStatusIntervalSec() != 60) { puts("FAIL status interval"); return 1; }

    // Clamp test: out-of-range write -> read clamped.
    writeStatusIntervalSec(99999);
    if (readStatusIntervalSec() != kMaxStatusIntervalSec) {
        printf("FAIL clamp high: %u\n", readStatusIntervalSec());
        return 1;
    }
    writeStatusIntervalSec(1);
    if (readStatusIntervalSec() != kMinStatusIntervalSec) {
        printf("FAIL clamp low: %u\n", readStatusIntervalSec());
        return 1;
    }

    // -----------------------------------------------------------------------
    // populateDefaultBrokers — Plan 2 v2 Task 3 Step 5
    // -----------------------------------------------------------------------
    // Slot 2 was just written above with a custom URL ("mqtt.example.org").
    // Slots 0 + 1 are still virgin. populateDefaultBrokers should:
    //   - fill slot 0 with EastMesh wss URL
    //   - fill slot 1 with LetsMesh-EU wss URL
    //   - LEAVE slot 2 alone (already has user-set URL)

    populateDefaultBrokers();

    BrokerConfig slot0, slot1, slot2;
    if (!readBrokerConfig(0, slot0)) { puts("FAIL read slot 0"); return 1; }
    if (!readBrokerConfig(1, slot1)) { puts("FAIL read slot 1"); return 1; }
    if (!readBrokerConfig(2, slot2)) { puts("FAIL read slot 2"); return 1; }

    if (strcmp(slot0.url, "wss://mqtt2.eastmesh.au:443/mqtt") != 0) {
        printf("FAIL slot 0 url after populate: '%s'\n", slot0.url);
        return 1;
    }
    if (strcmp(slot0.jwt_audience, "https://mqtt2.eastmesh.au") != 0) {
        printf("FAIL slot 0 jwt_audience: '%s'\n", slot0.jwt_audience);
        return 1;
    }
    if (slot0.transport != BrokerTransport::Wss) {
        printf("FAIL slot 0 transport: %d\n", (int)slot0.transport);
        return 1;
    }
    if (slot0.auth_type != BrokerAuthType::Jwt) {
        printf("FAIL slot 0 auth_type: %d\n", (int)slot0.auth_type);
        return 1;
    }
    if (slot0.enabled) {
        // Defaults must ship DISABLED so user explicitly opts in after
        // configuring owner identity for the JWT claim.
        printf("FAIL slot 0 should default to disabled\n");
        return 1;
    }
    if (strcmp(slot1.url, "wss://mqtt-eu-v1.letsmesh.net:443/mqtt") != 0) {
        printf("FAIL slot 1 url after populate: '%s'\n", slot1.url);
        return 1;
    }
    // Idempotency / no-overwrite: slot 2 STILL has the user-set URL.
    if (strcmp(slot2.url, "mqtt.example.org") != 0) {
        printf("FAIL slot 2 was overwritten by populateDefaultBrokers: '%s'\n", slot2.url);
        return 1;
    }
    if (strcmp(slot2.jwt_audience, "https://test.example.org") != 0) {
        printf("FAIL slot 2 jwt_audience was overwritten: '%s'\n", slot2.jwt_audience);
        return 1;
    }

    // Idempotency: call populateDefaultBrokers twice -> slot 0 still has same value.
    populateDefaultBrokers();
    BrokerConfig slot0_again;
    if (!readBrokerConfig(0, slot0_again)) { puts("FAIL re-read slot 0"); return 1; }
    if (strcmp(slot0_again.url, slot0.url) != 0) {
        printf("FAIL populateDefaultBrokers not idempotent: '%s' vs '%s'\n",
               slot0_again.url, slot0.url);
        return 1;
    }

    puts("OK");
    return 0;
}
"""


# ---------------------------------------------------------------------------
# Compiler discovery + build dispatchers.
# ---------------------------------------------------------------------------

# Known MSVC install roots (most → least common on dev boxes).
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
    """Compile the harness with MSVC. Returns (rc, stderr, exe_path)."""
    harness_src = td / "harness.cpp"
    exe_path = td / "harness.exe"
    # Write a batch wrapper that calls vcvars then cl. cmd doesn't bubble
    # cl's exit code by default -- explicit exit /b at the end.
    batch_path = td / "build.bat"
    batch_path.write_text(
        f'@echo off\r\n'
        f'call "{vcvars}" >nul\r\n'
        # /nologo silences the cl banner. /EHsc enables std C++ exceptions
        # (std::map etc. require it). /std:c++17 matches plan's -std=c++17.
        # /Fe specifies the output exe -- no separator between /Fe and path.
        f'cl /nologo /std:c++17 /EHsc /I "{td}" /I "{project_root}" '
        f'"{harness_src}" /Fe"{exe_path}"\r\n'
        f'exit /b %ERRORLEVEL%\r\n'
    )
    r = subprocess.run(
        [str(batch_path)],
        capture_output=True, text=True,
        # cl writes some output to stdout, errors to stderr -- capture both
    )
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

    # Pick a compiler: MSVC preferred (Windows dev boxes usually have it
    # via Visual Studio Build Tools), g++ fallback for Linux/Mac/MinGW.
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
        (td / "prefs_mock.h").write_text(PREFS_MOCK)
        (td / "harness.cpp").write_text(HARNESS)
        # Empty stub headers so ConfigSchema.cpp's `#include <Arduino.h>`
        # + `#include <Preferences.h>` (inside its #ifdef ARDUINO block)
        # resolve to no-ops. The actual String + Preferences classes come
        # from prefs_mock.h which the harness includes first. -I {td} puts
        # these stubs ahead of MSVC's INCLUDE path, so the harness builds
        # without an Arduino install.
        (td / "Arduino.h").write_text("#pragma once\n")
        (td / "Preferences.h").write_text("#pragma once\n")

        if vcvars is not None:
            rc, build_msg, exe_path = _build_with_msvc(td, project_root, vcvars)
        else:
            rc, build_msg, exe_path = _build_with_gcc(td, project_root)

        if rc != 0:
            print("FAIL compile:")
            print(build_msg)
            return 1

        r = subprocess.run([str(exe_path)], capture_output=True, text=True)
        out = (r.stdout or "").strip()
        if out.endswith("OK") and r.returncode == 0:
            print("OK: NVS round-trip + clamp tests pass.")
            return 0
        print(f"FAIL (rc={r.returncode}): {out}")
        if r.stderr:
            print(f"stderr:\n{r.stderr}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
