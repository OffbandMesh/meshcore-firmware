#!/usr/bin/env python3
"""
scripts/llm-consult.py - Multi-vendor LLM consult wrapper.

Strycher/LoRa#283 / LoRa-b72: lets the agent solicit independent second
opinions from external LLMs during diagnostic and review work, to
triangulate findings against within-session bias.

DESIGN
------
Option A (per #283 discussion): re-use Rossum's proven multi-vendor LLM
client code in C:/Dev/Rossum/shared/llm_client.py via sys.path injection.
No code duplication, no DifferentWire-level promotion yet. Rossum's clients
already handle 429 + Retry-After backoff, configurable timeouts, four
vendors (Claude, OpenAI, Gemini, Perplexity).

claude-cli backend special-cases to the local Claude Code CLI (`claude -p`)
which bills against the user's Pro/Max plan quota instead of metered API
tokens. Zero incremental cost for Claude consults; ideal for the bulk of
multi-vendor triangulation work where we want a Claude second opinion.

USAGE
-----
  python scripts/llm-consult.py \\
    --backend gemini \\
    --model gemini-2.0-pro \\
    --files src/helpers/wifi_observer/*.cpp,examples/companion_radio/main.cpp \\
    --prompt-file docs/heap-audit-prompt.md \\
    --max-tokens 8000 \\
    --dry-run

  python scripts/llm-consult.py \\
    --backend claude-cli \\
    --model claude-opus-4-5 \\
    --files docs/audit-context.md \\
    --prompt-file docs/heap-audit-prompt.md

SAFETY
------
- API keys read from env only (never CLI arg, never logged).
- Hardcoded denylist refuses --files paths matching any of: secret,
  credentials, .key, .pem, platformio.local.ini, platformio.secrets.ini,
  meshtastic_keys. Belt-and-suspenders against a careless glob.
- --dry-run is the recommended pre-send: shows bundle preview, token
  count, and per-vendor cost estimate without calling.
- Every non-dry-run send writes a full audit record under
  docs/llm-consultations/YYYY-MM-DD-<topic>-<vendor>-<model>.log.

DEPENDENCIES
------------
- Python 3.11+
- httpx (for Rossum's clients): `pip install httpx`
- `claude` CLI on PATH (only required for --backend claude-cli)
- API keys in env (only required for the backend you invoke):
    ANTHROPIC_API_KEY     for --backend anthropic
    OPENAI_API_KEY        for --backend openai
    GEMINI_API_KEY        for --backend gemini  (GOOGLE_API_KEY also accepted)
    PERPLEXITY_API_KEY    for --backend perplexity
"""
from __future__ import annotations

import argparse
import asyncio
import datetime
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from glob import glob
from pathlib import Path

# Path-coupled import of Rossum's multi-vendor LLM client.
# Tracked for cleanup: see #283 Followup section.
_ROSSUM_PATH = "C:/Dev/Rossum"
if _ROSSUM_PATH not in sys.path:
    sys.path.insert(0, _ROSSUM_PATH)
try:
    from shared.llm_client import LLMClientError, create_llm_client  # noqa: E402
except ImportError as exc:  # pragma: no cover
    print(
        f"[llm-consult] FATAL: failed to import Rossum's shared.llm_client.\n"
        f"  Tried sys.path entry: {_ROSSUM_PATH}\n"
        f"  Original error: {exc}\n"
        f"  Fix: ensure {_ROSSUM_PATH} exists and `pip install httpx`.",
        file=sys.stderr,
    )
    sys.exit(2)


# ----------------------------------------------------------------------
# Constants
# ----------------------------------------------------------------------

# Paths matching any of these substrings (case-insensitive) are refused
# regardless of intent. Belt-and-suspenders against a careless glob.
SECRET_DENYLIST = (
    "secret",
    "credentials",
    ".key",
    ".pem",
    "platformio.local.ini",
    "platformio.secrets.ini",
    "meshtastic_keys",
)

