#!/usr/bin/env python3
"""session-identity.py — mechanical, collision-free per-session identity.

Kills the shared-singleton collision: `agent_mail_identity` was one static
string in `.claude/agent-session.json`, so every session read it and 10 became
"RedCreek" on one mailbox, silently. Identity is instead keyed to the per-session
UUID. See standards#190/#192, Mercury#12, memory reference_session_identity_from_uuid.

STORE-AUTHORITATIVE (standards#192): the per-session file
`.claude/sessions/<uuid>.json` is the source of truth. Identities are assigned
once (by a human or, for a brand-new session, by derivation) and then **never
silently changed**. This is a storage mechanism, not a renamer.

Handle (all platform-independent; verified in Claude Desktop):
  1. $CLAUDE_CODE_SESSION_ID           env var — present in Desktop AND CLI
  2. UUID parsed from the scratchpad path / $PWD    (fallback)

The invariant (standards#192 review): an already-recorded identity is NEVER
overridden except by a deliberate, explicit `register --identity NAME`.
  - `whoami`   : print the recorded identity verbatim; derive ONLY if no record.
  - `register` : keep the recorded identity; derive ONLY if no record; set it
                 only when `--identity` is given.
  - A record that EXISTS but is UNREADABLE (corrupt/truncated) is a HARD ERROR,
    never treated as "absent" — otherwise a crash mid-write would silently
    re-derive over (e.g.) RedCreek. Absent -> derive; corrupt -> fail loud.

Registry is one file per UUID (never a shared mutable singleton — that is the
race we are fixing). Each session only ever writes its own file.

Commands
--------
  whoami                     print this session's identity (default)
  resolve                    JSON: {uuid, identity, recorded, source, entrypoint}
  register [--identity NAME] SessionStart uses the no-arg form: it preserves an
                             existing record and does NOT persist a fresh-derived
                             name (deploy-safety, #195). `--identity` sets/changes.
  migrate --from <root>      seed .claude/sessions/ from per-session scratchpad
                             stores (agent-identity.json) — data-driven, never
                             derives, never overrides a differing record (#195)
  list                       consolidated index of all known sessions

Exit codes
----------
  0  ok
  1  no session UUID handle / invalid --identity (fail loud)
  2  the session's record exists but is unreadable — refusing to override (fail loud)
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import time
from pathlib import Path

_UUID_RE = re.compile(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")
_MAX_IDENTITY_LEN = 64

ADJ = ["Amber", "Azure", "Basalt", "Cobalt", "Crimson", "Dusk", "Ember", "Flint",
       "Frost", "Glacier", "Granite", "Harbor", "Indigo", "Iron", "Jade", "Lunar",
       "Maple", "Mesa", "Onyx", "Opal", "Pewter", "Quartz", "Raven", "Rust",
       "Sable", "Slate", "Solar", "Storm", "Timber", "Umber", "Verde", "Willow"]
NOUN = ["Basin", "Bluff", "Canyon", "Cedar", "Creek", "Delta", "Dune", "Falls",
        "Fjord", "Glade", "Grove", "Hollow", "Knoll", "Ledge", "Marsh", "Moor",
        "Otter", "Peak", "Pine", "Reef", "Ridge", "Shoal", "Spire", "Summit",
        "Thicket", "Thorn", "Vale", "Warren", "Wharf", "Wren", "Yarrow", "Zephyr"]


class CorruptRecord(Exception):
    """The session's record file exists but could not be read/parsed."""


def session_uuid():
    """(uuid, source) or (None, None). Env var first, then path fallbacks."""
    sid = (os.environ.get("CLAUDE_CODE_SESSION_ID") or "").strip()
    if _UUID_RE.fullmatch(sid):
        return sid, "env:CLAUDE_CODE_SESSION_ID"
    for var in ("CLAUDE_SCRATCHPAD", "PWD", "OLDPWD"):
        m = _UUID_RE.search(os.environ.get(var, ""))
        if m:
            return m.group(0), f"path:{var}"
    return None, None


def derive(uuid: str) -> str:
    """UUID -> stable, unique, human-readable identity.

    Adjective+noun and the 32-bit suffix come from independent bytes of
    sha256(uuid), so the suffix does not correlate with the name (a raw
    UUID-prefix would, reintroducing collisions — standards#190 review).
    """
    h = hashlib.sha256(uuid.encode("utf-8")).digest()
    name = ADJ[h[0] % len(ADJ)] + NOUN[h[1] % len(NOUN)]
    return f"{name}-{h[4:8].hex()}"


def project_dir() -> Path:
    p = os.environ.get("CLAUDE_PROJECT_DIR")
    if p and Path(p).is_dir():
        return Path(p)
    try:
        import subprocess
        r = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True, timeout=5)
        if r.returncode == 0 and r.stdout.strip():
            return Path(r.stdout.strip())
    except Exception:
        pass
    return Path.cwd()


