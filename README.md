# FSMTable

A textual format for finite state machines, and three tools around it: a code generator (C++ or
TypeScript), an inspector that reads a machine back, and a transform that edits the text itself.

    my_machine.fsm  --fsmtable-gen-->               fsm_my_machine.hpp   (C++)       -->  your program
    my_machine.fsm  --fsmtable-gen --target deno-->  my_machine.ts       (TypeScript)  -->  your program
    my_machine.fsm  --fsmtable-inspect-->            a report: --canonical, --trace, --graph
    my_machine.fsm  --fsmtable-transform-->          .fsm text back: a rename, or two machines merged

The point is the shape of the work, not the size of the library. A machine that does something
specific and has no unexpected features is not easy for a person to write by hand — a table is.
So a machine is written as text, checked by a parser with a frozen set of rules, and turned into
a `constexpr` table that runs.

* **The text is the source.** The library reads a machine from text and writes one back out;
  `dump` is canonical, and a parse → dump → parse round trip is stable. `fsmtable-transform` is the
  direction that edits the text itself: `rename` moves a name and writes the machine back with every
  comment, blank line and untouched directive exactly where the file wrote it, and `merge` composes
  two machines into one — a union fused on the state names they share, which is a new file rather
  than a rewritten one (`TRANSFORM.md`).
* **The generator is the point.** `fsmtable-gen` emits one self-contained C++ header per machine:
  scoped enums for the states and the event kinds, the rows in the format's canonical order, one
  declaration per action, and a factory. Rows are typed `fsmgine::compiled::Transition`, so the
  generated table drives [FSMgine](https://github.com/HarryPehkonen/FSMgine)'s compiled back end —
  and a misspelt action name is a compile error rather than a row that silently never fires.
  `--target deno` writes the same machine as one TypeScript module instead — a state union, a kind
  enum, a pure `step(state, event)` table — and the gate type-checks and tests what it emitted;
  `GENERATOR.md` has the shape.
* **It is verified, not merely written.** The gate is 12 stages: frozen parser tests, a
  differential oracle against FSMgine, a fuzzer over the corpus, ASan/UBSan and ThreadSanitizer
  runs, a pristine build of `HEAD`, and `deno check` + `deno test` over the generated TypeScript.

## Status

`SPEC.md`'s stages A, B and C are delivered, as are the generator and two additive format
features (named event kinds, entry and exit actions). Sections 2 and 4 of the spec — the row
format and the library API — are frozen text, and the additions sit below that block: every
frozen test, the corpus and the dump oracle pass unchanged. `REPORT.md` carries the stage A, B and C
deliveries and the additions reported beside them, each with its gate verdict; `GENERATOR.md` ends
with what is deliberately not built yet.

The text → text direction is the third tool: `fsmtable-transform` has two verbs. `rename` moves a
state, a kind name or the machine's own name, and nothing else in the file moves with it, comments
included. `merge` composes two machines into one — a union whose shared state names are the seam it
meets on — and writes a new file with a provenance header naming both parents and the merged
fingerprint. A composition that would leave a state unreachable is refused with the states named
rather than written. `TRANSFORM.md` is the contract for both, the exit codes and the limits.

The generator and the inspector shipped to the same bar: `--target deno` emits the same machine as a
TypeScript module the gate type-checks and tests (`GENERATOR.md`), and `fsmtable-inspect` reads a
machine back — a summary, or `--canonical`, `--trace`, `--graph` — under its own ctest cases
(`examples/inspector/README.md`). Those three tools are everything the banner above shows.

`examples/` holds three complete programs, each with its own tests and its own README: a calculator
(the generator on arithmetic), a connection lifecycle with timeouts (the generator on time), and
`fsmtable-inspect`, a tool that reads `.fsm` files (the library with no generator at all).
`examples/README.md` is the index, and it ends with a list of the machines worth building next.

## Quick start

    ./scripts/gate.sh                     # the whole gate, 12 stages, ~2-3 minutes
    ./scripts/gate.sh --tier fast         # the fast loop while working

    ./build/fsmtable-gen my.fsm -o my.hpp      # or write the header to stdout
    ./build/fsmtable-gen my.fsm --target deno -o my.ts   # the same machine, as TypeScript
    ./build/fsmtable-transform rename my.fsm state:Idle=Waiting   # a name moves; comments stay
    ./build/fsmtable-transform merge login.fsm session.fsm -o session_full.fsm   # two machines, one
    ./build/calculator                         # the first worked example: a line in, an answer out
    ./build/protocol                           # the second: a connection lifecycle, script-driven
    ./build/fsmtable-inspect my.fsm            # the library without the generator

`fsmtable-gen` exit codes: `0` written, `1` the input is unusable, `2` the command line is.
`fsmtable-transform` carries its own three and differs in one place — a file that does not parse is
the caller's mistake there too — so its table is the authority (`TRANSFORM.md`).
Needs C++17, CMake, GTest, clang-format and clang-tidy for the gate, `kit-ci` to run it
(`scripts/gate.sh` is the entry point; KitCI is installed once per machine — its own README has the
one command), and FSMgine 2.1.0's headers at `~/hermes-workspace/FSMgine` (point
`-DFSMTABLE_FSMGINE_DIR=<path>` elsewhere; configuration fails loudly without them, because the
differential oracle needs them). The `deno` stage needs `deno` — 2.9.6 is what it is gated with
here — and skips itself rather than failing when `deno` is not on the PATH.

