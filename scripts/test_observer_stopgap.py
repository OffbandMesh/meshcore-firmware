#!/usr/bin/env python
"""
scripts/test_observer_stopgap.py

Host-runnable test for stopGapDecide (Strycher/LoRa#265). Compiles
src/helpers/wifi_observer/StopGap.cpp -- which is intentionally pure
logic with no Arduino / esp-idf dependencies -- against a small C++
driver that exercises every transition in the state machine.

Run with:  python scripts/test_observer_stopgap.py

Test coverage (every transition in StopGap.cpp's state machine MUST
be covered here; adding new states/transitions = adding tests):

  1.  First-ever boot (prev_lasted_s == 0)
  2.  Long stable boot (prev_lasted_s >= rapid_thresh_s)
  3.  First rapid boot (consecutive 0 -> 1)
  4.  Second rapid boot (1 -> 2; still below detection)
  5.  Third rapid - detection trips - AUTO_RECOVER OFF -> RapidDetected
  6.  Third rapid - detection trips - AUTO_RECOVER ON  -> AutoRecoveryAttempted
  7.  Rapid boot after recovery attempted -> OperatorPromptMode
  8.  Fatal count reached (consecutive >= fatal_count) -> OperatorPromptMode
  9.  Recovery succeeded path (long boot after recov flag set; clears flag)
  10. Threshold boundary: prev_lasted_s == rapid_thresh_s is NOT rapid (strict <)
  11. prev_lasted_s == 0 NEVER counts as rapid regardless of other state

Compiler discovery: MSVC (cl.exe via vcvars64.bat) first, then g++ on
PATH. Source is portable C++17; only the invocation differs per compiler.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


HARNESS_CPP = r"""
// Test driver for stopGapDecide(). Includes the header from the real
// source tree + links against StopGap.cpp compiled alongside this.
#include "CrashLog.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>

using namespace crosswire;

// ---------------------------------------------------------------------------
// Tiny assertion framework. PASS / FAIL are printed; final exit code = #fails.
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

static const char* state_name(StopGapState s) {
    switch (s) {
        case StopGapState::Normal:                return "Normal";
        case StopGapState::RapidDetected:         return "RapidDetected";
        case StopGapState::AutoRecoveryAttempted: return "AutoRecoveryAttempted";
        case StopGapState::OperatorPromptMode:    return "OperatorPromptMode";
    }
    return "???";
}

static void check(const char* name, StopGapDecision dec,
                  StopGapState expect_state,
                  uint32_t expect_consec,
                  uint32_t expect_recov) {
    bool ok = (dec.state == expect_state &&
               dec.consecutive_rapid_out == expect_consec &&
               dec.recovery_attempted_at_out == expect_recov);
    if (ok) {
        printf("PASS: %s\n", name);
        g_pass++;
    } else {
        printf("FAIL: %s\n", name);
        printf("  expected: state=%s consec=%u recov=0x%x\n",
               state_name(expect_state),
               (unsigned)expect_consec,
               (unsigned)expect_recov);
        printf("  got:      state=%s consec=%u recov=0x%x\n",
               state_name(dec.state),
               (unsigned)dec.consecutive_rapid_out,
               (unsigned)dec.recovery_attempted_at_out);
        g_fail++;
    }
}

// Default thresholds matching the production defaults in CrashLog.h.
static StopGapDecideInputs make_inputs(uint32_t consec_in,
                                       uint32_t recov_at_in,
                                       uint32_t prev_lasted_s,
                                       uint32_t nvs_boot_count,
                                       bool auto_recover_enabled) {
    StopGapDecideInputs in;
    in.consecutive_rapid_in     = consec_in;
    in.recovery_attempted_at_in = recov_at_in;
    in.prev_boot_lasted_s       = prev_lasted_s;
    in.nvs_boot_count           = nvs_boot_count;
    in.rapid_thresh_s           = 10;
    in.rapid_count              = 3;
    in.fatal_count              = 6;
    in.auto_recover_enabled     = auto_recover_enabled;
    return in;
}

