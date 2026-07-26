#!/usr/bin/env python3
"""Redaction tests for _cap_serial.redact_line (#379).

Runnable two ways:
    pytest scripts/test_observer_log_redaction.py
    python scripts/test_observer_log_redaction.py

Every "must redact" case is grounded in an actual serial string the Offband
firmware emits, or a conservative generic pattern for the unknown gessaman box.
Every "must preserve" case is a real diagnostic line we must NOT gut.
"""

import importlib.util
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("_cap_serial", os.path.join(_HERE, "_cap_serial.py"))
_cap = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_cap)
redact_line = _cap.redact_line


# (input, must_be_absent_substring) — the secret must not survive redaction.
MUST_REDACT = [
    # WiFi SSID — Offband boot (WifiBootstrap.cpp:109)
    ("[WifiBootstrap] Saved WiFi SSID=HomeNet_5G; attempting STA.", "HomeNet_5G"),
    # WiFi SSID — CLI reply (ObserverCli.cpp:451), SSID with a space
    ("wifi.ssid = My Home Network", "My Home Network"),
    # SSID with spaces on the boot form still stops at ';'
    ("Saved WiFi SSID=Cafe Guest WiFi; attempting STA.", "Cafe Guest WiFi"),
    # JWT bearer token (JwtHelper builds header.payload.signature)
    ("mqtt connect token=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N",
     "eyJzdWIiOiIxMjM0NTY3ODkwIn0"),
    # jwt_email PII (ObserverCli.cpp:580)
    ("mqtt.broker.0.jwt_email = tester@example.com", "tester@example.com"),
    # Generic password (gessaman / defensive)
    ("wifi.pass=SuperSecret123", "SuperSecret123"),
    ("psk: hunter2hunter2", "hunter2hunter2"),
    ("mqtt_password=brokerpw!", "brokerpw!"),          # underscore-joined key
    # Multi-word secret (WiFi PSKs commonly have spaces) — regression for
    # Gemini Finding 1: the WHOLE value must go, not just the first token.
    ("wifi.psk = my secret pass phrase", "secret pass phrase"),
    ("secret = \"a token with spaces\"", "token with spaces"),
    # Dot/dash-separated key (Gemini Finding 2 hardening)
    ("device.secret=abcdef123456", "abcdef123456"),
    ("reconnect-token: aWReallyLongTokenValue", "aWReallyLongTokenValue"),
    # Label-less interactive CLI reply (Gemini Finding 5)
    ("> s3cr3t_bridge_v4lu3", "s3cr3t_bridge_v4lu3"),
]

# Diagnostic lines that must survive untouched — redaction must not gut the log.
MUST_PRESERVE = [
    "mqtt.broker.0.auth_type = jwt",                 # config, not a secret
    "auth_fail:advert",                              # RemoteCommand diagnostic
    "mqtt.broker.0.jwt_audience = okimesh",          # not a secret
    "offband 1.2.0-1.16.0 booting",                  # version, not a JWT
    "RadioLibWrapper noise_floor = -103",            # RF health
    "WiFi disconnected, reason=201; reconnecting",   # the reconnect signal we need
    "all tests passed",                              # 'pass' as a word, no value
    "MQTT reconnect attempt 3 backoff=2000ms",       # the diagnostic we're after
    # Observer pubkey MUST survive — it's how CoreScope identifies which
    # observer went silent (justifies declining Gemini Finding 3's bare-'key'
    # catch-all, which would have scrubbed this).
    "observer pubkey = B4680F16DBFC443EC2D00FB7AF2181EA96C2C4D4A65C2E93E054BB159D81F3BA",
    "[WifiObserver] STA connected, RSSI=-67 dBm",    # a normal [Tag] line, not a '>' reply
]


def test_secrets_redacted():
    for line, secret in MUST_REDACT:
        out = redact_line(line)
        assert secret not in out, "secret survived: %r -> %r" % (line, out)
        assert "<redacted:" in out, "no redaction marker: %r -> %r" % (line, out)


def test_diagnostics_preserved():
    for line in MUST_PRESERVE:
        out = redact_line(line)
        assert out == line, "diagnostic altered: %r -> %r" % (line, out)


def _run():
    failures = 0
    for name, fn in [("secrets_redacted", test_secrets_redacted),
                     ("diagnostics_preserved", test_diagnostics_preserved)]:
        try:
            fn()
            print("PASS %s" % name)
        except AssertionError as e:
            failures += 1
            print("FAIL %s: %s" % (name, e))
    return failures


if __name__ == "__main__":
    sys.exit(1 if _run() else 0)
