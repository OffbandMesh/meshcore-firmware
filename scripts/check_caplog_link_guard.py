#!/usr/bin/env python3
"""Guard: caplog forward never controls the WiFi link (#1045).

A log-forwarding diagnostic may depend on a WiFi link. It must never bring one
up, tear one down, or extend or shorten one. The first implementation did both:
arming set the persistent-WiFi deadline and disarming cleared it, overriding a
link the operator had set up. This guard fails the build if any such call comes
back into a forward code path.

Scanned, with comments blanked first:
  - examples/simple_repeater/main.cpp: the bodies of the role hooks
    caplogForwarder() and caplogForwardLinkUp() (#1060) and of
    wifi_telemetry_caplog_forward_service()
  - src/helpers/CommonCLI.cpp: the `caplog forward` command arm
  - src/helpers/wifi_observer/ObserverCli.cpp: the bodies of the observer's
    caplog and syslog handlers (#1194). Only those: the same file's `wifi`
    verbs are the operator's link control and are correct there.
  - src/helpers/CaplogForward.h / .cpp, CaplogUdpSink.h, CaplogForwardCli.h /
    .cpp and wifi_observer/ObserverCaplogForward.h / .cpp: the whole files,
    when they exist

A required target that cannot be found is a failure, not a pass. Otherwise a
renamed function would take the guard with it, silently.

Usage:  python scripts/check_caplog_link_guard.py [--root DIR]
Exit:   0 clean, 1 on a violation or a missing target.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_cli_dispatch import strip_comments  # noqa: E402

# Calls that bring the link up or down, or move its deadline.
LINK_CONTROL = [re.compile(p) for p in (
    r"\bwifi_telemetry_set_persistent\s*\(",
    r"\bwifi_telemetry_force_now\s*\(",
    r"\bwifi_telemetry_set_disabled\s*\(",
    r"\bwifi_telemetry_reset_state\s*\(",
    r"\bWiFi\s*\.\s*(?:begin|disconnect|reconnect|mode|setSleep|softAP|softAPdisconnect|enableSTA|enableAP)\s*\(",
    r"\besp_wifi_(?:start|stop|connect|disconnect|set_mode|set_ps|restore|init|deinit)\s*\(",
    # #1194: the observer's link. wifiBootstrap() brings STA up and drives its
    # reconnects; the config handlers behind `wifi enable|disable|clear` and
    # `set wifi.*` change it. isStaConnected() is a read and stays allowed.
    r"\bwifiBootstrap\s*\(\s*\)\s*\.\s*(?:begin|loop)\s*\(",
    r"\bhandleSetWifi(?:Enabled|Field)\s*\(",
    r"\bhandleClearWifi\s*\(",
)]

REPEATER_MAIN = os.path.join("examples", "simple_repeater", "main.cpp")
COMMON_CLI = os.path.join("src", "helpers", "CommonCLI.cpp")
OBSERVER_CLI = os.path.join("src", "helpers", "wifi_observer", "ObserverCli.cpp")
HELPER_FILES = (os.path.join("src", "helpers", "CaplogForward.h"),
                os.path.join("src", "helpers", "CaplogForward.cpp"),
                os.path.join("src", "helpers", "CaplogUdpSink.h"),
                os.path.join("src", "helpers", "CaplogForwardCli.h"),
                os.path.join("src", "helpers", "CaplogForwardCli.cpp"),
                os.path.join("src", "helpers", "wifi_observer", "ObserverCaplogForward.h"),
                os.path.join("src", "helpers", "wifi_observer", "ObserverCaplogForward.cpp"))
FORWARD_FUNCTIONS = ("caplogForwarder", "caplogForwardLinkUp", "wifi_telemetry_caplog_forward_service")
OBSERVER_FUNCTIONS = ("handleCaplog", "handleSetSyslogHost", "handleSetSyslogPort")


def match_brace(s, start):
    """`start` indexes a '{'. Return the index just past its partner, or -1."""
    depth = 0
    for i in range(start, len(s)):
        if s[i] == "{":
            depth += 1
        elif s[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    return -1


def function_body(src, name):
    """(start, end) of the body of the definition of `name`, or None.

    Matches a definition, not a call or a declaration: the parameter list must be
    followed by '{'."""
    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", src):
        open_at = m.end() - 1
        end = match_brace(src, open_at)
        if end != -1:
            return open_at, end
    return None


def command_arm(src, literal):
    """(start, end) of the braced arm whose condition compares `literal`, or None."""
    at = src.find('"' + literal + '"')
    if at == -1:
        return None
    open_at = src.find("{", at)
    if open_at == -1:
        return None
    end = match_brace(src, open_at)
    return (open_at, end) if end != -1 else None


def violations_in(src, span, path):
    """Link-control calls inside src[span], as (path, line, text) tuples."""
    start, end = span
    found = []
    region = src[start:end]
    for pat in LINK_CONTROL:
        for m in pat.finditer(region):
            line = src.count("\n", 0, start + m.start()) + 1
            found.append((path, line, m.group(0).rstrip("(").strip()))
    return found


def scan_functions(files, path, names, required, violations, missing):
    """Scan the bodies of `names` in files[path]. A missing file is a failure
    only when `required`; a missing function in a present file always is."""
    text = files.get(path)
    if text is None:
        if required:
            missing.append(path)
        return
    clean = strip_comments(text)
    for fn in names:
        span = function_body(clean, fn)
        if span is None:
            missing.append(f"{path}: {fn}()")
        else:
            violations += violations_in(clean, span, path)


def analyze(files):
    """files: {relative path: source text or None if absent}.

    Returns (violations, missing): violations as (path, line, call), missing as
    human-readable target names that could not be found."""
    violations, missing = [], []

    scan_functions(files, REPEATER_MAIN, FORWARD_FUNCTIONS, True, violations, missing)

    cli = files.get(COMMON_CLI)
    if cli is None:
        missing.append(COMMON_CLI)
    else:
        clean = strip_comments(cli)
        span = command_arm(clean, "caplog forward")
        if span is None:
            missing.append(f"{COMMON_CLI}: the \"caplog forward\" arm")
        else:
            violations += violations_in(clean, span, COMMON_CLI)

    scan_functions(files, OBSERVER_CLI, OBSERVER_FUNCTIONS, False, violations, missing)

    for path in HELPER_FILES:
        text = files.get(path)
        if text is not None:
            clean = strip_comments(text)
            violations += violations_in(clean, (0, len(clean)), path)

    return violations, missing


def read_tree(root):
    files = {}
    for path in (REPEATER_MAIN, COMMON_CLI, OBSERVER_CLI) + HELPER_FILES:
        full = os.path.join(root, path)
        if os.path.exists(full):
            with open(full, encoding="utf-8", errors="replace") as f:
                files[path] = f.read()
        else:
            files[path] = None
    return files


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    help="repository root (default: this script's repo)")
    args = ap.parse_args(argv)

    violations, missing = analyze(read_tree(args.root))
    for path, line, call in violations:
        print(f"{path}:{line}: caplog forward calls link control: {call}")
    for target in missing:
        print(f"missing target: {target} -- update this guard if it was renamed")
    if violations or missing:
        print(f"FAIL: {len(violations)} link-control call(s), {len(missing)} missing target(s)")
        return 1
    print("OK: no caplog forward path controls the WiFi link")
    return 0


if __name__ == "__main__":
    sys.exit(main())
