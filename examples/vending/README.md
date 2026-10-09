# vending — two half-machines, composed on one shared name

A vending machine written as two small machines and composed into one. `coins.fsm` decides when
there is enough money; `dispenser.fsm` decides whether a vend may happen; `vending.fsm` is the two
of them as one machine, produced by `fsmtable-transform merge` and committed with the provenance
header that merge wrote. This is the worked example for the merge verb — and the one example here
that generates no code at all, which is part of what it shows.

Read `vending.fsm` first: it is the machine. The two halves are what it was made from, and why they
are two files rather than one is the next section.

## Why two machines, and not one

Payment and stock change for different reasons. A new price, a coin the slot will not take, a
loyalty card that pays — that is the payment half's argument. A second shelf, a hopper that runs
dry, a restock — that is the stock half's. One file would hold both arguments and neither would be
the whole story of the file.

The composition is what makes the split worth having rather than a fashion: the two halves meet at
exactly one name, and that name is the contract between them.

## coins.fsm — the payment half

    machine Coins
    initial Waiting

    kind credit = 1
    kind refund = 2

    transition Waiting --credit--> Waiting when lt 150
    transition Waiting --credit--> Credited
    transition Credited --refund--> Waiting

Read it as sentences. "A credit arrives: below 150 the machine waits for more money, and at 150 or
above it is `Credited`." "`refund` returns `Credited` to `Waiting`, and it is a person's decision
rather than the table's."

Two things are worth noticing:

* **A guarded row and the row behind it are a pair** (SPEC.md rule 9). The guarded row is tried
  first, so a credit below the price takes it and the machine stays where it is; the unguarded row
  is the fallback, and everything that is not "below 150" reaches `Credited`. The canonical order
  *is* the precedence rule, which is why the union further down holds them in that order whichever
  file wrote them first.
* **`credit` carries the running total, not the coin.** That is the format's boundary rather than a
  naming taste: a row has one `when` slot and the format cannot add two events, so the driver — the
  coin slot's own counter — keeps the sum and hands the machine the number the guard compares. The
  machine never sees the coins; it sees what the driver says they add up to.

## dispenser.fsm — the stock half

    machine Dispenser
    initial Credited

    kind dispense = 3
    kind served = 4
    kind empty = 5

    transition Credited --dispense--> Dispensing
    transition Dispensing --served--> Credited
    transition Dispensing --empty--> SoldOut

"A vend takes the machine out of service until the goods are out; `served` puts it back, and `empty`
ends the story in `SoldOut`."

**`SoldOut` is a sink, and it is deliberate.** Nothing leaves it, and the inspector reports that
without failing over it — the difference between a sink and an unreachable state is the whole of
that tool's judgement call (`examples/inspector/README.md`). A machine whose sink is *not*
deliberate quietly stops answering, and a trace of a real run says so far more clearly than a
report can.

## The seam

The two files share exactly one state name — `Credited` — and that is not a shared vocabulary, it
is the composition. A merge fuses two machines on the state names they have in common and inspects
nothing about what either of them meant by the name. `Credited` is where "I have the money" (the
payment half) becomes "you may go" (the stock half).

Everything the merge does follows from that one rule:

* The stock half's `initial` is `Credited`, a name the payment half already has, so the union holds
  one `Credited` and the stock half's rows are armed exactly when the payment half is satisfied.
  Begun in a name of its own, the union would have held a state nothing leads into, and the merge
  would have refused it — naming the states, writing no file — rather than hand back a machine with
  a dead branch (`TRANSFORM.md` shows that refusal at its smallest).
* The union's `initial` is the first parent's, `Waiting`, so the payment half has to be the first
  file on the command line. The second machine's initial becomes an ordinary state, which here
  costs nothing because it *is* the seam. Where it is not, its former initial-entry clause is dropped
  and the drop is reported (`TRANSFORM.md`).
