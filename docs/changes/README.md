# Documentation-impact decisions

Add or change `<issue>-<short-description>.yml` with each firmware-code PR.
Use `schema_version: 1`, a matching positive `issue`, `impact: updated` or `none`,
and a concrete `reason`. An `updated` record also names public Markdown `guides`
changed in that PR. A `none` decision is reviewed for accuracy, not accepted merely
because it passes the checker. Retain historical records.

See [the documentation workflow](../documentation-workflow.md) for examples,
planning, review and manual publication. These records are internal delivery
metadata; only `docs/user/` is staged into the public manual.
