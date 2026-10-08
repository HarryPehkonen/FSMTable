# fsmtable-gen — the text → code direction

`fsmtable-gen` reads one `.fsm` file and writes one self-contained artifact: a C++ header by
default, or a TypeScript module with `--target deno` (the second back end, below). The text is the
source; the generated file is a build product.

```
./scripts/gate.sh --tier fast                              # the fast loop; builds build/fsmtable-gen
./build/fsmtable-gen corpus/traffic_light.fsm -o traffic_light.hpp
./build/fsmtable-gen corpus/traffic_light.fsm              # or straight to stdout
./build/fsmtable-gen --target deno corpus/traffic_light.fsm -o traffic_light.ts
./build/fsmtable-gen --help
```

Exit codes: `0` written, `1` the input is unusable (a parse error, or a name that cannot become
a C++ identifier), `2` the command line is. `--namespace <name>` moves the generated symbols out
of the default `fsmtable_generated`.

## What one header holds

For a machine named `M` (the `.fsm`'s `machine` line), in one namespace:

| symbol | what it is |
| --- | --- |
| `MState` | `enum class : std::uint16_t`, the states in first-appearance order |
| `MKind` | `enum class : std::uint8_t`, the event kinds: a name the `.fsm` declared, or `k<number>` when it declared none |
| `MEvent` | `{ kind; value; }` — `kind` is what the back end reads, `value` is the format's single refinement slot |
| `MStateNames` | `std::array<std::string_view, N>` of the state names, for logs and code → text |
| `MNameOf` | `std::string_view (MState)`, the name function `withNames()` wants |
| `MKindNames` | `std::array<std::pair<MKind, std::string_view>, N>` of the kind names the file declared, for logs and code → text |
| `MKindNameOf` | `std::string_view (MKind)`, the declared name, or empty for an undeclared kind |
| `MRows` | `constexpr std::array<fsmgine::compiled::Transition<…>, N>`, the transition table |
| `makeM()` | a machine ready to drive, initial state set, names wired |
| `void action(const MEvent&)` | one declaration per action the file names — row actions and `entry`/`exit` clauses alike; you define these |
| `<from>_leaving_to_<to>` | the composed function for a row whose states are decorated: exit, then the row's action, then the entry |
| `enterInitialM()` | the initial state's entry clause, if it has one. Nothing calls it (Q14) |

The two functions in the middle of that table exist because the back end runs exactly one function
per transition: a decorated row points at a composed function instead of its own action, and rows
that compose the same three calls share one. Nothing in FSMgine changed — the composition happens
here, at generation time — and a row whose ends carry no clause still points straight at its own
action, which is why artifacts that use no clause are byte for byte what they were (ENTRY_EXIT.md).

Kind names come from the file. `kind tick = 1` in the `.fsm` makes the enumerator `tick` — and
`MKindNames` gives the string back, so code → text stays possible. The generated code then reads
in the machine's own words instead of `k1`, and the example in `examples/calculator/` no longer
needs a block of aliases to translate between the two (NAMED_KINDS.md). A kind the file did not
name keeps the `k<number>` spelling it always had; a kind name that is a C++ keyword is refused,
by the same rule as a state or action name; and the fingerprint covers the *named* canonical form,
so renaming a kind changes it.

## The two properties that make this a generator, not a pretty-printer

**1. Rows are emitted in the format's canonical order** — guarded before unguarded within one
`from`+`kind`. `fsmgine::compiled::Machine` scans rows first-match-wins, so the row order *is*
the precedence rule (SPEC.md rule 9, QUESTIONS.md Q6). Emitting the file's order instead would
silently change behaviour for a file like `tests/fixtures/refined_pair.fsm`, which writes the
fallback first: the refinement would become unreachable. `tests/generator_test.cpp` asserts
exactly that fixture's guarded row comes first, and that the boundary falls where the format
says (below `ge 10` → fallback, at or above → refinement).

**2. Action names become symbols.** Each action the file names is declared in the header, so a
name the file uses and you never define is a linker error rather than a row that quietly never
fires. Renaming an action in the `.fsm` breaks your build until you follow it — which is the
point.

## Limits, and where they come from

* One refinement slot, one action per row, `int` comparisons: these are the format's v1 limits
  and `fsmgine::compiled::Machine`'s v1 limits, and they are the same limits. Widening either
  side is a change to the format (or to the back end), not to this tool.
* What a limit *costs a caller* is not visible from that list, and it is where a design afternoon
  goes: one guard per `from`+`kind` turns a second threshold into a second kind; no arithmetic puts
  the clock and every counter in the driver; a self-transition is external, so a state's clauses
  re-run on an event that stays put. `examples/protocol/README.md` takes each of those in turn,
  beside the workaround it needs — read it before designing a machine that has to do anything with
  time.
* Names that are C++ keywords are refused rather than mangled — a state called `new` cannot
  become an enumerator, and mangling would break the round trip back to text (Q8).
* A machine with more than 65535 states is refused: the state enum is `std::uint16_t`.
* The body is wrapped in `// clang-format off` … `// clang-format on`, so a formatter run over
  your tree cannot turn regeneration into a two-step edit.
* Generated headers are not for editing. The `.fsm` is the source; regenerate instead.

## Wiring it into a build

`CMakeLists.txt` does it for every input in `FSMTABLE_GENERATOR_INPUTS` at once: one
`add_custom_command` per `.fsm`, the headers generated into `build/generated/`, and
`tests/generator_test.cpp` compiles them — so a
generator that emits something that does not compile fails the build, not a review. The calls run
with the source directory as their working directory, which is why each header's provenance
comment names its input the way the repo sees it (`corpus/traffic_light.fsm`).

The generator's own failure paths are ctest cases: a corpus file that does not parse, a fixture
whose state name is a keyword, and one shape check on the emitted table of the spec's example.

Entry and exit actions are no longer on the list above: a state may carry them, and a composed
function is what the table points at (ENTRY_EXIT.md).

## The second back end: `--target deno`

The same file, one self-contained TypeScript module:

```
./build/fsmtable-gen --target deno corpus/traffic_light.fsm -o traffic_light.ts
```

The flag is the only difference in the command line. `--target cpp` is the default and is the
unchanged direction above, byte for byte; the exit codes are the same three; `-o` writes the module
and without it the module goes to stdout. `--namespace` has no meaning here — there is one module
per file and the caller imports it under whatever name it likes — so the combination is refused
rather than silently ignored.

### What the module holds

| export | what it is |
| --- | --- |
| `State` | the states as a union of string literals, in first-appearance order |
| `states` | the same names as a `readonly` array, for logs and code → text |
| `Kind` | a numeric `enum`: a name the `.fsm` declared, or `k<number>` when it declared none |
| `Event` | `{ kind; value }` — `kind` is what `step` reads, `value` is the format's one refinement slot |
| `actionNames`, `Action` | every action the file names, as a `const` array and the union of its element type |
| `initial` | the initial state |
| `initialEntry` | the initial state's entry clause as action names, `[]` when it has none (Q14: nothing runs it) |
| `Step` | `{ state; fired; actions }` — what `step` returns |
| `step(state, event)` | the transition table, as a pure function |

`step` is the whole of the machine's behaviour: a 2D lookup on `from` + `kind`, then
first-match-wins over the rows for that pair, in the canonical order the header also uses — which is
what makes the guard precedence rule (SPEC.md rule 9) come out the same in both back ends. It reads
the table and the event and returns the state to move to, whether a row answered, and the action
names to run. It touches nothing else: there is no factory and no machine object, the caller holds
the state.

### Entry and exit, and why the shape differs from the header

The C++ header points each row at ONE generated function, because `fsmgine::compiled::Machine`
takes one function pointer per row. TypeScript has no such constraint, so there is no wrapper
function to invent: the same composition — the source state's `exit`, then the row's own `action`,
then the target state's `entry` (ENTRY_EXIT.md) — is carried as the ordered list of names on the
row, and `step` hands it back. Same three calls, same order, one name per call.

### The caller's half: a registry the checker holds to the file

`actionNames` is the union of every name the `.fsm` wrote, and the point of exporting it — besides
typing `Step.actions` — is that a registry keyed by it cannot be incomplete. That is the TypeScript
form of the header's "a misspelt action is a link error, never a row that quietly never fires":

```ts
import * as light from "./traffic_light.ts";

const log: string[] = [];
const actions: Record<light.Action, () => void> = {
  on_red_green: () => log.push("on_red_green"),
};

let state: light.State = light.initial;
for (const event of [{ kind: light.Kind.k1, value: 0 }]) {
  const result = light.step(state, event);
  state = result.state;
  for (const name of result.actions) actions[name]();
}
// state === "Green", log === ["on_red_green"]
```

Delete `on_red_green` from `actions` and `deno check` refuses the file; add a name the `.fsm` does
not have and it refuses that too. The generated module is a build product like the header: never
edit it, regenerate it.

### What is different about this target

* **The C++ keyword rule and the 65535-state limit do not apply.** Every state, action and kind
  becomes a string literal or an enum member here, and a reserved word is a legal enum member
  (measured: `enum Kind { class = 1 }` type-checks), so a machine the header refuses — the
  `tests/fixtures/keyword_state.fsm` case — generates correctly for this target. The rule stays on
  the target that has the problem.
* **Code → text for a kind comes with the language.** A numeric TypeScript enum carries its own
  reverse map, so `Kind[1]` is `"tick"` for a kind the file named `tick`, and `"k3"` for one nobody
  named — the round trip `NAMED_KINDS.md` gives the header's `KindNameOf`, with no emitted code.
* **The table is a `Map` of `Map`s, not an object literal.** A state named `__proto__` is a
  prototype key in an object literal and its rows would vanish; a `Map` keys by the value, whatever
  the value is.

### The gate stage

`scripts/deno.sh` is the stage (`gate.toml`'s `[stage.deno]`, skipped when `deno` is not on PATH).
It generates the module for every input `CMakeLists.txt` generates a header from, copies the
committed tests under `tests/deno/` beside the freshly generated modules, then runs `deno check`
over both and `deno test`. That is the whole bargain, and it is the same one the C++ direction
strikes by compiling its headers in `tests/generator_test.cpp`: a generator that emits something
that does not compile — or a table a test disagrees with — fails a stage, not a review. Nothing is
written into the source tree: the generated modules live in the gate's own `.ci-logs/deno/`, which
`.gitignore` already covers, and the tests import their modules as siblings there.

Python and plain-JS targets are not here. A new target is a new back end with its own card; the
`--target` enum grows one value at a time, so the two that exist stay the two that are tested.

## Two ways to consume a generated machine

The generator can sit in your build, or it can be absent from it. Both are legitimate; they
differ in what the consumer repository commits, and nothing in this repo's own gate changes to
pick one — the recipe is the consumer's.

**1. Build-time regeneration.** What this repo and its `examples/` do: the `.fsm` stays in the
tree, the build runs `fsmtable-gen` over it, and the artifact is a build product that never
enters the source tree. The generator is a *build* dependency. A stale table, a misspelt
action, a module that does not compile — each fails the build. This is the shape to prefer
while the machine is still changing.

**2. The committed artifact.** For a program that ships somewhere FSMTable is not installed,
commit the generated file and let the build read it. Building, installing and running then need
only the language toolchain — a C++ compiler for the header, `deno` for the module — and
FSMTable is a *dev-time* dependency: run when the machine changes, never again. Four rules keep
the mode honest.

* **The `.fsm` stays the source of truth**, committed beside the artifact; it is what you read
  and edit.
* **The artifact is `DO NOT EDIT`**, and it already says so. Its provenance header names the
  input, the exact command that regenerates it, and the **canonical-form fingerprint** (FNV-1a
  64) of the machine it came from:

      // Generated by fsmtable-gen --target deno from corpus/traffic_light.fsm — DO NOT EDIT.
      // Regenerate with: fsmtable-gen --target deno corpus/traffic_light.fsm -o <this file>
      // Fingerprint of the canonical form (FNV-1a 64): 0x9174CCB513465B7A

  Two artifacts with the same fingerprint are the same machine. The fingerprint is over the
  *named canonical* form, so renaming a kind moves it — the same rule the header's fingerprint
  follows.
* **A drift probe is the anti-drift tooth.** Nothing regenerates a committed file by accident,
  so nothing notices when the `.fsm` and the artifact come apart — the probe does. It
  regenerates to a temporary file and diffs the two, and it runs only when the generator is
  present, so a machine without `fsmtable-gen` **skips** rather than lies — which is why
  installing the engine is step one: `cmake --install build-release --prefix ~/.local` (the
  README's "Install the tools"), and then the probe verifies instead of skipping. That is the same
  `when = "tool:<name>"` rule the `format`, `tidy`, `fuzz` and `deno` stages here already use:

      [stage.generated]
      cmd = "scripts/generated.sh"
      when = "tool:fsmtable-gen"

  and the stage body is a regenerate-and-diff (the `ci_*` helpers are the gate's own; the teeth
  are the middle):

      #!/usr/bin/env bash
      set -uo pipefail
      . "$(dirname "$0")/gate-env.sh"

      run_stage() {
          ci_begin "generated (the committed module still matches its .fsm)"
          require_tool fsmtable-gen generated || return 0

          local tmp="$CI_LOG_DIR/generated-check.ts"
          if ! fsmtable-gen --target deno my_machine.fsm -o "$tmp" > "$CI_LOG_DIR/generated.log" 2>&1; then
              ci_fail generated "fsmtable-gen refused my_machine.fsm" "$CI_LOG_DIR/generated.log"
          fi
          if ! diff -u my_machine.ts "$tmp" >> "$CI_LOG_DIR/generated.log"; then
              ci_fail generated "the committed module is stale — regenerate and commit both" \
                  "$CI_LOG_DIR/generated.log"
          fi
          ci_pass generated
      }

      run_stage "$@"

  The C++ direction is the identical script with `my_machine.hpp` in place of the `.ts` and no
  `--target` flag.
* **Regeneration is a two-step edit.** Change the `.fsm`, regenerate, commit the `.fsm` and the
  artifact *together*. A commit that moves one without the other is exactly what the probe
  exists to catch, and it is the only edit that touches the artifact.

This is a recipe for a *consumer* repository. FSMTable's own `examples/` stay build-time: there
the generator is the subject, so taking it out of the build would test nothing.

## The other direction: `fsmtable-transform`

`fsmtable-gen` goes text → code. `fsmtable-transform` goes text → text: it reads a `.fsm` and writes
one back, and its first verb is `rename` —
`fsmtable-transform rename my_machine.fsm state:Idle=Waiting kind:tick=beat`. It is a surgical edit
rather than a `dump`, so the comments that carry a machine's reasoning come back byte for byte and a
rename is one line of diff rather than a reformatted file; the artifact a consumer commits is then
regenerated from the renamed `.fsm` exactly as it always was (the two-step edit above).
`TRANSFORM.md` has the contract, the exit codes, and the limits — including the one worth knowing
before you rename anything, that a comment naming a state keeps the old spelling, because a comment
is prose.

## Not here yet

Useful, and each one is a format decision before it is a generator change:

* parameterised actions, or a named registry instead of one function per action;
* more than one refinement slot, i.e. richer predicates than one `int` comparison;
* whether an action may post further events (reentrancy).
