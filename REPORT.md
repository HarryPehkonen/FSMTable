# REPORT.md — FSMTable, stages A, B and C

    Gate verdict line (verbatim):
    all 12 stage(s) passed in 94s
    GATE PASSED
    Both lines are from the run on commit 3712fb8; this file is the only difference from that
    tree, and the run was repeated after it was added.

    Test count:
    46, in two binaries, all passing.
      * fsmTable_tests — 41 ("41 tests from 6 test suites ran", "[  PASSED  ] 41 tests").
        Stage A — 24: the 21 frozen names of SPEC.md section 5, under exactly those names, plus
        RejectsVersionThatIsNotFirst (rule 1, which the frozen names do not reach),
        EveryCorpusFileObeysTheFrozenRules (section 7) and NumbersAgreeWithTheString (the kit's
        version template asks for it). Stage B — 17: the two analyses of section 8, including
        one that walks the corpus and holds every machine in it to the contract both share.
      * fsmTable_differential — 5 (stage C): 3 corpus machines, 200 generated machine/seed
        pairs, 40 canonical-round-trip comparisons and 2 for the deliberate-disagreement control
        — 285 trace comparisons over 54606 events, each comparing the state after every event,
        the action log, and the count of events that moved nothing.

    Fuzz stage: runs / seconds / artifacts
    2777953 runs / 60 s / 0 artifacts
    "#2777953	DONE   cov: 252 ft: 946 corp: 63/5490b lim: 4096 exec/s: 45540 rss: 462Mb"
    The run uses a copy of the frozen corpus inside the build directory, not the tracked
    corpus/: libFuzzer writes new coverage-increasing inputs into the directory it is handed,
    and doing that to the tracked tree would leave it dirty.
    Stage B raised coverage from 150 to 252 (edges) and from 457 to 946 (features) by holding
    the analyses to the same invariants as the rest of the library. Stage C adds no library code
    for the target to reach, so its numbers are unchanged from that run.

    Stage reached:
    Stage C, the last one section 8 lists. All three stages are green.

    What I could not do:
    Nothing that stages A, B and C ask for. Four things belong here rather than being left out:
      * stage A was delivered with the kit's hooks PRESENT but NOT ARMED in this repository: the
        copy from the arena brought the files, not .git/config, so core.hooksPath was unset and
        no hook ever ran. Armed in stage B, and every commit since has run the hook.
      * the gate now REQUIRES FSMgine 2.1.0's headers at ~/hermes-workspace/FSMgine (override:
        -DFSMTABLE_FSMGINE_DIR=<checkout>). Stage C's oracle is an independent library, and a
        configuration without it fails loudly rather than skipping the cross-check — so a clone
        on a machine with no FSMgine cannot configure this repo at all. That is the price of
        section 8's oracle, stated rather than hidden. FSMgine itself is untouched: only its
        headers are read, and nothing is built inside its tree.
      * two clang-tidy findings remain accepted in .ci/tidy-baseline.txt because the text they
        sit on is not this repo's to change: the underlying type of `enum class Op`, frozen by
        section 4, and libFuzzer's entry-point signature. Every other finding the lint stage
        reported was fixed in the code rather than accepted.
      * the first version of the footprint probe this repo carries was too strict: it demanded
        the kit's audit line byte-for-byte, while this gate legitimately audits a seventh path
        (its fuzz build dir). The probe now derives the audited set from the gate it checks, and
        the kit was corrected as well.

    What I had to guess:
    Seven readings, each with its alternative written up in QUESTIONS.md — Q1 and Q2 (stage A),
    Q3 and Q4 (stage B), Q5, Q6 and Q7 (stage C). The three from stage C:
      Q5 — where stage C's second trace comes from, given that stages A and B define no execution
           semantics and section 8 names no runner. Chosen: a reference interpreter inside the
           test, with FSMgine as the independent oracle. Alternative: a public runner, which
           would be a change to section 4's frozen API.
      Q6 — whether a guarded row refines the unguarded row for the same (from, kind) whatever
           order the file wrote them in. Chosen: yes — precedence, which is also the order `dump`
           emits and what makes first-match-wins comparable at all. Alternative: strict file
           order, under which the canonical form would discard behaviour.
      Q7 — whether a canonical round trip preserves the order of `states`. Chosen: no; it
           preserves the machine's meaning, its set of states and its canonical text. The first
           draft of stage C's round-trip test asserted the opposite and failed. Alternative:
           preserve it — impossible without a `state` directive the canonical form does not have.

## Appendix — the four additions after stage C

The report above is what it was: the spec's three stages, delivered. Four additions followed, each
one additive to the frozen sections in the shape stage B used, each with the whole gate over it:

    addition                        commit                document
    the generator (fsmtable-gen)    056495f               GENERATOR.md
    the calculator example          39ba706               examples/calculator/README.md
    named event kinds               23e4123 + b4e7f7e     NAMED_KINDS.md
    entry and exit actions          a7d6dde               ENTRY_EXIT.md

    Gate verdict line for the last of them (verbatim):
    all 12 stage(s) passed in 155s
    GATE PASSED

