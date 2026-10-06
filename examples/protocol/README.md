# A connection lifecycle, with timeouts

`protocol.fsm` is this repository's second worked example, and the harder of the two. The calculator
shows the format on arithmetic; this one shows it on **time** — the thing a state machine can never
hold by itself, because a table has no clock.

```
   open              syn_ack                 fin                  tick >= 2000
Closed --> SynSent ----------> Established ------> TimeWait -------------------> Closed
             |  |
             |  +-- tick >= 1000 --> retransmit (stays in SynSent)
             +----- reset, expire --> Refused   (a sink: nothing leaves it)
```

`Refused` is where the story ends for this connection. A caller that wants to try again opens a new
one, which is why nothing leaves it — and why `fsmtable-inspect` reports it as a sink and still
exits 0 (`examples/inspector/README.md`).

## The three layers

The same three as the calculator, and the same rule about who may edit what:

| what | where | who owns it |
| --- | --- | --- |
| the table | `protocol.fsm` | **you.** The only file that decides anything. |
| the machine | `build/generated/fsm_protocol.hpp` | **the generator.** Never edit it; it is rebuilt on every build. |
| the driver | `protocol.cpp`, `main.cpp` | **you.** The clock, the retry count, and everything that is not a decision about legality. |

`protocol.hpp` is the seam between the last two: the driver's whole interface to the machine.

## Who owns what, in this machine's own terms

This is the part worth reading twice, because it is where a real connection and a table part company.

**The machine decides** what is legal in each state, what a tick means once a timeout has passed,
that a reset while opening is a refusal, and that `Refused` is the end.

**The driver owns** everything the format cannot hold:

- **the clock.** `tick 1200` means "1200 milliseconds have passed" — a value the *driver* chose. The
  machine compares it against one threshold (`when ge 1000`) and nothing else. A real driver tracks
  how long it has been waiting; in this example the script says it.
- **the retry budget.** No row can hold a counter and no guard can compare two values, so "the
  retries are exhausted" cannot be a row. It is the driver's judgement, delivered as its own event:
  `expire` is a **kind**, not a second threshold. `Session::retransmits()` is the driver counting
  what the machine did, which is the information such a decision needs.
- **the socket.** In a real program the directives come off a file descriptor instead of stdin;
  `main.cpp`'s `getline` loop is the only thing that changes.

## The contract, in four lines

```cpp
bool feed(std::string_view directive, Step& step, std::string& error);
```

- `feed` returns **false** when the *directive* could not be read (`data twelve`, `tick`, `open 3`,
  `frobnicate`). That is the driver's mistake, and the machine has not been asked anything yet.
- `feed` returns **true** and `step.matched == false` when the machine has no row for it in the
  current state — `data 64` in `Closed`. That is the machine answering: *not in my language here*.
  The state is unchanged and no action ran.
- `step.actions` is what ran, in the order the format ran it: the source state's `exit`, then the
  row's own action, then the target state's `entry`.
- `step.from` and `step.to` are the states either side of the decision, so a caller can log a
  transition without asking the machine twice.

`./build/protocol` reports all of that as a trace and exits **1** if any directive in the script went
unanswered — the exit code is the part a script would use.

## Reading `protocol.fsm` as sentences

```
transition Closed --open--> SynSent action on_open
```
*In `Closed`, an `open` goes to `SynSent` and runs `on_open`.* One line, one sentence.

```
transition SynSent --tick--> SynSent when ge 1000 action retransmit
transition SynSent --tick--> SynSent
```
*In `SynSent`, a `tick` of at least 1000 retransmits; any other tick stays put and does nothing.*
This is a **refinement pair** (the frozen rules allow exactly one guarded row and one unguarded row
per `from`+`kind`), and the canonical order puts the guarded row first because the back end is
first-match-wins. Read together, they say: every tick in `SynSent` is a decision the table makes,
rather than a gap the driver has to fill. The guard is inclusive — `tick 1000` retransmits, and
`tick 999` does not; both are tested.

```
state SynSent entry arm_timer exit cancel_timer
state Refused entry on_refused
```
*Whenever `SynSent` is entered, arm the timer; whenever it is left, cancel it. Whenever `Refused` is
entered, run `on_refused`.* A row that fires from `A` to `B` therefore runs three things in order —
`A`'s exit, the row's action, `B`'s entry. `reset` in `SynSent` is the clearest one to watch:

```
cancel_timer, on_refused_while_opening, on_refused
```

## The recorded trace

`input/session.txt` is the script; `input/session.expected` is its recorded output, compared line for
line by the `protocol_session` test. The whole run:

```
open                  Closed -> SynSent   on_open, arm_timer
tick 400              SynSent -> SynSent   cancel_timer, arm_timer
tick 1200             SynSent -> SynSent   cancel_timer, retransmit, arm_timer
syn_ack               SynSent -> Established   cancel_timer, on_syn_ack
data 64               Established -> Established   on_data
tick 1000             Established -> Established   (nothing ran)
fin                   Established -> TimeWait   on_peer_fin, arm_time_wait
tick 1999             TimeWait -> TimeWait   arm_time_wait
tick 2000             TimeWait -> Closed   on_time_wait_over
open                  Closed -> SynSent   on_open, arm_timer
data 512              ! SynSent is not a state this directive means anything in
reset                 SynSent -> Refused   cancel_timer, on_refused_while_opening, on_refused
expire                ! Refused is not a state this directive means anything in
```

