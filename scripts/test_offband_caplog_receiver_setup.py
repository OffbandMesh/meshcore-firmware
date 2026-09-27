#!/usr/bin/env python3
"""Tests for offband-caplog-receiver-setup.sh's rotation policy (#1264, #1265).

The receiver's logrotate policy used to name `syslog` as the owner of the
rotated log. A host with no such user rejects the policy outright, so its
nightly rotation failed and the caplog never rotated. The script now asks the
host who should own the file and has logrotate check the policy before writing
anything.

This drives the real script in --dry-run. `id`, `rsyslogd`, `ps`, `getent` and
`logrotate` are stubs placed first on the PATH, so no root, no rsyslog and no
logrotate are needed.

Run: python scripts/test_offband_caplog_receiver_setup.py
  or python -m pytest scripts/test_offband_caplog_receiver_setup.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "offband-caplog-receiver-setup.sh")

STUBS = {
    # The script refuses to run unless it is root.
    "id": 'echo 0\n',
    # Present, so the script's "is rsyslog installed" check passes.
    "rsyslogd": 'exit 0\n',
    # `ps -o uid= -C rsyslogd`, behaving like the real thing: procps exits 1
    # when no process matches, and busybox has no -C at all. An earlier stub
    # exited 0 either way, which let a no-match case pass for the wrong reason.
    "ps": (
        'if [ -n "${STUB_PS_NO_DASH_C:-}" ]; then echo "ps: unrecognized option: C" >&2; exit 1; fi\n'
        'if [ -n "${STUB_RSYSLOG_UID:-}" ]; then echo "  $STUB_RSYSLOG_UID"; exit 0; fi\n'
        'exit 1\n'
    ),
    # Only the users and the group the case says exist.
    "getent": (
        'case "$1" in\n'
        '  passwd) if [ -n "${STUB_USER:-}" ] && [ "$2" = "${STUB_UID:-}" ]; then\n'
        '            echo "$STUB_USER:x:$2:$2::/nonexistent:/usr/sbin/nologin"; exit 0; fi ;;\n'
        '  group)  if [ "$2" = adm ] && [ "${STUB_HAS_ADM:-1}" = 1 ]; then echo "adm:x:4:"; exit 0; fi ;;\n'
        'esac\n'
        'exit 2\n'
    ),
    # Keeps a copy of the policy it was asked about, then accepts or rejects it.
    "logrotate": (
        'for last; do :; done\n'
        '[ -n "${STUB_CAPTURE:-}" ] && cat "$last" > "$STUB_CAPTURE"\n'
        'if [ -n "${STUB_LOGROTATE_ERROR:-}" ]; then echo "$STUB_LOGROTATE_ERROR" >&2; exit 1; fi\n'
        'echo "rotating pattern: /var/log/offband-caplog.log  weekly (8 rotations)"\n'
        'exit 0\n'
    ),
}


def _bash():
    """Git's bash on Windows; WSL's System32 shim would run in another filesystem."""
    candidates = [shutil.which("bash")]
    if os.name == "nt":
        candidates.insert(0, r"C:\Program Files\Git\bin\bash.exe")
    for c in candidates:
        if c and os.path.isfile(c) and "system32" not in c.lower():
            return c
    raise AssertionError("no usable bash found")


def run(**case):
    """Run the script in --dry-run under the stubs; return (exit, stdout, stderr, policy)."""
    stubdir = tempfile.mkdtemp(prefix="caplog-stubs-")
    try:
        for name, body in STUBS.items():
            path = os.path.join(stubdir, name)
            with open(path, "w", newline="\n") as f:
                f.write("#!/usr/bin/env bash\n" + body)
            os.chmod(path, 0o755)
        capture = os.path.join(stubdir, "policy-seen-by-logrotate")
        env = dict(os.environ)
        env["STUB_DIR"] = stubdir
        env["STUB_CAPTURE"] = capture
        for key in ("STUB_RSYSLOG_UID", "STUB_UID", "STUB_USER", "STUB_HAS_ADM",
                    "STUB_LOGROTATE_ERROR", "STUB_PS_NO_DASH_C"):
            env.pop(key, None)
        env.update({k: str(v) for k, v in case.items()})
        # The stubs go on the PATH from inside bash: Git Bash's launcher puts its
        # own /usr/bin ahead of whatever PATH it is handed, which would hide them.
        # cygpath only exists there; on Linux the paths are already POSIX.
        inner = ('d="$STUB_DIR"; s="$1"; '
                 'if command -v cygpath >/dev/null 2>&1; then '
                 'd="$(cygpath -u "$d")"; s="$(cygpath -u "$s")"; '
                 'STUB_CAPTURE="$(cygpath -u "$STUB_CAPTURE")"; export STUB_CAPTURE; fi; '
                 'PATH="$d:$PATH" exec bash "$s" --dry-run')
        p = subprocess.run([_bash(), "-c", inner, "_", SCRIPT], env=env,
                           capture_output=True, text=True, timeout=60)
        policy = open(capture).read() if os.path.exists(capture) else ""
        return p.returncode, p.stdout, p.stderr, policy
    finally:
        shutil.rmtree(stubdir, ignore_errors=True)


ROOT = dict(STUB_RSYSLOG_UID="0", STUB_UID="0", STUB_USER="root", STUB_HAS_ADM="1")


# ------------------------------------------------------------- the owner

def test_rsyslog_running_as_root_gives_root_adm():
    # The host that failed every night: Raspberry Pi OS, rsyslog as root.
    rc, out, err, _ = run(**ROOT)
    assert rc == 0, err
    assert "create 0640 root adm" in out, out


def test_rsyslog_running_as_syslog_keeps_debians_owner():
    rc, out, err, _ = run(STUB_RSYSLOG_UID="104", STUB_UID="104", STUB_USER="syslog", STUB_HAS_ADM="1")
    assert rc == 0, err
    assert "create 0640 syslog adm" in out, out


def test_no_rsyslog_process_falls_back_to_root():
    rc, out, err, _ = run(STUB_HAS_ADM="1")
    assert rc == 0, err
    assert "create 0640 root adm" in out, out


def test_a_ps_without_dash_c_falls_back_to_root():
    # busybox ps rejects -C; that must mean "fall back", never an abort.
    rc, out, err, _ = run(**dict(ROOT, STUB_PS_NO_DASH_C="1"))
    assert rc == 0, err
    assert "create 0640 root adm" in out, out


def test_a_uid_with_no_user_falls_back_to_root():
    # Never write a name the host can't resolve -- that is the #1264 failure.
    rc, out, err, _ = run(STUB_RSYSLOG_UID="999", STUB_UID="104", STUB_USER="syslog", STUB_HAS_ADM="1")
    assert rc == 0, err
    assert "create 0640 root adm" in out, out


def test_no_adm_group_falls_back_to_root():
    rc, out, err, _ = run(**dict(ROOT, STUB_HAS_ADM="0"))
    assert rc == 0, err
    assert "create 0640 root root" in out, out


# ------------------------------------------------------------- the check

def test_logrotate_checks_the_same_policy_that_gets_installed():
    rc, out, err, policy = run(**ROOT)
    assert rc == 0, err
    assert "logrotate accepts the rotation policy" in out, out
    assert "create 0640 root adm" in policy, policy
    for line in policy.strip().splitlines():
        assert line in out, f"checked policy line not in the installed one: {line!r}"


def test_a_rejected_policy_stops_before_anything_is_written():
    error = "error: offband-caplog:7 unknown user 'syslog'"
    rc, out, err, _ = run(**dict(ROOT, STUB_LOGROTATE_ERROR=error))
    assert rc != 0, "a policy logrotate rejects must fail the install"
    assert "logrotate rejects the rotation policy" in err, err
    assert "unknown user 'syslog'" in err, "the operator needs logrotate's own reason"
    assert "would write" not in out, f"nothing may be written after a rejection:\n{out}"


if __name__ == "__main__":
    failures = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failures += 1
                print(f"FAIL {name}: {e}")
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
