#!/usr/bin/env python
"""
offband-cmd - command surface for the Offband remote-diagnostics cmdrelay (#567).

Queue commands to a device, see queue/liveness status, retrieve results, and
tail forwarded caplog output -- all from the operator's machine over cmdrelay's
HTTP admin API. No manual SSH-and-curl. The ONLY SSH is the `caplog tail`
convenience (reads the forwarded syslog file); that goes away when cmdrelay is
adopted in-repo with a pure-HTTP /caplog endpoint (#572). Part of the Remote
Diagnostics epic (#565).

Config via env (so no secret lives in the repo):
  OFFBAND_CMDRELAY_URL          e.g. http://192.168.50.24:8765
  OFFBAND_CMDRELAY_ADMIN_TOKEN  the cmdrelay ADMIN bearer token
  OFFBAND_PI_SSH    (tail only) e.g. user@host   (ssh target for the log host)
  OFFBAND_CAPLOG_PATH (tail)    default /var/log/offband-caplog.log

Examples:
  offband-cmd queue wsmj898-ltb "caplog forward 300"   # run a CLI line on the node
  offband-cmd queue stp-lab --action reboot            # a raw action
  offband-cmd status wsmj898-ltb                        # liveness + recent cmds
  offband-cmd result wsmj898-ltb 7 --wait 120           # fetch a cmd's reply
  offband-cmd caplog wsmj898-ltb forward 300            # arm the syslog forward
  offband-cmd caplog wsmj898-ltb off                    # disarm + stop capture
  offband-cmd caplog wsmj898-ltb tail -n 50             # last 50 forwarded lines
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_EXPIRES_SEC = 400
DEFAULT_CAPLOG_PATH = "/var/log/offband-caplog.log"


def _env(name, required=True, default=None):
    val = os.environ.get(name, default)
    if required and not val:
        sys.exit(f"error: environment variable ${name} is not set (see --help for config)")
    return val


# --- pure request builders (unit-tested; no I/O) ------------------------------

def build_queue_payload(action, cli_cmd=None, window_sec=None, params=None,
                        expires_sec=DEFAULT_EXPIRES_SEC):
    """Build the JSON body for POST /devices/<device>/cmds."""
    merged = dict(params or {})
    if cli_cmd is not None:
        merged["cmd"] = cli_cmd
    if window_sec is not None:
        merged["window_sec"] = int(window_sec)
    body = {"action": action, "expires_in_sec": int(expires_sec)}
    if merged:
        body["params"] = merged
    return body


def _dev(device):
    # URL-encode the device segment so a name with '/', '%', etc. can't break
    # the path routing (Gemini review #567).
    return urllib.parse.quote(device, safe="")


def cmds_url(base, device):
    return f"{base.rstrip('/')}/devices/{_dev(device)}/cmds"


def result_url(base, device, cmd_id, wait_sec=0):
    return f"{base.rstrip('/')}/devices/{_dev(device)}/cmds/{int(cmd_id)}/result?wait={int(wait_sec)}"


def state_url(base, device):
    return f"{base.rstrip('/')}/devices/{_dev(device)}/state"


def caplog_tail_cmd(ssh_target, device, path, lines):
    """Build the argv for the SSH tail of one device's forwarded caplog lines."""
    # shlex.quote every interpolated value so a device name or path containing
    # shell metacharacters (e.g. a single quote) can't break out of the remote
    # command — injection-safe (Gemini review #567).
    remote = (f"grep -F {shlex.quote('caplog-' + device + ':')} {shlex.quote(path)} "
              f"| tail -n {int(lines)}")
    return ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", ssh_target, remote]


# --- HTTP --------------------------------------------------------------------

