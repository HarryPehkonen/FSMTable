# FSMTable — specification v1

You are implementing a small C++17 library from scratch, test-first. Read this whole
file before writing code. Follow the stages in order. Do not skip the gate.

## 0. How to work

- **Tests first.** Write the test that fails, watch it fail for the right reason, then
  make it pass. A test that has never failed proves nothing.
- **Run the gate after every change**: `./scripts/gate.sh` must print a PASS verdict (the gate is
  `gate.toml`, run by `kit-ci`; section 6).
- **Do not invent scope.** If this spec does not ask for it, do not build it. If
  something is genuinely ambiguous, write the question into `QUESTIONS.md` and take the
  simplest reading rather than guessing elaborately.
- **Report evidence, not impressions.** Paste the real gate output and the real test
  counts. Never say "all tests pass" without the line that shows it.
- Keep every file formatted and warning-free. The gate is the judge, not your taste.

## 1. What to build

`FSMTable` reads a state machine from plain text, and writes one back out. Nothing
else: no code generation, no execution, no FSMgine dependency in stage A. The point is
a **total** text format — any byte string is either a valid machine or an error with a
line number, never undefined behaviour.

The later stages add static analysis and an FSMgine cross-check. Build stage A first.

## 2. The row format (frozen — do not change it)

One directive per line. Blank lines are ignored. A line whose first non-space
character is `#` is a comment. Leading and trailing whitespace is insignificant.
Fields are separated by one or more spaces.

```
version 1
machine TrafficLight
initial Red
transition Red --1--> Green action on_red_green
transition Green --2--> Yellow
transition Yellow --3--> Red when ge 30
```

- `version 1` — required, exactly once, and it must be the first non-comment line.
- `machine <Name>` — required, exactly once.
- `initial <state>` — required, exactly once. The named state is declared here if it
  has not appeared yet.
- `transition <from> --<kind>--> <to> [when <op> <int>] [action <ident>]`
  - `<from>`, `<to>`: state names, `[A-Za-z_][A-Za-z0-9_]*`, at most 64 characters.
  - `<kind>`: a decimal integer, `0` to `255` inclusive.
  - `<op>`: exactly one of `eq`, `lt`, `le`, `gt`, `ge`.
  - `<int>`: a decimal integer, `-2147483648` to `2147483647`.
  - `<ident>`: `[A-Za-z_][A-Za-z0-9_]*`, at most 64 characters. An action is a **name**
    that refers to a function the caller supplies. The format names actions; it never
    contains them.

Exactly one optional `when` clause and exactly one optional `action` clause are
allowed, in that order. States are not declared separately: the first time a state
name appears (in `initial`, `<from>` or `<to>`) it is declared.

## 3. Rules the parser must enforce

Every violation below is an error. An error has a **1-based line number** and a
message, and the parse produces **no machine at all** — never a partial one.

1. `version` missing, duplicated, not first, or not exactly `version 1`.
2. `machine` missing or duplicated; `initial` missing or duplicated.
3. An unknown directive, or a known one with too few fields.
4. A malformed arrow-kind token: the kind must be a single token of the form
   `--<digits>-->`. `-- 1 -->` is an error, not a lenient parse.
5. `kind` outside `0`..`255`; a `when` value outside the 32-bit range; a leading `+`;
   whitespace inside a number.
6. An `<op>` that is not one of `eq`, `lt`, `le`, `gt`, `ge`.
7. A state name or action name that is empty, does not match the pattern, or is longer
   than 64 characters.
8. Anything left over after the optional clauses: `transition A --1--> B bogus`.
9. **An ambiguous row**: for the same `from` and `kind`, more than one row *without* a
   `when` clause, or more than one row *with* one. A machine may refine at most one
   value slot, and the text format has exactly one slot, so this is the only
   ambiguity possible.
10. A `when` clause after an `action` clause.

## 4. The API (frozen)

```cpp
namespace fsmtable {

enum class Op { Eq, Lt, Le, Gt, Ge };

struct Error {
    int line;            // 1-based; 0 when the error is not tied to a line
    std::string message; // short, lowercase-ish, names the fault
};

struct Transition {
    std::string from;
    int kind;
    bool has_when;       // true when the row carried a `when` clause
    Op when_op;          // meaningful only when has_when
    int when_value;      // meaningful only when has_when
    std::string to;
    std::string action;  // empty when the row carried no `action` clause
};

struct Machine {
    std::string name;
    std::string initial;
    std::vector<std::string> states;      // first-appearance order, unique
    std::vector<Transition> transitions;  // file order
};

std::optional<Machine> parse(std::string_view text, Error& error);
std::string dump(const Machine& m);

}  // namespace fsmtable
```

`parse` returns an empty optional on any error and fills `error`. In stage A a
successful parse never errors on `dump`.

`dump` emits a **canonical** form: `version 1`, `machine`, `initial`, then the
transitions sorted by `from`, then `kind`, then guarded-before-unguarded, then `to`,
then `action`. One space between fields, one newline at the end, no comments, no
blank lines.

