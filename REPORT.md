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

The four additions above changed what the format can *express*. The four below change nothing: they
are consumers, and between them they are evidence that the two frozen sections are enough to write a
real program against.

    example                                   commit    document
    the calculator                            39ba706   examples/calculator/README.md
    the connection lifecycle, the inspector
    and tools/check-doc-links.sh              b377eb3   examples/protocol/README.md
                                                        examples/inspector/README.md
                                                        examples/README.md
    the vending example — two half-machines    db08ac8   examples/vending/README.md
    fused on one shared name, no code

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


## Appendix — the text → text direction: `fsmtable-transform`

The first thing in this repository that reads a `.fsm` and writes a `.fsm` back. `rename` is its
first verb: a state, a kind name or the machine's own name moves, and nothing else in the file moves
with it.

    what                                        document
    fsmtable-transform rename                   TRANSFORM.md
    the library call behind it                  src/fsmtable.hpp (the addition), src/transform.cpp
    the tests                                   tests/transform_test.cpp — 26 cases
    the checker that holds the doc's output     tools/check-doc-claims.sh (item 6)

The decision the tool exists for is a refusal: **not** `parse` → edit the machine → `dump`. `dump` is
canonical and has no comments in it, and the machines in this fleet carry comments that are the
reason a reader can follow them at all, so a rename is a surgical text edit — the parse tree says
where the names are, and only those bytes move. The cost is stated in TRANSFORM.md rather than
hidden: the tool has to know where names live in the text, so a v2 directive is a change here too,
and the rewrite is proved by re-parsing its own output (one extra parse of a file that was just
parsed).

Which tests do the work was measured rather than assumed. Sabotage: `edit_line` returns nothing for a
comment line, which is what a canonical rewrite does to them. Six cases fail and the seventh — the
one that compares the machine `parse` reads back, names substituted — still passes. A tool built on
`dump` would pass the machine oracle and fail every byte-level case, so the byte-level cases are the
ones holding this property up, and `tests/transform_test.cpp` says so at the top.

The document checker grew one rule to be able to pin this tool's output. `tools/check-doc-claims.sh`
read a blank line in a block as a separator between commands, which is fine for the inspector's
output and impossible for a tool whose promise is that a file's blank lines survive. A blank line is
now a separator exactly when a command follows it, and content anywhere else. Teeth: removing one
blank line from TRANSFORM.md's block fails `docs_claims_hold` with that line in the diff.

Two readings the card did not settle are written up rather than guessed at: Q15 (a collision is
tested over the names the whole list produces, which is what makes `state:A=B state:B=A` a swap
rather than a refusal) and Q16 (a `state <name> entry …` line IS a rename site — the card's own list
of sites omitted it, and leaving it out would write a file that does not parse).

The first full run failed, and it is worth writing down why: the `tidy` stage found two real
findings in the new file (`performance-inefficient-string-concatenation`, the messages a collision
builds). The fix is in the code — an `append`-based helper, no suppression and no baseline entry —
and the stage was re-run on its own before the verdict below.

    Gate verdict line for that commit (verbatim):
    GATE PASSED — 12 passed, 0 failed, 0 skipped
    That line is from the run on commit f767e98; this file is the only difference from that tree, and
    the run was repeated after it was added.

The stage report for that run, for the record: `tree` 0.0s, `format` 0.1s, `build` 0.4s, `tests`
0.2s, `release` 9.7s, `version` 0.0s, `asan` 10.5s, `tsan` 7.2s, `tidy` 67.9s, `pristine` 14.8s,
`fuzz` 62.9s, `deno` 0.1s. `pristine` is the stage that matters for this delivery: it builds the
renamed tree from a `git archive` of the commit, so the new directory, the new test binary and the
extended install rule are exercised from a checkout that has none of the working tree's state.

## Appendix — the merge verb: two machines composed into one

The second verb of `fsmtable-transform`, and the first thing here that reads TWO files and writes one
that neither of them was. The policy is the delivery: states and kinds are composed by NAME — a name
the two machines share is one thing — because that is what makes a merge a composition rather than a
concatenation, and because a state name the two already agree on is the seam a composition meets on.

    what                                        document
    fsmtable-transform merge a.fsm b.fsm        TRANSFORM.md
    the library call behind it                  src/fsmtable.hpp (the addition), src/merge.cpp
    the tests                                   tests/merge_test.cpp — 27 cases
    the CLI cases                               CMakeLists.txt — 14 ctest cases
    the checker that holds the doc's output     tools/check-doc-claims.sh (item 6, second block)