## Install the tools (once per machine)

Every command above runs `./build/fsmtable-gen`, which exists only while this checkout is built.
A consumer repository does not have this tree, and its committed-artifact recipe leans on the
engine being on the PATH: a drift probe declared `when = "tool:fsmtable-gen"` has teeth exactly
where the tool is installed and **skips** where it is not, and a skip reads as green. So
installing FSMTable is the first step of that recipe, not an afterthought:

    cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release -j
    cmake --install build-release --prefix ~/.local   # -> ~/.local/bin/fsmtable-gen
                                                      #    ~/.local/bin/fsmtable-inspect
                                                      #    ~/.local/bin/fsmtable-transform

The prefix belongs to `--install`, and Release is the configuration to install: this is the
engine every repo on the machine runs, the way KitCI's `kit-ci` is installed once per machine.
The tools go on the PATH together, because they are one engine's halves — the generator writes an
artifact, the inspector reads it back, and the transform edits the machine itself. Installing also
removes a trap: a probe that finds `fsmtable-gen` wherever the shell happens to point it may be
reading a build directory from an old checkout rather than the binary the machine runs.

## The format, in one screen

    # `version 1` is always the first non-comment line
    version 1
    machine Door
    initial Closed
    # a name for an event kind, 0..255
    kind open = 1
    kind knock = 2
    # entry and exit actions are per state
    state Closed entry on_entry_closed
    transition Closed --open--> Open action on_opening
    # a row that stays put is still an external transition
    transition Open --knock--> Open
    # the number works wherever the name does
    transition Closed --2--> Broken

A comment is its own line — the format has no trailing comments — which is why the notes in that
block stand above the lines they explain, not beside them. Rules 1–10 of `SPEC.md` section 3 are the
parser's rules and they are the authority; `NAMED_KINDS.md` and `ENTRY_EXIT.md` document the two
additions. Any byte string is either a valid machine or an error with a line number and a message.

## Documentation

    SPEC.md          the specification: the frozen rules and API, and the stages to build
    GENERATOR.md     the tool, the emitted artifact, the exit codes, the limits
    TRANSFORM.md     `fsmtable-transform` — the text → text direction: a rename applied to the
                     text (what moves, what cannot, why it is not a `dump`) and the merge that
                     composes two machines into one
    NAMED_KINDS.md   `kind <name> = <n>` — names for event kinds, so rows read `--open-->`
    ENTRY_EXIT.md    `state <name> entry ... exit ...` — entry and exit actions per state
    QUESTIONS.md     every place the spec left a choice, and the reading taken
    REPORT.md        what was delivered, and the gate verdict for each delivery
    INCIDENTS.md     edits to the files that came from the project kit, and why
    examples/README.md
                     the three examples, the recipe they share, and what to build next
    examples/calculator/README.md
                     the worked example: how to read it, and how to build your own from it
    examples/protocol/README.md
                     time instead of arithmetic: refinement pairs, a sink, and four limits
    examples/inspector/README.md
                     the library without the generator, and why a sink is not a failure

## Layout

    src/             the library: fsmtable.hpp (the frozen API) and the files behind it
    gen/             fsmtable-gen: the text → code direction
    transform/       fsmtable-transform: the text → text direction
    tests/           the gtest suites, the corpus, the fixtures, the differential oracle
    corpus/          .fsm files the tests and the fuzzer share
    examples/        calculator, protocol, inspector — three complete consumers
    fuzz/            the libFuzzer target
    scripts/         the gate: gate.sh, gate-env.sh, and one script per stage
    gate.toml        the gate's policy — the stages, the two tiers, the failure rules
    tools/           the two document checkers, and the kit's release script
    .ci/             the accepted clang-tidy findings, with the reason for each
    .githooks/       the hooks that NAME a gate tier (pre-commit fast, pre-push full)

## Two things worth knowing before reading the code

* **The canonical row order is the precedence rule.** `fsmgine::compiled::Machine` scans rows
  first-match-wins, so the generator emits guarded rows before unguarded ones within one
  `from`+`kind`. The order in the artifact is the semantics — a file written the other way round
  still behaves as the format says (`QUESTIONS.md` Q6).
* **Names become symbols, and the strings are kept beside them.** A `.fsm` action name becomes a
  declared function (so a typo is a link error, not a silent no-op), a state becomes an
  enumerator, and the generated header also carries the name tables, so code → text round trips
  stay possible.

## Naming

The project is **FSMTable**. The library keeps the names the spec froze: `fsmtable.hpp`,
`namespace fsmtable`, the `fsmtable-gen` binary and the CMake targets in the `fsmTable*` family.

## License

The Unlicense — public domain. See `LICENSE`.