* The two files may not disagree about a kind's number, and these two do not: `credit` and `refund`
  are 1 and 2, and the stock half's kinds start at 3.

**Why the two files were written already aligned instead of one being renamed into place.**
`fsmtable-transform rename` is the tool for a name that has to move, and it is what to reach for
when two machines have grown apart. It was not used to build this artifact, for two reasons worth
knowing before reaching for it: a rename writes its result to stdout only, so an aligned
intermediate would have to be a file somebody commits; and the header at the top of `vending.fsm`
records the exact command that reproduces the file, which would then be a command that no longer
runs against the two committed halves. A name that *is* the contract is worth writing once, in both
files, with the meaning spelled out beside it in a comment.

## The union, as the tool writes it

`fsmtable-transform merge` writes the union to stdout, header and all. This is that command, and
exactly what it prints — `vending.fsm` in this directory is those bytes, and the gate re-runs the
command and diffs the committed file against it, so the artifact cannot drift from the two halves:

```
$ ./build/fsmtable-transform merge examples/vending/coins.fsm examples/vending/dispenser.fsm
# Merged by fsmtable-transform from examples/vending/coins.fsm (machine Coins)
# and examples/vending/dispenser.fsm (machine Dispenser), fused on the state names the two
# machines share. Re-merge with: fsmtable-transform merge examples/vending/coins.fsm examples/vending/dispenser.fsm -o <this file>
# Fingerprint of the canonical form (FNV-1a 64): 0xABCC1A2DF89CE23D
version 1
machine Coins
initial Waiting
kind credit = 1
kind refund = 2
kind dispense = 3
kind served = 4
kind empty = 5
transition Credited --refund--> Waiting
transition Credited --dispense--> Dispensing
transition Dispensing --served--> Credited
transition Dispensing --empty--> SoldOut
transition Waiting --credit--> Waiting when lt 150
transition Waiting --credit--> Credited
```

Four things about that output are the merge verb itself:

* **The header is the file's provenance**: the two parents with the machine name each had, the
  exact command that composes them again, and the fingerprint of the canonical form — the same
  value `fsmtable-gen` stamps into a header generated from the same machine, so a merged machine and
  an artifact made from it can be compared without diffing text.
* **The machine is introduced as `Coins`**, the first parent's name, inside a file called
  `vending.fsm`. A merge is not symmetric and it does not invent an identity; the section below has
  the one-command fix.
* **The two halves' comments are gone.** `rename` keeps a file's bytes byte for byte; `merge` writes
  a machine no reader has yet, so what survives is the header rather than the argument. That is what
  the comments in the two halves are for — and why the header names the files to go back to.
* **The rows are in canonical order** (by `from`, then kind, guarded row first), which is why they
  appear in neither parent's order. The table is the same either way; this is the form the writer
  emits, and it is what makes two files that say the same thing hash to one fingerprint.

## The composed walk

`fsmtable-inspect --trace` walks the table over a flat event list. It is not a driver — no clock,
no registers, no actions — which makes it the honest way to show what the union *is*: the coins'
worth of credit, and then a vend.

```
$ ./build/fsmtable-inspect --trace examples/vending/vending.fsm credit=100 credit=150 dispense served
examples/vending/vending.fsm: Coins — trace of 4 event(s) from Waiting
  Waiting --credit--> Waiting
  Waiting --credit--> Credited
  Credited --dispense--> Dispensing
  Dispensing --served--> Credited
```

The first line is the guard refusing a credit below the price; the second crosses the seam, which is
the event that would have meant nothing to either half alone; the last two are the stock half's
states. The value after `=` is the number a `when` clause is compared against, so `credit=100` is
the driver saying "the customer has 100 in", not a coin of 100 — and the tool never invents a value
(`examples/inspector/README.md`).

## What the inspector reports

```
$ ./build/fsmtable-inspect examples/vending/vending.fsm
examples/vending/vending.fsm: Coins — 4 state(s), 6 row(s), 5 named kind(s)
  initial:  Waiting
  sinks:    SoldOut  (nothing leaves them; often deliberate)
  every state is reachable from Waiting
```

