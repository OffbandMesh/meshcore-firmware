#!/usr/bin/env python3
"""#2232: every owner that drives the broker pool also reports its link.

The pool's worker task connects enabled broker slots on its own timer. Unless
the owner calls setLinkUp() every loop, the pool never connects (the gate
defaults to down) -- or, before #2232, connected before WiFi existed and
crashed the repeater at boot. So a source file that drives a pool's loop()
must also call setLinkUp() on it.

Exit 0 when every owner sets the gate, 1 otherwise. --root points the scan at
another tree (used by scripts/test_check_pool_link_gate.py).
"""
import argparse
import os
import re
import sys

# A pool owner's loop() call: the repeater's accessor, or any variable whose
# name ends in "pool" (the observer's s_pool).
LOOP_CALL = re.compile(r"\b(repeaterMqttPool\(\)|\w*pool)\s*\.\s*loop\s*\(", re.IGNORECASE)
GATE_CALL = re.compile(r"\.\s*setLinkUp\s*\(")
SCAN_DIRS = ("src", "examples")
EXTS = (".cpp", ".h", ".ino")
# The pool's own sources and its thin wrapper are not owners.
EXEMPT = ("MqttBrokerPool.cpp", "MqttBrokerPool.h", "RepeaterMqttPool.cpp", "RepeaterMqttPool.h")


def owners_missing_gate(root):
    missing = []
    for d in SCAN_DIRS:
        for base, _, files in os.walk(os.path.join(root, d)):
            for name in files:
                if not name.endswith(EXTS) or name in EXEMPT:
                    continue
                path = os.path.join(base, name)
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
                # Ignore the ring-log and other non-broker "pool" loops by
                # requiring the file to be a broker-pool user at all.
                if "MqttPool" not in text and "MqttBrokerPool" not in text:
                    continue
                if LOOP_CALL.search(text) and not GATE_CALL.search(text):
                    missing.append(os.path.relpath(path, root))
    return sorted(missing)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    args = ap.parse_args()
    missing = owners_missing_gate(args.root)
    if missing:
        for m in missing:
            print(f"FAIL: {m} drives a broker pool's loop() but never calls setLinkUp() (#2232)")
        return 1
    print("OK: every broker-pool owner reports its link (#2232)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