def _http(method, url, token, body=None, timeout=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace")[:300]
        sys.exit(f"error: cmdrelay {method} -> HTTP {e.code}: {detail}")
    except urllib.error.URLError as e:
        sys.exit(f"error: cannot reach cmdrelay at {url}: {e.reason}")
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {"raw": raw}


# --- subcommands -------------------------------------------------------------

def cmd_queue(args):
    base = _env("OFFBAND_CMDRELAY_URL")
    tok = _env("OFFBAND_CMDRELAY_ADMIN_TOKEN")
    params = {}
    for kv in (args.param or []):
        key, _, val = kv.partition("=")
        params[key] = val
    body = build_queue_payload(args.action, cli_cmd=args.command,
                               window_sec=args.window, params=params,
                               expires_sec=args.expires)
    resp = _http("POST", cmds_url(base, args.device), tok, body, timeout=15)
    if args.json:
        print(json.dumps(resp))
        return
    cid = resp.get("cmd_id")
    tail = f'  cmd="{args.command}"' if args.command else ""
    print(f"queued cmd_id={cid}  action={args.action}{tail}")
    if cid is not None:
        print(f"  -> offband-cmd result {args.device} {cid} --wait 120")


def cmd_status(args):
    base = _env("OFFBAND_CMDRELAY_URL")
    tok = _env("OFFBAND_CMDRELAY_ADMIN_TOKEN")
    resp = _http("GET", state_url(base, args.device), tok, timeout=15)
    if args.json:
        print(json.dumps(resp))
        return
    print(f"device: {args.device}")
    lp = resp.get("last_poll") or {}
    age = lp.get("age_sec")
    if age is not None:
        print(f"  last poll: {age:.0f}s ago  (returned {lp.get('returned_count')})")
    else:
        print("  last poll: never seen")
    counts = resp.get("counts") or {}
    pend = counts.get("pending", len(resp.get("pending") or []))
    infl = counts.get("inflight", len(resp.get("inflight") or []))
    print(f"  queue: pending={pend}  inflight={infl}")
    rc = resp.get("recent_completed") or []
    entries = rc if isinstance(rc, list) else list(rc.values())
    if entries:
        print("  recent completed:")
        for e in entries[-10:]:
            e = e or {}
            r = e.get("response") or {}
            cid = e.get("cmd_id", r.get("cmd_id", "?"))
            print(f"    #{cid}: {r.get('status','?')} - {r.get('message','')}")


def cmd_result(args):
    base = _env("OFFBAND_CMDRELAY_URL")
    tok = _env("OFFBAND_CMDRELAY_ADMIN_TOKEN")
    # wait maxes at 300 server-side; give the HTTP read a little more.
    wait = min(int(args.wait), 300)
    resp = _http("GET", result_url(base, args.device, args.cmd_id, wait), tok, timeout=wait + 15)
    if args.json:
        print(json.dumps(resp))
        return
    print(f"cmd #{args.cmd_id}: state={resp.get('state')}")
    r = resp.get("response") or {}
    if r:
        print(f"  status: {r.get('status')}")
        if r.get("message"):
            print(f"  message: {r.get('message')}")
        reply = (r.get("data") or {}).get("reply")
        if reply:
            print("  reply:")
            for line in str(reply).splitlines():
                print(f"    {line}")


def cmd_caplog(args):
    if args.caplog_action == "tail":
        ssh_target = _env("OFFBAND_PI_SSH")
        path = os.environ.get("OFFBAND_CAPLOG_PATH", DEFAULT_CAPLOG_PATH)
        argv = caplog_tail_cmd(ssh_target, args.device, path, args.lines)
        # Stream the remote tail straight through.
        sys.exit(subprocess.call(argv))
    # forward <sec> | off -> a convenience wrapper over the `cli` action.
    if args.caplog_action == "off":
        cli = "caplog forward off"
    else:  # forward
        cli = f"caplog forward {int(args.seconds)}"
    fwd = argparse.Namespace(device=args.device, action="cli", command=cli,
                             window=None, param=[], expires=DEFAULT_EXPIRES_SEC,
                             json=args.json)
    cmd_queue(fwd)


# --- CLI ---------------------------------------------------------------------

def build_parser():
    p = argparse.ArgumentParser(prog="offband-cmd", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--json", action="store_true", help="machine-readable JSON output")
    sub = p.add_subparsers(dest="cmd", required=True)

    q = sub.add_parser("queue", help="queue a command to a device")
    q.add_argument("device")
    q.add_argument("command", nargs="?", help="CLI line to run (default action=cli)")
    q.add_argument("--action", default="cli", help="raw action (default: cli)")
    q.add_argument("--param", action="append", metavar="K=V", help="extra param (repeatable)")
    q.add_argument("--window", type=int, help="window_sec param (ota/wifi actions)")
    q.add_argument("--expires", type=int, default=DEFAULT_EXPIRES_SEC, help="cmd TTL sec")
    q.set_defaults(func=cmd_queue)

    s = sub.add_parser("status", help="device liveness + recent cmds")
    s.add_argument("device")
    s.set_defaults(func=cmd_status)

    r = sub.add_parser("result", help="fetch a cmd's result")
    r.add_argument("device")
    r.add_argument("cmd_id", type=int)
    r.add_argument("--wait", type=int, default=0, help="long-poll seconds (max 300)")
    r.set_defaults(func=cmd_result)

    c = sub.add_parser("caplog", help="arm/disarm forward, or tail forwarded output")
    c.add_argument("device")
    csub = c.add_subparsers(dest="caplog_action", required=True)
    cf = csub.add_parser("forward", help="arm live syslog forward for <sec>")
    cf.add_argument("seconds", type=int)
    csub.add_parser("off", help="disarm forward + stop capture")
    ct = csub.add_parser("tail", help="last N forwarded lines (SSH)")
    ct.add_argument("-n", "--lines", type=int, default=40)
    c.set_defaults(func=cmd_caplog)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