# Per-million-token costs, USD. Update as vendor pricing changes.
# (in_per_mtok, out_per_mtok). Used for the dry-run cost preview only;
# real billing comes from each vendor.
COST_TABLE = {
    "claude-cli": (0.0, 0.0),  # billed against Pro/Max plan, zero incremental
    "anthropic": {
        "default": (3.0, 15.0),
        "claude-opus-4-5": (15.0, 75.0),
        "claude-opus-4": (15.0, 75.0),
        "claude-sonnet-4-5": (3.0, 15.0),
        "claude-sonnet-4": (3.0, 15.0),
        "claude-haiku-4-5": (1.0, 5.0),
    },
    "openai": {
        "default": (2.5, 10.0),
        "gpt-4o": (2.5, 10.0),
        "gpt-4o-mini": (0.15, 0.6),
        "gpt-4-turbo": (10.0, 30.0),
        "o1": (15.0, 60.0),
        "o1-mini": (3.0, 12.0),
    },
    "gemini": {
        "default": (1.25, 5.0),
        "gemini-2.0-pro": (1.25, 5.0),
        "gemini-2.5-pro": (1.25, 5.0),
        "gemini-2.0-flash": (0.075, 0.3),
        "gemini-1.5-pro": (1.25, 5.0),
        "gemini-1.5-flash": (0.075, 0.3),
    },
    "perplexity": {
        "default": (1.0, 1.0),
        "sonar-large": (1.0, 1.0),
        "sonar-small": (0.2, 0.2),
    },
}

# Rough estimate: 1 token ~= 4 characters for English text + code mix.
# Real tokenization varies per vendor; this is for cost preview only.
CHARS_PER_TOKEN = 4

# Env var names accepted per backend.
ENV_KEYS = {
    "anthropic": ("ANTHROPIC_API_KEY",),
    "openai": ("OPENAI_API_KEY",),
    "gemini": ("GEMINI_API_KEY", "GOOGLE_API_KEY"),
    "perplexity": ("PERPLEXITY_API_KEY",),
}

# Map our --backend names to Rossum's _BACKEND_MAP keys.
ROSSUM_BACKEND_MAP = {
    "anthropic": "claude",
    "openai": "openai",
    "gemini": "gemini",
    "perplexity": "perplexity",
}

# Where audit logs go. Created on demand. Repo-relative (scripts/ -> ../docs/)
# so consults log into THIS repo's docs/llm-consultations (ported from LoRa,
# which hardcoded C:/Dev/LoRa; deriving from __file__ keeps it portable).
AUDIT_LOG_DIR = Path(__file__).resolve().parent.parent / "docs" / "llm-consultations"

# Placeholder substituted with the bundled file content.
FILES_PLACEHOLDER = "__FILES__"


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------


@dataclass
class Bundle:
    """A bundle of files prepared for a single LLM consult."""

    paths: list[Path]
    rendered: str  # what gets sent: each file under '=== path ===' header
    char_count: int

    @property
    def estimated_input_tokens(self) -> int:
        return self.char_count // CHARS_PER_TOKEN


def check_secret_denylist(paths: list[Path]) -> None:
    """Refuse any path matching the SECRET_DENYLIST; otherwise return.

    Exits with status 2 on first match, printing the offending path and
    the matched pattern to stderr.
    """
    for path in paths:
        as_lower = str(path).lower()
        for pattern in SECRET_DENYLIST:
            if pattern in as_lower:
                print(
                    f"[llm-consult] REFUSED: path {path!s} matches secret "
                    f"denylist pattern {pattern!r}. Adjust --files or "
                    f"explicitly omit secret files.",
                    file=sys.stderr,
                )
                sys.exit(2)


def expand_files_arg(files_arg: str) -> list[Path]:
    """Expand a comma-separated list of literal paths and globs to Path[]."""
    raw_specs = [s.strip() for s in files_arg.split(",") if s.strip()]
    paths: list[Path] = []
    seen: set[str] = set()
    for spec in raw_specs:
        matches = glob(spec, recursive=True)
        if not matches:
            # If literal path doesn't exist, treat as fatal (typo guard).
            if not Path(spec).exists():
                print(
                    f"[llm-consult] WARN: no matches for spec {spec!r} "
                    f"(treating as literal path; file does not exist)",
                    file=sys.stderr,
                )
                sys.exit(2)
            matches = [spec]
        for match in matches:
            mpath = Path(match).resolve()
            if str(mpath) in seen:
                continue
            seen.add(str(mpath))
            if not mpath.is_file():
                # Skip directories silently; only file globs make sense here.
                continue
            paths.append(mpath)
    if not paths:
        print(
            "[llm-consult] FATAL: --files expanded to zero files.",
            file=sys.stderr,
        )
        sys.exit(2)
    return paths


