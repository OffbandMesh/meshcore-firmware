#!/usr/bin/env python
"""
scripts/test_observer_status_payload.py

Plan 2 v2 Task 5: golden-JSON contract test for crosswire::buildStatusJson.

Compiles MqttPayload.cpp on the host (MSVC or g++), runs the status
builder with a deterministic snapshot + ctx, captures the output, and
diffs against scripts/fixtures/plan2_status_payload_golden.json.

This is the integrity contract for the entire Plan 2 rewrite: if it
passes, the wire schema seen by analyzers (FreeKopcap, CoreScope,
LetsMesh dashboard) cannot silently drift during the upcoming MqttUplink
retirement (Task 11). Any future change to buildStatusJson must come
with an explicit fixture update -- which is loud.

Usage:
    python scripts/test_observer_status_payload.py
        --> diff mode (default). Expects fixture exists; passes if match.

    python scripts/test_observer_status_payload.py --capture-golden
        --> regenerate the fixture from current builder output.
            Use ONLY when intentionally changing the schema.

Run from project root (worktree root). The harness uses -I . to resolve
src/helpers/wifi_observer/MqttPayload.cpp.

Timestamp handling: the status JSON includes a "timestamp" field built
from time(nullptr), which varies per run. The diff redacts that field
to "<REDACTED>" in BOTH fixture and live output before comparison.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


# ---------------------------------------------------------------------------
# Test fixture: deterministic inputs that produce a deterministic output
# (modulo the timestamp, which we redact).
# ---------------------------------------------------------------------------

# Mirrors the snapshot fields used by the vendored MqttUplink in production.
# Values chosen to be obvious in the JSON output for human review.
HARNESS = r"""
// Host-side test harness for crosswire::buildStatusJson.
// We do NOT #define ARDUINO -- this exercises the host-portable branch
// of MqttPayload.cpp. CROSSWIRE_OBSERVER is required by WifiObserverConfig.h.
#define CROSSWIRE_OBSERVER 1
#include "src/helpers/wifi_observer/MqttPayload.cpp"

#include <cstdio>
#include <cstring>

int main() {
    using namespace crosswire;

    MqttStatusSnapshot snap;
    snap.battery_mv     = 4100;
    snap.uptime_secs    = 12345;
    snap.error_flags    = 0;
    snap.queue_len      = 0;
    snap.noise_floor    = -110;
    snap.tx_air_secs    = 100;
    snap.rx_air_secs    = 200;
    snap.recv_errors    = 0;
    snap.radio_freq     = 910.525000f;
    snap.radio_bw       = 250.0f;
    snap.radio_sf       = 11;
    snap.radio_cr       = 5;
    snap.repeat_enabled = false;
    snap.node_lat       = 0.0;     // Task D: position fields. loc_valid=false
    snap.node_lon       = 0.0;     // => lat/lon omitted, output byte-identical
    snap.loc_valid      = false;   // to the pre-position golden fixture.

    MqttPayloadCtx ctx;
    ctx.iata             = "CMH";
    ctx.device_id        = "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
    ctx.node_name        = "TestNode";
    ctx.topic_prefix     = "meshcore";
    ctx.client_version   = "1.0.0-test";
    ctx.firmware_version = "v1.15.0-test";
    ctx.model            = "TestManufacturer Model X";  // has spaces -> sanitized to _

    char buf[1024];
    int n = buildStatusJson(buf, sizeof(buf), snap, ctx, /*online=*/true);
    if (n <= 0) {
        fprintf(stderr, "FAIL: buildStatusJson returned %d\n", n);
        return 1;
    }
    if (static_cast<size_t>(n) >= sizeof(buf)) {
        fprintf(stderr, "FAIL: output overflowed (%d bytes)\n", n);
        return 1;
    }
    // Single-line JSON output (no trailing newline -- captured as-is for diff).
    fwrite(buf, 1, n, stdout);
    return 0;
}
"""


# ---------------------------------------------------------------------------
# Compiler discovery (re-used from test_observer_nvs_round_trip.py).
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
# Diff core
# ---------------------------------------------------------------------------

# Status JSON includes "timestamp":"YYYY-MM-DDTHH:MM:SS.000000" from
# time(nullptr) at build time. Redact to compare just the schema + values.
_TIMESTAMP_PATTERN = re.compile(r'"timestamp":"[^"]*"')


def redact_timestamp(json_str: str) -> str:
    return _TIMESTAMP_PATTERN.sub('"timestamp":"<REDACTED>"', json_str)


def build_and_run(project_root: Path) -> str:
    """Build the harness, run it, return stdout (raw JSON output)."""
    vcvars = _find_msvc_vcvars()
    gcc_path = shutil.which("g++")
    if vcvars is not None:
        compiler_label = f"MSVC ({vcvars})"
    elif gcc_path is not None:
        compiler_label = f"g++ ({gcc_path})"
    else:
        raise RuntimeError("No C++ host compiler found (tried MSVC + g++).")

    print(f"compiler: {compiler_label}", file=sys.stderr)

    with tempfile.TemporaryDirectory() as td_str:
        td = Path(td_str)
        # Empty stub headers so any transitive Arduino/Preferences include
        # is a no-op. (MqttPayload.cpp without #define ARDUINO shouldn't
        # need these, but harmless to include for robustness.)
        (td / "Arduino.h").write_text("#pragma once\n")
        (td / "Preferences.h").write_text("#pragma once\n")
        (td / "harness.cpp").write_text(HARNESS)

        if vcvars is not None:
            rc, build_msg, exe_path = _build_with_msvc(td, project_root, vcvars)
        else:
            rc, build_msg, exe_path = _build_with_gcc(td, project_root)

        if rc != 0:
            raise RuntimeError(f"compile failed:\n{build_msg}")

        r = subprocess.run([str(exe_path)], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"harness returned {r.returncode}; stderr:\n{r.stderr}")
        return r.stdout


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--capture-golden", action="store_true",
        help="Regenerate the fixture from current output. "
             "Use ONLY when intentionally changing the schema.",
    )
    args = parser.parse_args()

    project_root = Path.cwd()
    fixture_path = project_root / "scripts" / "fixtures" / "plan2_status_payload_golden.json"
    fixture_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        actual_raw = build_and_run(project_root)
    except RuntimeError as e:
        print(f"FAIL: {e}")
        return 1

    actual_redacted = redact_timestamp(actual_raw)

    if args.capture_golden:
        fixture_path.write_text(actual_redacted, encoding="utf-8")
        print(f"OK: wrote golden fixture ({len(actual_redacted)} bytes) to {fixture_path}")
        print(f"raw output:\n  {actual_raw}")
        return 0

    if not fixture_path.is_file():
        print(f"FAIL: fixture missing at {fixture_path}.")
        print("      Run with --capture-golden to generate it (one-time).")
        return 1

    expected = fixture_path.read_text(encoding="utf-8")
    if actual_redacted == expected:
        print("OK: status payload byte-matches golden fixture.")
        return 0

    # Drift detected -- print both for human review.
    print("FAIL: status payload diverged from golden fixture.")
    print(f"--- expected (fixture) ---\n{expected}")
    print(f"--- actual (redacted)  ---\n{actual_redacted}")
    print(f"--- actual (raw)       ---\n{actual_raw}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
