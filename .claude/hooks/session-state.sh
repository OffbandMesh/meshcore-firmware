#!/usr/bin/env bash
# Shim: the current preflight.sh §6 invokes `session-state.sh`, but the
# canonical implementation is session-state.py (standards#107). Dispatch to it.
# Forward-compatible: a future preflight that calls session-state.py directly
# still finds the .py alongside this shim. Crosswire/Strycher#59.
exec python3 "$(dirname "$0")/session-state.py" "$@"
