# fsmtable-gen — the text → code direction

`fsmtable-gen` reads one `.fsm` file and writes one self-contained C++ header. The text is the
source; the header is a build product.

```
./tools/ci.sh build                                        # builds build/fsmtable-gen
./build/fsmtable-gen corpus/traffic_light.fsm -o traffic_light.hpp
./build/fsmtable-gen corpus/traffic_light.fsm              # or straight to stdout
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
| `MKind` | `enum class : std::uint8_t`, the event kinds the rows use |
| `MEvent` | `{ kind; value; }` — `kind` is what the back end reads, `value` is the format's single refinement slot |
| `MStateNames` | `std::array<std::string_view, N>` of the state names, for logs and code → text |
| `MNameOf` | `std::string_view (MState)`, the name function `withNames()` wants |
| `MRows` | `constexpr std::array<fsmgine::compiled::Transition<…>, N>`, the transition table |
| `makeM()` | a machine ready to drive, initial state set, names wired |
| `void action(const MEvent&)` | one declaration per action the file names — you define these |

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
* Names that are C++ keywords are refused rather than mangled — a state called `new` cannot
  become an enumerator, and mangling would break the round trip back to text (Q8).
* A machine with more than 65535 states is refused: the state enum is `std::uint16_t`.
* The body is wrapped in `// clang-format off` … `// clang-format on`, so a formatter run over
  your tree cannot turn regeneration into a two-step edit.
* Generated headers are not for editing. The `.fsm` is the source; regenerate instead.

## Wiring it into a build

`CMakeLists.txt` does it for four inputs at once: one `add_custom_command` per `.fsm`, all four
headers generated into `build/generated/`, and `tests/generator_test.cpp` compiles them — so a
generator that emits something that does not compile fails the build, not a review. The calls run
with the source directory as their working directory, which is why each header's provenance
comment names its input the way the repo sees it (`corpus/traffic_light.fsm`).

The generator's own failure paths are ctest cases: a corpus file that does not parse, a fixture
whose state name is a keyword, and one shape check on the emitted table of the spec's example.

## Not here yet

Useful, and each one is a format decision before it is a generator change:

* named event kinds — today `--1-->` is a raw 0..255 number, emitted as `k1`;
* entry and exit actions per state;
* parameterised actions, or a named registry instead of one function per action;
* more than one refinement slot, i.e. richer predicates than one `int` comparison;
* whether an action may post further events (reentrancy).
