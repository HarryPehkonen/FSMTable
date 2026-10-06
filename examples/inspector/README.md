# fsmtable-inspect — the library half, with no generator

Every other example in this repository feeds `fsmtable-gen`. This one uses `fsmtable` itself, the way
a tool that reads `.fsm` files would: it takes a file at *run* time, parses it, reports what the
analyses see, and can write the canonical form back out. Nothing is generated, and there is no
machine compiled into it.

It is also the only place in the repository where the two analyses of `SPEC.md` section 8
(`unreachable`, `sink_states`) are used by something other than their own tests, which makes it the
smallest complete answer to "what else can I build on this library?" — about a hundred and fifty
lines, including the usage text.

## What it reports

```
fsmtable-inspect <file.fsm>              summarize it, and report findings
fsmtable-inspect --canonical <file.fsm>  write the canonical form to stdout
fsmtable-inspect --help
```

- a one-line summary: the machine's name, its state and row counts, how many kinds it named;
- its initial state;
- **sink** states — nothing leaves them. Reported, and *not* a failure;
- **unreachable** states — no path from the initial state. Always a failure;
- with `--canonical`, the canonical text instead of the summary: the same machine the parser read,
  with the kinds and states declared above the frozen row block, in the canonical order the format
  defines.

Exit codes, the same convention as `fsmtable-gen`: **0** nothing to report, **1** the file could not
be read or a state is unreachable, **2** the command line is wrong. That makes it usable from a
script or a hook without parsing its output.

## Real output, on this repository's own machines

```
$ ./build/fsmtable-inspect examples/protocol/protocol.fsm
examples/protocol/protocol.fsm: Connection — 5 state(s), 12 row(s), 7 named kind(s)
  initial:  Closed
  sinks:    Refused  (nothing leaves them; often deliberate)
  every state is reachable from Closed

$ ./build/fsmtable-inspect examples/inspector/dirty.fsm
examples/inspector/dirty.fsm: Dirty — 4 state(s), 3 row(s), 2 named kind(s)
  initial:  Start
  sinks:    Done  (nothing leaves them; often deliberate)
  UNREACHABLE: Orphan  (no path from Start)
```

`dirty.fsm` is in this directory precisely so that output has an example: `Orphan` is never a
destination, and `Done` has no outgoing row. Both are one line away from being correct.

## Why a sink is not a failure and an unreachable state is

A sink is how a machine says "the story ends here" — a refused connection, a terminal error, a
finished order. Half the machines anyone writes have one, and it is deliberate in almost all of
them. A machine whose sink is *not* deliberate is a machine that quietly stops answering, and the
trace of a real run says that much more clearly than a linter can.

An unreachable state is the opposite: it is a state you wrote, named, and can never be in. It cannot
be deliberate — there is no path to it, so nothing it contains can ever happen. This is why the tool
reports the first and fails on the second.

## The library calls behind it

```
fsmtable::parse(text, error, kind_names, state_actions)   the four-argument reading, so a machine's
                                                          names and entry/exit clauses survive
fsmtable::dump(machine, kind_names, state_actions)        the matching writer
fsmtable::unreachable(machine)                            SPEC.md section 8
fsmtable::sink_states(machine)                            SPEC.md section 8
```

The narrow readers — two-argument `parse`, one- and two-argument `dump` — are for a caller that does
not care about the side tables: a machine that declares no kinds and decorates no states parses to
the same `Machine` either way, which is what makes the two additions additive rather than a fork of
the frozen block. `SPEC.md` section 4 holds the frozen signatures; `NAMED_KINDS.md` and
`ENTRY_EXIT.md` hold the two additions, and each says which reader a caller needs.

## How to adapt it

**Use it as a check over a tree.** The repository's own `.fsm` files, minus the fixtures that are
deliberately invalid:

```sh
for f in $(git ls-files 'corpus/*.fsm' 'examples/*/*.fsm'); do
    ./build/fsmtable-inspect "$f" || exit 1
done
```

`tests/fixtures/` is left out on purpose: those files exist to be rejected, and
`tests/fixtures/keyword_state.fsm` is one of them.

**Writing the canonical form back needs a temporary file.** The tool reads its input before it writes
anything, but the shell truncates on `>` before the process starts, so

```sh
./build/fsmtable-inspect --canonical f.fsm > f.new && mv f.new f.fsm
```

is the safe shape. `./build/fsmtable-inspect --canonical f.fsm > f.fsm` empties `f.fsm` and then
reports that it cannot parse it — which is a shell trap rather than a bug, and the kind of thing
worth knowing before pointing this at a file you care about.

**Add the check the library does not have.** The obvious next one is a row that can never fire — a
guarded row whose unguarded partner above it in the canonical order already matches every value, so
the guard is dead code. The shape to follow is `SPEC.md` section 8's: a free function taking
`const Machine&`, returning `std::vector<std::string>` or a small struct, no mutation and no
failure mode. `tests/analysis_test.cpp` is where its tests would go, and it would get a line in `REPORT.md` beside the other decisions this format has taken.