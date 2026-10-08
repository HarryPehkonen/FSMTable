# fsmtable-inspect — the library half, with no generator

Every other example in this repository feeds `fsmtable-gen`. This one uses `fsmtable` itself, the way
a tool that reads `.fsm` files would: it takes a file at *run* time, parses it, reports what the
analyses see, and can write the canonical form back out. Nothing is generated, and there is no
machine compiled into it.

It is also the only place in the repository where the analyses of `SPEC.md` section 8
(`unreachable`, `sink_states`) are used by something other than their own tests, which makes it the
smallest complete answer to "what else can I build on this library?" — about five hundred lines,
including the usage text and the trace.

## What it reports

```
fsmtable-inspect <file.fsm>                      summarize it, and report findings
fsmtable-inspect --canonical <file.fsm>          write the canonical form to stdout
fsmtable-inspect --trace <file.fsm> <event>...   walk the table over those events
fsmtable-inspect --help
```

- a one-line summary: the machine's name, its state and row counts, how many kinds it named;
- its initial state;
- **sink** states — nothing leaves them. Reported, and *not* a failure;
- **partial** pairs — a row carrying a `when` clause with no unguarded row behind it for the same
  state and kind, so a false guard leaves the event unhandled. Reported, and *not* a failure;
- **unreachable** states — no path from the initial state. Always a failure;
- with `--canonical`, the canonical text instead of the summary: the same machine the parser read,
  with the kinds and states declared above the frozen row block, in the canonical order the format
  defines;
- with `--trace <file.fsm> <event>...`, the path instead of the summary: one line per event, from
  the state the machine was in to the state the row it matched leads to, with the entry and exit
  clauses that run on the way. The walk stops at the first event the table has no row for, names it,
  and exits 1.

Exit codes, the same convention as `fsmtable-gen`: **0** nothing to report, **1** the file could not
be read, a state is unreachable, or a traced event matched no row, **2** the command line is wrong.
That makes it usable from a script or a hook without parsing its output.

## Real output, on this repository's own machines

```
$ ./build/fsmtable-inspect examples/protocol/protocol.fsm
examples/protocol/protocol.fsm: Connection — 5 state(s), 12 row(s), 7 named kind(s)
  initial:  Closed
  sinks:    Refused  (nothing leaves them; often deliberate)
  every state is reachable from Closed

$ ./build/fsmtable-inspect examples/inspector/dirty.fsm
examples/inspector/dirty.fsm: Dirty — 4 state(s), 4 row(s), 3 named kind(s)
  initial:  Start
  sinks:    Done  (nothing leaves them; often deliberate)
  partial:  Running --bump-->  (a guarded row with no unguarded row: a false guard leaves the event)
  UNREACHABLE: Orphan  (no path from Start)

$ ./build/fsmtable-inspect --trace examples/calculator/calculator.fsm number add number equals
examples/calculator/calculator.fsm: Calculator — trace of 4 event(s) from Entry
  Entry --number--> Accum  (action take_operand)
  Accum --add--> PendingAdd
  PendingAdd --number--> Accum  (action add_operand)
  Accum --equals--> Accum

$ ./build/fsmtable-inspect --trace examples/protocol/protocol.fsm open syn_ack fin tick=2500
examples/protocol/protocol.fsm: Connection — trace of 4 event(s) from Closed
  Closed --open--> SynSent  (action on_open, entry arm_timer)
  SynSent --syn_ack--> Established  (exit cancel_timer, action on_syn_ack)
  Established --fin--> TimeWait  (action on_peer_fin, entry arm_time_wait)
  TimeWait --tick--> Closed  (action on_time_wait_over)

$ ./build/fsmtable-inspect --trace examples/inspector/dirty.fsm step bump=5
examples/inspector/dirty.fsm: Dirty — trace of 2 event(s) from Start
  Start --step--> Running
  trace stopped at event 2: Running --bump--> (value 5) matched no row
  the table's row for that pair refused it — examples/inspector/dirty.fsm:23: transition Running --bump--> Running when lt 3 action note_bump
```

`dirty.fsm` is in this directory precisely so that output has an example: `Orphan` is never a
destination, `Done` has no outgoing row, and `Running --bump-->` is guarded with nothing behind the
guard. Each is one line away from being correct — an incoming row, an outgoing row, an unguarded
row — which is the point.

The block above is compared line for line by `tools/check-doc-claims.sh` on every gate run: lines
beginning `$ ` are the commands it runs, and every other line is what those commands must print.

## The trace, and what it does not simulate

`--trace` is the one thing this tool does that *runs* the table, so the boundary is worth being
precise about — it is the format's own boundary (`examples/protocol/README.md`). An event is a kind,
optionally with the value that rides on it:

```sh
./build/fsmtable-inspect --trace examples/protocol/protocol.fsm open syn_ack fin tick=2500
```

The list is **flat**: whitespace separates the events, `=` is the only punctuation, and each kind is
read the way a row spells it — a declared kind name, or a number `0`..`255` (`NAMED_KINDS.md`). The
value after `=` is the number a `when` clause is compared against, and an event with no `=` carries
`0`: the tool never invents a value, the caller hands over the one the guard should be tested
against. That is the whole event language. It is not a driver — no clock, no state beyond the
machine's current state, no action, no way to say "the action did X". A machine whose behaviour
depends on what the driver remembers (a retry count, a socket, the number a calculator is holding)
traces here as the sequence of states the *table* allows, which is all a `.fsm` file can say.

Each line names the state the machine was in, the kind that arrived, and the state the matched row
leads to, with the clauses `ENTRY_EXIT.md` composes on the way — the source state's `exit`, then the
row's own `action`, then the target state's `entry`:

```
  Closed --open--> SynSent  (action on_open, entry arm_timer)
  SynSent --syn_ack--> Established  (exit cancel_timer, action on_syn_ack)
```

Two rows can answer one (state, kind): the guarded one first, the unguarded one behind it as the
fallback (rule 9 — and the canonical order is the precedence rule, so a file written the other way
round still behaves as the format says). The guard is tested against the event's own value; when it
is false the fallback fires, and when there is no fallback the event matches no row at all. The walk
then stops at that event and the report names the row that refused it, with the line the file wrote
it on:

```
  trace stopped at event 2: Running --bump--> (value 5) matched no row
  the table's row for that pair refused it — examples/inspector/dirty.fsm:23: transition Running --bump--> Running when lt 3 action note_bump
```

That is the partial pair this tool has always reported, with the event that hit it named. The
initial state's own `entry` clause is never run, for the same reason the generated factory does not
run it: the machine is being created, not entered (`ENTRY_EXIT.md`).

## Why a sink is not a failure and an unreachable state is

A sink is how a machine says "the story ends here" — a refused connection, a terminal error, a
finished order. Half the machines anyone writes have one, and it is deliberate in almost all of
them. A machine whose sink is *not* deliberate is a machine that quietly stops answering, and the
trace of a real run says that much more clearly than a linter can.

An unreachable state is the opposite: it is a state you wrote, named, and can never be in. It cannot
be deliberate — there is no path to it, so nothing it contains can ever happen. This is why the tool
reports the first and fails on the second.

A partial pair is the third case, and it is not a failure either. Rule 9 lets one row per state and
kind carry a `when` clause, puts that row first, and leaves any unguarded row behind it as the
fallback; a pair with no fallback is a machine saying "this event may go unhandled here" — and the
back end says so, since `process()` reports it rather than pretending a row matched. That is a real
feature ("ignore a tick unless the budget allows") and also how a machine quietly stops answering,
and the difference between the two is exactly what a report can draw. The protocol example takes the
other route for its own rejections: every pair there has a fallback row, so nothing is ever
unhandled, and its driver turns the events it does receive into an exit code. A machine that omits
the fallback shows up in this report instead.

## The library calls behind it

```
fsmtable::parse(text, error, kind_names, state_actions)   the four-argument reading, so a machine's
                                                          names and entry/exit clauses survive
fsmtable::dump(machine, kind_names, state_actions)        the matching writer
fsmtable::unreachable(machine)                            SPEC.md section 8
fsmtable::sink_states(machine)                            SPEC.md section 8
fsmtable::partial_pairs(machine)                          the addition beside them: a guarded row
                                                          with no unguarded partner
```

The narrow readers — two-argument `parse`, one- and two-argument `dump` — are for a caller that does
not care about the side tables: a machine that declares no kinds and decorates no states parses to
the same `Machine` either way, which is what makes the two additions additive rather than a fork of
the frozen block. `SPEC.md` section 4 holds the frozen signatures; `NAMED_KINDS.md` and
`ENTRY_EXIT.md` hold the two additions, and each says which reader a caller needs. `partial_pairs`
is a third addition of that kind: it adds a name beside section 8's two rather than changing either,
which is why the two analyses the spec names still have the signatures it fixed.

## How to adapt it

**Use it as a check over a tree.** The repository's own `.fsm` files, minus the fixtures that are
deliberately invalid:

```sh
for f in $(git ls-files 'corpus/*.fsm' 'examples/*/*.fsm' |
           grep -v -e 'corpus/invalid_' -e 'examples/inspector/dirty.fsm'); do
    ./build/fsmtable-inspect "$f" || exit 1
done
```

Three exclusions, each for its own reason. `tests/fixtures/` is left out because those files exist to
be rejected — `tests/fixtures/keyword_state.fsm` is one. `corpus/invalid_*.fsm` is left out for the
same reason: the corpus carries deliberately broken input for the parser's own tests, and two of them
(`invalid_junk.fsm`, `invalid_ambiguous.fsm`) are exactly the kind of file this tool is supposed to
complain about. And `examples/inspector/dirty.fsm` is left out because it is in *this* directory
precisely so that being reported is its job.

**Writing the canonical form back needs a temporary file.** The tool reads its input before it writes
anything, but the shell truncates on `>` before the process starts, so

```sh
./build/fsmtable-inspect --canonical f.fsm > f.new && mv f.new f.fsm
```

is the safe shape. `./build/fsmtable-inspect --canonical f.fsm > f.fsm` empties `f.fsm` and then
reports that it cannot parse it — which is a shell trap rather than a bug, and the kind of thing
worth knowing before pointing this at a file you care about.

**Both shell recipes above are checked too.** `tools/check-doc-claims.sh` runs the tree check over
`corpus/` and `examples/`, and it runs the canonical-form recipe — including the trap. A documented
footgun is still a claim about behaviour, and the way to know it is still there is to test for it.

**Add the check the library does not have.** The one this format can actually have is the other half
of rule 9: **a guarded row with no unguarded partner** — a machine that answers that kind only above
(or below) some value and silently ignores the rest, a partial row. That is legal, sometimes
deliberate, and it is exactly the shape that made `expire` a separate kind in `examples/protocol/`.

A *dead guard* cannot happen here, which is worth knowing before proposing that check instead: within
one `from`+`kind` the canonical order puts the guarded row first, so its guard is always reachable —
the ordering rule is what buys that. The shape to follow for a new analysis is `SPEC.md` section 8's:
a free function taking `const Machine&`, returning `std::vector<std::string>` or a small struct, no
mutation and no failure mode. `tests/analysis_test.cpp` is where its tests would go.