# edge — the on-ramp: six rows, three states, two kinds

An edge detector written as a table, and the example to read first. `edge.fsm` is six rows, three
states and two kinds; `edge.cpp` decides which byte is which kind. Nothing here is guarded, no row
compares a value, no state carries an entry or exit clause, and no action does more than print a
line — this example's whole content is the four nouns: **a machine**, its **states**, its **kinds**,
and its **rows**.

Feed it a stream of `0` and `1` bytes and it prints one line per edge — a change from the last bit
to this one — and nothing for a bit that simply repeats:

    $ ./build/edge < examples/edge/input/session.txt
    0 --> 1
    1 --> 0
    0 --> 1
    1 --> 0
    0 --> 1
    1 --> 0

The three states, in the file's own words:

- **`Start`** — nothing has arrived yet. There is no last bit, so there is nothing to compare the
  first one against: the first `0` moves to `SawZero` and the first `1` moves to `SawOne`, and both
  report nothing. Nothing → 0 is not a change.
- **`SawZero`** — the last bit read was a `0`. Another `0` stays here and reports nothing; a `1` is
  a rising edge.
- **`SawOne`** — the last bit read was a `1`. Another `1` stays here and reports nothing; a `0` is a
  falling edge.

A state here is exactly one bit of memory — *the last bit seen* — and every row is a sentence about
what a bit means **given where you are**.

```
  Start --zero--> SawZero --zero--> SawZero        the first bit, then the same bit again
    |               |
    |               +--one--> SawOne   (rising)     a 0 then a 1: the row prints `0 --> 1`
    +--one--> SawOne --one--> SawOne               the first bit, then the same bit again
                 |
                 +--zero--> SawZero  (falling)      a 1 then a 0: the row prints `1 --> 0`
```

| rows | action | what it does |
| --- | --- | --- |
| `SawZero --one--> SawOne` — one row | `rising` | prints `0 --> 1` |
| `SawOne --zero--> SawZero` — one row | `falling` | prints `1 --> 0` |
| the other four | *(none)* | a move, and a move is all they are |

## One kind, three meanings — this example's whole argument

Three of the six rows take the **same** kind, `zero`, and only one of them speaks. The state it
arrives in is the entire difference:

```
  Start   --zero--> SawZero    absorbed   the first bit: nothing → 0 is not a change
  SawZero --zero--> SawZero    absorbed   the same bit twice: nothing changed
  SawOne  --zero--> SawZero    reported   a 1 followed by a 0: the row prints `1 --> 0`
```

Two of those rows print nothing and the third prints `1 --> 0`, and no rule in the table separates
them except *which state the byte arrived in*. That is the format's argument in three lines: a state
is what you remember, a kind is what can arrive, a row is what that kind means **given where you
are**, and an action is the row that speaks. Every other example in this directory adds one idea to
this one.

## The three layers

| what | where | who owns it |
| --- | --- | --- |
| the table | `edge.fsm` | **you.** The only file that decides anything. |
| the machine | `build/generated/fsm_edge.hpp` | **the generator.** Never edit it; it is rebuilt on every build. |
| the driver | `edge.cpp`, `main.cpp` | **you.** What a byte *is* — `zero`, `one`, or neither — and the refusal. |

`edge.hpp` is the seam between the last two: the driver's whole interface to the machine, and the
same shape as the csv example's — a `Result`, a `Reader`, and the two free functions the actions
reach a run through — one action and two pieces of driver state smaller.

## The machine, walked without the driver

`fsmtable-inspect --trace` walks the table over a flat event list with no driver and no bytes at all,
and names the action each row carries. The four events below are `zero zero one zero`: the three
`--zero-->` rows in order, with the one `--one-->` row that takes `SawZero` to `SawOne` between the
second and the third:

```
$ ./build/fsmtable-inspect --trace examples/edge/edge.fsm zero zero one zero
examples/edge/edge.fsm: Edge — trace of 4 event(s) from Start
  Start --zero--> SawZero
  SawZero --zero--> SawZero
  SawZero --one--> SawOne  (action rising)
  SawOne --zero--> SawZero  (action falling)
```

Read the three `--zero-->` lines together — the first, the second and the last — and compare them
with the one in between: the same kind, three arrivals, and the `(action ...)` tag on only the third
of them. A trace is not a driver — no byte is classified and no action runs — which is exactly why
it is the honest way to show a row: it shows the row, and nothing else.

## The recorded run, line for line

The recorded trace is `input/session.expected`, diffed byte for byte by the `edge_session` ctest:

```
0 --> 1
1 --> 0
0 --> 1
1 --> 0
0 --> 1
1 --> 0
```

`input/session.txt` is the ten bits `0010110110` and nothing else — no trailing newline, because the
input is a bit stream rather than a text file (the reading below). Every line in that trace is a row
that fired: the first `0` was absorbed in `Start`, the three `1`s after a `0` ran `rising`, and the
three `0`s after a `1` ran `falling`.

## The refusal

`edge.fsm` is total over its own alphabet: every state has a row for `zero` and a row for `one`, so
the machine can never have nothing to say about a bit. A byte that is **neither** bit is therefore
not the machine's problem — it has no kind for it and no state to arrive in — and refusing it is the
driver's, which is where the boundary sits in every example here. `input/stray.txt` is `01x`, the
smallest stream that reaches the refusal, and
`input/stray.expected`, the message and all, is what the run printed:

```
0 --> 1
! byte 3: 'x' is not a bit
```