Identity comes from the first machine: its `machine` name and its `initial`, the second machine's
initial becoming an ordinary state. Kinds must agree — a number two names disagree about, or one name
standing for two numbers, is a refusal that names both declarations, and `rename` is the tool that
gets out of the way. Rows union, and the one pair the format has no room for, two rows of the SAME
guardedness for one `(from, kind)` (SPEC.md rule 9), is a refusal rather than a silent pick. Clauses
fuse per state and per clause, the first machine's winning where both decorate one, with one
exception: the second machine's FORMER initial-entry clause is dropped and the drop is reported on
stderr, because a merged machine is created rather than entered (ENTRY_EXIT.md) and a merged machine
cannot have been entered.

The verb exists for its refusal, and the refusal is a finding rather than an error: the union is
`dump`ed, parsed back, and required to reach every state from the merged initial. A composition that
does not meet — two machines with no state name in common, which is the ordinary case for unrelated
machines — is refused with the unreachable states named and NOTHING written, because a new file that
is a machine with a state nobody can reach is a worse thing to leave on disk than no file. That is
the property a concatenation cannot have, and it is why this verb's exit code is part of its contract.

Four readings the card did not settle are written up in QUESTIONS.md rather than guessed at: Q17 (a
merge refusal carries no line number — the parse tree resolves names and keeps no positions, so the
message names the two DECLARATIONS, which is what "naming both lines" was for), Q18 (an unreachable
union is a refusal and not a report, with the consequence — unrelated machines never merge — recorded
rather than discovered later), Q19 (the merged machine takes the first machine's name as well as its
initial, because a composed name would not be writable in the format's own rule 7), and Q20 (which
clause wins when both machines decorate one state, with the honesty of what the drop costs a row that
led back into the second machine's initial state).

Which tests do the work was measured rather than assumed, three times.

Sabotage 1 — the reachability refusal replaced by a warning that prints the same finding. Two of the
27 library cases fail (`RefusesAResultThatLeavesTheSecondMachinesStatesUnreachable` and
`RefusesAResultWhereTheFirstMachinesOwnOrphanIsStillUnreachable`) and the other 25 pass, so that
property is held up by exactly the two cases that claim it.

Sabotage 2 — the tool made to exit 1 where the merge succeeded. The four ctest cases that assert
`[ $code -eq 0 ]` fail, so the exit codes are enforced in both directions.

Sabotage 3 — one hex digit in TRANSFORM.md's merge block flipped. `tools/check-doc-claims.sh` fails
with the changed line in the diff, so the doc's second output block is checked and not just printed.

### The exit code a ctest case thought it was pinning

Sabotage 1 found something that is not about merge at all. The six `merge_*` ctest cases had been
written in the shape the fifteen cases above them use:

    add_test(NAME X COMMAND sh -c "...; printf '%s\n' \"$out\"; [ $code -eq 1 ]")
    set_tests_properties(X PROPERTIES PASS_REGULAR_EXPRESSION "...")

and ctest IGNORES a case's exit code as soon as `PASS_REGULAR_EXPRESSION` is set — "The process exit
code is ignored" (`cmake --help-property PASS_REGULAR_EXPRESSION`, cmake 3.31.6). Measured: with the
reachability refusal sabotaged away the run exits 0 while still printing the finding, the same command
string run through `sh` exits 1, and `ctest -R merge_refuses_a_union_that_leaves_a_state_unreachable`
still reported PASSED. Every case of that shape proves the message and silently stopped proving the
code, under block comments that say "these cases pin the EXIT CODES".

The six merge cases now put both halves in the shell — print the output for a reader, grep it, then
check the code — with the reason and the measurement in the block comment above them. Re-measured
after the change: sabotage 1 fails the reachability case, and sabotage 2 fails the four cases that
assert 0.

The fifteen pre-existing cases with the same hole were deliberately NOT rewritten here: the
inspector's and tier 2a's tests are other cards' deliveries, and burying a harness repair inside a
merge diff is how a gate stops being auditable. Card **t_198cb151** carried the finding, the case
list and the exact fix per case, and it was gated on this card so two workers were never in this file
at once. That card has since landed the repair: all fifteen cases now grep their own output and check
their code together in the shell, and no case in `CMakeLists.txt` sets `PASS_REGULAR_EXPRESSION` any
more. Proved on this tree by sabotage in both directions — with every tool's exit code flipped
between 0 and nonzero all fifteen fail, and with the tools' output dropped (their exit codes left
intact) all fifteen fail again — while each case pins the same message pattern it always did. Before
the repair the same exit-code flip left all fifteen reporting PASSED.

