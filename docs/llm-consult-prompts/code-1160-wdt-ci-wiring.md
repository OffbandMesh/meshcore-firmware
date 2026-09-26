# Review: #1160 — watchdog tests wired into CI as blocking steps

Third task in the #1083 chain (epic #446, OffbandMesh/meshcore-firmware). You reviewed
#1083 and #1159 today. This is the CI wiring so those tests gate merges. Review the
workflow diff and the self-test for: steps that could pass without running, ordering or
dependency mistakes, anything that would make `ci-green` report success with these unrun.
Rank Critical / High / Medium / Low; say when a level is empty.

## Context

`ci.yml` has a `config-lint` job (explicit `python scripts/test_*.py` steps — nothing
discovers script tests, they run only if named), a `native-tests` job (discovers every
`platform = native` env and runs `pio test` on each — the googletest suite
`test/test_wdt_timeout/` from #1083 is picked up that way), a `build` matrix, and a
`ci-green` aggregator with `needs: [config-lint, native-tests, build]` that is the required
check on `firmware-base`. Two earlier script tests (`test_cli_dispatch.py`,
`check_cli_dispatch.py`) are `continue-on-error: true` because their fixes had not landed
when they were added; the watchdog tree is clean, so the new steps are blocking.

## The change (diff attached)

- Four blocking steps in `config-lint`, placed before the advisory CLI-dispatch block:
  `scripts/test_wdt_guard.py`, `scripts/check_wdt_guard.py`,
  `scripts/test_wdt_hang_verb.py`, `scripts/test_ci_wdt_steps.py`.
- `scripts/test_ci_wdt_steps.py` (attached): pins that each script is a `run:` step in
  `config-lint`, none carries `continue-on-error`, `ci-green` needs and tests both
  `config-lint` and `native-tests`, and `native-tests` discovers envs rather than listing
  them. Light structural parse; no PyYAML dependency.
- Local proof the guard goes red on a regression: `check_wdt_guard.py` run against the
  pre-#1083 role files reports the four original call sites and exits 1.

## Questions

1. Any way one of these steps is skipped or reports success without executing its script?
2. The self-test parses YAML structurally with regexes (job = two-space-indented
   `name:`, step = six-space `- name:`). Is that fragile against the file as it stands, and
   is it worth a PyYAML dependency instead?
3. `check_wdt_guard.py` exits 2 on a missing target file. Does the workflow treat that as
   failure (it should), and is that the right behaviour if a role's `main.cpp` is ever
   renamed?
4. Anything that passes without proving what it claims.

__FILES__