## 5. Tests to write first (frozen list — these names, this meaning)

Parsing:
- `ParsesTheExampleFromTheSpec`
- `IgnoresCommentsAndBlankLines`
- `DeclaresStatesInFirstAppearanceOrder`
- `ParsesWhenAndActionClauses`
- `AcceptsLeadingAndTrailingWhitespace`

Errors — each one asserts **both** the line number **and** that no machine came back:
- `RejectsMissingVersion`  /  `RejectsWrongVersionNumber`
- `RejectsDuplicateMachine`  /  `RejectsMissingInitial`
- `RejectsUnknownDirective`  /  `RejectsTrailingJunk`
- `RejectsBadKindToken`  /  `RejectsKindOutOfRange`
- `RejectsBadOp`  /  `RejectsWhenValueOutOfRange`
- `RejectsOverlongName`  /  `RejectsAmbiguousRow`
- `ReportsTheRightLineNumber` — the malformed line is line 7, `error.line` must be 7.

Dumping:
- `DumpIsCanonical` — sorted, no comments, single trailing newline.
- `DumpsEveryField` — round-trips every field of a machine that uses all of them.
- `DumpParseDumpIsStable` — the oracle: `dump(parse(dump(parse(t))))` equals
  `dump(parse(t))` for every text in the corpus.

## 6. The gate (no exceptions, no CI)

There is a project kit at `~/hermes-workspace/AI-DEV-STARTER`. Follow its **PLUNK-IN**
for the C++ template, then make sure the gate runs and these stages are all covered:

    format -> build -> lint -> tests -> fuzz

- `build`: both **gcc and clang**, no warnings. Warnings are errors.
- `lint`: whatever the kit configures. Fix findings in the code, never by widening the
  configuration or adding suppressions.
- `tests`: all of them, and the count must be printed.
- `fuzz`: the libFuzzer target built with `-fsanitize=fuzzer,address,undefined`, run for
  **60 seconds** with the checked-in corpus. Any artifact is a failure to investigate,
  not to delete.
- the gate with no arguments must run all of the above and print an unmistakable
  verdict line.
- **Do not add `.github/workflows/` or any hosted CI.** Gates live in the repo.
- Commit hooks: the kit's, wired and working. Do not commit with `--no-verify`.

The gate was one bash script (`tools/ci.sh`) when this stage was built. It is now `gate.toml`
— the stages, their tiers and their failure rules — run by `kit-ci` from `scripts/gate.sh`,
with one `scripts/<stage>.sh` per stage (`INCIDENTS.md` records the conversion). The five
stages above are all still run: `lint` is named `tidy` there (clang-tidy is the linter), and
the full tier adds `tree`, `release`, `version`, `asan`, `tsan`, `pristine` and `deno` beside
them.

## 7. The corpus (frozen, checked in)

    corpus/traffic_light.fsm    the example in section 2
    corpus/minimal.fsm          the smallest legal machine
    corpus/guarded.fsm          every op used at least once
    corpus/invalid_ambiguous.fsm  two unguarded rows for one from+kind
    corpus/invalid_junk.fsm     trailing junk after the clauses

A test walks the directory: every `invalid_*.fsm` must fail to parse, and its expected
line number is recorded in a table in the test file. Every other `.fsm` must parse and
must satisfy `DumpParseDumpIsStable`.

## 8. Stages

Build them in order. **Stop at the end of each stage and report.** Do not start the
next stage in the same sitting.

- **Stage A (this specification):** the format, the parser, the canonical dump, the
  error contract, the frozen tests, the corpus, the fuzz target, the gate.
- **Stage B (only when A is green):** two analysis functions, each with tests —
  `std::vector<std::string> unreachable(const Machine&)` (no path from `initial`) and
  `std::vector<std::string> sink_states(const Machine&)` (no outgoing transition).
  Deterministic order: first-appearance.
- **Stage C (only when B is green):** cross-check against FSMgine 2.1.0 in
  `~/hermes-workspace/FSMgine`. Build the parsed machine with `fsmgine::compiled` and
  require an identical trace (state after every event, plus the action log) against a
  generated event sequence. This is the differential oracle; a disagreement is a bug in
  one of the two, and finding out which is the work.

## 9. Acceptance for stage A

- `./scripts/gate.sh` prints its PASS verdict, with the fuzz stage having run 60 seconds.
- Every test in section 5 exists, under that name, and passes.
- The corpus rules in section 7 hold.
- A `REPORT.md` exists, filled in as described below.

## 10. REPORT.md — the only narration allowed

Fill in exactly these fields, with real values copied from real runs:

    Gate verdict line (verbatim):
    Test count:
    Fuzz stage: runs / seconds / artifacts
    Stage reached:
    What I could not do:
    What I had to guess:      (with the question, or "nothing")

Do not grade yourself, do not summarise your approach, do not describe the code. The
gate decides whether this worked; your job is to report what it did. If you could not
finish a stage, say which part and stop — an honest stop is a valid result, and a
claim you cannot back with gate output is not.
