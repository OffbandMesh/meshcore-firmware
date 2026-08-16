## What this changes
Brief description, and which role(s)/variant(s) it affects
(companion / observer / repeater / room / bridge / tooling / docs).

Closes #

## Tests

**If this fixes a bug: does the test embed the broken behaviour, or have you watched it fail?**

A regression test is not verified until it has failed for the right reason. Either:
- keep the pre-fix behaviour in the test as a reference and assert it differs (permanent, runs on every CI run), or
- revert the fix, watch the test fail, restore it, and say so here.

Beware the circular assertion: if a test derives its expected value from the thing under test,
a bug that corrupts that value also corrupts the expectation, and the test passes while the
product is broken. (#711 -- a regression of #450 that shipped because nothing asserted the
invariant, and whose first draft test passed against the broken code.)

- [ ] Tests added/updated, and shown to fail without the fix
- [ ] No test: state why here (only the owner waives a test)

## Verification
- [ ] Builds affected envs (name them; include any DRAM-tight env not in the CI matrix, e.g. `Heltec_v2_companion_radio_ble`)
- [ ] `pio test` green for every native env
- [ ] Hardware-verified, or explicitly stated as NOT hardware-verified

## Review
- [ ] Gemini-reviewed (`scripts/llm-consult.py --backend gemini`); findings fixed or justified below

Note: an LLM review is a second opinion, not a gate that can be trusted alone. The #454 review
certified as "correct" the exact two things that later broke.

## Notes
Anything a reviewer needs: risks, follow-ups filed, what is deliberately out of scope.

Agent: <AMName> (session <uuid8>)