Each one's evidence is the same three things, and together they are the reason the additions are
additions rather than a second format:

  * the frozen suite, the corpus and `RejectsVersionThatIsNotFirst` pass unchanged — `version 2`
    stays rejected, so what changed is version 1's reader, not the version number;
  * `tests/generator_test.cpp` still holds the traffic light's one action to `&on_red_green`, so an
    artifact that had been respelled (named kinds) or composed (entry/exit) would fail it, and the
    calculator's recorded session still diffs byte for byte after its seven `--clear-->` rows lost
    their action clause to an entry clause;
  * a sabotage per round, run and reverted: reversing the canonical order fails exactly the
    canonical-order case, and inverting the entry/exit composition fails exactly the three
    order-sensitive cases.

QUESTIONS.md grew by Q12 (a kind name is declared before the row that uses it), Q13 (a row that
stays put is an external transition) and Q14 (the factory does not run the initial state's entry
clause) — three more readings, each with its alternative written up beside it.


## Appendix — the examples after the additions

The four additions above changed what the format can *express*. The three below change nothing: they
are consumers, and between them they are evidence that the two frozen sections are enough to write a
real program against.

    example                                   commit    document
    the calculator                            39ba706   examples/calculator/README.md
    the connection lifecycle, the inspector
    and tools/check-doc-links.sh              b377eb3   examples/protocol/README.md
                                                        examples/inspector/README.md
                                                        examples/README.md

    Gate verdict line for the second (verbatim):
    all 12 stage(s) passed in 115s
    GATE PASSED
    Both lines are from the run on commit b377eb3; this file is the only difference from that tree,
    and the run was repeated after it was added.

What the second round exercised that the first could not:

  * the generator on **time** rather than arithmetic — a value compared against one threshold and
    never accumulated, and a `tick` refinement pair whose two halves appear side by side in the
    recorded trace (`tick 400` absorbs, `tick 1200` retransmits);
  * the library **without** the generator: `fsmtable-inspect` is the first consumer of
    `unreachable` and `sink_states` that is not their own test, and the first thing in this
    repository to read a `.fsm` at run time;
  * `pristine` earning its keep. The doc-link checker passed in the working tree and failed in the
    archive, because `git archive` has no `.git` for `git ls-files` to read; the stage caught a real
    bug in a tool written the same day, which is the whole argument for building the tree a stranger
    receives;
  * the limits the protocol example meets, each written down where it bites rather than in the
    abstract: one guard per `from`+`kind` (so giving up is its own kind), no arithmetic (so the
    clock and the retry budget are the driver's), an external self-transition (so a state's clauses
    re-run on a tick that stays put) and no internal transition (so `Established` cannot carry an
    entry clause). `examples/protocol/README.md` states each one beside its workaround.


## Appendix — the document checkers

Two tools keep this repository's documentation honest, and each is a ctest case, so both run in the
`tests` stage and again against the `pristine` archive:

    tool                        what it re-derives                added in
    tools/check-doc-links.sh    every relative link resolves      b377eb3
    tools/check-doc-claims.sh   every count, output and recipe      ed297d8

Claims, not links, are the ones that went wrong: two false statements reached shipped documentation
in two days — a link to the Kit's `DESIGN-NOTES.md`, which this repository does not have, and a
"dead guard" analysis that rule 9's ordering makes impossible. The second tool re-derives the rest
from the tools themselves, and it found three more on its first run: `calculator.fsm` had no final
newline, so the README's append recipe glued the block onto its last row and the generator refused
it; the inspector's tree recipe left out the corpus's deliberately invalid files and its own
`dirty.fsm`; and the check itself passed its canonical-form case against the shell's own error
message, because a relative tool path stopped resolving the moment it changed directory. Each was
fixed in the file it belonged to, and the check's teeth are proved by sabotage on three claim
families — a wrong count, a name that does not exist, and a stale output line.

    Gate verdict line for that commit (verbatim):
    all 12 stage(s) passed in 122s
    GATE PASSED
    Both lines are from the run on commit ed297d8; this file is the only difference from that tree, and
    the run was repeated after it was added.


## Appendix — the third analysis, and what measuring it corrected

`fsmtable::partial_pairs` (`66cd3bd`) is the other half of rule 9. The rule allows one guarded row per
state and kind, and fixes the canonical order so the guarded row comes first with an unguarded row
behind it as the fallback; a pair with no fallback is the only shape where a false guard leaves
nothing to match, and the back end reports the event rather than pretending a row matched. The shape
is legal — "ignore a tick unless the budget allows" is exactly one — so it is an analysis to report
rather than a rule to enforce.

It was built test-first against a stub that returned nothing, and that RED run is worth reading twice:
five of the eight tests failed against the stub, and the three that passed were the ones expecting an
empty result, which any stub satisfies. The tests that pin only what must *not* be reported are the
weakest in the set, and knowing which ones those are is the difference between a green suite and
evidence.

One claim of mine died under the new analysis rather than under review. I had described the protocol
example as carrying deliberate partial pairs — a `data` event in `SynSent` that the driver reports as
a protocol violation — and the report says it carries none: every pair in that machine has an
unguarded fallback row, which is exactly how it turns an unacceptable event into a `Refused` sink.
The tool said so on its first run, against the machine it was built beside.

    Gate verdict line for that commit (verbatim):
    all 12 stage(s) passed in 171s
    GATE PASSED
    That line is from the run on commit 66cd3bd; this file is the only difference from that tree, and
    the run was repeated after it was added.
