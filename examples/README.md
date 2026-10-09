# Examples

Six worked examples. Five are complete programs you can build, run and read; the sixth is three
`.fsm` files and the tools this repository already ships. They are listed in the order to read them
in: the first teaches the four nouns and nothing else, and each one after it adds one idea.

| example | what it is | what it is here to show |
| --- | --- | --- |
| [`edge/`](edge/README.md) — **start here** | an edge detector: six rows, three states, two kinds | the format itself, with **no prerequisites**: a state is what you remember, a kind is what can arrive, a row is what that kind means given where you are, and an action is the row that speaks. The same `zero` kind is silence from two states and a report from the third, and the state is the whole difference — every example below adds one idea to this one |
| [`calculator/`](calculator/README.md) | a line-oriented calculator: an expression in, an answer or an error out | the generator on **arithmetic** — refined rows as an operator-precedence table, a guard on an event's value, a self-transition, and the split between what the table decides and what the code around it holds |
| [`protocol/`](protocol/README.md) | a connection lifecycle with retransmission and a timeout | the generator on **time** — a clock the format cannot hold, a refinement pair, a sink state, exit/action/entry composition, and the four limits a real design runs into |
| [`csv/`](csv/README.md) | an RFC 4180 reader: a byte stream in, a `Cell:` line per cell and a `NEW LINE` per record out | the generator on **actions** — fifteen rows sharing three actions, a value-carrying kind whose value *is* the data, a state whose whole job is to be the instant between two others, and one row deliberately missing |
| [`inspector/`](inspector/README.md) | `fsmtable-inspect`: summarize a `.fsm`, report unreachable states, sinks and partially covered pairs, walk the table over a flat event list (`--trace`), draw it as a Mermaid state diagram (`--graph`), write the canonical form back (`--canonical`) | the **library without the generator** — `parse`, `dump` and the analyses, wrapped in a tool with an exit code you could put in a hook |
| [`vending/`](vending/README.md) | a vending machine as two half-machines — the payment and the stock — composed into one by `fsmtable-transform merge` | the **text → text direction** — a seam that is nothing but a shared state name, a provenance header as a committed artifact, and the difference between a machine's name and its identity |

## Each one stands alone

There is no shared example library. `edge.cpp`, `calculator.cpp`, `protocol.cpp`, `csv.cpp` and
`inspector.cpp` each carry their own small driver, and the four that build a machine each have their
own header. That is a deliberate trade: a few hundred duplicated lines in exchange for being able to
read one directory and understand all of it, without following a third file that exists only to be
shared. `vending/` takes the same trade to its limit and has no driver at all: its three `.fsm` files
are the whole example, and the tools that read them are the ones already installed on the machine.

## The recipe they follow

1. **Write the machine as text** — `<name>.fsm`. This is the only file that decides anything.
2. **Write down who owns what**: what the table decides, and what has to live beside it (a clock, a
   stack of operands, a socket, a retry count, a coin slot's sum). The format cannot hold those, and
   pretending otherwise is the most common way to end up fighting it.
3. **Generate the header** with one `add_custom_command` in `CMakeLists.txt`. Never edit the
   generated file, and never check it in.
4. **Write the driver** — the actions the generated header declared, plus whatever feeds events in.
   A misspelt action name is a linker error, so the machine and the driver cannot drift apart
   quietly.
5. **Write the tests**: gtest on the driver (states reached, actions run, in order), and one
   end-to-end case that pipes a recorded script through the built program and diffs the output
   against a recorded trace.

`vending/` steps out at 2: it has no driver, so there is nothing to generate a header for and no
script to pipe. What it wires instead is a merge the gate re-runs against the committed union and
the blocks its README prints — its own README says why, and the shape is worth copying for any
example whose subject *is* the driver's job.

## Ideas worth a weekend

Machines that are more fun than a calculator, roughly in order of how much they teach:

- **A vending machine with two products.** [`vending/`](vending/README.md) is the two-half version of
  this one, and it stops where the format stops: the credit is a sum the driver keeps, and one price
  is one constant in one guard. A second price is the next wall — a `when` clause compares against a
  constant, so a second price wants a second state rather than a second guard.
- **An elevator.** Doors opening and closing are entry and exit actions, travel time is a value on a
  tick, and the request queue is the driver's. The state space is small and the edge cases are not.
- **A traffic light with a pedestrian button.** The corpus already has a three-state light; add a
  minimum-green rule and a button that is only honoured in some states, and the guard/refinement
  pair stops being an exercise.
- **An order lifecycle** — cart, paid, shipped, delivered, cancelled, refunded. This is the shape
  most backend work actually has, and it is a good way to find out how much of it is a table and how
  much of it is a database.
- **A lexer.** Character classes are event kinds, one state per lexical context, and a value can
  carry the character that did not fit. [`csv/`](csv/README.md) is this shape at its smallest: four
  character classes, four states, and a value that carries the byte. Then feed the tokens to a second machine that is the parser:
  two generated tables, one driver, and the classic FSM job done the way the textbooks describe it.
- **A retry or backoff supervisor.** The driver counts attempts and decides when to give up; the
  machine decides what is legal in between. `protocol/` is already most of the way there.
- **A game's turn machine.** Whose turn it is, what is legal right now, and a sink for the end of
  the game — a good fit for any rules engine where illegal moves have to be *impossible* rather than
  merely rejected.
- **A user-onboarding flow.** Each step is a state, going back is legal only from some of them, and
  "resume where you left off" is the machine's current state stored as a single value.

What they have in common is the reason this format exists at all: the hard part is the *shape* of
what may happen next, not the code that does the work. If you can draw the picture, you can write
the table.

## Documentation for a new example

A new example needs its own `README.md` in the shape of the ones here: the three layers and who owns
what, the machine's text read as sentences, the decisions that look surprising in hindsight, the
tests, and a verified "build your own from this". Wire it in with the four pieces `CMakeLists.txt`
uses per example: the `add_custom_command` that runs `fsmtable-gen`, the library, the executable, and
the test target — plus its own `protocol_session`-style ctest case if it has a recorded trace. An
example that generates no code needs none of those four: `vending/` is three `.fsm` files, four ctest
cases that re-run the tools it documents, and the doc-claims rules that hold every block it prints,
which is the lighter shape and the one to copy when the subject is the machine rather than a driver.