def build_bundle(paths: list[Path]) -> Bundle:
    """Bundle file contents with '=== path ===' headers for the LLM."""
    parts: list[str] = []
    for path in paths:
        try:
            content = path.read_text(encoding="utf-8", errors="replace")
        except Exception as exc:
            print(
                f"[llm-consult] WARN: failed to read {path}: {exc}; "
                f"including a placeholder note instead.",
                file=sys.stderr,
            )
            content = f"<<llm-consult: failed to read this file: {exc}>>"
        parts.append(f"=== {path} ===\n{content}\n")
    rendered = "\n".join(parts)
    return Bundle(paths=paths, rendered=rendered, char_count=len(rendered))


def load_prompt(prompt_file: Path, bundle: Bundle) -> str:
    """Read a prompt file; substitute FILES_PLACEHOLDER with bundle.rendered.

    If the prompt does not contain FILES_PLACEHOLDER, the bundle is
    appended at the end under a clear delimiter.
    """
    raw = prompt_file.read_text(encoding="utf-8", errors="replace")
    if FILES_PLACEHOLDER in raw:
        return raw.replace(FILES_PLACEHOLDER, bundle.rendered)
    return (
        f"{raw}\n\n"
        f"--- Bundled files (no {FILES_PLACEHOLDER} placeholder in prompt) ---\n\n"
        f"{bundle.rendered}"
    )


def lookup_cost(backend: str, model: str) -> tuple[float, float]:
    """Look up (input_per_mtok, output_per_mtok) for a backend+model."""
    entry = COST_TABLE.get(backend)
    if entry is None:
        return (0.0, 0.0)
    if isinstance(entry, tuple):
        return entry
    return entry.get(model, entry["default"])


def estimate_cost(
    backend: str, model: str, in_tokens: int, out_tokens: int
) -> float:
    """Estimate USD cost for in_tokens + out_tokens at backend+model rates."""
    in_rate, out_rate = lookup_cost(backend, model)
    return (in_tokens / 1_000_000.0) * in_rate + (out_tokens / 1_000_000.0) * out_rate


def get_api_key(backend: str) -> str:
    """Read API key from env for the named backend; fatal if absent."""
    names = ENV_KEYS.get(backend, ())
    for name in names:
        val = os.environ.get(name)
        if val:
            return val.strip()
    print(
        f"[llm-consult] FATAL: no API key in env for backend {backend!r}. "
        f"Set one of: {', '.join(names)}.",
        file=sys.stderr,
    )
    sys.exit(2)


# ----------------------------------------------------------------------
# Backend sends
# ----------------------------------------------------------------------


def send_claude_cli(prompt: str, model: str, max_tokens: int) -> tuple[str, dict]:
    """Send via the local `claude` Claude Code CLI (zero-cost path).

    Returns (response_text, metadata_dict). metadata_dict mirrors what
    we record from the API path so the audit log shape is consistent.
    """
    if shutil.which("claude") is None:
        print(
            "[llm-consult] FATAL: --backend claude-cli requires the `claude` "
            "binary on PATH. Install Claude Code CLI or choose a different "
            "backend.",
            file=sys.stderr,
        )
        sys.exit(2)
    # claude -p (or --print) is the one-shot non-interactive mode in
    # current Claude Code CLI. Output goes to stdout.
    cmd = ["claude", "-p", "--model", model, prompt]
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        print(
            f"[llm-consult] FATAL: claude CLI exited {result.returncode}.\n"
            f"  stderr: {result.stderr[:500]}",
            file=sys.stderr,
        )
        sys.exit(2)
    return result.stdout, {
        "backend": "claude-cli",
        "model": model,
        "input_tokens": None,  # CLI does not surface counts in --print mode
        "output_tokens": None,
        "latency_ms": None,
        "finish_reason": "cli_complete",
        "billing": "Claude Code Pro/Max plan quota (zero incremental USD)",
    }


