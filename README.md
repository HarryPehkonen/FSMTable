# FSMTable

A textual format for finite state machines, and a code generator that turns it into C++.

    my_machine.fsm  --fsmtable-gen-->  fsm_my_machine.hpp  -->  your program

The point is the shape of the work, not the size of the library. A machine that does something
specific and has no unexpected features is not easy for a person to write by hand — a table is.
So a machine is written as text, checked by a parser with a frozen set of rules, and turned into
a `constexpr` table that runs.

* **The text is the source.** The library reads a machine from text and writes one back out;
  `dump` is canonical, and a parse → dump → parse round trip is stable.
* **The generator is the point.** `fsmtable-gen` emits one self-contained C++ header per machine:
  scoped enums for the states and the event kinds, the rows in the format's canonical order, one
  declaration per action, and a factory. Rows are typed `fsmgine::compiled::Transition`, so the
  generated table drives [FSMgine](https://github.com/HarryPehkonen/FSMgine)'s compiled back end —
  and a misspelt action name is a compile error rather than a row that silently never fires.
* **It is verified, not merely written.** The gate is 12 stages: frozen parser tests, a
  differential oracle against FSMgine, a fuzzer over the corpus, ASan/UBSan and ThreadSanitizer
  runs, and a pristine build of `HEAD`.

## Status

`SPEC.md`'s stages A, B and C are delivered, as are the generator and two additive format
features (named event kinds, entry and exit actions). Sections 2 and 4 of the spec — the row
format and the library API — are frozen text, and the additions sit below that block: every
frozen test, the corpus and the dump oracle pass unchanged. `REPORT.md` carries the deliveries
and the gate verdict for each; `GENERATOR.md` ends with what is deliberately not built yet.

`examples/` holds three complete programs, each with its own tests and its own README: a calculator
(the generator on arithmetic), a connection lifecycle with timeouts (the generator on time), and
`fsmtable-inspect`, a tool that reads `.fsm` files (the library with no generator at all).
`examples/README.md` is the index, and it ends with a list of the machines worth building next.

## Quick start

    ./tools/ci.sh                     # the whole gate, 12 stages, ~2-3 minutes
    ./tools/ci.sh build tests         # the fast loop while working

    ./build/fsmtable-gen my.fsm -o my.hpp      # or write the header to stdout
    ./build/calculator                         # the first worked example: a line in, an answer out
    ./build/protocol                           # the second: a connection lifecycle, script-driven
    ./build/fsmtable-inspect my.fsm            # the library without the generator

`fsmtable-gen` exit codes: `0` written, `1` the input is unusable, `2` the command line is.
Needs C++17, CMake, GTest, clang-format and clang-tidy for the gate, and FSMgine 2.1.0's headers
at `~/hermes-workspace/FSMgine` (point `-DFSMTABLE_FSMGINE_DIR=<path>` elsewhere; configuration
fails loudly without them, because the differential oracle needs them).

## The format, in one screen

    version 1                                  # always the first non-comment line
    machine Door
    initial Closed
    kind open = 1                              # a name for an event kind, 0..255
    kind knock = 2
    state Closed entry on_entry_closed         # entry and exit actions per state
    transition Closed --open--> Open action on_opening
    transition Open --knock--> Open            # stays put — still an external transition
    transition Closed --2--> Broken            # the number still works

Rules 1–13 of `SPEC.md` section 2 are the authority. `NAMED_KINDS.md` and `ENTRY_EXIT.md`
document the two additions. Any byte string is either a valid machine or an error with a line
number and a message.

## Documentation

    SPEC.md          the specification: the frozen rules and API, and the stages to build
    GENERATOR.md     the tool, the emitted artifact, the exit codes, the limits
    NAMED_KINDS.md   `kind <name> = <n>` — names for event kinds, so rows read `--open-->`
    ENTRY_EXIT.md    `state <name> entry ... exit ...` — entry and exit actions per state
    QUESTIONS.md     every place the spec left a choice, and the reading taken
    REPORT.md        what was delivered, and the gate verdict for each delivery
    examples/README.md
                     the three examples, the recipe they share, and what to build next
    examples/calculator/README.md
                     the worked example: how to read it, and how to build your own from it
    examples/protocol/README.md
                     time instead of arithmetic: refinement pairs, a sink, and four limits
    examples/inspector/README.md
                     the library without the generator, and why a sink is not a failure

## Layout

    src/             the library: fsmtable.hpp (the frozen API) and fsmtable.cpp
    gen/             fsmtable-gen: the text → code direction
    tests/           the gtest suites, the corpus, the fixtures, the differential oracle
    corpus/          .fsm files the tests and the fuzzer share
    examples/        calculator, protocol, inspector — three complete consumers
    fuzz/            the libFuzzer target
    tools/           ci.sh (the gate), the kit probes, and the two document checkers
    .ci/             the accepted clang-tidy findings, with the reason for each

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
