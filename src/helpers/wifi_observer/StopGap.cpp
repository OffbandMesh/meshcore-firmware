// src/helpers/wifi_observer/StopGap.cpp
//
// PURE LOGIC for stop-gap rapid-reboot detection (Strycher/LoRa#265).
// No I/O, no Arduino, no Preferences. Compiles for ARDUINO and host builds.
//
// Lives in its own .cpp file (separate from CrashLog.cpp's ARDUINO-only I/O
// wrapper) so host-runnable tests can compile + link this without dragging
// in arduino-esp32 / esp-idf headers.
//
// State machine (full spec):
//
//   inputs (from NVS at boot + this boot's runtime context + thresholds):
//     consecutive_rapid_in, recovery_attempted_at_in
//     prev_boot_lasted_s, nvs_boot_count
//     rapid_thresh_s, rapid_count, fatal_count, auto_recover_enabled
//
//   is_rapid := (prev_boot_lasted_s > 0  AND  prev_boot_lasted_s < rapid_thresh_s)
//
//   if NOT is_rapid:
//       consecutive_out = 0
//       recov_at_out    = kStopGapRecoveryNeverAttempted   // clear
//       state           = Normal
//   else:
//       consecutive_out = consecutive_rapid_in + 1
//       recov_at_out    = recovery_attempted_at_in         // unchanged (default)
//       if consecutive_out < rapid_count:
//           state = Normal
//       elif (consecutive_out >= fatal_count) OR (recovery_attempted_at_in != never):
//           state = OperatorPromptMode
//       elif auto_recover_enabled:
//           recov_at_out = nvs_boot_count                  // mark as attempted
//           state        = AutoRecoveryAttempted
//       else:
//           state = RapidDetected
//
// Invariants enforced by this function:
//   - consecutive_rapid_in == 0 on a non-rapid boot is preserved (no-op write)
//   - recovery_attempted_at marker is ALWAYS cleared on a non-rapid boot
//     (signaling "this boot ran long enough that whatever was wedging is gone")
//   - recovery_attempted_at_out is set to nvs_boot_count ONLY on the boot
//     where AutoRecoveryAttempted is decided; later boots inherit the value
//     until the next non-rapid boot clears it
//
// Test coverage (see scripts/test_observer_stopgap.py): every transition
// in this state machine is exercised with an asserted (state, consecutive_out,
// recov_at_out) tuple. New states or transitions added here MUST add tests.

#include "CrashLog.h"

namespace crosswire {

StopGapDecision stopGapDecide(const StopGapDecideInputs& in) {
    bool is_rapid_boot = (in.prev_boot_lasted_s > 0 &&
                          in.prev_boot_lasted_s < in.rapid_thresh_s);

    uint32_t consecutive = in.consecutive_rapid_in;
    uint32_t recov_at    = in.recovery_attempted_at_in;

    if (is_rapid_boot) {
        consecutive++;
    } else {
        // Non-rapid boot: reset both counter and the "recovery attempted" flag.
        // The previous recovery (if any) either worked or was unnecessary; in
        // either case we're stable now and can clear forward-looking state.
        consecutive = 0;
        recov_at    = kStopGapRecoveryNeverAttempted;
    }

    StopGapDecision dec;
    dec.consecutive_rapid_out     = consecutive;
    dec.recovery_attempted_at_out = recov_at;

    // Detection threshold not yet tripped.
    if (consecutive < in.rapid_count) {
        dec.state = StopGapState::Normal;
        return dec;
    }

    // Detection tripped. Pick branch.
    bool already_attempted = (in.recovery_attempted_at_in != kStopGapRecoveryNeverAttempted);
    bool fatal_reached     = (consecutive >= in.fatal_count);

    if (fatal_reached || already_attempted) {
        dec.state = StopGapState::OperatorPromptMode;
        return dec;
    }

    if (in.auto_recover_enabled) {
        // Mark that recovery is being attempted at this boot, so the post-
        // restart boot sees already_attempted=true and falls through to
        // OperatorPromptMode if it's still in rapid-reboot.
        dec.recovery_attempted_at_out = in.nvs_boot_count;
        dec.state = StopGapState::AutoRecoveryAttempted;
    } else {
        dec.state = StopGapState::RapidDetected;
    }
    return dec;
}

}  // namespace crosswire
