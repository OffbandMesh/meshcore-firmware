#!/usr/bin/env python3
"""Shared log redaction for Offband capture tooling (#667).

Both retrieval paths for the same device log must scrub the same things:

  * scripts/_cap_serial.py     -- live serial capture (#379/#380)
  * scripts/companion_harness.py -- caplog download over the framed protocol

They used to disagree: the serial path redacted credentials, the caplog download
returned the ring verbatim. A caplog pulled off the owner's companion therefore
carried his real GPS coordinates into a file in plaintext. Keeping the patterns
in one module is the point -- two copies drift, and the copy that drifts is the
one that leaks.

DESIGN CONSTRAINT ON POSITION (owner ruling, #667):
    Do NOT simply delete coordinates. Log files are read to diagnose a GPS that
    is misbehaving, and the reader must still be able to tell
        "a real fix"  vs  "0,0"  vs  "something entirely different"
    from someone else's log. Blanking the value destroys exactly that.

So position is CLASSIFIED rather than erased:

    lat=0          -> lat=0                  zero is kept: it IS the diagnosis
    lat=39380817   -> lat=<redacted:+8d>     sign + digit count
    lat=3938       -> lat=<redacted:+4d>     a scale/precision bug stays visible
    lat=-84451566  -> lat=<redacted:-8d>     a sign-flip bug stays visible
    lat=999999999  -> lat=<redacted:+9d,oor> out of range == garbage, stays visible

Sign and digit count survive because the classic GPS failures are sign flips and
1e-N scaling errors, and both are still obvious. Neither locates anybody: every
node in a hemisphere shares them.
"""
import re

# --- position -------------------------------------------------------------

# 1e-6 degrees, matching the SELF_INFO wire format.
_LAT_MAX_UD = 90_000_000
_LON_MAX_UD = 180_000_000

# `lat=` / `lon=` in any log line. alt_cm is deliberately NOT redacted: altitude
# alone does not locate anyone, and an absurd or negative altitude is a genuine
# diagnostic signal.
_POS_RE = re.compile(r"\b(lat|lon|latitude|longitude)\s*=\s*(-?\d+)\b", re.IGNORECASE)


def _classify_position(key: str, raw: str) -> str:
    """Sign + digit count, plus an out-of-range flag. Never the value."""
    try:
        value = int(raw)
    except ValueError:
        return f"<redacted:{key}>"

    # Zero is not sensitive and is frequently the whole point of the log line
    # ("is this thing actually getting a fix?"). Keep it exactly.
    if value == 0:
        return "0"

    digits = len(raw.lstrip("-"))
    sign = "-" if value < 0 else "+"
    limit = _LAT_MAX_UD if key.lower().startswith("lat") else _LON_MAX_UD
    oor = ",oor" if abs(value) > limit else ""
    return f"<redacted:{sign}{digits}d{oor}>"


def redact_positions(line: str) -> str:
    """Classify every lat/lon in `line`. Safe to run on non-GPS lines."""
    return _POS_RE.sub(lambda m: f"{m.group(1)}={_classify_position(m.group(1), m.group(2))}",
                       line)


# --- credentials ----------------------------------------------------------
#
# Moved verbatim from _cap_serial.py so both tools share one list. Ordering
# matters: the more specific patterns must run before the generic key=value one.

_SECRET_WORDS = (r"password|passwd|pwd|psk|secret|token|apikey|api_key|bearer|pass")

# Named so a caller that already KNOWS which command it issued can opt out of
# just this one rule (#849). `pio-flash send` has to keep printing
# `> 910.525,62.5,7,5` verbatim for ordinary keys while still redacting
# `get prv.key`; a blanket net cannot do both. The length is carried so a
# sweep can assert that a secret key ANSWERED without recording what it said.
_CLI_REPLY_RE = re.compile(r"(^\s*>\s*)([^\r\n]+)")

# The rule above assumes a CLI reply BEGINS its line. That assumption was broken
# by the firmware, not by this file. Unsynchronized UART writers splice log
# output in front of a reply, and a real ST-P capture contained:
#
#   [247008][E][Preferences.cpp:483] getString(): nvs_get_str  -> > <private key>
#
# `^\s*>` never matched, and a node private key was written to a capture file in
# full. The control was not wrong when written -- its PRECONDITION stopped
# holding. Every fixture fed it clean, line-anchored input, so no test failed.
#
# For a reply already classified as secret, position must not decide whether the
# secret is protected. These are unanchored and deliberately greedy: on a
# known-secret reply, over-redacting costs a re-read; under-redacting leaks a key.
_SECRET_ANYWHERE_RES = (
    # `> value` anywhere on the line, however much junk precedes it.
    re.compile(r"(>[ \t]*)([^\r\n]+)"),
    # Belt and braces: a long opaque token with no `>` marker at all, because a
    # splice can land mid-value and destroy the marker itself.
    re.compile(r"([0-9A-Fa-f]{16,}|[A-Za-z0-9+/=_-]{24,})"),
)

