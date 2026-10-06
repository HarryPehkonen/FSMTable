# INCIDENTS.md — edits to files that came from the kit, and why

The gate and hook files copied in from AI-DEV-STARTER carry a line asking that an edit here says
why. This is that record, newest first.

## 2026-10-06 — the hooks name a tier; the tier lists moved into the gate

`.githooks/pre-commit` and `.githooks/pre-push` used to spell out their stages, and this repo's
`tools/ci.sh` had grown past them: its list had gained `fuzz` (the stage the spec requires) and
`lint`, while the hooks still spoke the kit's original eleven. Every push therefore ran one stage
fewer than a hand run, and the stage it skipped was the fuzzer. The same day, a commit whose files
clang-format would have rewritten passed the fast tier, because `format` sat in the minutes-tier.

Both hooks now pass `fast` or `full`; the two lists live in `tools/ci.sh` as `CI_FAST_STAGES` and
`CI_FULL_STAGES`; `format` moved into the fast tier; and `tools/kit-probes/hook-tiers-agree.sh`
fails the gate when a hook names a stage again, when a stage the gate defines is in no tier, or when
the lists printed in the docs stop matching the variables.