async def _send_via_rossum_async(
    backend: str, model: str, prompt: str, max_tokens: int
) -> tuple[str, dict]:
    """Async core: invoke a Rossum LLM client and return (text, meta)."""
    rossum_backend = ROSSUM_BACKEND_MAP[backend]
    api_key = get_api_key(backend)
    client = create_llm_client(
        backend=rossum_backend, api_key=api_key, model=model
    )
    try:
        response = await client.complete(
            messages=[{"role": "user", "content": prompt}],
            max_tokens=max_tokens,
        )
    except LLMClientError as exc:
        print(f"[llm-consult] FATAL: vendor call failed: {exc}", file=sys.stderr)
        sys.exit(2)
    finally:
        await client.close()
    meta = {
        "backend": backend,
        "model": response.model,
        "input_tokens": response.input_tokens,
        "output_tokens": response.output_tokens,
        "latency_ms": response.latency_ms,
        "finish_reason": response.finish_reason,
    }
    return response.content, meta


def send_via_rossum(
    backend: str, model: str, prompt: str, max_tokens: int
) -> tuple[str, dict]:
    """Sync wrapper around the async Rossum send."""
    return asyncio.run(_send_via_rossum_async(backend, model, prompt, max_tokens))


# ----------------------------------------------------------------------
# Audit log
# ----------------------------------------------------------------------


def write_audit_log(
    topic: str,
    backend: str,
    model: str,
    bundle: Bundle,
    prompt: str,
    response_text: str,
    response_meta: dict,
    estimated_cost_usd: float,
) -> Path:
    """Persist a full record under docs/llm-consultations/."""
    AUDIT_LOG_DIR.mkdir(parents=True, exist_ok=True)
    today = datetime.date.today().isoformat()
    safe_topic = "".join(c if c.isalnum() or c in "-_" else "_" for c in topic)
    safe_backend = backend.replace("/", "_")
    safe_model = model.replace("/", "_")
    fname = f"{today}-{safe_topic}-{safe_backend}-{safe_model}.log"
    path = AUDIT_LOG_DIR / fname
    record = {
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "topic": topic,
        "backend": backend,
        "model": model,
        "bundle_files": [str(p) for p in bundle.paths],
        "bundle_char_count": bundle.char_count,
        "estimated_input_tokens": bundle.estimated_input_tokens,
        "estimated_cost_usd": estimated_cost_usd,
        "response_meta": response_meta,
        "prompt": prompt,
        "response": response_text,
    }
    path.write_text(json.dumps(record, indent=2), encoding="utf-8")
    return path


