#!/usr/bin/env bash
# =============================================================================
# DifferentWire Standard Preflight Hook
# =============================================================================
# Runs on SessionStart. Blocks the session (exit 2) if required coordination
# services are unavailable. ALL checks are hard blocks.
#
# Required services:
#   1. Citadel API — task management (HARD BLOCK)
#   2. Agent Mail  — multi-agent coordination (HARD BLOCK)
#
# Exit codes:
#   0 = all checks passed
#   2 = BLOCKING — required service unavailable
#
# Projects copy this to .claude/hooks/preflight.sh and customize PROJECT_DIR.
# =============================================================================

set -euo pipefail

# ─── Project-specific overrides (edit these) ──────────────────────────────
PROJECT_DIR="/c/Dev/meshcore-firmware"
# ──────────────────────────────────────────────────────────────────────────

CITADEL_HEALTH="https://getunfocused.app/citadel/health"
AGENT_MAIL_HEALTH="https://getunfocused.app/health/liveness"
HETZNER_HOST="unfocused@46.224.181.82"

PASS="✓"
FAIL="✗"
WARN="⚠"
errors=0

log()  { echo "$@" >&2; }
pass() { log "$PASS $1"; }
fail() { log "$FAIL $1"; errors=$((errors + 1)); }
warn() { log "$WARN $1"; }

log ""
log "═══ Preflight Check ═══"
log ""

# ─── 1. Citadel API (HARD BLOCK) ────────────────────────────────────────
# Validate response body contains "citadel", not just HTTP 200.
# The Flutter catch-all returns 200 with HTML for any path — checking
# status code alone produces false positives.
CITADEL_BODY=$(curl -s --max-time 5 "$CITADEL_HEALTH" 2>/dev/null || echo "")
if echo "$CITADEL_BODY" | grep -q '"citadel"'; then
  pass "Citadel API"
else
  fail "Citadel API unreachable or returning wrong content"
  log "  Response: ${CITADEL_BODY:0:80}"
  log "  No task management = no work. Fix before proceeding."
  log "  Check: ssh ${HETZNER_HOST} \"docker ps | grep api\""
fi

# ─── 2. Agent Mail (HARD BLOCK) ─────────────────────────────────────────
if curl -s --connect-timeout 5 "$AGENT_MAIL_HEALTH" 2>/dev/null | grep -q "alive"; then
  pass "Agent Mail"
else
  fail "Agent Mail unreachable"
  log "  No coordination layer = no multi-agent safety. Fix before proceeding."
  log "  Check: ssh ${HETZNER_HOST} \"cd /opt/mcp_agent_mail && docker compose -f docker-compose.prod.yml logs --tail 20\""
fi

# ─── 3. Git Hooks Path ──────────────────────────────────────────────────
HOOKS_PATH=$(cd "$PROJECT_DIR" && git config core.hookspath 2>/dev/null || echo "")
if [ "$HOOKS_PATH" = ".claude/hooks" ]; then
  pass "Git hooks path (.claude/hooks)"
else
  warn "Git hooks path is '${HOOKS_PATH:-<unset>}' — fixing to .claude/hooks"
  cd "$PROJECT_DIR" && git config core.hookspath .claude/hooks
  pass "Git hooks path fixed"
fi

# ─── 4. Abandoned Worktree Detection ────────────────────────────────────
ABANDONED=()
while IFS= read -r wt_line; do
  wt_path=$(echo "$wt_line" | awk '{print $1}')
  wt_branch=$(echo "$wt_line" | awk '{print $2}' | tr -d '[]')
  if [ "$wt_path" = "$PROJECT_DIR" ]; then continue; fi
  if [ -d "$wt_path" ]; then
    LAST_COMMIT=$(cd "$wt_path" 2>/dev/null && git log -1 --format=%ct 2>/dev/null || echo "0")
    NOW=$(date +%s)
    AGE_HOURS=$(( (NOW - LAST_COMMIT) / 3600 ))
    if [ "$AGE_HOURS" -gt 24 ]; then
      ABANDONED+=("$wt_path (branch: $wt_branch, ${AGE_HOURS}h ago)")
    fi
  fi
done < <(cd "$PROJECT_DIR" && git worktree list 2>/dev/null | grep -v "^$")