The first `tidy` run found six real findings in the new code (three in the test, three in the tool): a
one-character string literal passed to `find` where a character was meant, and three copies of a
`std::vector` element where a const reference was meant — the second of which had been in the tool all
along and only began being reported once the rename verb's body moved into a function taking a const
reference. Both are fixed in the code, with no suppression and no baseline entry.

    Gate verdict line for that commit (verbatim):
    GATE PASSED — 12 passed, 0 failed, 0 skipped
    That line is from the run on commit dbdb181, which carries the merge and the ctest repair; this
    file is the only difference from that tree, and the run was repeated after it was added.

The stage report for that run, for the record: `tree` 0.0s, `format` 0.1s, `build` 0.4s, `tests`
0.2s, `release` 10.7s, `version` 0.0s, `asan` 11.8s, `tsan` 8.1s, `tidy` 70.6s, `pristine` 16.4s,
`fuzz` 63.2s, `deno` 0.1s. `pristine` matters here as it did for the rename: it builds the merged tree
from a `git archive` of the commit, so the new library unit, the new fixtures and the new test binary
are exercised from a checkout that has none of the working tree's state.

### What the format pushed back on (the tier 3 evidence)

Tier 3 is waiting on this paragraph. The honest answer is that merge needed no new format feature —
but there are three places where the format's silence is now a documented refusal rather than a
decision buried in code:

* **Two rows of one guardedness for a `(from, kind)` pair have no precedence.** Merging two machines
  that grew from a common ancestor, or a machine with itself, hits this on every shared row, and the
  format cannot say "these two rows are the same row" or "one of them wins". The refusal names the
  pair; the fix is a `rename` plus a hand edit, or a v2 directive that says which row survives — a
  format decision, not a tool one.
* **A cross-machine row cannot be written.** The seam is a state NAME the two machines already share;
  the format has no way to say "the second machine's initial is entered from the first machine's
  `Done`" when the two names differ. Two machines that share no name therefore never merge, and the
  only honest alternative is to rename one side until they do — which is what tier 2a is for.
* **The initial-entry drop is lossy in one direction**, and it is recorded rather than worked around:
  a row that led back into the second machine's initial state ran nothing before and runs nothing
  after, because the clause is gone. Carrying the clause would run an action the second machine never
  ran on entry, which is the hidden side effect ENTRY_EXIT.md's Q14 exists to keep out.

## Appendix — the vending example: the merge verb, worked

`examples/vending/` is the fourth example and the first one that generates no code: three `.fsm`
files, the two tools that read them, and a README in the shape the other three keep. It exists
because the text → text direction shipped with a contract (`TRANSFORM.md`) and no worked example — a
reader could see what `merge` promises and not what a composition looks like when it is real.

    what                                        document
    the two halves and the seam                 examples/vending/coins.fsm, dispenser.fsm
    the union, header and all (committed)       examples/vending/vending.fsm
    the worked example                          examples/vending/README.md
    the four cases that hold it                 CMakeLists.txt — 4 ctest cases (71 in the suite)
    the checker that holds its output blocks    tools/check-doc-claims.sh (item 8)

The composition is the smallest one that teaches the point: the payment half's `Credited` and the
stock half's `initial` are the SAME name, so the seam is visible in the two files rather than
described in prose, and the union is committed as the bytes the tool writes. What the example spends
its words on instead is what the format cannot do: the credit is a sum and a row has one `when` slot,
so the driver adds the coins and the machine compares one number against one number —
`Waiting --credit--> Waiting when lt 150` with the unguarded row behind it is that boundary in two
lines, and the two refusals the README prints are the same boundary seen from the other side.

Three things this delivery fixed rather than wrote down:

* **A doc-claims block with no command line passed against the wrong output.** `check_output_block`
  left `$work/commands.txt` to be truncated by its awk's first write, so a block whose `$ ` line was
  missing re-ran the PREVIOUS block's commands — and the `[ ! -s "$commands" ]` guard the script
  already had could not see it, because the file it read was never emptied. The first run of the new
  rules "passed" three vending blocks against TRANSFORM.md's merge output. Both files are emptied
  before the splitter now, and the guard fires: with the `$ ` line deleted from one block the run
  fails with "runs no command, so nothing was checked" instead of diffing another document's output.
* **The four cases were sabotaged before being trusted.** Deleting `transition Waiting --credit-->
  Credited` from the committed union fails three of the four — the union stops being the parents'
  output, the trace stops composing, the summary stops naming the sink — and adding a
  `Waiting --dispense-->` row fails the fourth, with it the claim that a vend before payment is
  refused. Both edits were undone by re-running the merge, and the regenerated file is byte-identical
  to the committed one, which is the property the first case exists to hold.