int main() {
    // Constants for readability
    const uint32_t NEVER = kStopGapRecoveryNeverAttempted;

    // ---- Test 1: First-ever boot (prev_lasted=0) is treated as NOT rapid ----
    check("01_first_ever_boot",
          stopGapDecide(make_inputs(/*consec*/0, /*recov*/NEVER,
                                    /*prev_lasted*/0, /*boot_count*/1,
                                    /*auto_recover*/true)),
          StopGapState::Normal, /*expect_consec*/0, /*expect_recov*/NEVER);

    // ---- Test 2: Long stable boot resets counter + clears recov flag ----
    check("02_long_stable_boot_clears_recov_flag",
          stopGapDecide(make_inputs(/*consec*/3, /*recov*/42,
                                    /*prev_lasted*/100, /*boot_count*/50,
                                    /*auto_recover*/true)),
          StopGapState::Normal, /*expect_consec*/0, /*expect_recov*/NEVER);

    // ---- Test 3: First rapid boot increments counter ----
    check("03_first_rapid_boot",
          stopGapDecide(make_inputs(/*consec*/0, /*recov*/NEVER,
                                    /*prev_lasted*/5, /*boot_count*/10,
                                    /*auto_recover*/true)),
          StopGapState::Normal, /*expect_consec*/1, /*expect_recov*/NEVER);

    // ---- Test 4: Second rapid boot (still below detection threshold) ----
    check("04_second_rapid_boot_below_threshold",
          stopGapDecide(make_inputs(/*consec*/1, /*recov*/NEVER,
                                    /*prev_lasted*/5, /*boot_count*/11,
                                    /*auto_recover*/true)),
          StopGapState::Normal, /*expect_consec*/2, /*expect_recov*/NEVER);

    // ---- Test 5: Detection trips with AUTO_RECOVER off -> RapidDetected ----
    check("05_detection_trips_auto_recover_off",
          stopGapDecide(make_inputs(/*consec*/2, /*recov*/NEVER,
                                    /*prev_lasted*/5, /*boot_count*/12,
                                    /*auto_recover*/false)),
          StopGapState::RapidDetected, /*expect_consec*/3, /*expect_recov*/NEVER);

    // ---- Test 6: Detection trips with AUTO_RECOVER on -> AutoRecoveryAttempted ----
    // recov_at_out should be set to nvs_boot_count (12 here).
    check("06_detection_trips_auto_recover_on",
          stopGapDecide(make_inputs(/*consec*/2, /*recov*/NEVER,
                                    /*prev_lasted*/5, /*boot_count*/12,
                                    /*auto_recover*/true)),
          StopGapState::AutoRecoveryAttempted, /*expect_consec*/3, /*expect_recov*/12);

    // ---- Test 7: Rapid boot AFTER recovery attempted -> OperatorPromptMode ----
    // recov_at is set (not NEVER), so even if consecutive < fatal_count we go to operator-prompt.
    check("07_rapid_after_recovery_operator_prompt",
          stopGapDecide(make_inputs(/*consec*/3, /*recov*/12,
                                    /*prev_lasted*/5, /*boot_count*/13,
                                    /*auto_recover*/true)),
          StopGapState::OperatorPromptMode, /*expect_consec*/4, /*expect_recov*/12);

    // ---- Test 8: Fatal count reached -> OperatorPromptMode (even without recov flag) ----
    check("08_fatal_count_reached",
          stopGapDecide(make_inputs(/*consec*/5, /*recov*/NEVER,
                                    /*prev_lasted*/5, /*boot_count*/20,
                                    /*auto_recover*/true)),
          StopGapState::OperatorPromptMode, /*expect_consec*/6, /*expect_recov*/NEVER);

    // ---- Test 9: Recovery succeeded -> long boot after recov set clears flag ----
    check("09_recovery_succeeded_clears_recov_flag",
          stopGapDecide(make_inputs(/*consec*/3, /*recov*/12,
                                    /*prev_lasted*/120, /*boot_count*/13,
                                    /*auto_recover*/true)),
          StopGapState::Normal, /*expect_consec*/0, /*expect_recov*/NEVER);

    // ---- Test 10: Threshold boundary (prev_lasted == rapid_thresh_s is NOT rapid) ----
    // rapid_thresh_s=10; prev_lasted_s=10 -> strict-less check fails -> not rapid -> Normal.
    check("10_threshold_boundary_exact_thresh_not_rapid",
          stopGapDecide(make_inputs(/*consec*/2, /*recov*/NEVER,
                                    /*prev_lasted*/10, /*boot_count*/15,
                                    /*auto_recover*/true)),
          StopGapState::Normal, /*expect_consec*/0, /*expect_recov*/NEVER);

    // ---- Test 11: prev_lasted=0 never counts as rapid even if other conditions met ----
    // Important for first-ever-boot AND for boot where NVS write of last_up_s failed.
    check("11_prev_lasted_zero_never_rapid",
          stopGapDecide(make_inputs(/*consec*/2, /*recov*/NEVER,
                                    /*prev_lasted*/0, /*boot_count*/16,
                                    /*auto_recover*/true)),
          StopGapState::Normal, /*expect_consec*/0, /*expect_recov*/NEVER);

    // ---- Summary ----
    printf("\n%s: %d pass, %d fail\n",
           g_fail == 0 ? "OK" : "FAILED",
           g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
"""


# ---------------------------------------------------------------------------
# Compiler discovery and invocation.
# ---------------------------------------------------------------------------

def find_msvc_vcvars() -> Path | None:
    """Find vcvars64.bat under any installed Visual Studio. Return None if missing."""
    bases = [
        Path(r"C:\Program Files\Microsoft Visual Studio"),
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio"),
    ]
    for base in bases:
        if not base.is_dir():
            continue
        for vs_ver in sorted(base.iterdir(), reverse=True):
            if not vs_ver.is_dir():
                continue
            for ed in vs_ver.iterdir():
                vcv = ed / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
                if vcv.is_file():
                    return vcv
    return None


def build_with_msvc(temp_dir: Path, sources: list[Path], includes: list[Path], output: Path) -> bool:
    """Compile via a .bat wrapper that calls vcvars64 then cl. cmd /c with
    multi-command strings is fragile around paths-with-spaces; a batch file
    invoked directly is reliable (matches test_observer_nvs_round_trip.py
    pattern)."""
    vcv = find_msvc_vcvars()
    if vcv is None:
        return False
    inc_args = " ".join(f'/I "{p}"' for p in includes)
    src_args = " ".join(f'"{s}"' for s in sources)
    batch = temp_dir / "build.bat"
    batch.write_text(
        f'@echo off\r\n'
        f'call "{vcv}" >nul\r\n'
        f'cl /nologo /std:c++17 /EHsc /DCROSSWIRE_OBSERVER=1 {inc_args} {src_args} '
        f'/Fe"{output}" /Fo"{temp_dir}\\\\"\r\n'
        f'exit /b %ERRORLEVEL%\r\n'
    )
    print(f"[msvc] {batch}")
    rc = subprocess.run([str(batch)], capture_output=True, text=True)
    if rc.returncode != 0:
        print(rc.stdout)
        print(rc.stderr)
    return rc.returncode == 0


def build_with_gpp(sources: list[Path], includes: list[Path], output: Path) -> bool:
    if shutil.which("g++") is None:
        return False
    inc_args = [f"-I{p}" for p in includes]
    cmd = ["g++", "-std=c++17", "-Wall", "-DCROSSWIRE_OBSERVER=1"] + inc_args + [str(s) for s in sources] + ["-o", str(output)]
    print(f"[g++] {' '.join(cmd)}")
    return subprocess.run(cmd).returncode == 0


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    src_stopgap = repo_root / "src" / "helpers" / "wifi_observer" / "StopGap.cpp"
    inc_observer = repo_root / "src" / "helpers" / "wifi_observer"
    inc_src = repo_root / "src"
    if not src_stopgap.is_file():
        print(f"ERROR: StopGap.cpp not found at {src_stopgap}")
        return 2

    with tempfile.TemporaryDirectory(prefix="test_stopgap_") as td:
        td_path = Path(td)
        harness = td_path / "harness.cpp"
        harness.write_text(HARNESS_CPP)
        output = td_path / ("test_stopgap.exe" if os.name == "nt" else "test_stopgap")

        sources = [src_stopgap, harness]
        # Define CROSSWIRE_OBSERVER so WifiObserverConfig.h's #error guard passes.
        # We pass it via the source-level #define in harness... actually it's cleaner
        # to pass as a compiler flag. For msvc /D and for g++ -D.
        # Simpler: prepend the #define to the harness so the build doesn't need a flag.
        harness.write_text("#define CROSSWIRE_OBSERVER 1\n" + HARNESS_CPP)

        includes = [inc_observer, inc_src]

        ok = build_with_msvc(td_path, sources, includes, output)
        if not ok:
            ok = build_with_gpp(sources, includes, output)
        if not ok:
            print("ERROR: no working compiler (tried MSVC vcvars64 + g++)")
            return 2

        print(f"\n[run] {output}\n")
        rc = subprocess.run([str(output)]).returncode
        return rc


if __name__ == "__main__":
    sys.exit(main())