if [ ${#ABANDONED[@]} -gt 0 ]; then
  warn "Found ${#ABANDONED[@]} potentially abandoned worktree(s):"
  for wt in "${ABANDONED[@]}"; do
    log "    $wt"
  done
fi

# ─── 5. Canonical Slash Commands Sync ───────────────────────────────────
# Sync command files from standards/.claude/commands/ into this project's
# .claude/commands/. Convention: any file in standards/.claude/commands/
# is canonical — meant to be available in every DW project. Updates
# propagate to all projects at next SessionStart, OR when /refresh-context
# is invoked in a long-running session (since /refresh-context invokes this
# preflight). Project-local-only commands (like /work) live in
# <project>/.claude/commands/ but have no canonical counterpart, so the
# sync never touches them.

CANONICAL_CMDS="/c/Dev/DifferentWire/standards/.claude/commands"
PROJECT_CMDS="$PROJECT_DIR/.claude/commands"

if [ -d "$CANONICAL_CMDS" ]; then
  mkdir -p "$PROJECT_CMDS"
  synced=0
  installed=0
  backed_up=0
  # Guard against `set -e` aborting on the cmp/cp non-zero returns
  for canonical in "$CANONICAL_CMDS"/*.md; do
    [ -f "$canonical" ] || continue
    name=$(basename "$canonical")
    local="$PROJECT_CMDS/$name"
    if [ ! -f "$local" ]; then
      cp "$canonical" "$local" && installed=$((installed + 1)) || true
    elif ! cmp -s "$canonical" "$local"; then
      # First-install backup for commands that were project-local before
      # becoming canonical (e.g., work.md prior to standards#112). If a
      # ".pre-canonical.bak" doesn't already exist for this command, the
      # current local file is a project-local hand-roll — preserve it
      # before the sync overwrites with canonical. Subsequent updates to
      # the synced file are overwritten without backup (canonical owns
      # the file once installed).
      backup="$PROJECT_CMDS/${name}.pre-canonical.bak"
      if [ ! -f "$backup" ]; then
        cp "$local" "$backup" 2>/dev/null && backed_up=$((backed_up + 1)) || true
      fi
      cp "$canonical" "$local" && synced=$((synced + 1)) || true
    fi
  done
  if [ "$installed" -gt 0 ] || [ "$synced" -gt 0 ] || [ "$backed_up" -gt 0 ]; then
    if [ "$backed_up" -gt 0 ]; then
      pass "Canonical commands: $installed installed, $synced updated, $backed_up pre-canonical backup(s) saved (.pre-canonical.bak)"
    else
      pass "Canonical commands: $installed installed, $synced updated"
    fi
  else
    pass "Canonical commands: up to date"
  fi
else
  warn "Canonical commands dir not found at $CANONICAL_CMDS — standards repo not at canonical path?"
fi

# ─── 6. Session State Recovery ───────────────────────────────────────────
#
# Hook-point for the session-state script — a per-project tool that writes
# a snapshot of the agent's volatile state (claimed Citadel tasks, active
# worktree, Agent Mail identity, open PRs, last Tier 2 approval, in-flight
# notes) to .claude/agent-session.json. Preflight reads it here so post-
# compaction sessions resume with structured handoff.
#
# Canonical writer (as of standards#107): hooks/session-state.py. Lifted
# from the Strycher/LoRa#260 prototype on 2026-06-04. Projects install via
# copy + post-commit wire-up (REPOCONFIG.md §Enforcement Hooks); future
# auto-propagation tracked at standards#76.
#
# This block tries .py FIRST (canonical), then .sh as transition fallback
# for any project still on a hand-rolled bash variant.
#
PREFLIGHT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SESSION_STATE_FILE="$PROJECT_DIR/.claude/agent-session.json"
SESSION_STATE_SCRIPT_PY="$PREFLIGHT_DIR/session-state.py"
SESSION_STATE_SCRIPT_SH="$PREFLIGHT_DIR/session-state.sh"

if [ -f "$SESSION_STATE_FILE" ]; then
  SESSION_OUTPUT=""
  if [ -f "$SESSION_STATE_SCRIPT_PY" ]; then
    # Prefer python3 if present, else python.
    if command -v python3 >/dev/null 2>&1; then
      SESSION_OUTPUT=$(python3 "$SESSION_STATE_SCRIPT_PY" read 2>/dev/null || true)
    elif command -v python >/dev/null 2>&1; then
      SESSION_OUTPUT=$(python "$SESSION_STATE_SCRIPT_PY" read 2>/dev/null || true)
    fi
  elif [ -f "$SESSION_STATE_SCRIPT_SH" ]; then
    SESSION_OUTPUT=$(bash "$SESSION_STATE_SCRIPT_SH" read 2>/dev/null || true)
  fi
  if [ -n "$SESSION_OUTPUT" ]; then
    log ""
    log "═══ Active Agent Session ═══"
    log ""
    while IFS= read -r line; do
      log "  $line"
    done <<< "$SESSION_OUTPUT"
    log ""
  fi
fi

# ─── Result ──────────────────────────────────────────────────────────────
log ""
if [ "$errors" -gt 0 ]; then
  log "═══ BLOCKED: $errors check(s) failed ═══"
  log "Agents must not operate without Citadel AND Agent Mail."
  log ""
  exit 2
fi

log "═══ All checks passed — session ready ═══"
log ""
exit 0