def sessions_dir() -> Path:
    """Where per-session identity records live.

    Anchored to the git COMMON dir's parent (the primary repo root) so ALL
    linked worktrees of a repo share ONE registry — identity is per-session-UUID,
    not per-worktree, and a session must resolve to the same name whether it runs
    in the primary clone or a worktree (standards#197). For the primary clone this
    is the same path as project_dir(), so existing records are unaffected.
    Falls back to project_dir() when not in a git repo.
    """
    base = project_dir()
    try:
        import subprocess
        r = subprocess.run(
            ["git", "-C", str(base), "rev-parse", "--path-format=absolute", "--git-common-dir"],
            capture_output=True, text=True, timeout=5)
        common = r.stdout.strip().replace("\\", "/")
        if r.returncode == 0 and common:
            return Path(common).parent / ".claude" / "sessions"
    except Exception:
        pass
    return base / ".claude" / "sessions"


def record_path(uuid: str) -> Path:
    return sessions_dir() / f"{uuid}.json"


def read_record(uuid: str):
    """Return the record dict, or None if ABSENT.

    Raises CorruptRecord if the file EXISTS but cannot be read/parsed — the
    caller must NOT treat that as "absent" (that would re-derive over a real
    identity). Distinguishing absent from corrupt is the core safety property.
    """
    f = record_path(uuid)
    if not f.exists():
        return None
    try:
        data = json.loads(f.read_text(encoding="utf-8"))
    except Exception as e:  # unreadable / invalid JSON / permission / truncated
        raise CorruptRecord(f"{f}: {e}") from e
    if not isinstance(data, dict):
        raise CorruptRecord(f"{f}: not a JSON object")
    return data


def _require_uuid():
    uuid, source = session_uuid()
    if not uuid:
        print("NO_SESSION_UUID (no $CLAUDE_CODE_SESSION_ID or UUID in path)",
              file=sys.stderr)
        sys.exit(1)
    return uuid, source


def _read_or_die(uuid: str):
    """read_record but turn CorruptRecord into a loud exit(2) — never override."""
    try:
        return read_record(uuid)
    except CorruptRecord as e:
        print(
            f"BLOCKED: session identity record is unreadable — refusing to "
            f"re-derive over it.\n  {e}\n  Fix or remove the file by hand once "
            f"you know the correct identity; do NOT let it be silently renamed.",
            file=sys.stderr,
        )
        sys.exit(2)


def _validate_identity(name: str) -> str:
    n = (name or "").strip()
    if not n:
        print("BLOCKED: --identity must be non-empty / non-whitespace.", file=sys.stderr)
        sys.exit(1)
    if len(n) > _MAX_IDENTITY_LEN:
        print(f"BLOCKED: --identity too long (>{_MAX_IDENTITY_LEN} chars).", file=sys.stderr)
        sys.exit(1)
    uuid, _ = session_uuid()
    d = sessions_dir()
    if d.is_dir():
        for f in d.glob("*.json"):
            try:
                other = json.loads(f.read_text(encoding="utf-8"))
            except Exception:
                continue
            if (isinstance(other, dict) and other.get("identity") == n
                    and other.get("uuid") not in (None, uuid)):
                print(f"⚠ identity '{n}' is already held by {other.get('uuid')} "
                      f"({f.name}) — duplicate identities defeat the point.",
                      file=sys.stderr)
                break
    return n


def cmd_whoami() -> int:
    uuid, _ = _require_uuid()
    rec = _read_or_die(uuid)
    print(rec["identity"] if (rec and rec.get("identity")) else derive(uuid))
    return 0


def cmd_resolve() -> int:
    uuid, source = _require_uuid()
    rec = _read_or_die(uuid)
    identity = rec["identity"] if (rec and rec.get("identity")) else derive(uuid)
    print(json.dumps({
        "uuid": uuid,
        "identity": identity,
        "recorded": bool(rec and rec.get("identity")),
        "source": source,
        "entrypoint": os.environ.get("CLAUDE_CODE_ENTRYPOINT", "unknown"),
    }))
    return 0


def _write_record(uuid: str, identity: str, source: str) -> None:
    """Atomic pure write of KNOWN fields only (no read-merge -> no TOCTOU)."""
    d = sessions_dir()
    d.mkdir(parents=True, exist_ok=True)
    out = {
        "uuid": uuid,
        "identity": identity,
        "entrypoint": os.environ.get("CLAUDE_CODE_ENTRYPOINT", "unknown"),
        "source": source,
        "cwd": str(Path.cwd()),
        "updated": int(time.time()),
    }
    tmp = d / f".{uuid}.{os.getpid()}.tmp"   # PID-unique temp; final replace is atomic
    tmp.write_text(json.dumps(out, indent=2), encoding="utf-8")
    os.replace(tmp, record_path(uuid))