* **The tree check's comment was a count, and it was low.** It said the tree check finds 5 `.fsm`
  files; with this example's three it finds 8. The comment says 8 now and the floor stays where it
  was — a vacuity guard, not a pin on the number.

    Gate verdict, verbatim, from the run on commit db08ac8 (this section was added after it, and the
    full tier runs again on the push):

    kit-ci: gate.toml — tier 'full', 12 stage(s)
    GATE PASSED — 12 passed, 0 failed, 0 skipped

    The tests stage's own line, from .ci-logs/tests.log on that run:

    100% tests passed, 0 tests failed out of 71

## Appendix — the csv example: the rows that carry the actions

`examples/csv/` is the fifth example and the one whose subject is the row → action tie rather than a
decision about a value: four states, fifteen rows, three actions, and a byte stream in which one of
the four kinds carries the byte itself — the calculator's value-on-the-event trick (`event.value`),
spent here on the data. It exists because the generator had taught a value that is a threshold and a
clock (`protocol/`) and not a value that *is* the input.

    what                                        document
    the machine                                 examples/csv/csv.fsm (4 states, 15 rows, 4 kinds)
    the driver: classification, CRLF, EOF       examples/csv/csv.cpp
    the API                                     examples/csv/csv.hpp
    the shell                                   examples/csv/main.cpp
    the worked example                          examples/csv/README.md
    the two recorded runs                       examples/csv/input/session.{csv,expected},
                                                examples/csv/input/malformed.{csv,expected}
    the four cases that hold it                 CMakeLists.txt — 4 ctest cases (75 in the suite)
    the checker that holds its blocks           tools/check-doc-claims.sh (item 9, check_file_block)
    the readings                                QUESTIONS.md Q21 the CR, Q22 the tolerant quote,
                                                Q23 the missing row, Q24 the one action slot,
                                                Q25 the end of input

The machine's argument is its asymmetry: a comma and a line end are DATA inside `Quoted` and
delimiters in every other state, so the difference between `a,b` and `"a,b"` is a state and nothing
else — one table, and the quoting falls out of which state the byte arrived in. The driver owns the
three things a table cannot hold: what a byte IS, that the two bytes of CRLF are one line end, and
what a record left open at end of input means.

Five things this delivery measured rather than assumed:

* **The design brief's row cannot be written.** It asked for `action end_cell, end_record` on every
  `--newline-->` row. The frozen format gives a row one `action` slot and the generator says so with a
  line number — `fsmtable-gen: ...:5: malformed action name 'end_cell,'`, exit 1 — and the back end's
  `action()` refuses a second call as well. So the newline row's single action, `end_record`, closes
  the field and then the record (Q24). The bytes are identical to what the brief wanted; what the
  format cannot express is the decomposition into two names.
* **A bare `state <Name>` line is not legal.** `state FieldStart`, with no clause, is `state needs an
  entry or exit clause`. A state is declared by appearing in `initial`, `<from>` or `<to>`; the `state`
  directive exists only to carry an `entry`/`exit` clause. `csv.fsm` therefore has four states and no
  `state` lines at all.
* **The tree check's comment was a count, and it was low.** With this example's `csv.fsm` the tree
  check inspects 9 `.fsm` files; before it, 8. The comment says 9 now and the floor stays where it was
  — a vacuity guard, not a pin on the number.
* **A README that quotes a recorded RUN needs a different check from one that quotes a tool.** The
  output blocks re-run a command; this example's two recorded blocks are fed through the binary by the
  ctest cases, and a piped stdin is not something that block vocabulary can express. `check_file_block`
  compares the block against the committed `input/*.expected` file instead, which puts the README inside
  the chain the ctest case already completes — so the quoted run cannot drift from the trace a reader
  would get even when the binary and the `.expected` file still agree.
* **Six sabotages, each caught, each undone.** Deleting `transition QuoteSeen --quote--> Quoted` from
  `csv.fsm` fails the ctest suite AND the worked recipe's counts; dropping `end_cell()` from
  `end_record` fails the suite; flipping `main`'s refusal exit to 0 fails the exit-code half of the
  refusal case; editing one `Cell:` line in the README's session block fails `check_file_block`;
  deleting the trace block's `$ ` line fails `check_output_block` with "runs no command, so nothing
  was checked". After every edit the tree was restored and re-run green (ctest `csv_` exit 0,
  `check-doc-claims.sh` exit 0), and the ctest suite runs 75 cases, the four new ones included.

    Gate verdict, verbatim, from the run on commit 3f8fb4d (this section was added after it, and the
    full tier runs again on the push):

    kit-ci: gate.toml — tier 'full', 12 stage(s)
    GATE PASSED — 12 passed, 0 failed, 0 skipped

    The tests stage's own line, from .ci-logs/tests.log on that run:

    100% tests passed, 0 tests failed out of 75
