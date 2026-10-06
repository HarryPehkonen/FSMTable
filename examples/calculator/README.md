# calculator — the first real consumer of the generator

A line of numbers and operators in, one answer out, until Ctrl-D. The arithmetic is not the point:
this is a small, complete example of the whole pipeline —

    calculator.fsm --fsmtable-gen--> fsm_calculator.hpp + calculator.cpp --> ./build/calculator

— so the next machine (yours) is a variation of this file set rather than a new project. Read
`calculator.fsm` first: it is the authority for what the program does.

## Run it

    ./tools/ci.sh build
    ./build/calculator                       # answers; a prompt only on a terminal
    ./build/calculator < input/session.txt   # the recorded session, answers only

    $ ./build/calculator
    > 12
    12
    > 10 - 2 * 3
    24
    > 7 / 0
    error: division by zero

## What it does

One line of numbers and operators per calculation. Enter prints the answer and waits for the next
line. Piped input gets the answers and nothing else — the prompt is only for a terminal.

    12                                   ->  12
    10 - 2 * 3                           ->  24      left to right: (10 - 2) * 3, not 10 - 6
    1 + 2 + 3                            ->  6
    7 / 2                                ->  3       integer division
    7 / 0                                ->  error: division by zero
    7 / 0 + 3 * 2                        ->  error: division by zero
    12 +                                 ->  error: incomplete expression
    12 3                                 ->  error: unexpected number 3
    + 3                                  ->  error: unexpected operator +
    12 & 3                               ->  error: unexpected character '&' at column 4
    99999999999                          ->  error: number out of range
    2147483647 * 2147483647 * 2147483647  ->  error: overflow

Every line is independent, and the reset is the *machine's*: the `Entry` state carries an `entry`
clause, so arriving in it runs `clear_all` (ENTRY_EXIT.md). The seven `--clear-->` rows therefore
need no action of their own — one clause instead of seven copies of the same one.

**Left to right, not precedence, on purpose.** This is a desk calculator: the machine commits each
operand as it arrives, which is the shape a table-driven machine has. Precedence needs a parser and
a stack outside the machine, which would leave the machine owning very little.

## The three layers, and who owns what

    calculator.fsm   what is legal and what it means: states, kinds, rows. The authority.
    fsm_calculator.hpp  generated from the .fsm at build time; never edited by hand
    calculator.cpp   the tokeniser, the register file, and the seven actions the machine names
    calculator.hpp   Shell / Outcome / Registers: everything that decides, no I/O
    main.cpp         a line in, a line out. Nothing else
    calculator_test.cpp  16 tests driving the same Shell the REPL does

The machine owns the grammar and the pending operator — the state *is* the operator
(`PendingAdd`, `PendingSub`, `PendingMul`, `PendingDiv`), so no register records it.
`calculator.cpp` owns the numbers, because no row can hold one: a `when` clause compares one `int`
that rides on the event (SPEC.md section 3).

**The whole contract between an application and a generated machine** is four lines:

    auto machine = makeCalculator();                     // from the generated header
    for (const Token& token : tokenise(line))            // whatever your input is
        machine.process(CalculatorEvent{token.kind, token.value});
    machine.currentState();                              // the state it is in now

`process()` returns false when no row matches the current state — the machine saying "not in my
language" — and changing nothing. Actions have no return value and no context parameter (Q11), so
the example hands them the registers through a pointer installed for the duration of one line
(`calc::Shell`, `calculator.cpp`). The machine owns the grammar; the shell owns the numbers and the
printing.

## Reading calculator.fsm

    version 1
    machine Calculator
    initial Entry
    kind number = 1      # a name for what the rows below used to spell --1--
    ...
    state Entry entry clear_all            # arriving in Entry resets the line (ENTRY_EXIT.md)

    transition Entry --number--> Accum action take_operand       # first operand of a line
    transition Accum --add--> PendingAdd                         # no action: the state IS the operator
    transition PendingAdd --number--> Accum action add_operand   # second operand: do the sum
    transition PendingDiv --1--> Error when eq 0 action note_division_by_zero
    transition PendingDiv --1--> Accum action div_operand        # guarded first: canonical order

Read it as a sentence: "in `PendingAdd`, a number means add it and go back to `Accum`". Three
things are worth noticing, because they are the vocabulary the format gives you:

* A state named `PendingX` is how the machine remembers a pending operator, and the row that
  enters it needs no action at all.