# ----------------------------------------------------------------------
# main
# ----------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Multi-vendor LLM consult wrapper (Strycher/LoRa#283).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--backend",
        required=True,
        choices=["claude-cli", "anthropic", "openai", "gemini", "perplexity"],
        help=(
            "Which vendor to consult. 'claude-cli' uses the local `claude` "
            "binary and bills against your Pro/Max plan; the others use API "
            "keys from env."
        ),
    )
    parser.add_argument(
        "--model",
        required=True,
        help="Vendor model id (e.g. gpt-4o, gemini-2.5-pro, claude-opus-4-5).",
    )
    parser.add_argument(
        "--files",
        required=True,
        help=(
            "Comma-separated list of file paths or globs. Each match is "
            "bundled into the prompt under '=== path ===' headers. "
            "Refused if any path matches the hardcoded secret denylist."
        ),
    )
    parser.add_argument(
        "--prompt-file",
        required=True,
        type=Path,
        help=(
            f"Markdown prompt file. Use the placeholder {FILES_PLACEHOLDER} "
            "where the file bundle should be inserted; if absent, the bundle "
            "is appended at the end."
        ),
    )
    parser.add_argument(
        "--max-tokens",
        type=int,
        default=8000,
        help="Max response tokens (default 8000).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help=(
            "Show bundled prompt + token + cost estimates without sending. "
            "Recommended before any real send."
        ),
    )
    parser.add_argument(
        "--output-file",
        type=Path,
        default=None,
        help="Write response to this file instead of stdout.",
    )
    parser.add_argument(
        "--topic",
        default=None,
        help=(
            "Short slug for the audit log filename. Defaults to the prompt "
            "file's stem."
        ),
    )

    args = parser.parse_args()

    # Validate prompt file
    if not args.prompt_file.exists():
        print(
            f"[llm-consult] FATAL: --prompt-file does not exist: {args.prompt_file}",
            file=sys.stderr,
        )
        return 2

    # Expand and guard files
    paths = expand_files_arg(args.files)
    check_secret_denylist(paths)

    # Build bundle and prompt
    bundle = build_bundle(paths)
    prompt = load_prompt(args.prompt_file, bundle)

    # Estimate tokens + cost
    prompt_tokens = len(prompt) // CHARS_PER_TOKEN
    estimated_in = prompt_tokens
    estimated_out = args.max_tokens
    estimated_cost = estimate_cost(
        args.backend, args.model, estimated_in, estimated_out
    )

    topic = args.topic or args.prompt_file.stem

    # Dry-run path: print summary and exit 0
    if args.dry_run:
        print("=== llm-consult dry-run ===", file=sys.stderr)
        print(f"  backend:                {args.backend}", file=sys.stderr)
        print(f"  model:                  {args.model}", file=sys.stderr)
        print(f"  topic:                  {topic}", file=sys.stderr)
        print(f"  files bundled:          {len(bundle.paths)}", file=sys.stderr)
        print(f"  bundle chars:           {bundle.char_count:,}", file=sys.stderr)
        print(
            f"  est. input tokens:      {estimated_in:,} (rough; "
            f"~{CHARS_PER_TOKEN} chars/token)",
            file=sys.stderr,
        )
        print(f"  est. output tokens:    {estimated_out:,}", file=sys.stderr)
        in_rate, out_rate = lookup_cost(args.backend, args.model)
        print(
            f"  rates (USD/Mtok):       in={in_rate}, out={out_rate}",
            file=sys.stderr,
        )
        print(
            f"  est. cost (USD):        ${estimated_cost:.4f}",
            file=sys.stderr,
        )
        print(
            f"  audit log destination:  {AUDIT_LOG_DIR}/<date>-{topic}-...",
            file=sys.stderr,
        )
        print(
            "\n(--dry-run: NO request was sent. Re-run without --dry-run to send.)",
            file=sys.stderr,
        )
        return 0

    # Real send path
    print(
        f"[llm-consult] sending: backend={args.backend} model={args.model} "
        f"est_cost=${estimated_cost:.4f}",
        file=sys.stderr,
    )
    if args.backend == "claude-cli":
        response_text, response_meta = send_claude_cli(
            prompt, args.model, args.max_tokens
        )
    else:
        response_text, response_meta = send_via_rossum(
            args.backend, args.model, prompt, args.max_tokens
        )

    # Recompute cost from actual tokens if vendor provided them
    actual_in = response_meta.get("input_tokens")
    actual_out = response_meta.get("output_tokens")
    if actual_in is not None and actual_out is not None:
        actual_cost = estimate_cost(args.backend, args.model, actual_in, actual_out)
    else:
        actual_cost = estimated_cost

    # Write audit log
    log_path = write_audit_log(
        topic=topic,
        backend=args.backend,
        model=args.model,
        bundle=bundle,
        prompt=prompt,
        response_text=response_text,
        response_meta=response_meta,
        estimated_cost_usd=actual_cost,
    )
    print(f"[llm-consult] audit log: {log_path}", file=sys.stderr)
    if actual_in is not None:
        print(
            f"[llm-consult] actual tokens: in={actual_in:,} out={actual_out:,} "
            f"cost=${actual_cost:.4f}",
            file=sys.stderr,
        )

    # Emit response
    if args.output_file is not None:
        args.output_file.write_text(response_text, encoding="utf-8")
        print(f"[llm-consult] response written to {args.output_file}", file=sys.stderr)
    else:
        print(response_text)

    return 0


if __name__ == "__main__":
    sys.exit(main())
