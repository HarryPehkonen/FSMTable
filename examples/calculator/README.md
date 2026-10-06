# calculator — the first real consumer of the generator

`calculator.fsm` is the machine, `calculator.cpp` is the support code the machine names, and
`main.cpp` is the shell around both. Built with the rest of the repo:

```
./tools/ci.sh build
./build/calculator           # type a line, read the answer, Ctrl-D or Ctrl-C to end
```

## What it does

One line of numbers and operators per calculation. Enter prints the answer and waits for the
next line. Piped input gets the answers and nothing else — the prompt is only for a terminal.

```
12                                      ->  12
10 - 2 * 3                              ->  24      left to right: (10 - 2) * 3, not 10 - 6
1 + 2 + 3                               ->  6
7 / 2                                   ->  3       integer division
7 / 0                                   ->  error: division by zero
7 / 0 + 3 * 2                           ->  error: division by zero
12 +                                    ->  error: incomplete expression
12 3                                    ->  error: unexpected number 3
+ 3                                     ->  error: unexpected operator +
12 & 3                                  ->  error: unexpected character '&' at column 4
99999999999                             ->  error: number out of range
2147483647 * 2147483647 * 2147483647     ->  error: overflow
```

Every line is independent: the machine's own `Clear` row runs at the start of the next line, so
nothing carries over. That is deliberate — it keeps a test down to one line — and it is the row,
not the shell, that resets the registers.

**Left to right, not precedence, on purpose.** This is a desk calculator: the machine commits
each operand as it arrives, which is the shape a table-driven machine has. Precedence needs a
parser and a stack outside the machine, which would leave the machine owning very little.

## The split, and what it cost

The machine owns the grammar and the pending operator — the state *is* the operator
(`PendingAdd`, `PendingSub`, `PendingMul`, `PendingDiv`), so no register records it.
`calculator.cpp` owns the numbers, because no row can hold one: a `when` clause compares one
`int` that rides on the event.

That produced two findings, and they are the point of this example (QUESTIONS.md Q10):

* **Division by zero is a ROW.** The divisor is the event's value and the pending operator is a
  state, so a guard can see what it needs:

      transition PendingDiv --1--> Error when eq 0 action note_division_by_zero
      transition PendingDiv --1--> Accum action div_operand

  Two rows for one `from`+`kind`, which only works because the generated table is in canonical
  order — guarded first. `calculator_test.cpp` asserts the machine ends in `Error`, so a
  generator that emitted the file's order instead would fail the tests rather than quietly
  compute `7 / 0` through the arithmetic layer.
* **Overflow cannot be a row.** The product is in neither the event nor the state, so it is
  caught in the arithmetic and reported by the shell. `error: number out of range` is refused
  even earlier, by the tokeniser — the machine never sees it.

The `Error` state is total over the rest of the line on purpose: if a token were refused there
instead, `7 / 0 + 3` would report "unexpected operator +" and bury the division that went wrong.

Actions reach the registers through a pointer installed for the duration of one line
(Q11), because the compiled back end's action type is `void (*)(const Event&)` — no context
parameter.

## Files

| file | what it is |
| --- | --- |
| `calculator.fsm` | the machine: 7 states, 7 kinds, 24 rows, one comment naming the kind numbers |
| `calculator.cpp` | the tokeniser, the line driver, and the 7 actions the generated header declares |
| `calculator.hpp` | `Outcome`, `Registers`, `Shell` — the whole API, no I/O |
| `main.cpp` | read a line, hand it over, print what came back |
| `calculator_test.cpp` | 16 tests, feeding lines through the same `Shell` the REPL uses |
| `input/session.txt`, `input/session.expected` | a session and its exact output, diffed by one ctest case |

## Tests

The 16 unit tests run the machine and the registers directly. The session case pipes
`input/session.txt` through the built binary and `diff`s the result against
`input/session.expected`, so the path from a typed line to a printed answer is covered as one
piece — and any change in output formatting has to be an explicit edit to that file.

Both run in every configuration the gate builds, including ASan/UBSan and ThreadSanitizer.