One sink, named, with the line that says a sink is usually on purpose — and no partial pair, because
`Waiting`'s two rows are a pair rather than a lone guard. Every state is reachable, which the merge
had already insisted on before it wrote the file.

## The three layers, and who owns what

    coins.fsm, dispenser.fsm   what is legal and what it means, one argument each. The authority.
    vending.fsm                the union of the two, with the header the merge wrote
    (no generated header)      nothing here is generated; the section after this one says why
    the driver                 the coin slot's sum, the stock count, the clock, and the printer
    fsmtable-inspect           the table walked without a driver: --trace, --graph, --canonical
    fsmtable-transform         the two halves composed, and the union's identity moved

The machine owns the shape of what may happen next. Everything a table cannot hold is the driver's,
and naming those things is the point rather than an apology:

    the credit        a sum, and the format cannot add: the driver keeps it and sends it as `credit`
    the stock count   a count, and the format has no counter: `empty` is the driver's judgement
    the price         150, a constant, so it lives in the guard that compares against it
    the actions       names the format only names — `on_dispense` would be a function the caller
                      supplies, and none is defined here because there is no driver here

**Why this example generates no code.** The others in this directory feed `fsmtable-gen` because each
has a driver — a program whose actions the generated header declares, so that a misspelt action name
is a link error rather than a silent no-op. This example has no driver: the sum, the stock count and
the clock are the parts it is *about*, and they are exactly the parts a driver would hold. Generating
a header would mean writing a driver to justify it, and the driver would then be the interesting
file. So the wiring here is what is left: the two tools that read a `.fsm` at run time, the ctest
cases and the doc-claims rule under "Tests", and this document.

## The two refusals

Two commands that fail, because they are the boundary of what this machine can say:

```
$ ./build/fsmtable-inspect corpus/invalid_ambiguous.fsm
corpus/invalid_ambiguous.fsm:5: ambiguous row: a second unguarded row for this from and kind
$ ./build/fsmtable-inspect --trace examples/vending/vending.fsm dispense
examples/vending/vending.fsm: Coins — trace of 1 event(s) from Waiting
  trace stopped at event 1: Waiting --dispense--> (value 0) matched no row
  the table has no row for Waiting --dispense-->
```

The first is the format refusing a second row for one state and kind: rule 9 gives a pair one `when`
slot and no more, and that second slot is exactly what keeping the running total in the table would
need — "a coin of 100 *and* a coin of 50". The second is the composition refusing to vend before it
has been paid: `Waiting` is the payment half's state, the stock half arms `dispense` on `Credited`,
and the union has no row for the pair. The walk stops and names the pair rather than inventing a
transition, which is what makes a trace of the union a walk over what the two halves agreed to.

## Decisions that look surprising in hindsight

* **The union is called `Coins`.** The first parent's name wins, so `vending.fsm` introduces itself
  as `Coins` and `Dispenser` survives only in the header. That is the merge refusing to invent an
  identity: a composed machine is the first parent extended, not a third thing with a name of its
  own.
* **A merge keeps no comments.** Both halves are commented and the union has none of it: `merge`
  writes through the canonical writer, so a merged file carries a provenance header instead of the
  two arguments. The arguments stay in the halves, which is where a reader who wants the reasoning
  goes — and the header says which two files those are.
* **A vend returns the machine to `Credited`, credit and all.** The credit is the driver's register,
  because the format cannot hold a sum, so clearing it is the driver's job as well: `served` is the
  stock half's event and the stock half knows nothing about money. A reader looking for "and now
  the money is spent" will not find it in the table, and that absence is the boundary this example
  exists to draw.
