#!/usr/bin/env bash
# =============================================================================
# offband-caplog-receiver-setup.sh  (#569)
#
# Stand up an rsyslog sink for Offband's caplog live syslog forward: a UDP
# listener that routes the device's datagrams — matched by their tag
# `caplog-<node>:` (programname) — into their own rotated logfile, without
# touching the host's other syslog traffic.
#
# Device side (see docs/remote-diagnostics.md):
#   set syslog.host <this-host-ip>
#   set syslog.port <port>            (default 514)
#   caplog forward <seconds>
#
# The device sends `<134>caplog-<node>: <line>` (PRI 134 = local0.info). This
# script installs an rsyslog drop-in that binds the UDP input (only if imudp
# isn't already loaded) and routes lines whose programname starts with
# `caplog-` to a dedicated file, plus a logrotate policy. Only caplog lines are
# captured — the host's other syslog is untouched. Idempotent; re-running is
# safe. `--uninstall` reverses it.
#
# Target: Debian / Raspberry Pi OS (systemd + rsyslog). Run as root.
# =============================================================================
set -euo pipefail

RSYSLOG_CONF="/etc/rsyslog.d/30-offband-caplog.conf"
LOGROTATE_CONF="/etc/logrotate.d/offband-caplog"

# Defaults (overridable by flags)
LOG_PATH="/var/log/offband-caplog.log"
PORT=514
RETAIN_WEEKS=8
ACTION="install"
DRY_RUN=0

usage() {
  cat <<EOF
NAME
    $(basename "$0") — install or remove the Offband caplog rsyslog receiver

SYNOPSIS
    sudo $0 [--port <n>] [--log-path <path>] [--retention <weeks>] [--dry-run]
    sudo $0 --uninstall [--log-path <path>] [--dry-run]

DESCRIPTION
    Install (default) or remove an rsyslog drop-in plus a logrotate policy that
    routes Offband devices' caplog datagrams (tag caplog-<node>:, matched by
    programname) into a dedicated logfile. Idempotent; safe to re-run.

OPTIONS
    --port <n>
        UDP port to listen on for incoming syslog datagrams.
        Range 1-65535. Default: $PORT (the syslog well-known port). Must match
        the device's 'set syslog.port'.

    --log-path <path>
        Absolute path of the file caplog lines are written to.
        Default: $LOG_PATH. On --uninstall this names the
        logrotate target that gets removed, so pass the same value you
        installed with if you changed it.

    --retention <weeks>
        Number of weekly-rotated logs to keep before deletion.
        Integer >= 0. Default: $RETAIN_WEEKS (~two months); 0 keeps none.

    --uninstall
        Remove the rsyslog drop-in and the logrotate policy, then restart
        rsyslog. The existing logfile(s) are left in place. Reverses an install.

    --dry-run
        Print every change (files that would be written/removed, the service
        restart) without modifying anything. Combines with install or
        --uninstall.

    -h, --help
        Print this help and exit.

EXAMPLES
    sudo $0
        Install with defaults: UDP $PORT -> $LOG_PATH, keep ${RETAIN_WEEKS}w.

    sudo $0 --port 5514 --retention 4
        Listen on UDP 5514; keep 4 weeks of rotated logs.

    sudo $0 --uninstall
        Remove the receiver configuration.
EOF
}

log()  { printf '  %s\n' "$*"; }
note() { printf 'NOTE: %s\n' "$*" >&2; }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --log-path)   LOG_PATH="${2:?}"; shift 2 ;;
    --port)       PORT="${2:?}"; shift 2 ;;
    --retention)  RETAIN_WEEKS="${2:?}"; shift 2 ;;
    --uninstall)  ACTION="uninstall"; shift ;;
    --dry-run)    DRY_RUN=1; shift ;;
    -h|--help)    usage; exit 0 ;;
    *)            die "unknown argument: $1 (see --help)" ;;
  esac
done

case "$PORT" in
  ''|*[!0-9]*) die "--port must be numeric (got: $PORT)" ;;
esac
[ "$PORT" -ge 1 ] && [ "$PORT" -le 65535 ] || die "--port out of range 1-65535 (got: $PORT)"
case "$RETAIN_WEEKS" in
  ''|*[!0-9]*) die "--retention must be numeric (got: $RETAIN_WEEKS)" ;;
esac

[ "$(id -u)" -eq 0 ] || die "must run as root (use sudo)"
command -v rsyslogd >/dev/null 2>&1 || die "rsyslog not found — install it first (apt install rsyslog)"

restart_rsyslog() {
  if [ "$DRY_RUN" -eq 1 ]; then log "[dry-run] would restart rsyslog"; return; fi
  if command -v systemctl >/dev/null 2>&1; then
    systemctl restart rsyslog
  else
    service rsyslog restart
  fi
}