def cmd_register(explicit_identity=None) -> int:
    uuid, source = _require_uuid()
    rec = _read_or_die(uuid)  # corrupt -> exit 2 BEFORE we could override
    if explicit_identity is not None:
        identity = _validate_identity(explicit_identity)    # deliberate set / migration
        _write_record(uuid, identity, source)
    elif rec and rec.get("identity"):
        identity = rec["identity"]                          # KEEP existing — refresh metadata
        _write_record(uuid, identity, source)
    else:
        # NO record + NO explicit identity: DO NOT persist a derived name — that
        # would override an assigned-but-not-yet-seeded session (e.g. RedCreek
        # before migration). Derivation is deterministic, so whoami recovers it
        # on demand; a session claims a name explicitly with `register --identity`
        # (or `migrate` seeds it). standards#195 deploy-safety.
        print(derive(uuid))
        return 0
    print(identity)
    return 0


_STORE_FILES = ("agent-identity.json", "agent-mail-identity.json")
_IDENTITY_KEYS = ("agent_name", "identity", "agent_mail_identity", "name", "mail_identity")


def _extract_stored_identity(path: Path):
    """Pull an assigned identity out of a workaround scratchpad store, or None."""
    try:
        d = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None
    if not isinstance(d, dict) or d.get("do_not_use_identity"):
        return None
    for k in _IDENTITY_KEYS:
        v = d.get(k)
        if isinstance(v, str) and v.strip():
            return v.strip()
    return None


def cmd_migrate(scratch_root=None) -> int:
    """Seed .claude/sessions/<uuid>.json from per-session scratchpad stores.

    Data-driven, never derives, never overrides: for each <uuid>/scratchpad/
    agent-identity.json (or agent-mail-identity.json) that carries an assigned
    identity, write the canonical record IF ABSENT. A canonical record that
    already exists with a DIFFERENT identity is left alone (warned), never
    clobbered — so re-running is idempotent and no one is renamed. standards#195.
    """
    if not scratch_root:
        print("usage: session-identity.py migrate --from <scratchpad-root>", file=sys.stderr)
        return 1
    root = Path(scratch_root)
    if not root.is_dir():
        print(f"BLOCKED: --from is not a directory: {root}", file=sys.stderr)
        return 1
    seeded = ok = conflict = skipped = 0
    for sess in sorted(root.iterdir()):
        if not sess.is_dir() or not _UUID_RE.fullmatch(sess.name):
            continue
        uuid = sess.name
        ident = None
        for fn in _STORE_FILES:
            ident = _extract_stored_identity(sess / "scratchpad" / fn)
            if ident:
                break
        if not ident or len(ident) > _MAX_IDENTITY_LEN:
            skipped += 1
            continue
        try:
            existing = read_record(uuid)
        except CorruptRecord:
            print(f"  ! {uuid}: canonical record corrupt — skipping (fix by hand)", file=sys.stderr)
            skipped += 1
            continue
        if existing and existing.get("identity"):
            if existing["identity"] == ident:
                ok += 1
            else:
                print(f"  ! {uuid}: canonical '{existing['identity']}' != store "
                      f"'{ident}' — NOT overriding", file=sys.stderr)
                conflict += 1
            continue
        _write_record(uuid, ident, f"migrated:{uuid[:8]}")
        print(f"  + {ident:<22} {uuid}")
        seeded += 1
    print(f"\n  seeded={seeded} already-ok={ok} conflicts={conflict} skipped={skipped}",
          file=sys.stderr)
    return 0


def cmd_list() -> int:
    d = sessions_dir()
    rows = []
    if d.is_dir():
        for f in sorted(d.glob("*.json")):
            try:
                rows.append(json.loads(f.read_text(encoding="utf-8")))
            except Exception:
                print(f"  ⚠ unreadable: {f.name}", file=sys.stderr)
                continue
    for r in rows:
        if isinstance(r, dict):
            print("  %-20s %-14s %s" % (
                r.get("identity", "?"), r.get("entrypoint", "?"), r.get("uuid", "?")))
    print(f"  ({len(rows)} session(s))")
    return 0


def main() -> int:
    args = sys.argv[1:]
    cmd = args[0] if args else "whoami"
    if cmd == "register":
        explicit = None
        if "--identity" in args:
            i = args.index("--identity")
            explicit = args[i + 1] if i + 1 < len(args) else ""
        return cmd_register(explicit)
    if cmd == "migrate":
        frm = None
        if "--from" in args:
            i = args.index("--from")
            frm = args[i + 1] if i + 1 < len(args) else None
        return cmd_migrate(frm)
    return {
        "whoami": cmd_whoami,
        "resolve": cmd_resolve,
        "list": cmd_list,
    }.get(cmd, cmd_whoami)()


if __name__ == "__main__":
    sys.exit(main())