* **Nothing was dropped in this merge, which is worth noticing.** The one thing a merge drops is the
  second machine's former initial-entry clause, and neither half here decorates a state at all. Add
  an `entry` clause to `dispenser.fsm`'s `Credited` and the merge reports the drop on stderr while
  still writing the union — the case `TRANSFORM.md` documents, with the `merge_entry_*` fixtures
  behind it.
* **The two files share a kind namespace by number.** One number named differently by the two, or
  one name standing for two numbers, is a refusal that names both declarations. These halves were
  kept in disjoint numbers — 1 and 2 for money, 3 to 5 for stock — which is what lets the union
  declare all five without a rename first.

**Renaming the union.** When the name matters, one rename puts it right — and that is an edit, so
the file is no longer the bytes the header's command reproduces:

```sh
fsmtable-transform rename vending.fsm machine:Coins=Vending > vending.new && mv vending.new vending.fsm
```

## Tests

Four ctest cases in `CMakeLists.txt` hold this example, and they are the claims this document makes,
made where no reader can edit them: `vending_union_is_the_two_parents_merged` (the merge of the two
committed halves, diffed against the committed union, so the artifact and its parents cannot drift
apart), `vending_trace_composes_the_two_halves` (the four-event walk above, and its exit code),
`vending_refuses_a_vend_before_it_is_paid` (the stop, and exit 1), and
`vending_reports_its_sink_and_stays_clean` (the sink line, and exit 0). Every output block above is
re-run and diffed line for line by `tools/check-doc-claims.sh` on every gate run — the lines
beginning `$ ` are what it runs — and that script's tree check inspects every `.fsm` under
`examples/`, so these three files are held to the same "no unreachable state" rule as the rest of
the repository. There is no recorded session file here, because there is no binary to pipe one into:
the walk is an invocation of the tool, and the tool is what the cases run.

## Build your own from this

1. **Write the first machine** as if it were alone: `version 1`, `machine <Name>`, `initial`, the
   kinds it needs, one `transition` per row. Write the second the same way. Neither one mentions the
   other, and each has to make sense by itself — the stock half below has no idea what money is.
2. **Name the seam.** Decide which state name means the same thing in both files — "paid" here —
   and use that name in both, with a comment saying so. This is the whole design decision: a merge
   fuses on names and checks no meanings.
3. **Merge.** `fsmtable-transform merge a.fsm b.fsm > u.new && mv u.new u.fsm` — the two-step form,
   because a refusal exits 1 and truncates nothing that way. Put the machine that owns the story's
   start first: its `initial` and its name become the union's.
4. **Check the union is a machine**, not just a file: `fsmtable-inspect u.fsm` reports the sinks,
   the partial pairs and any state nothing can reach. A composition whose halves share no name is
   refused by the merge itself, with the unreachable states named.
5. **Trace the composition** over the events a real driver would send: `fsmtable-inspect --trace
   u.fsm <event>...`. It is the fastest way to find the seam you meant but did not write.

### Worked adaptation: a restock

The sink above is one row away from not being one, and coming back from `SoldOut` is the shortest
honest change to make. Append two lines to the union's row block:

```fsm
kind restock = 6
transition SoldOut --restock--> Credited
```

The machine is then **4 states and 7 rows** (from 4 and 6), `SoldOut` stops being a sink — run
`fsmtable-inspect` over it and the `sinks:` line is gone — and the recipe is checked rather than
believed: on every gate run `tools/check-doc-claims.sh` appends exactly this block to the real
`vending.fsm`, requires the generator to accept it, and requires the counts to be the two written
here. The change to the two halves is a judgement rather than two lines — `restock` is stock, so it
belongs in `dispenser.fsm` — and the merge then has to be re-run, which is exactly how a committed
union is supposed to age.

## Where the rest is written down

    SPEC.md                     the frozen format and API (sections 2 and 4 are the frozen ones)
    TRANSFORM.md                the merge verb: the policy, the header, the refusals, the limits
    examples/README.md          the examples, the recipe they share, and what to build next
    examples/inspector/README.md    the tool this example is read with: the summary, --trace, --graph