write_file() {  # write_file <path> <<<content-on-stdin
  local path="$1" content
  content="$(cat)"
  if [ "$DRY_RUN" -eq 1 ]; then
    log "[dry-run] would write $path:"
    printf '%s\n' "$content" | sed 's/^/      | /'
    return
  fi
  printf '%s\n' "$content" > "$path"
  log "wrote $path"
}

if [ "$ACTION" = "uninstall" ]; then
  log "Removing Offband caplog receiver..."
  for f in "$RSYSLOG_CONF" "$LOGROTATE_CONF"; do
    if [ -e "$f" ]; then
      if [ "$DRY_RUN" -eq 1 ]; then log "[dry-run] would remove $f"; else rm -f "$f"; log "removed $f"; fi
    else
      log "not present: $f"
    fi
  done
  restart_rsyslog
  note "The logfile $LOG_PATH (and its rotated copies) were left in place — remove by hand if you want them gone."
  log "Done. Offband caplog routing removed; rsyslog restarted."
  exit 0
fi

# ---- install --------------------------------------------------------------
log "Installing Offband caplog receiver (port $PORT -> $LOG_PATH, keep ${RETAIN_WEEKS}w)..."

# Is imudp already loaded anywhere BUT our own drop-in? rsyslog hard-errors on a
# duplicate module load, so if the host already listens on UDP we must NOT load
# it (or bind an input) again — we only add the routing rule. Match BOTH the
# modern `module(load="imudp")` and the legacy `$ModLoad imudp` syntaxes; then
# drop our own conf from the results (re-runs just overwrite it, so it doesn't
# count as "elsewhere").
imudp_elsewhere=0
if grep -rlE 'load="?imudp|ModLoad[[:space:]]+imudp' /etc/rsyslog.conf /etc/rsyslog.d/ 2>/dev/null \
     | grep -qvF "$RSYSLOG_CONF"; then
  imudp_elsewhere=1
fi

if [ "$imudp_elsewhere" -eq 1 ]; then
  note "rsyslog already loads imudp elsewhere — NOT binding a new UDP input."
  note "Verify an existing UDP input covers port $PORT (e.g. in /etc/rsyslog.conf)."
  write_file "$RSYSLOG_CONF" <<EOF
# Offband caplog receiver (#569) — routing only.
# imudp is loaded elsewhere on this host; verify a UDP input covers port $PORT.
# Match the devices' tag (caplog-<node>:) by programname so ONLY caplog lines
# are captured; the host's other syslog traffic is untouched.
if (\$programname startswith "caplog-") then {
    action(type="omfile" file="$LOG_PATH")
    stop
}
EOF
else
  write_file "$RSYSLOG_CONF" <<EOF
# Offband caplog receiver (#569).
# Binds a UDP syslog listener and routes the Offband devices' datagrams into a
# dedicated file by matching their tag (caplog-<node>:) on programname — so ONLY
# caplog lines are captured and the host's other syslog traffic is untouched.
# \`stop\` keeps the matched caplog lines out of /var/log/syslog as well.
module(load="imudp")
input(type="imudp" port="$PORT")

if (\$programname startswith "caplog-") then {
    action(type="omfile" file="$LOG_PATH")
    stop
}
EOF
fi

write_file "$LOGROTATE_CONF" <<EOF
# Offband caplog logfile rotation (#569).
$LOG_PATH {
    weekly
    rotate $RETAIN_WEEKS
    compress
    delaycompress
    missingok
    notifempty
    create 0640 syslog adm
    postrotate
        /usr/lib/rsyslog/rsyslog-rotate 2>/dev/null || true
    endscript
}
EOF

restart_rsyslog

cat >&2 <<EOF

Done. Offband caplog receiver is live.

  Listening : UDP $PORT $( [ "$imudp_elsewhere" -eq 1 ] && echo "(via the host's existing imudp input — verify it covers $PORT)" )
  Logfile   : $LOG_PATH  (rotated weekly, ${RETAIN_WEEKS} kept)

Verify it end to end:
  1. Send a test line from another host:
       logger -n <this-host-ip> -P $PORT -p local0.info -t caplog-test "hello"
     then on this host:
       tail -n1 $LOG_PATH        # -> ... caplog-test hello
  2. Point a device at it:  set syslog.host <this-host-ip>  (set syslog.port $PORT if not 514)
     then arm:              caplog forward 300
  3. Watch one node's lines: grep -F 'caplog-<node>:' $LOG_PATH

If this host runs a firewall, allow inbound UDP $PORT (e.g. ufw allow $PORT/udp).
Roll back any time:  sudo $0 --uninstall
EOF
