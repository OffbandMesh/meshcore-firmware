#!/usr/bin/env python
"""
scripts/test_observer_cli_allowlist.py

Host-runnable test for CliPassthrough's allowlist gate. Compiles
CliPassthrough.cpp + a driver harness + a stub for the two symbols
CliPassthrough forward-declares (dispatchObserverCli + wifiObserverPool)
and asserts the 16 allowlist cases from the Plan 3 Task 4 spec.

Run with:  python scripts/test_observer_cli_allowlist.py

Plan 3 Task 4 (Strycher/LoRa#272). The driver exercises
cliPassthroughIsAllowed(...) directly -- it never invokes the
dispatcher, so the dispatchObserverCli stub only needs to link
(its body returns false unconditionally). The MqttBrokerPool stub
is an empty class that wifiObserverPool() hands a reference to.

Pattern note: CliPassthrough.cpp forward-declares the two symbols it
needs from the rest of wifi_observer, so it has no Arduino /
esp-idf header dependencies. The host test just provides linkable
definitions; no header substitution or define-juggling required.

Compiler discovery: tries MSVC (Visual Studio Build Tools cl.exe)
first via vcvars64.bat invocation, then falls back to g++ on PATH.
Matches the pattern established in
scripts/test_observer_nvs_round_trip.py.
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

# Linkable stub definitions for the two symbols CliPassthrough.cpp
# forward-declares. Body of dispatchObserverCli is "return false" so
# the dispatch path goes to the "unknown:" branch -- but the allowlist
# test never reaches it; the stub is here for the linker only.
STUB_DEFS_CPP = r"""
#include <cstddef>
namespace crosswire {
class MqttBrokerPool {};
bool dispatchObserverCli(const char* /*cmd*/, char* /*reply*/,
                         std::size_t /*reply_size*/,
                         MqttBrokerPool& /*pool*/) {
    return false;
}
MqttBrokerPool& wifiObserverPool() {
    static MqttBrokerPool inst;
    return inst;
}
}  // namespace crosswire
"""

# Driver: 16 spec cases, PASS/FAIL per case, OK summary on full pass.
HARNESS = r"""
#include "src/helpers/wifi_observer/CliPassthrough.h"
#include <cstdio>
#include <cstring>

struct Case { const char* line; bool expect_allowed; };

static const Case kCases[] = {
    {"get mqtt.iata",                true},
    {"set mqtt.broker.0.url x.com",  true},
    {"set wifi.ssid tsunami",        true},
    {"get wifi.rssi",                true},
    {"  set mqtt.iata CMH",          true},   // whitespace-tolerant
    {"reboot",                       false},
    {"set reboot",                   false},  // banned tail token
    {"set fs.format",                false},
    {"!ls",                          false},
    {"$(rm -rf /)",                  false},
    {"`whoami`",                     false},
    {"set ota.enable 1",             false},
    {"format",                       false},
    {"cat /etc/passwd",              false},
    {"rm -rf",                       false},
    {"get factory.reset",            false},
};

int main() {
    int n = (int)(sizeof(kCases)/sizeof(kCases[0]));
    int fails = 0;
    for (int i = 0; i < n; ++i) {
        bool got = crosswire::cliPassthroughIsAllowed(kCases[i].line);
        const char* tag = (got == kCases[i].expect_allowed) ? "PASS" : "FAIL";
        if (got != kCases[i].expect_allowed) ++fails;
        printf("[%s] case %2d: allowed=%d expect=%d  line=%s\n",
               tag, i, got ? 1 : 0,
               kCases[i].expect_allowed ? 1 : 0,
               kCases[i].line);
    }
    if (fails == 0) {
        printf("OK: %d allowlist cases pass.\n", n);
        return 0;
    }
    printf("FAIL: %d of %d cases mismatched.\n", fails, n);
    return 1;
}
"""


# ---------------------------------------------------------------------------
# Compiler discovery + build dispatchers.
# ---------------------------------------------------------------------------

# Known MSVC install roots (most -> least common on dev boxes).
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


def _build_with_msvc(td: Path, project_root: Path, vcvars: Path,
                     sources: list[Path]) -> tuple[int, str, Path]:
    """Compile + link sources with MSVC. Returns (rc, msg, exe_path)."""
    exe_path = td / "harness.exe"
    src_args = " ".join(f'"{s}"' for s in sources)
    batch_path = td / "build.bat"
    # cl writes .obj files to the process CWD by default, which
    # would leave stray .obj files in the project root. Two fixes:
    #   (a) cd to the temp dir before invoking cl, so intermediates
    #       land there
    #   (b) /Fe with a full path keeps the .exe in the temp dir too
    # The path-with-trailing-backslash form of /Fo is brittle (the
    # backslash escapes the closing quote in MSVC's flag parser);
    # cd'ing into the temp dir is the simplest workaround.
    batch_path.write_text(
        f'@echo off\r\n'
        f'call "{vcvars}" >nul\r\n'
        f'cd /d "{td}"\r\n'
        # /nologo silences banner. /EHsc enables std C++ exceptions.
        # /std:c++17 matches the plan + the rest of the project.
        f'cl /nologo /std:c++17 /EHsc /I "{project_root}" '
        f'{src_args} /Fe"{exe_path}"\r\n'
        f'exit /b %ERRORLEVEL%\r\n'
    )
    r = subprocess.run([str(batch_path)], capture_output=True, text=True)
    return (r.returncode, (r.stderr or "") + (r.stdout or ""), exe_path)


def _build_with_gcc(td: Path, project_root: Path,
                    sources: list[Path]) -> tuple[int, str, Path]:
    """Compile + link sources with g++. Returns (rc, msg, exe_path)."""
    exe_path = td / "harness"
    cmd = ["g++", "-std=c++17", "-I", str(project_root)]
    cmd += [str(s) for s in sources]
    cmd += ["-o", str(exe_path)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return (r.returncode, r.stderr, exe_path)


# ---------------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------------

def main() -> int:
    # Run from project root so the harness's `#include "src/helpers/..."`
    # resolves via the `/I project_root` include path.
    project_root = Path.cwd()
    cli_passthrough_cpp = (project_root / "src" / "helpers" /
                           "wifi_observer" / "CliPassthrough.cpp")
    if not cli_passthrough_cpp.is_file():
        print(f"FAIL: missing {cli_passthrough_cpp}")
        print("Run this from the meshcore-firmware (or worktree) root.")
        return 1

    # Pick a compiler: MSVC preferred, g++ fallback.
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
        harness_src.write_text(HARNESS)
        stubs_src = td / "stubs.cpp"
        stubs_src.write_text(STUB_DEFS_CPP)

        sources = [harness_src, stubs_src, cli_passthrough_cpp]

        if vcvars is not None:
            rc, build_msg, exe_path = _build_with_msvc(
                td, project_root, vcvars, sources)
        else:
            rc, build_msg, exe_path = _build_with_gcc(
                td, project_root, sources)

        if rc != 0:
            print("FAIL compile:")
            print(build_msg)
            return 1

        r = subprocess.run([str(exe_path)], capture_output=True, text=True)
        out = (r.stdout or "").rstrip()
        # Echo full per-case output for visibility.
        print(out)
        if r.returncode == 0 and "OK: 16 allowlist cases pass." in out:
            return 0
        print(f"FAIL (rc={r.returncode})")
        if r.stderr:
            print(f"stderr:\n{r.stderr}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