Four things to notice in it:

- `tick 400` and `tick 1200` are the two halves of the refinement pair, side by side.
- `tick 1000` in `Established` ran **nothing**: a tick while a connection is up is answered, and the
  answer is "carry on".
- The two `!` lines are the machine's language boundary, and they are what make the run exit 1.
- `data 512` in `SynSent` is a protocol violation; `expire` in `Refused` is a caller that has not
  understood the end of the story. The driver reports both the same way, because the table does not
  explain a refusal — it only refuses.

## What this machine cannot do, and where the work goes instead

Four limits, all of them visible in the file and in the trace, and all of them things a real protocol
implementation has to decide consciously:

1. **One guard per `from`+`kind`.** Two thresholds on one kind is not expressible, so `expire` is a
   kind. The frozen rule is what keeps the canonical order a total precedence order.
2. **No arithmetic.** A value can be compared with one constant, not with another value, and nothing
   can be accumulated. The clock and the retry budget are the driver's, by construction.
3. **A self-transition is an external one** (`Q13`). A tick that stays in `SynSent` still *leaves*
   `SynSent` and *enters* it again, so its exit and entry clauses run: that is why `tick 400` above
   shows `cancel_timer, arm_timer` around a row that has no action of its own.
4. **There is no internal transition.** So a state that takes self-transitions cannot carry an entry
   clause without it re-running on every one of them. `Established` therefore has no entry clause;
   the connection's setup rides on `on_syn_ack`, the row that gets there.

Points 3 and 4 are the same limit seen from two sides, and they give the rule to follow in a driver:
**a row fires once per event, a state's clauses run on every entry and every exit.** If a clause
means "start a timer" or "send a packet", put it in the row's action. If it means "this state is
where we are now", a clause is the right place — which is what the two in `protocol.fsm` are
announcing.

## The tests

- `protocol_test.cpp` — twelve cases: the exit/action/entry order, both halves of the refinement
  pair, the guard at its boundary (999 and 1000), giving up, a reset while opening, `Refused` as an
  end, a violation in `Closed`, a tick in `Established`, the `fin`/`TimeWait`/`Closed` path, data
  arriving in `TimeWait`, and the six ways a directive can be unreadable.
- `protocol_session` (ctest) — the script above through the built binary, diffed against the
  recorded trace, which also pins the exit code 1.
- `fsmtable-inspect examples/protocol/protocol.fsm` — exits 0, reporting one sink and no unreachable
  state. It is a ctest case too.

## Build your own from this

1. Copy this directory next to wherever you are working, or write your own `.fsm` from the format
   described in `SPEC.md` section 2.
2. Write down the states as sentences: which situations exist, and what ends each one.
3. Write the rows as sentences: *in `<state>`, `<event>` means `<next state>` and `<work>`.*
4. Write the driver: what does it own (a clock? a socket? a counter?), and what does it do when
   `process` returns false?
5. Wire it into CMake the way `CMakeLists.txt` does for this example: one `add_custom_command` that
   runs `fsmtable-gen`, one library, one executable, one test binary.

A worked adaptation — a half-open probe, where a timed-out connection gets one more chance before it
is refused:

```
kind probe = 8
state HalfOpen entry arm_probe

transition SynSent --probe--> HalfOpen action on_probe
transition HalfOpen --syn_ack--> Established action on_syn_ack
transition HalfOpen --expire--> Refused action on_give_up
transition HalfOpen --tick--> HalfOpen
```

Verified against the real generator, not by eye: `fsmtable-gen` accepts it at **6 states and 16 rows**,
and generates `SynSent_leaving_to_HalfOpen` and `HalfOpen_leaving_to_HalfOpen` because `SynSent` has
an exit clause and `HalfOpen` an entry clause — the wrapper exists exactly where there is something to
compose. `fsmtable-inspect` still exits 0.

`tools/check-doc-claims.sh` re-derives that on every gate run: it appends the block above to the real
`protocol.fsm`, requires the generator to accept it, and requires the two names it read out of this
sentence to appear in the header it wrote.

## Where to read next

- `../../README.md` — the front page, and the index of everything else.
- `../../SPEC.md` — the frozen format and the rules the generator enforces.
- `../../GENERATOR.md` — what `fsmtable-gen` emits and why the table is a literal array.
- `../../ENTRY_EXIT.md` — the `state` clause this example leans on, and `Q13`/`Q14`.
- `../../NAMED_KINDS.md` — the `kind` declarations both examples use.
- `../calculator/README.md` — the other worked example: same three layers, arithmetic instead of time.
- `../inspector/README.md` — the library without the generator, and what a sink means.