The run keeps the edge that really happened (`01` is a rising edge, printed before the bad byte),
then stops at the byte, reports its 1-based position, and exits **1** — the exit code is the part a
script would use. Nothing is invented for the bytes after it. The message names the byte the way it
can be named: a printable one as the character it is, anything else by its value, so the newline a
shell adds to `echo 01` reads as `0x0a` rather than as a blank.

## Who owns what, in this machine's own terms

**The machine decides** what sequence of bits is a stream of edges: that the first bit is absorbed
without reporting, that a repeated bit changes nothing, and that a change is one row that reports
and one bit of new memory.

**The driver owns** the only two things the format cannot hold:

- **what a byte is.** The machine has two kinds and no notion of `'0'` or `'1'`; the driver reads a
  byte, decides which of the three cases it is (a bit, a bit, or neither), and feeds the machine a
  kind. Both kinds are plain — neither carries a value on the event — so there is no refinement slot
  here at all, and the event is nothing but its kind. (The csv example's table has no guard either,
  but its actions read the byte off the event; here nothing reads anything.)
- **the refusal.** A byte that is neither bit is the driver's report and the driver's exit code
  (`edge.cpp`). The alternative — a third kind such as `other`, with a row in every state to absorb
  it — is the machine absorbing a byte it cannot see; this example leaves the machine total over
  bits and the driver holding everything else.

**The text** is the driver's too: `0 --> 1` and `1 --> 0` are the actions' words, and the machine's
own vocabulary is state and row names. Printing *those* would show the table rather than drive it.

## The reading this example took

- **The input is a bit stream, so a byte that is not a bit is refused — a newline included.** A
  shell adds a newline to `echo 01`, and that newline is refused (`byte 3: 0x0a is not a bit`); the
  recorded session is therefore the bits alone, with no trailing newline. The alternative is for the
  driver to skip whitespace, which would make `echo 01 | ./build/edge` work and would also mean the
  driver reads two vocabularies — bits, and gaps between them. The example stays with one, because
  the point it is teaching is that a kind is what can arrive (QUESTIONS.md Q27).
- **`Start` is a state and not a special case in the code.** The first bit had to be absorbed
  without reporting, and the table is the natural place for that decision: give it a state with no
  action on either row, and *nothing → 0* stops being a transition anyone reports. The alternative —
  a `bool started` in the driver — is the same behaviour with the decision moved out of the file
  that exists to hold decisions (QUESTIONS.md Q26).
- **No `state` lines.** `edge.fsm` declares no entry or exit clauses, and a bare `state <Name>` line
  with no clause is not legal: a state is declared by appearing in `initial`, `<from>` or `<to>`
  (the csv example found the same thing). Three states, six rows, and no other lines.

## The tests

- `edge_test.cpp` — eleven cases: a rising edge, a falling edge, a repeated bit ignored, the first
  bit absorbed, the same `zero` kind meaning three different things, empty input, the state being
  the last bit seen, the ten-bit stream's six lines in order, the refusal (position, message and the
  line kept before it), that a refused byte leaves the machine where it was, and that a newline is
  refused by value.
- `edge_session` (ctest) — `input/session.txt` through the built binary, diffed byte for byte
  against `input/session.expected`, with the exit code pinned beside the bytes.
- `edge_refuses_a_byte_that_is_not_a_bit` (ctest) — `input/stray.txt` through the binary: the
  message and exit code 1 checked together in the shell.
- `edge_trace_shows_one_kind_three_ways` (ctest) — the trace above, pinning all three `--zero-->`
  rows and the two action tags.
- `fsmtable-inspect examples/edge/edge.fsm` — exits 0, reporting no sink and no unreachable state.
  It is not a case of its own, because `tools/check-doc-claims.sh`'s tree check inspects every
  `.fsm` under `examples/` on every gate run, and this file is one of them.

## Build your own from this

1. **Write down what a kind is.** Here it is two values — `zero` and `one` — and the driver maps a
   byte onto them. A kind that carries a value is a different example's job (the calculator and csv
   ones); start with kinds that are just names.
2. **Write down the states as "what am I holding".** Not "what happens next": the three here are
   *nothing yet*, *the last bit was a 0*, and *the last bit was a 1*. If you can name what a state
   remembers, the rows write themselves.
3. **Write the rows as sentences**, one per legal kind per state: *in `<state>`, `<kind>` goes to
   `<state>` and runs `<action>`.* Leave the action off the rows that change nothing, which is how
   `00` and `11` come out silent.
4. **Name the actions by what they do to the driver's output**, not by where they are: `rising` and
   `falling` are each one row here, and nothing else.
5. **Write the driver**: what a byte *is*, and what to do with one that is neither kind — the
   machine has no row for it, so the refusal is yours to report.
6. **Wire it into CMake** the way `CMakeLists.txt` does for this example: one `add_custom_command`
   that runs `fsmtable-gen`, one library, one executable, one test binary — plus a recorded session
   file fed through the binary and diffed.

## Where the rest is written down

    SPEC.md                         the frozen format and API (sections 2 and 4 are the frozen ones)
    GENERATOR.md                    what fsmtable-gen emits and why the table is a literal array
    NAMED_KINDS.md                  the `kind` declarations this file uses, and what a name buys
    examples/README.md              the examples, the recipe they share, and what to build next
    examples/csv/README.md          the next step: kinds that carry a value, and a refusal the
                                    machine itself makes
    examples/inspector/README.md    the tool this example is read with: the summary, --trace, --graph
