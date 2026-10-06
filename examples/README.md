# Examples

Three worked examples. Each one is a complete program you can build, run and read, and each one is
here to show a different face of the format:

| example | what it is | what it is here to show |
| --- | --- | --- |
| [`calculator/`](calculator/README.md) | a line-oriented calculator: an expression in, an answer or an error out | the generator on **arithmetic** — refined rows as an operator-precedence table, a guard on an event's value, a self-transition, and the split between what the table decides and what the code around it holds |
| [`protocol/`](protocol/README.md) | a connection lifecycle with retransmission and a timeout | the generator on **time** — a clock the format cannot hold, a refinement pair, a sink state, exit/action/entry composition, and the four limits a real design runs into |
| [`inspector/`](inspector/README.md) | `fsmtable-inspect`: summarize a `.fsm`, report unreachable states, sinks and partially covered pairs, write the canonical form back | the **library without the generator** — `parse`, `dump` and the analyses, wrapped in a tool with an exit code you could put in a hook |

## Each one stands alone

There is no shared example library. `calculator.cpp`, `protocol.cpp` and `inspector.cpp` each carry
their own small driver, and the two that build a machine each have their own header. That is a
deliberate trade: a few hundred duplicated lines in exchange for being able to read one directory
and understand all of it, without following a third file that exists only to be shared.

## The recipe all three follow

1. **Write the machine as text** — `<name>.fsm`. This is the only file that decides anything.
2. **Write down who owns what**: what the table decides, and what has to live beside it (a clock, a
   stack of operands, a socket, a retry count). The format cannot hold those, and pretending
   otherwise is the most common way to end up fighting it.
3. **Generate the header** with one `add_custom_command` in `CMakeLists.txt`. Never edit the
   generated file, and never check it in.
4. **Write the driver** — the actions the generated header declared, plus whatever feeds events in.
   A misspelt action name is a linker error, so the machine and the driver cannot drift apart
   quietly.
5. **Write the tests**: gtest on the driver (states reached, actions run, in order), and one
   end-to-end case that pipes a recorded script through the built program and diffs the output
   against a recorded trace.

## Ideas worth a weekend

Machines that are more fun than a calculator, roughly in order of how much they teach:

- **A vending machine.** Money arrives as an event value, "do I have enough?" is a guard, and
  dispensing is an entry action. Halfway through you meet the format's real boundary: the machine
  cannot add, so the credit lives in the driver and the machine only ever compares one number
  against one number.
- **An elevator.** Doors opening and closing are entry and exit actions, travel time is a value on a
  tick, and the request queue is the driver's. The state space is small and the edge cases are not.
- **A traffic light with a pedestrian button.** The corpus already has a three-state light; add a
  minimum-green rule and a button that is only honoured in some states, and the guard/refinement
  pair stops being an exercise.
- **An order lifecycle** — cart, paid, shipped, delivered, cancelled, refunded. This is the shape
  most backend work actually has, and it is a good way to find out how much of it is a table and how
  much of it is a database.
- **A lexer.** Character classes are event kinds, one state per lexical context, and a value can
  carry the character that did not fit. Then feed the tokens to a second machine that is the parser:
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

A new example needs its own `README.md` in the shape of the two here: the three layers and who owns
what, the machine's text read as sentences, the decisions that look surprising in hindsight, the
tests, and a verified "build your own from this". Wire it in with the four pieces `CMakeLists.txt`
uses per example: the `add_custom_command` that runs `fsmtable-gen`, the library, the executable, and
the test target — plus its own `protocol_session`-style ctest case if it has a recorded trace.