* A guarded row and an unguarded row for the same `from`+`kind` are a refinement pair (rule 9):
  the guarded one is tried first, which is what makes `7 / 0` a decision in the table.
* `Error` is total over the rest of the line on purpose: if a token were refused there instead,
  `7 / 0 + 3` would report "unexpected operator +" and bury the division that went wrong.

## Build your own from this

1. **Write the machine.** `version 1`, `machine <Name>`, `initial <State>`, the `kind` names you
   need, then one `transition` per row. States are declared by their first appearance — there is no
   `state` declaration; a `state` line only decorates one with entry/exit actions.
2. **Generate.** Add the file to `FSMTABLE_GENERATOR_INPUTS` in `CMakeLists.txt`, or run the tool
   by hand: `./build/fsmtable-gen my.fsm -o my.hpp` (exit 0 written, 1 unusable input, 2 bad usage).
3. **Read the header it wrote** (`build/generated/fsm_my.hpp`): an `enum class` for the states and
   one for the kinds, `MyEvent{kind, value}`, one declaration per action your file named (including
   its `entry`/`exit` clauses), `MyRows` in canonical order, `makeMy()`, and `MyStateNames` /
   `MyNameOf` for logs. Every declared action is yours to define: a missing one is a link error, not
   a silent no-op.
4. **Drive it.** `auto machine = makeMy();` then `machine.process(MyEvent{MyKind::step, 0})`. An
   event that matches nothing returns false and changes nothing.
5. **Test the machine, not the I/O.** The machine is data, so a test needs no pipe and no terminal —
   the 16 tests here drive the same `Shell` the REPL does.

### Worked example: add a `%` (remainder) operator

The `.fsm` half of this is verified against the real generator (it generates: 8 states, 27 rows);
the C++ half is the two lines any new kind needs.

    calculator.fsm   kind mod = 8                                     # with the other kinds
    calculator.fsm   transition Accum --mod--> PendingMod
    calculator.fsm   transition PendingMod --number--> Error when eq 0 action note_division_by_zero
    calculator.fsm   transition PendingMod --number--> Accum action mod_operand
    calculator.cpp   case '%': return Token{CalculatorKind::mod, 0};   # in the tokeniser
    calculator.cpp   void mod_operand(const CalculatorEvent& event) { ... }   # the header declares it

Nothing else changes. `PendingMod` needs no declaration — the row that names it declares it — and
the two `PendingMod --number-->` rows are the same guarded pair as division, because `x % 0` traps
too and the guard can see it (the divisor rides on the event). Rebuild and `dump` will print the new
rows in canonical order.

## The split, and what it cost

That produced two findings, and they are the point of this example (QUESTIONS.md Q10):

* **Division by zero is a ROW.** The divisor is the event's value and the pending operator is a
  state, so a guard can see what it needs — two rows for one `from`+`kind`, which only works because
  the generated table is in canonical order, guarded first. `calculator_test.cpp` asserts the machine
  ends in `Error`, so a generator that emitted the file's order instead would fail the tests rather
  than quietly compute `7 / 0` through the arithmetic layer.
* **Overflow cannot be a row.** The product is in neither the event nor the state, so it is caught in
  the arithmetic and reported by the shell. `error: number out of range` is refused even earlier, by
  the tokeniser — the machine never sees it.

Actions reach the registers through a pointer installed for the duration of one line (Q11), because
the compiled back end's action type is `void (*)(const Event&)` — no context parameter. The v2 fix is
a context parameter, the shape libuv's callbacks use.

## Tests

The 16 unit tests run the machine and the registers directly. The session case pipes
`input/session.txt` through the built binary and `diff`s the result against `input/session.expected`,
so the path from a typed line to a printed answer is covered as one piece — and any change in output
formatting has to be an explicit edit to that file.

Both run in every configuration the gate builds, including ASan/UBSan and ThreadSanitizer.

## Where the rest is written down

    SPEC.md            the frozen format and API (sections 2 and 4 are the frozen ones)
    GENERATOR.md       the tool, the emitted artifact, the exit codes
    NAMED_KINDS.md     `kind <name> = <n>` — the first addition to the reader
    ENTRY_EXIT.md      `state <name> entry ... exit ...` — the second
    QUESTIONS.md       every place the spec left a choice, and the reading taken
    REPORT.md          what was delivered, and the gate verdict for each delivery
