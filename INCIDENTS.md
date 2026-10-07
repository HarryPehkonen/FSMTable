## 2026-10-06 — the gate is no longer a 902-line bash script (wiring record, not a breakage)

What changed:      `tools/ci.sh` is deleted. The gate is now `gate.toml` — the policy: 11 stages
                   in two tiers — run by `kit-ci`, one binary installed once per machine
                   (`cmake --install build --prefix ~/.local`), plus `scripts/gate-env.sh` (the
                   .ci.env knobs and the helpers every stage reads), `scripts/gate.sh` (the one
                   file all three callers run) and one `scripts/<stage>.sh` per stage. The two
                   hooks are the kit's files with one line changed each: they NAME the tier
                   instead of repeating a stage list. This entry is a wiring record, not an
                   incident — it is here because the hooks and the retired gate both say "if you
                   edit this file, say why in INCIDENTS.md".
Check moved:       Every stage the old gate ran is still run, under the same name and in the
                   same order. full tier: tree format build tests release version asan tsan tidy pristine fuzz. fast tier: format build tests.
                   The teeth were re-measured on the converted gate rather than assumed:
                   an unformatted new source file dropped in the tree makes the fast tier
                   report GATE FAILED naming the `format` stage — captured in
                   `gate-evidence/t_075c0a6f/FSMTable-teeth.txt`.
Probes:            the `kitprobes` stage and `tools/kit-probes/` are DELETED. A probe checks a
                   FILE, and the file it checked (tools/ci.sh) is gone; a stage whose verdict is
                   an accident of how a probe searches is the failure mode the gate exists to
                   prevent. Measured 2026-10-06, each probe from HEAD run against the new entry
                   point `scripts/gate.sh`:
                   * format-checks-staged.sh      GREEN against scripts/gate.sh
                   * gitignore-footprint.sh       GREEN against scripts/gate.sh
                   * hook-tiers-agree.sh          RED against scripts/gate.sh
                   The guarantees themselves did not go:
                   * format-checks-staged      -> scripts/format.sh — the staged-copy check is the same code, in the stage that owns formatting
                   * gitignore-footprint     -> scripts/tree.sh — the .gitignore audit is the tree stage's first job
                   * hook-tiers-agree          -> gate.toml — the tier lists are [tier.fast] and [tier.full], the hooks NAME a tier, and `kit-ci --list` prints what each one holds
                   and gate-stage-guards is subsumed by the engine's runner (a stage whose
                   command does not exit 0 fails the run).

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