_REDACTIONS = [
    # WiFi SSID, in the shapes the firmware actually prints.
    (re.compile(r"(SSID\s*=\s*)([^;\r\n]+)", re.IGNORECASE), r"\1<redacted:ssid>"),
    (re.compile(r"(wifi\.ssid\s*[:=]\s*)([^\r\n]+)", re.IGNORECASE), r"\1<redacted:ssid>"),

    # JWT. Distinctive enough to redact anywhere; version strings like 1.16.0
    # don't match because each segment needs 8+ chars.
    (re.compile(r"\b[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\b"),
     "<redacted:jwt>"),

    (re.compile(r"(jwt_email\s*[:=]\s*)([^\r\n]+)", re.IGNORECASE), r"\1<redacted:email>"),
    (re.compile(r"[\w.+-]+@[\w-]+\.[\w.-]+"), "<redacted:email>"),

    # key: value / key=value
    (re.compile(r"(\b[\w.-]*(?:" + _SECRET_WORDS + r")\b\s*[:=]\s*)([^\r\n]+)",
                re.IGNORECASE), r"\1<redacted:secret>"),

    # The firmware echoes `password now: <value>` on `set password`
    # (CommonCLI.cpp). The key is not adjacent to the separator, so the pattern
    # above misses it and the value passed straight through -- which is exactly
    # what this redaction exists to prevent (#379/#380).
    (re.compile(r"(\b[\w.-]*(?:" + _SECRET_WORDS + r")\b"
                r"(?:\s+(?:now|is|set(?:\s+to)?|changed(?:\s+to)?|updated(?:\s+to)?)){1,2}\s*[:=]?\s+)"
                r"([^\r\n]+)", re.IGNORECASE), r"\1<redacted:secret>"),

    # Last-resort net for label-less interactive CLI replies. The serial console
    # prints `get wifi.ssid` / `get guest_password` / `get bridge_secret` replies
    # as "> <value>" with NO field label (CommonCLI `> %s`), so nothing above can
    # catch them. #382: the live reply is INDENTED ("  > tsunami"), so the anchor
    # must allow leading whitespace; the original `^>` missed it and leaked a real
    # SSID on the bench. Runtime log lines are "[ms] LEVEL: ..." or "[Tag] ...",
    # never a leading ">", so this does not over-redact a caplog.
    (_CLI_REPLY_RE,
     lambda m: m.group(1) + "<redacted:cli-reply:" + str(len(m.group(2))) + ">"),
]


def redact_line(line: str, cli_reply_net: bool = True,
                secret: bool = False) -> str:
    """Apply every rule to one line. Position classification runs last so a
    coordinate inside an otherwise-redacted line is still handled.

    `cli_reply_net` defaults True, so every existing caller is unchanged.
    Pass False ONLY when you already know which command produced this line
    and know it is not secret-bearing (see `pio-flash send`, #849). Turning
    it off blindly reopens #382, where a real SSID reached chat as `  > ...`
    with no label for any other rule to match on.
    """
    for pattern, repl in _REDACTIONS:
        if not cli_reply_net and pattern is _CLI_REPLY_RE:
            continue
        line = pattern.sub(repl, line)
    if secret:
        # Applied LAST and unanchored, so it also catches a value the positional
        # rules skipped because a log splice moved it off column 0.
        #
        # MUST be idempotent: the anchored rule above may already have replaced
        # the value with a marker, and re-redacting that marker would overwrite
        # the real length with the marker's own length -- destroying the one
        # property the sweep relies on to assert a secret key ANSWERED.
        def _once(m):
            val = m.group(m.lastindex)
            if "<redacted:" in val:
                return m.group(0)
            return "<redacted:cli-reply:%d>" % len(val.strip())

        for pat in _SECRET_ANYWHERE_RES:
            line = pat.sub(_once, line)
    return redact_positions(line)


def redact_text(text: str) -> str:
    """Line-based, matching the capture tooling: our firmware never splits a
    secret across lines, so there is nothing to gain from multiline matching."""
    return "\n".join(redact_line(l) for l in text.split("\n"))


def redact_bytes(data: bytes) -> bytes:
    """For the caplog download, which is bytes off the wire. Undecodable bytes
    are preserved via surrogateescape rather than dropped -- a corrupt ring is
    itself diagnostic and must not be silently mangled."""
    return redact_text(data.decode("utf-8", "surrogateescape")).encode("utf-8", "surrogateescape")
